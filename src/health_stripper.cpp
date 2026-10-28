#include "health_stripper.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include "fit_validator.h"
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <bitset>
#include <string_view>
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>


namespace {

struct HealthKeepRule {
    uint16_t mesg;
    const uint8_t* fields;
    std::size_t count;
};

// Generated from docs/health-strip-table.tsv by tools/gen_health_table.py.
#include "health_table.inc"

constexpr uint16_t kMesgNumEvent = 21; // FIT_MESG_NUM_EVENT

struct Tables {
    std::unordered_map<uint16_t, std::bitset<256>> keepFields;
    std::bitset<256> keepEvents;

    Tables() {
        for (const HealthKeepRule& rule : kKeepRules) {
            std::bitset<256>& set = keepFields[rule.mesg];
            for (std::size_t i = 0; i < rule.count; ++i) {
                set.set(rule.fields[i]);
            }
        }
        for (uint8_t e : kKeepEvents) {
            keepEvents.set(e);
        }
    }
};

const Tables& tables() {
    static const Tables t;
    return t;
}

uint32_t readU32(const std::string& s, std::size_t pos) {
    return static_cast<uint32_t>(static_cast<unsigned char>(s[pos]))
        | (static_cast<uint32_t>(static_cast<unsigned char>(s[pos + 1])) << 8)
        | (static_cast<uint32_t>(static_cast<unsigned char>(s[pos + 2])) << 16)
        | (static_cast<uint32_t>(static_cast<unsigned char>(s[pos + 3])) << 24);
}

/**
 * Length of the FIT file (header + data + CRC) starting at pos, or 0 when the
 * bytes at pos do not look like a complete FIT file.
 */
std::size_t chunkLength(const std::string& s, std::size_t pos) {
    if (s.size() - pos < 12) {
        return 0;
    }
    const std::size_t headerSize = static_cast<unsigned char>(s[pos]);
    if ((headerSize != 12 && headerSize != 14) || s.compare(pos + 8, 4, ".FIT") != 0) {
        return 0;
    }
    const uint64_t total = static_cast<uint64_t>(headerSize) + readU32(s, pos + 4) + 2;
    if (total > s.size() - pos) {
        return 0;
    }
    return static_cast<std::size_t>(total);
}

// ---------------------------------------------------------------------------
// Fast path: one linear pass over the FIT bytes, driven by the same allow-list
// table. Definitions are rewritten to the kept fields, data messages are
// copied field by field, everything else is skipped. No SDK objects, no
// per-message allocation.
// Every length is bounds checked; anything inconsistent throws (fail closed).
// ---------------------------------------------------------------------------

struct NeedTimestampExpansion {};

uint16_t crc16(const unsigned char* d, std::size_t n, uint16_t crc = 0) {
    static const uint16_t table[16] = {0x0000, 0xCC01, 0xD801, 0x1400, 0xF001, 0x3C00, 0x2800, 0xE401,
                                       0xA001, 0x6C00, 0x7800, 0xB401, 0x5000, 0x9C01, 0x8801, 0x4400};
    for (std::size_t i = 0; i < n; ++i) {
        uint16_t tmp = table[crc & 0xF];
        crc = (crc >> 4) & 0x0FFF;
        crc = crc ^ tmp ^ table[d[i] & 0xF];
        tmp = table[crc & 0xF];
        crc = (crc >> 4) & 0x0FFF;
        crc = crc ^ tmp ^ table[(d[i] >> 4) & 0xF];
    }
    return crc;
}

struct Seg {
    uint32_t off;
    uint32_t len;
};

struct LocalDef {
    bool valid = false;
    bool bigEndian = false;
    bool keepMessage = false;   // message kind is in the table and keeps at least one field
    bool isEvent = false;
    bool addTimestamp = false;  // expansion mode: output carries a timestamp field this definition lacks
    int eventOff = -1;          // offset of event.event inside the data bytes, -1 when absent
    int tsOff = -1;             // offset of a 4 byte timestamp field (253), -1 when absent
    uint32_t dataSize = 0;      // bytes of one data message (fields + developer fields)
    uint32_t keptFields = 0;
    uint32_t removedFields = 0;
    uint32_t devFields = 0;
    std::vector<Seg> segs;      // byte ranges of kept fields
};

constexpr uint16_t kProfileVersion = 21158; // what the vendor encoder wrote into its headers (SDK 21.158)

uint32_t readU32At(const unsigned char* p, bool big) {
    return big ? ((static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16)
                  | (static_cast<uint32_t>(p[2]) << 8) | p[3])
               : (static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
                  | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24));
}

void putU32(std::string& out, uint32_t v, bool big) {
    for (int i = 0; i < 4; ++i) {
        const int shift = big ? 24 - 8 * i : 8 * i;
        out.push_back(static_cast<char>((v >> shift) & 0xFF));
    }
}

/**
 * One linear pass. With `expand` false a compressed timestamp header throws
 * NeedTimestampExpansion and the caller runs the pass again with `expand` true:
 * then every compressed-header message becomes a normal data message carrying a
 * full timestamp field (253), computed from the last full timestamp with the
 * rolling 5 bit offset rule, and kept definitions without a timestamp field get one.
 */
std::string stripChunkPass(const unsigned char* d, std::size_t n, HealthStripStats& stats, bool expand) {
    const Tables& t = tables();
    const std::size_t hdr = d[0];
    const uint32_t dataSize = static_cast<uint32_t>(d[4]) | (static_cast<uint32_t>(d[5]) << 8)
        | (static_cast<uint32_t>(d[6]) << 16) | (static_cast<uint32_t>(d[7]) << 24);
    if (hdr + static_cast<std::size_t>(dataSize) + 2 != n) {
        throw std::runtime_error("FIT size mismatch");
    }
    if (hdr == 14) {
        const uint16_t stored = static_cast<uint16_t>(d[12] | (d[13] << 8));
        if (stored != 0 && stored != crc16(d, 12)) {
            throw std::runtime_error("FIT header CRC mismatch");
        }
    }
    const uint16_t fileCrc = static_cast<uint16_t>(d[n - 2] | (d[n - 1] << 8));
    if (fileCrc != crc16(d, n - 2)) {
        throw std::runtime_error("FIT CRC mismatch");
    }

    HealthStripStats local;
    // Header written as the vendor encoder wrote it (14 bytes, protocol 2.0, SDK profile version), so
    // the output does not depend on the input's header bytes.
    const unsigned char outHdr[14] = {14, 0x20, static_cast<unsigned char>(kProfileVersion & 0xFF),
                                      static_cast<unsigned char>(kProfileVersion >> 8), 0, 0, 0, 0, '.', 'F', 'I', 'T', 0, 0};
    std::string out;
    out.reserve(n + 64);
    out.append(reinterpret_cast<const char*>(outHdr), 14);

    LocalDef defs[16];
    bool haveTs = false;
    uint32_t lastTs = 0;
    std::size_t pos = hdr;
    const std::size_t end = hdr + dataSize;
    while (pos < end) {
        const unsigned char h = d[pos++];
        const bool compressed = (h & 0x80) != 0;
        if (compressed && !expand) {
            throw NeedTimestampExpansion{};
        }
        if (!compressed && (h & 0x40)) {
            // ---- definition message
            const unsigned local_type = h & 0x0F;
            if (end - pos < 5) throw std::runtime_error("truncated definition");
            const unsigned char* gptr = d + pos + 2;
            const unsigned arch = d[pos + 1];
            if (arch > 1) throw std::runtime_error("bad architecture byte");
            const uint16_t global = arch == 0 ? static_cast<uint16_t>(d[pos + 2] | (d[pos + 3] << 8))
                                              : static_cast<uint16_t>((d[pos + 2] << 8) | d[pos + 3]);
            const unsigned nFields = d[pos + 4];
            pos += 5;
            if (end - pos < static_cast<std::size_t>(nFields) * 3) throw std::runtime_error("truncated definition");
            const unsigned char* fdef = d + pos;
            pos += static_cast<std::size_t>(nFields) * 3;
            unsigned nDev = 0;
            const unsigned char* ddef = nullptr;
            if (h & 0x20) {
                if (end - pos < 1) throw std::runtime_error("truncated definition");
                nDev = d[pos++];
                if (end - pos < static_cast<std::size_t>(nDev) * 3) throw std::runtime_error("truncated definition");
                ddef = d + pos;
                pos += static_cast<std::size_t>(nDev) * 3;
            }

            LocalDef& def = defs[local_type];
            def = LocalDef();
            def.valid = true;
            def.bigEndian = arch == 1;
            def.devFields = nDev;
            const auto rule = t.keepFields.find(global);
            def.isEvent = (global == kMesgNumEvent);
            std::bitset<256> seen;
            uint32_t off = 0;
            std::string outDef;
            for (unsigned i = 0; i < nFields; ++i) {
                const unsigned num = fdef[i * 3];
                const unsigned size = fdef[i * 3 + 1];
                if (seen.test(num)) throw std::runtime_error("duplicate field number in a definition");
                seen.set(num);
                if (const char* e = fitFieldDefError(size, fdef[i * 3 + 2])) throw std::runtime_error(e);
                if (rule != t.keepFields.end() && rule->second.test(num)) {
                    if (!def.segs.empty() && def.segs.back().off + def.segs.back().len == off) {
                        def.segs.back().len += size;
                    } else {
                        def.segs.push_back({off, size});
                    }
                    outDef.append(reinterpret_cast<const char*>(fdef + i * 3), 3);
                    ++def.keptFields;
                } else {
                    ++def.removedFields;
                }
                if (def.isEvent && num == 0 && size == 1) def.eventOff = static_cast<int>(off);
                if (num == 253 && size == 4) def.tsOff = static_cast<int>(off);
                off += size;
            }
            for (unsigned i = 0; i < nDev; ++i) {
                if (ddef[i * 3 + 1] == 0) throw std::runtime_error("developer field size 0");
                off += ddef[i * 3 + 1];
            }
            def.dataSize = off;
            def.keepMessage = rule != t.keepFields.end() && def.keptFields > 0;
            if (def.keepMessage) {
                if (expand && !seen.test(253) && rule->second.test(253)) {
                    def.addTimestamp = true;
                    const unsigned char tsField[3] = {253, 4, 0x86};
                    outDef.append(reinterpret_cast<const char*>(tsField), 3);
                    ++def.keptFields; // only for the written count
                }
                out.push_back(static_cast<char>(0x40 | local_type));
                out.push_back(0);
                out.push_back(static_cast<char>(arch));
                out.append(reinterpret_cast<const char*>(gptr), 2);
                out.push_back(static_cast<char>(def.keptFields));
                out.append(outDef);
            }
        } else {
            // ---- data message (normal or compressed timestamp header)
            const unsigned local_type = compressed ? ((h >> 5) & 0x03) : (h & 0x0F);
            const LocalDef& def = defs[local_type];
            if (!def.valid) throw std::runtime_error("data message without a definition");
            if (end - pos < def.dataSize) throw std::runtime_error("truncated data message");
            const unsigned char* m = d + pos;
            pos += def.dataSize;

            uint32_t ts = 0xFFFFFFFFu;
            if (compressed) {
                if (def.tsOff >= 0) throw std::runtime_error("compressed timestamp header on a definition with a timestamp field");
                if (!haveTs) throw std::runtime_error("compressed timestamp without a previous full timestamp");
                const uint32_t offset = h & 0x1F;
                ts = (lastTs & ~0x1Fu) + offset;
                if (offset < (lastTs & 0x1F)) ts += 0x20;
                lastTs = ts;
            } else if (def.tsOff >= 0) {
                const uint32_t v = readU32At(m + def.tsOff, def.bigEndian);
                if (v != 0xFFFFFFFFu) {
                    lastTs = v;
                    haveTs = true;
                }
            }

            local.developerFieldsRemoved += def.devFields;
            bool keep = def.keepMessage;
            if (keep && def.isEvent) {
                keep = def.eventOff >= 0 && t.keepEvents.test(m[def.eventOff]);
            }
            if (!keep) {
                ++local.droppedMessages;
                continue;
            }
            local.strippedFields += def.removedFields;
            out.push_back(static_cast<char>(local_type));
            for (const Seg& sg : def.segs) {
                out.append(reinterpret_cast<const char*>(m + sg.off), sg.len);
            }
            if (def.addTimestamp) {
                putU32(out, ts, def.bigEndian);
            }
        }
    }
    if (pos != end) throw std::runtime_error("FIT record stream does not end at the data size");

    const uint32_t newSize = static_cast<uint32_t>(out.size() - 14);
    if (newSize == 0) {
        throw std::runtime_error("no messages left after cleaning"); // an empty data section is not a FIT the SDK reads
    }
    for (int i = 0; i < 4; ++i) {
        out[4 + i] = static_cast<char>((newSize >> (8 * i)) & 0xFF);
    }
    const uint16_t hc = crc16(reinterpret_cast<const unsigned char*>(out.data()), 12);
    out[12] = static_cast<char>(hc & 0xFF);
    out[13] = static_cast<char>(hc >> 8);
    const uint16_t oc = crc16(reinterpret_cast<const unsigned char*>(out.data()), out.size());
    out.push_back(static_cast<char>(oc & 0xFF));
    out.push_back(static_cast<char>(oc >> 8));

    stats.droppedMessages += local.droppedMessages;
    stats.strippedFields += local.strippedFields;
    stats.developerFieldsRemoved += local.developerFieldsRemoved;
    return out;
}

std::string stripChunk(const std::string_view chunk, HealthStripStats& stats) {
    const unsigned char* d = reinterpret_cast<const unsigned char*>(chunk.data());
    try {
        return stripChunkPass(d, chunk.size(), stats, false);
    } catch (const NeedTimestampExpansion&) {
        return stripChunkPass(d, chunk.size(), stats, true);
    }
}

} // namespace

std::string HealthStripper::stripBytes(const std::string& input, HealthStripStats& stats) {
    if (input.empty()) {
        throw std::runtime_error("empty input");
    }
    std::string output;
    output.reserve(input.size());
    std::size_t pos = 0;
    while (pos < input.size()) {
        const std::size_t len = chunkLength(input, pos);
        if (len == 0) {
            if (pos == 0) {
                throw std::runtime_error("not a valid FIT file (bad header or truncated)");
            }
            break; // trailing bytes after the last valid file are not copied
        }
        const std::string_view chunk(input.data() + pos, len);
        try {
            output += stripChunk(chunk, stats);
        } catch (const std::runtime_error& e) {
            if (pos == 0) {
                throw std::runtime_error(std::string("invalid FIT structure: ") + e.what());
            }
            break; // damaged 2nd+ chained file: not copied, reported in trailingBytesDropped
        }
        pos += len;
    }
    stats.trailingBytesDropped += input.size() - pos;
    return output;
}

void HealthStripper::writeAtomically(const std::string& outputPath, const std::string& bytes) {
    // Unpredictable temp name in the output's directory (mkstemp: O_EXCL, mode 0600), renamed on
    // success, unlinked on every handled failure.
    std::string tmp = outputPath + ".tmp.XXXXXX";
    std::vector<char> name(tmp.begin(), tmp.end());
    name.push_back('\0');
    const int fd = mkstemp(name.data());
    if (fd < 0) {
        throw std::runtime_error("Cannot create temporary output file next to: " + outputPath);
    }
    tmp.assign(name.data());
    auto fail = [&](const std::string& why) {
        close(fd);
        unlink(tmp.c_str());
        throw std::runtime_error(why + ": " + outputPath);
    };
    const mode_t mask = umask(0);
    umask(mask);
    if (fchmod(fd, 0666 & ~mask) != 0) {
        fail("Cannot set permissions of the output file");
    }
    std::size_t done = 0;
    while (done < bytes.size()) {
        const ssize_t w = ::write(fd, bytes.data() + done, bytes.size() - done);
        if (w < 0) {
            if (errno == EINTR) continue;
            fail("Cannot write output file");
        }
        done += static_cast<std::size_t>(w);
    }
    if (close(fd) != 0) {
        unlink(tmp.c_str());
        throw std::runtime_error("Cannot write output file: " + outputPath);
    }
    if (std::rename(tmp.c_str(), outputPath.c_str()) != 0) {
        unlink(tmp.c_str());
        throw std::runtime_error("Cannot move output file into place: " + outputPath);
    }
}

void HealthStripper::stripFile(const std::string& inputPath, const std::string& outputPath, HealthStripStats& stats) {
    std::ifstream file(inputPath, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + inputPath);
    }
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    file.seekg(0, std::ios::beg);
    if (size <= 0) {
        throw std::runtime_error("Cannot read file: " + inputPath);
    }
    std::string input(static_cast<std::size_t>(size), '\0');
    file.read(&input[0], size);
    if (!file) {
        throw std::runtime_error("Cannot read file: " + inputPath);
    }
    file.close();

    const std::string cleaned = stripBytes(input, stats);
    writeAtomically(outputPath, cleaned);
}
