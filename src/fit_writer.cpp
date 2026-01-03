#include "fit_writer.h"
#include <fstream>
#include <stdexcept>
#include <cmath>
#include <ctime>

// Garmin FIT SDK
#include <fit_encode.hpp>
#include <fit_file_id_mesg.hpp>
#include <fit_record_mesg.hpp>
#include <fit_event_mesg.hpp>
#include <fit_session_mesg.hpp>
#include <fit_activity_mesg.hpp>
#include <fit_lap_mesg.hpp>

FitWriter::FitWriter() {}

void FitWriter::write(const std::string& filename, const RideStatistic& stats) {
    if (filename.empty()) {
        throw std::runtime_error("Invalid filename");
    }

    std::fstream file;
    file.open(filename.c_str(), std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);

    if (!file.is_open()) {
        throw std::runtime_error("Error opening file for writing: " + filename);
    }

    fit::Encode encode(fit::ProtocolVersion::V20);
    encode.Open(file);

    // 1. File ID Message
    fit::FileIdMesg fileIdMesg;
    fileIdMesg.SetType(FIT_FILE_ACTIVITY);
    fileIdMesg.SetManufacturer(FIT_MANUFACTURER_DEVELOPMENT);
    fileIdMesg.SetProduct(0);
    fileIdMesg.SetSerialNumber(12345);
    // Use start time as file creation time if available, otherwise current time
    // FIT epoch is 1989-12-31 00:00:00 UTC (631065600 seconds offset from Unix epoch)
    // The timestamp in Coordinate is mostly Unix timestamp from GPX? 
    // If stats.startTime is Unix timestamp, we need to convert.
    // However, FitParser::extractCoordinates just takes `recordMesg.GetTimestamp()`.
    // If `GetTimestamp()` returns seconds since FIT epoch, then `stats.startTime` is FIT time.
    // If we write it back, we should use it as is.
    // GPX parser usually provides Unix timestamp.
    // Let's assume input stats uses the same time basis as output.
    // Since this is for converting GPX->FIT, input stats.startTime will be Unix Timestamp.
    // We need to convert Unix -> FIT.
    // offset = 631065600
    
    // Check if timestamp looks like FIT (small) or Unix (large, > 1000000000)
    // FIT epoch 0 is 1989.
    // Unix now is ~1.7 billion.
    // FIT now is ~1.1 billion.
    // Difference is ~631 million.
    
    // Just in case, let's normalize to FIT time.
    // If > 1600000000 (roughly 2020 in Unix), subtract offset.
    uint32_t timeOffset = 631065600;
    
    uint32_t creationTime = stats.startTime;
    if (creationTime > 1600000000) {
        creationTime -= timeOffset;
    }
    
    fileIdMesg.SetTimeCreated(creationTime);
    encode.Write(fileIdMesg);

    // 2. Records
    if (!stats.coordinates.empty()) {
        // Start Event
        fit::EventMesg startEvent;
        startEvent.SetEvent(FIT_EVENT_TIMER);
        startEvent.SetEventType(FIT_EVENT_TYPE_START);
        startEvent.SetTimestamp(creationTime);
        encode.Write(startEvent);

        for (const auto& coord : stats.coordinates) {
            fit::RecordMesg record;
            
            // Timestamp
            uint32_t recordTime = coord.timestamp;
            if (recordTime > 1600000000) recordTime -= timeOffset;
            record.SetTimestamp(recordTime);

            // Coordinates (Degrees to Semicircles)
            // semicircles = degrees * (2^31 / 180)
            int32_t lat = (int32_t)(coord.lat * (2147483648.0 / 180.0));
            int32_t lon = (int32_t)(coord.lon * (2147483648.0 / 180.0));
            record.SetPositionLat(lat);
            record.SetPositionLong(lon);

            // Altitude (meters)
            if (coord.elevation != 0) {
                record.SetAltitude(static_cast<float>(coord.elevation));
            }

            // Health Data
            if (coord.hasHeartRate) {
                record.SetHeartRate(coord.heartRate);
            }
            if (coord.hasPower) {
                record.SetPower(coord.power);
            }
            if (coord.hasCadence) {
                record.SetCadence(coord.cadence);
            }
            if (coord.hasTemperature) {
                record.SetTemperature(coord.temperature);
            }

            encode.Write(record);
        }

        // Stop Event
        fit::EventMesg stopEvent;
        stopEvent.SetEvent(FIT_EVENT_TIMER);
        stopEvent.SetEventType(FIT_EVENT_TYPE_STOP_ALL);
        
        uint32_t endTime = stats.endTime;
        if (endTime > 1600000000) endTime -= timeOffset;
        stopEvent.SetTimestamp(endTime);
        
        encode.Write(stopEvent);
    }

    // 3. Lap (One single lap for the ride)
    fit::LapMesg lapMesg;
    uint32_t startTime = stats.startTime;
    if (startTime > 1600000000) startTime -= timeOffset;
    lapMesg.SetStartTime(startTime);
    lapMesg.SetTimestamp(startTime); // Should be end time? Usually timestamp is when message is written (end of lap)
    
    uint32_t endTime = stats.endTime;
    if (endTime > 1600000000) endTime -= timeOffset;
    lapMesg.SetTimestamp(endTime);
    
    lapMesg.SetTotalElapsedTime(stats.durationMin * 60.0);
    lapMesg.SetTotalTimerTime(stats.durationMin * 60.0);
    lapMesg.SetTotalDistance(stats.distanceKm * 1000.0);
    
    // Average/Max Health Data
    if (stats.hasHeartRateData) {
        lapMesg.SetAvgHeartRate((uint8_t)stats.avgHeartRate);
        lapMesg.SetMaxHeartRate((uint8_t)stats.maxHeartRate);
    }
    if (stats.hasPowerData) {
        lapMesg.SetAvgPower((uint16_t)stats.avgPower);
        lapMesg.SetMaxPower((uint16_t)stats.maxPower);
    }
    if (stats.hasCadenceData) {
        lapMesg.SetAvgCadence((uint8_t)stats.avgCadence);
    }
    
    encode.Write(lapMesg);

    // 4. Session
    fit::SessionMesg sessionMesg;
    sessionMesg.SetStartTime(startTime);
    sessionMesg.SetTimestamp(endTime);
    sessionMesg.SetTotalElapsedTime(stats.durationMin * 60.0);
    sessionMesg.SetTotalTimerTime(stats.durationMin * 60.0);
    sessionMesg.SetTotalDistance(stats.distanceKm * 1000.0);
    sessionMesg.SetSport(FIT_SPORT_CYCLING);
    sessionMesg.SetSubSport(FIT_SUB_SPORT_GENERIC);
    sessionMesg.SetNumLaps(1);
    
    // Session Health Data
    if (stats.hasHeartRateData) {
        sessionMesg.SetAvgHeartRate((uint8_t)stats.avgHeartRate);
        sessionMesg.SetMaxHeartRate((uint8_t)stats.maxHeartRate);
    }
    if (stats.hasPowerData) {
        sessionMesg.SetAvgPower((uint16_t)stats.avgPower);
        sessionMesg.SetMaxPower((uint16_t)stats.maxPower);
    }
    if (stats.hasCadenceData) {
        sessionMesg.SetAvgCadence((uint8_t)stats.avgCadence);
    }
    
    encode.Write(sessionMesg);

    // 5. Activity
    fit::ActivityMesg activityMesg;
    activityMesg.SetTimestamp(endTime);
    activityMesg.SetTotalTimerTime(stats.durationMin * 60.0);
    activityMesg.SetNumSessions(1);
    activityMesg.SetType(FIT_ACTIVITY_MANUAL);
    activityMesg.SetEvent(FIT_EVENT_ACTIVITY);
    activityMesg.SetEventType(FIT_EVENT_TYPE_STOP);
    encode.Write(activityMesg);

    encode.Close();
    file.close();
}
