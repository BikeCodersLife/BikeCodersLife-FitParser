#include "health_stripper.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <bitset>
#include <string_view>
#include <unistd.h>

#include <fit_decode.hpp>
#include <fit_encode.hpp>
#include <fit_mesg.hpp>
#include <fit_mesg_listener.hpp>
#include <fit_profile.hpp>
#include <fit_runtime_exception.hpp>

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

/**
 * Decides per message: drop it, or rebuild it from its kept fields only.
 * Developer fields are never copied (the rebuilt message has none).
 */
class StripListener : public fit::MesgListener {
public:
    StripListener(fit::Encode& encode, HealthStripStats& stats)
        : encode_(encode), stats_(stats) {}

    void OnMesg(fit::Mesg& mesg) override {
        stats_.developerFieldsRemoved += static_cast<std::size_t>(mesg.GetNumDevFields());

        const Tables& t = tables();
        const auto rule = t.keepFields.find(mesg.GetNum());
        if (rule == t.keepFields.end()) {
            ++stats_.droppedMessages;
            return;
        }

        if (mesg.GetNum() == kMesgNumEvent && !eventAllowed(mesg, t)) {
            ++stats_.droppedMessages;
            return;
        }

        // Component expansion adds derived fields (e.g. enhanced_speed from
        // speed). Only the fields the file itself carried are re-encoded.
        mesg.RemoveExpandedFields();

        fit::Mesg out(mesg.GetNum());
        out.SetLocalNum(mesg.GetLocalNum());
        const int total = mesg.GetNumFields();
        for (int i = 0; i < total; ++i) {
            const fit::Field* field = mesg.GetFieldByIndex(static_cast<FIT_UINT16>(i));
            if (field == nullptr) {
                continue;
            }
            if (rule->second.test(field->GetNum())) {
                out.AddField(*field);
            } else {
                ++stats_.strippedFields;
            }
        }
        encode_.Write(out);
        ++written_;
    }

    std::size_t written() const { return written_; }

private:
    static bool eventAllowed(const fit::Mesg& mesg, const Tables& t) {
        const fit::Field* field = mesg.GetField(static_cast<FIT_UINT8>(0)); // event
        if (field == nullptr || field->GetNumValues() == 0) {
            return false;
        }
        const FIT_ENUM value = field->GetENUMValue();
        if (value == FIT_ENUM_INVALID) {
            return false;
        }
        return t.keepEvents.test(value);
    }

    fit::Encode& encode_;
    HealthStripStats& stats_;
    std::size_t written_ = 0;
};

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

// Slow path (Garmin SDK decode + re-encode). Only used for files with
// compressed-timestamp headers, whose timestamps depend on the messages in
// front of them, so a message cannot simply be cut out of the byte stream.
std::string stripChunkSdk(const std::string_view chunk, HealthStripStats& stats) {
    {
        std::istringstream check(std::string(chunk), std::ios::in | std::ios::binary);
        fit::Decode decode;
        if (!decode.IsFIT(check)) {
            throw std::runtime_error("not a FIT file");
        }
    }
    {
        std::istringstream check(std::string(chunk), std::ios::in | std::ios::binary);
        fit::Decode decode;
        if (!decode.CheckIntegrity(check)) {
            throw std::runtime_error("FIT integrity check failed (CRC or structure)");
        }
    }

    std::stringstream out(std::ios::in | std::ios::out | std::ios::binary);
    fit::Encode encode(fit::ProtocolVersion::V20);
    encode.Open(out);
    StripListener listener(encode, stats);

    std::istringstream in(std::string(chunk), std::ios::in | std::ios::binary);
    fit::Decode decode;
    decode.SuppressComponentExpansion();
    if (!decode.Read(in, listener)) {
        throw std::runtime_error("Failed to decode FIT file");
    }
    if (!encode.Close()) {
        throw std::runtime_error("Failed to encode the cleaned FIT file");
    }
    return out.str();
}

// ---------------------------------------------------------------------------
// Fast path: one linear pass over the FIT bytes, driven by the same allow-list
// table. Definitions are rewritten to the kept fields, data messages are
// copied field by field, everything else is skipped. No SDK objects, no
// per-message allocation: ~20-50 MB/s against ~1.2 MB/s for the SDK path.
// Every length is bounds checked; anything inconsistent throws (fail closed).
// ---------------------------------------------------------------------------

struct CompressedTimestampSeen {};

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
    bool keepMessage = false;   // message kind is in the table and keeps at least one field
    bool isEvent = false;
    int eventOff = -1;          // offset of event.event inside the data bytes, -1 when absent
    uint32_t dataSize = 0;      // bytes of one data message (fields + developer fields)
    uint32_t keptFields = 0;
    uint32_t removedFields = 0;
    uint32_t devFields = 0;
    std::vector<Seg> segs;      // byte ranges of kept fields
};

std::string stripChunkFast(const unsigned char* d, std::size_t n, HealthStripStats& stats) {
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
    std::string out;
    out.reserve(n);
    out.append(reinterpret_cast<const char*>(d), hdr);

    LocalDef defs[16];
    std::size_t pos = hdr;
    const std::size_t end = hdr + dataSize;
    while (pos < end) {
        const unsigned char h = d[pos++];
        if (h & 0x80) {
            throw CompressedTimestampSeen{};
        }
        const unsigned local_type = h & 0x0F;
        if (h & 0x40) {
            // definition message
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
            def.devFields = nDev;
            const auto rule = t.keepFields.find(global);
            def.isEvent = (global == kMesgNumEvent);
            uint32_t off = 0;
            std::string outDef;
            for (unsigned i = 0; i < nFields; ++i) {
                const unsigned num = fdef[i * 3];
                const unsigned size = fdef[i * 3 + 1];
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
                if (def.isEvent && num == 0 && size == 1) {
                    def.eventOff = static_cast<int>(off);
                }
                off += size;
            }
            for (unsigned i = 0; i < nDev; ++i) {
                off += ddef[i * 3 + 1];
            }
            def.dataSize = off;
            def.keepMessage = rule != t.keepFields.end() && def.keptFields > 0;
            if (def.keepMessage) {
                out.push_back(static_cast<char>(0x40 | local_type));
                out.push_back(0);
                out.push_back(static_cast<char>(arch));
                out.append(reinterpret_cast<const char*>(gptr), 2);
                out.push_back(static_cast<char>(def.keptFields));
                out.append(outDef);
            }
        } else {
            const LocalDef& def = defs[local_type];
            if (!def.valid) throw std::runtime_error("data message without a definition");
            if (end - pos < def.dataSize) throw std::runtime_error("truncated data message");
            const unsigned char* m = d + pos;
            pos += def.dataSize;
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
        }
    }
    if (pos != end) throw std::runtime_error("FIT record stream does not end at the data size");

    const uint32_t newSize = static_cast<uint32_t>(out.size() - hdr);
    for (int i = 0; i < 4; ++i) {
        out[4 + i] = static_cast<char>((newSize >> (8 * i)) & 0xFF);
    }
    if (hdr == 14) {
        const uint16_t hc = crc16(reinterpret_cast<const unsigned char*>(out.data()), 12);
        out[12] = static_cast<char>(hc & 0xFF);
        out[13] = static_cast<char>(hc >> 8);
    }
    const uint16_t oc = crc16(reinterpret_cast<const unsigned char*>(out.data()), out.size());
    out.push_back(static_cast<char>(oc & 0xFF));
    out.push_back(static_cast<char>(oc >> 8));

    stats.droppedMessages += local.droppedMessages;
    stats.strippedFields += local.strippedFields;
    stats.developerFieldsRemoved += local.developerFieldsRemoved;
    return out;
}

std::string stripChunk(const std::string_view chunk, HealthStripStats& stats) {
    try {
        return stripChunkFast(reinterpret_cast<const unsigned char*>(chunk.data()), chunk.size(), stats);
    } catch (const CompressedTimestampSeen&) {
        HealthStripStats chunkStats;               // a discarded chunk must not count
        std::string out = stripChunkSdk(chunk, chunkStats);
        stats.droppedMessages += chunkStats.droppedMessages;
        stats.strippedFields += chunkStats.strippedFields;
        stats.developerFieldsRemoved += chunkStats.developerFieldsRemoved;
        return out;
    }
}

} // namespace

std::string HealthStripper::stripBytes(const std::string& input, HealthStripStats& stats) {
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
        } catch (const std::runtime_error& e) { // includes fit::RuntimeException
            if (pos == 0) {
                throw std::runtime_error(std::string("FIT decode error: ") + e.what());
            }
            break; // damaged 2nd+ chained file: not copied, reported in trailingBytesDropped
        }
        pos += len;
    }
    stats.trailingBytesDropped += input.size() - pos;
    return output;
}

void HealthStripper::writeAtomically(const std::string& outputPath, const std::string& bytes) {
    const std::string tmp = outputPath + ".tmp." + std::to_string(static_cast<long>(getpid()));
    {
        std::ofstream file(tmp, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open output file: " + tmp);
        }
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        file.flush();
        if (!file.good()) {
            file.close();
            std::remove(tmp.c_str());
            throw std::runtime_error("Cannot write output file: " + tmp);
        }
    }
    if (std::rename(tmp.c_str(), outputPath.c_str()) != 0) {
        std::remove(tmp.c_str());
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
