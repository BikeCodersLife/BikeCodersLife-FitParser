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

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "rich") == 0) {
        return makeRich(argv[2]);
    }
    if (argc == 3 && std::strcmp(argv[1], "dump") == 0) {
        return dump(argv[2]);
    }
    std::cerr << "usage: health-fit-tool rich <out.fit> | dump <in.fit>\n";
    return 1;
}
