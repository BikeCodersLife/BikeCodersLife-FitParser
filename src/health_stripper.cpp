#include "health_stripper.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <bitset>
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

std::string stripChunk(const std::string& chunk, HealthStripStats& stats) {
    {
        std::istringstream check(chunk, std::ios::in | std::ios::binary);
        fit::Decode decode;
        if (!decode.IsFIT(check)) {
            throw std::runtime_error("not a FIT file");
        }
    }
    {
        std::istringstream check(chunk, std::ios::in | std::ios::binary);
        fit::Decode decode;
        if (!decode.CheckIntegrity(check)) {
            throw std::runtime_error("FIT integrity check failed (CRC or structure)");
        }
    }

    std::stringstream out(std::ios::in | std::ios::out | std::ios::binary);
    fit::Encode encode(fit::ProtocolVersion::V20);
    encode.Open(out);
    StripListener listener(encode, stats);

    std::istringstream in(chunk, std::ios::in | std::ios::binary);
    fit::Decode decode;
    if (!decode.Read(in, listener)) {
        throw std::runtime_error("Failed to decode FIT file");
    }
    if (!encode.Close()) {
        throw std::runtime_error("Failed to encode the cleaned FIT file");
    }
    return out.str();
}

} // namespace

std::string HealthStripper::stripBytes(const std::string& input, HealthStripStats& stats) {
    std::string output;
    std::size_t pos = 0;
    while (pos < input.size()) {
        const std::size_t len = chunkLength(input, pos);
        if (len == 0) {
            if (pos == 0) {
                throw std::runtime_error("not a valid FIT file (bad header or truncated)");
            }
            break; // trailing bytes after the last valid file are not copied
        }
        const std::string chunk = input.substr(pos, len);
        try {
            output += stripChunk(chunk, stats);
        } catch (const fit::RuntimeException& e) {
            if (pos == 0) {
                throw std::runtime_error(std::string("FIT decode error: ") + e.what());
            }
            break;
        } catch (const std::runtime_error&) {
            if (pos == 0) {
                throw;
            }
            break;
        }
        pos += len;
    }
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
    std::ostringstream buffer;
    buffer << file.rdbuf();
    file.close();

    const std::string cleaned = stripBytes(buffer.str(), stats);
    writeAtomically(outputPath, cleaned);
}
