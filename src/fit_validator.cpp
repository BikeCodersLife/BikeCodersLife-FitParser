#include "fit_validator.h"

#include <bitset>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

// Validates ONE FIT file at d (n bytes available). On success `consumed` is its full length
// (header + data + CRC). dataSize 0 in the header means "records run to the CRC at the end of the input".
std::string validateChunk(const unsigned char* d, std::size_t n, std::size_t& consumed, bool first) {
    if (n < 14) return "file too short";
    const std::size_t hdr = d[0];
    if (hdr != 12 && hdr != 14) return "bad header size";
    if (d[8] != '.' || d[9] != 'F' || d[10] != 'I' || d[11] != 'T') return "missing .FIT tag";
    if (n < hdr + 2) return "file too short";
    uint32_t dataSize = static_cast<uint32_t>(d[4]) | (static_cast<uint32_t>(d[5]) << 8)
        | (static_cast<uint32_t>(d[6]) << 16) | (static_cast<uint32_t>(d[7]) << 24);
    std::size_t end;
    if (dataSize == 0) {
        if (!first) return "data size 0";
        end = n - 2; // header without data size: records run to the CRC
    } else {
        if (static_cast<uint64_t>(hdr) + dataSize + 2 > n) return "truncated file";
        end = hdr + dataSize;
    }

    struct Def {
        bool valid = false;
        uint32_t dataSize = 0;
    } defs[16];

    std::size_t pos = hdr;
    while (pos < end) {
        const unsigned char h = d[pos++];
        if (h & 0x80) { // compressed timestamp header: a data message
            const Def& def = defs[(h >> 5) & 0x03];
            if (!def.valid) return "data message without a definition";
            if (end - pos < def.dataSize) return "truncated data message";
            pos += def.dataSize;
        } else if (h & 0x40) {
            if (end - pos < 5) return "truncated definition";
            if (d[pos + 1] > 1) return "unsupported architecture byte";
            const unsigned nFields = d[pos + 4];
            pos += 5;
            if (end - pos < static_cast<std::size_t>(nFields) * 3) return "truncated definition";
            Def& def = defs[h & 0x0F];
            def = Def();
            std::bitset<256> seen;
            uint32_t size = 0;
            for (unsigned i = 0; i < nFields; ++i) {
                const unsigned num = d[pos + i * 3];
                if (seen.test(num)) return "duplicate field number in a definition";
                seen.set(num);
                if (const char* e = fitFieldDefError(d[pos + i * 3 + 1], d[pos + i * 3 + 2])) return e;
                size += d[pos + i * 3 + 1];
            }
            pos += static_cast<std::size_t>(nFields) * 3;
            if (h & 0x20) {
                if (end - pos < 1) return "truncated definition";
                const unsigned nDev = d[pos++];
                if (end - pos < static_cast<std::size_t>(nDev) * 3) return "truncated definition";
                for (unsigned i = 0; i < nDev; ++i) {
                    if (d[pos + i * 3 + 1] == 0) return "developer field size 0";
                    size += d[pos + i * 3 + 1];
                }
                pos += static_cast<std::size_t>(nDev) * 3;
            }
            def.valid = true;
            def.dataSize = size;
        } else {
            const Def& def = defs[h & 0x0F];
            if (!def.valid) return "data message without a definition";
            if (end - pos < def.dataSize) return "truncated data message";
            pos += def.dataSize;
        }
    }
    if (pos != end) return "record stream does not end at the data size";
    consumed = end + 2;
    return std::string();
}

} // namespace

std::string validateFitStructure(const unsigned char* d, std::size_t n) {
    // Every chained FIT file in the input is validated: the vendor decoder reads them all.
    std::size_t pos = 0;
    while (pos < n) {
        // The vendor decoder (Decode::Read) starts decoding a new file at EVERY byte that follows a
        // file: header size >= 12 (any value up to 255!), protocol major <= 2, ".FIT" tag, and so on.
        // So no tail is ignored: whatever follows the first file must itself be a complete, valid
        // FIT file under the strict rules (header size 12 or 14), otherwise the input is refused.
        std::size_t used = 0;
        const std::string reason = validateChunk(d + pos, n - pos, used, pos == 0);
        if (!reason.empty()) return pos == 0 ? reason : "chained file at byte " + std::to_string(pos) + ": " + reason;
        pos += used;
    }
    return std::string();
}

std::string validateFitFile(const std::string& path) {
    const int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return "cannot open file";
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size <= 0) {
        close(fd);
        return "cannot read file";
    }
    void* map = mmap(nullptr, static_cast<std::size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) return "cannot read file";
    const std::string result = validateFitStructure(static_cast<const unsigned char*>(map), static_cast<std::size_t>(st.st_size));
    munmap(map, static_cast<std::size_t>(st.st_size));
    return result;
}
