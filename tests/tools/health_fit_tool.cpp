// Test helper for --strip-health (not shipped in the release archive).
//
//   health-fit-tool rich <out.fit>   FIT that carries EVERY field of EVERY message
//                                    of the SDK profile, developer fields (CORE
//                                    body temperature, power) on records, laps and
//                                    the session, and an event for every event type
//   health-fit-tool dump <in.fit>    one line per message: number, fields, developer
//                                    field count (expanded component fields skipped)
//
// tests/run_tests.sh checks the dump of the stripped file against
// docs/health-strip-table.tsv.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <fit_decode.hpp>
#include <fit_developer_data_id_mesg.hpp>
#include <fit_developer_field.hpp>
#include <fit_encode.hpp>
#include <fit_field_description_mesg.hpp>
#include <fit_mesg.hpp>
#include <fit_mesg_listener.hpp>
#include <fit_profile.hpp>
#include <fit_record_mesg.hpp>

namespace {

constexpr uint32_t kStart = 1100000000; // FIT epoch seconds

// Fill every field of the message with a plausible non-invalid value.
void fillAll(fit::Mesg& mesg, uint32_t timestamp) {
    const fit::Profile::MESG* profile = fit::Profile::GetMesg(mesg.GetNum());
    for (int i = 0; i < profile->numFields; ++i) {
        const fit::Profile::FIELD& f = profile->fields[i];
        fit::Field* field = mesg.AddField(f.num);
        if (field == nullptr) {
            continue;
        }
        if (f.num == 253) {
            field->SetUINT32Value(timestamp);
        } else if ((f.type & FIT_BASE_TYPE_NUM_MASK) == (FIT_BASE_TYPE_STRING & FIT_BASE_TYPE_NUM_MASK)) {
            field->SetSTRINGValue(L"x");
        } else {
            field->SetFLOAT64Value(1.0);
        }
    }
}

int makeRich(const std::string& path) {
    std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return 2;
    }
    fit::Encode encode(fit::ProtocolVersion::V20);
    encode.Open(file);

    fit::Mesg fileId(fit::Profile::GetMesg(FIT_MESG_NUM_FILE_ID)->num);
    fillAll(fileId, kStart);
    fileId.SetFieldUINT8Value(0, FIT_FILE_ACTIVITY);
    encode.Write(fileId);

    fit::DeveloperDataIdMesg devId;
    for (FIT_UINT8 i = 0; i < 16; ++i) {
        devId.SetApplicationId(i, i + 1);
    }
    devId.SetDeveloperDataIndex(0);
    devId.SetApplicationVersion(1);
    encode.Write(devId);

    fit::FieldDescriptionMesg coreTemp;
    coreTemp.SetDeveloperDataIndex(0);
    coreTemp.SetFieldDefinitionNumber(0);
    coreTemp.SetFitBaseTypeId(FIT_BASE_TYPE_FLOAT32);
    coreTemp.SetFieldName(0, L"core_temperature");
    coreTemp.SetUnits(0, L"C");
    coreTemp.SetNativeMesgNum(FIT_MESG_NUM_RECORD);
    encode.Write(coreTemp);

    fit::FieldDescriptionMesg devPower;
    devPower.SetDeveloperDataIndex(0);
    devPower.SetFieldDefinitionNumber(1);
    devPower.SetFitBaseTypeId(FIT_BASE_TYPE_UINT16);
    devPower.SetFieldName(0, L"Power");
    devPower.SetUnits(0, L"watts");
    devPower.SetNativeMesgNum(FIT_MESG_NUM_RECORD);
    encode.Write(devPower);

    // The track: 30 one-second records, every record field filled, then the
    // position / time / distance / speed set to a known track.
    for (int i = 0; i < 30; ++i) {
        fit::Mesg rec(FIT_MESG_NUM_RECORD);
        fillAll(rec, kStart + i);
        rec.SetFieldSINT32Value(fit::RecordMesg::FieldDefNum::PositionLat, 626000000 + i * 1000);
        rec.SetFieldSINT32Value(fit::RecordMesg::FieldDefNum::PositionLong, 60000000 + i * 1500);
        rec.SetFieldUINT8Value(fit::RecordMesg::FieldDefNum::HeartRate, 140 + i);
        rec.SetFieldUINT16Value(fit::RecordMesg::FieldDefNum::Power, 200 + i);
        rec.SetFieldUINT8Value(fit::RecordMesg::FieldDefNum::Cadence, 90);
        fit::DeveloperField core(coreTemp, devId);
        core.AddValue(38.5);
        rec.AddDeveloperField(core);
        fit::DeveloperField dpow(devPower, devId);
        dpow.AddValue(250.0);
        rec.AddDeveloperField(dpow);
        encode.Write(rec);
    }

    // one event per event type, data filled (alert events carry thresholds)
    for (int ev = 0; ev < 256; ++ev) {
        fit::Mesg event(FIT_MESG_NUM_EVENT);
        fillAll(event, kStart + 5);
        event.SetFieldENUMValue(0, static_cast<FIT_ENUM>(ev));
        event.SetFieldUINT32Value(3, 150);
        encode.Write(event);
    }

    // laps, session, activity, and one message of every other kind in the profile
    for (int i = 0; i < fit::Profile::MESGS; ++i) {
        const fit::Profile::MESG& m = fit::Profile::mesgs[i];
        if (m.num == FIT_MESG_NUM_FILE_ID || m.num == FIT_MESG_NUM_RECORD || m.num == FIT_MESG_NUM_EVENT
            || m.num == FIT_MESG_NUM_DEVELOPER_DATA_ID || m.num == FIT_MESG_NUM_FIELD_DESCRIPTION
            || m.num == FIT_MESG_NUM_PAD) {
            continue;
        }
        fit::Mesg mesg(m.num);
        fillAll(mesg, kStart + 60);
        if (m.num == FIT_MESG_NUM_LAP || m.num == FIT_MESG_NUM_SESSION) {
            fit::DeveloperField core(coreTemp, devId);
            core.AddValue(38.5);
            mesg.AddDeveloperField(core);
        }
        encode.Write(mesg);
    }

    if (!encode.Close()) {
        return 3;
    }
    return 0;
}

class DumpListener : public fit::MesgListener {
public:
    void OnMesg(fit::Mesg& mesg) override {
        std::cout << "mesg " << mesg.GetNum() << " " << mesg.GetName() << " fields";
        const int n = mesg.GetNumFields();
        for (int i = 0; i < n; ++i) {
            const fit::Field* f = mesg.GetFieldByIndex(static_cast<FIT_UINT16>(i));
            if (f == nullptr || f->GetIsExpanded()) {
                continue;
            }
            std::cout << " " << static_cast<int>(f->GetNum());
        }
        std::cout << " dev " << mesg.GetNumDevFields();
        if (mesg.GetNum() == FIT_MESG_NUM_EVENT) {
            const fit::Field* e = mesg.GetField(static_cast<FIT_UINT8>(0));
            if (e != nullptr && e->GetNumValues() > 0) {
                std::cout << " event " << static_cast<int>(e->GetENUMValue());
            }
        }
        std::cout << "\n";
    }
};

int dump(const std::string& path) {
    std::fstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        return 2;
    }
    fit::Decode decode;
    if (!decode.IsFIT(file) || !decode.CheckIntegrity(file)) {
        std::cerr << "not a valid FIT file\n";
        return 3;
    }
    file.clear();
    file.seekg(0, std::ios::beg);
    DumpListener listener;
    if (!decode.Read(file, listener)) {
        return 4;
    }
    return 0;
}

// checktable <tsv>: the table against the SDK profile itself.
//  - every KEEP row exists in the profile with that number and name
//  - every message of the profile has a decision, every field of a kept message has a row
//  - no KEEP row looks like health data (hard-coded deny list, independent of the table)
int checkTable(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        return 2;
    }
    struct Row { std::string kind, mname, fname, dec; int mnum, fnum; };
    std::vector<Row> rows;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> c;
        std::size_t start = 0;
        for (;;) {
            const std::size_t tab = line.find('\t', start);
            c.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
            if (tab == std::string::npos) break;
            start = tab + 1;
        }
        if (c.size() < 8) { std::cerr << "bad row: " << line << "\n"; return 3; }
        rows.push_back({c[0], c[2], c[4], c[6], std::stoi(c[1]), c[3].empty() ? -1 : std::stoi(c[3])});
    }
    int errors = 0;
    auto err = [&](const std::string& m) { std::cerr << "ERROR: " << m << "\n"; ++errors; };
    // whole "_" separated words that must never be KEPT, and substrings that must never appear
    static const char* denyWords[] = {"hr", "hrv", "power", "cadence", "calories", "stress", "spo2", "torque", "pco",
                                      "stance", "oscillation", "rmssd", "sdrr", "stroke", "strokes", "stride", "strides",
                                      "vo2", "effectiveness", "smoothness", "hemoglobin", "work", "rpe", "feel", "zone"};
    static const char* denySubstr[] = {"heart", "respirat", "calorie", "core_temp", "body_temp", "cadence", "hemoglobin"};
    std::map<int, std::set<int>> rowsFor;
    std::set<int> decided;
    for (const Row& r : rows) {
        if (r.kind == "M") {
            decided.insert(r.mnum);
            const fit::Profile::MESG* m = fit::Profile::GetMesg(static_cast<FIT_UINT16>(r.mnum));
            if (m == nullptr || m->name != r.mname || m->num != r.mnum) err("message " + std::to_string(r.mnum) + " " + r.mname + " not in profile");
        } else if (r.kind == "F") {
            rowsFor[r.mnum].insert(r.fnum);
            const fit::Profile::FIELD* f = fit::Profile::GetField(static_cast<FIT_UINT16>(r.mnum), static_cast<FIT_UINT8>(r.fnum));
            if (f == nullptr || f->name != r.fname) { err("field " + std::to_string(r.mnum) + "." + std::to_string(r.fnum) + " " + r.fname + " not in profile"); continue; }
            if (r.dec == "K") {
                const std::string w = "_" + r.fname + "_";
                for (const char* d : denyWords) {
                    if (w.find(std::string("_") + d + "_") != std::string::npos) err("KEEP field looks like health data: " + std::to_string(r.mnum) + "." + r.fname + " (word " + d + ")");
                }
                for (const char* d : denySubstr) {
                    if (r.fname.find(d) != std::string::npos) err("KEEP field looks like health data: " + std::to_string(r.mnum) + "." + r.fname + " (" + d + ")");
                }
            }
        } else if (r.kind == "E" && r.dec == "K") {
            for (const char* d : {"hr_", "heart", "cad_", "recovery", "calorie", "power_high", "power_low"}) {
                if (r.fname.find(d) != std::string::npos) err("KEEP event looks like health data: " + r.fname);
            }
        }
    }
    for (int i = 0; i < fit::Profile::MESGS; ++i) {
        const fit::Profile::MESG& m = fit::Profile::mesgs[i];
        if (!decided.count(m.num)) err("profile message " + std::to_string(m.num) + " " + m.name + " has no decision");
        bool kept = false;
        for (const Row& r : rows) if (r.kind == "M" && r.mnum == m.num && r.dec == "K") kept = true;
        if (!kept) continue;
        for (int j = 0; j < m.numFields; ++j) {
            if (!rowsFor[m.num].count(m.fields[j].num)) err("field " + std::to_string(m.num) + "." + m.fields[j].name + " of a kept message has no row");
        }
    }
    std::cout << rows.size() << " rows checked against profile " << FIT_PROFILE_VERSION_MAJOR << "." << FIT_PROFILE_VERSION_MINOR << ", " << errors << " errors\n";
    return errors == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "rich") == 0) {
        return makeRich(argv[2]);
    }
    if (argc == 3 && std::strcmp(argv[1], "dump") == 0) {
        return dump(argv[2]);
    }
    if (argc == 3 && std::strcmp(argv[1], "checktable") == 0) {
        return checkTable(argv[2]);
    }
    std::cerr << "usage: health-fit-tool rich <out.fit> | dump <in.fit> | checktable <tsv>\n";
    return 1;
}
