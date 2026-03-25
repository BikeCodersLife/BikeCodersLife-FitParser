#include "fit_writer.h"
#include <fstream>
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <numeric>

#include <fit_encode.hpp>
#include <fit_file_id_mesg.hpp>
#include <fit_event_mesg.hpp>
#include <fit_record_mesg.hpp>
#include <fit_lap_mesg.hpp>
#include <fit_session_mesg.hpp>
#include <fit_activity_mesg.hpp>
#include <fit_date_time.hpp>

int32_t FitWriter::degreesToSemicircles(double degrees) {
    // 2^31 semicircles = 180 degrees
    return static_cast<int32_t>(degrees * (std::pow(2, 31) / 180.0));
}

void FitWriter::write(const ParsedActivity& activity, const std::string& outputPath) {
    if (activity.points.empty()) {
        throw std::runtime_error("Cannot write FIT file: no track points");
    }

    // Open output file
    std::fstream file;
    file.open(outputPath, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open output file: " + outputPath);
    }

    // Create FIT encoder (Protocol V2.0 for modern compatibility)
    fit::Encode encode(fit::ProtocolVersion::V20);
    encode.Open(file);

    // --- File ID Message (required) ---
    // Privacy by design: no serial number, no user name, no device info
    fit::FileIdMesg fileIdMesg;
    fileIdMesg.SetType(FIT_FILE_ACTIVITY);
    fileIdMesg.SetManufacturer(FIT_MANUFACTURER_DEVELOPMENT);
    fileIdMesg.SetProduct(0);
    fileIdMesg.SetTimeCreated(activity.startTime);
    encode.Write(fileIdMesg);

    // --- Timer Start Event ---
    fit::EventMesg eventStart;
    eventStart.SetTimestamp(activity.startTime);
    eventStart.SetEvent(FIT_EVENT_TIMER);
    eventStart.SetEventType(FIT_EVENT_TYPE_START);
    encode.Write(eventStart);

    // --- Record Messages (per track point) ---
    // Track statistics for session/lap summary
    double maxSpeed = 0.0;
    uint8_t maxHr = 0;
    uint8_t maxCad = 0;
    uint16_t maxPwr = 0;
    double totalSpeed = 0.0;
    uint32_t hrSum = 0;
    uint32_t cadSum = 0;
    uint32_t pwrSum = 0;
    size_t hrCount = 0;
    size_t cadCount = 0;
    size_t pwrCount = 0;
    size_t speedCount = 0;

    for (const auto& point : activity.points) {
        fit::RecordMesg record;

        // Timestamp
        if (point.timestamp > 0) {
            record.SetTimestamp(point.timestamp);
        }

        // Position
        record.SetPositionLat(degreesToSemicircles(point.lat));
        record.SetPositionLong(degreesToSemicircles(point.lon));

        // Elevation
        if (point.hasElevation) {
            record.SetAltitude(static_cast<FIT_FLOAT32>(point.elevation));
        }

        // Distance (cumulative, in meters)
        if (point.distance > 0.0) {
            record.SetDistance(static_cast<FIT_FLOAT32>(point.distance));
        }

        // Heart rate
        if (point.hasHeartRate && point.heart_rate > 0) {
            record.SetHeartRate(point.heart_rate);
            hrSum += point.heart_rate;
            hrCount++;
            if (point.heart_rate > maxHr) maxHr = point.heart_rate;
        }

        // Cadence
        if (point.hasCadence && point.cadence > 0) {
            record.SetCadence(point.cadence);
            cadSum += point.cadence;
            cadCount++;
            if (point.cadence > maxCad) maxCad = point.cadence;
        }

        // Power
        if (point.hasPower && point.power > 0) {
            record.SetPower(point.power);
            pwrSum += point.power;
            pwrCount++;
            if (point.power > maxPwr) maxPwr = point.power;
        }

        // Temperature
        if (point.hasTemperature) {
            record.SetTemperature(point.temperature);
        }

        // Speed
        if (point.hasSpeed && point.speed > 0.0f) {
            record.SetSpeed(point.speed);
            totalSpeed += point.speed;
            speedCount++;
            if (point.speed > maxSpeed) maxSpeed = point.speed;
        }

        encode.Write(record);
    }

    // --- Timer Stop Event ---
    fit::EventMesg eventStop;
    eventStop.SetTimestamp(activity.endTime);
    eventStop.SetEvent(FIT_EVENT_TIMER);
    eventStop.SetEventType(FIT_EVENT_TYPE_STOP);
    encode.Write(eventStop);

    // Compute summary values
    FIT_FLOAT32 elapsedTime = static_cast<FIT_FLOAT32>(activity.durationSec);
    FIT_FLOAT32 totalDist = static_cast<FIT_FLOAT32>(activity.totalDistanceM);
    FIT_FLOAT32 avgSpeed = (speedCount > 0) ? static_cast<FIT_FLOAT32>(totalSpeed / speedCount) : 0.0f;

    // If no explicit speed data, compute from distance/time
    if (speedCount == 0 && elapsedTime > 0 && totalDist > 0) {
        avgSpeed = totalDist / elapsedTime;
        maxSpeed = avgSpeed; // Best estimate without per-point data
    }

    // --- Lap Message ---
    fit::LapMesg lap;
    lap.SetTimestamp(activity.endTime);
    lap.SetStartTime(activity.startTime);
    lap.SetTotalElapsedTime(elapsedTime);
    lap.SetTotalTimerTime(elapsedTime);
    lap.SetTotalDistance(totalDist);

    if (activity.totalAscentM > 0) {
        lap.SetTotalAscent(static_cast<FIT_UINT16>(std::round(activity.totalAscentM)));
    }
    if (activity.totalDescentM > 0) {
        lap.SetTotalDescent(static_cast<FIT_UINT16>(std::round(activity.totalDescentM)));
    }
    if (avgSpeed > 0) {
        lap.SetAvgSpeed(avgSpeed);
    }
    if (maxSpeed > 0) {
        lap.SetMaxSpeed(static_cast<FIT_FLOAT32>(maxSpeed));
    }

    encode.Write(lap);

    // --- Session Message ---
    fit::SessionMesg session;
    session.SetTimestamp(activity.endTime);
    session.SetStartTime(activity.startTime);
    session.SetTotalElapsedTime(elapsedTime);
    session.SetTotalTimerTime(elapsedTime);
    session.SetTotalDistance(totalDist);
    session.SetSport(FIT_SPORT_CYCLING);
    session.SetSubSport(FIT_SUB_SPORT_GENERIC);
    session.SetFirstLapIndex(0);
    session.SetNumLaps(1);

    if (activity.totalAscentM > 0) {
        session.SetTotalAscent(static_cast<FIT_UINT16>(std::round(activity.totalAscentM)));
    }
    if (activity.totalDescentM > 0) {
        session.SetTotalDescent(static_cast<FIT_UINT16>(std::round(activity.totalDescentM)));
    }
    if (avgSpeed > 0) {
        session.SetAvgSpeed(avgSpeed);
    }
    if (maxSpeed > 0) {
        session.SetMaxSpeed(static_cast<FIT_FLOAT32>(maxSpeed));
    }
    if (hrCount > 0) {
        session.SetAvgHeartRate(static_cast<FIT_UINT8>(hrSum / hrCount));
        session.SetMaxHeartRate(maxHr);
    }
    if (cadCount > 0) {
        session.SetAvgCadence(static_cast<FIT_UINT8>(cadSum / cadCount));
        session.SetMaxCadence(maxCad);
    }
    if (pwrCount > 0) {
        session.SetAvgPower(static_cast<FIT_UINT16>(pwrSum / pwrCount));
        session.SetMaxPower(maxPwr);
    }

    encode.Write(session);

    // --- Activity Message (exactly one required) ---
    fit::ActivityMesg activityMesg;
    activityMesg.SetTimestamp(activity.endTime);
    activityMesg.SetNumSessions(1);
    activityMesg.SetLocalTimestamp(static_cast<FIT_LOCAL_DATE_TIME>(activity.endTime));
    encode.Write(activityMesg);

    // Finalize: update header data size and write CRC
    if (!encode.Close()) {
        file.close();
        throw std::runtime_error("Error closing FIT encoder");
    }

    file.close();
}

void FitWriter::write(const std::string& outputPath, const RideStatistic& stats) {
    // Convert legacy RideStatistic to ParsedActivity
    ParsedActivity activity;
    activity.totalDistanceM = stats.distanceKm * 1000.0;
    activity.durationSec = stats.durationMin * 60.0;
    activity.startTime = stats.startTime;
    activity.endTime = stats.endTime;

    for (const auto& coord : stats.coordinates) {
        TrackPoint pt;
        pt.lat = coord.lat;
        pt.lon = coord.lon;
        pt.elevation = coord.elevation;
        pt.hasElevation = (coord.elevation != 0.0);
        pt.timestamp = coord.timestamp;
        activity.points.push_back(pt);
    }

    write(activity, outputPath);
}
