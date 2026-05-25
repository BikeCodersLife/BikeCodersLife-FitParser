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

    // Carry the previous track point across iterations so we can derive
    // per-record speed from the cumulative-distance + timestamp delta
    // when the source didn't supply explicit <speed> records.
    TrackPoint prevPoint;
    bool hasPrevPoint = false;

    // GPS-derived speed only: hold the previous accepted speed so a single
    // doubled-distance sample (a GPS position glitch) cannot spike the
    // session/lap max. A bicycle cannot gain more than ~5 m/s² (18 km/h per
    // second) even sprinting or on a steep descent; a derived speed implying
    // more than that between consecutive samples is a position artefact, not
    // a real burst. v2.1.3 — fixes the 65 km/h-from-41 km/h max-speed
    // overshoot on Strava-synth rides that lack explicit <speed>.
    constexpr double kMaxPlausibleAccelMs2 = 5.0;
    float prevAcceptedSpeedMs = 0.0f;
    bool hasAcceptedSpeed = false;

    for (const auto& point : activity.points) {
        fit::RecordMesg record;

        // Timestamp
        if (point.timestamp > 0) {
            record.SetTimestamp(point.timestamp);
        }

        // Position — omit entirely when hasPosition=false so gps-stripper
        // can redact the start/end trim zones. Per FIT spec a RECORD without
        // position is valid; the head unit / Strava will just render a gap
        // in the trace, which is the privacy guarantee we promise. Before
        // this gate, every stripped point still leaked its original GPS.
        if (point.hasPosition) {
            record.SetPositionLat(degreesToSemicircles(point.lat));
            record.SetPositionLong(degreesToSemicircles(point.lon));
        }

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

        // Speed — prefer explicit per-point speed; otherwise derive from
        // the GPS-derived cumulative distance + timestamp delta so the
        // session/lap max-speed is a real peak instead of equal to the
        // average. Earlier this branch wrote no per-record speed at all
        // when the source GPX lacked <speed>, and the session-block
        // fallback at the bottom collapsed maxSpeed to distance/elapsed
        // = avgSpeed (the "MAX = AVG" symptom on every Strava-synth ride).
        float pointSpeedMs = point.speed;
        bool pointHasSpeed = point.hasSpeed && point.speed > 0.0f;
        bool speedWasDerived = false;
        if (!pointHasSpeed && hasPrevPoint
            && point.timestamp > prevPoint.timestamp
            && point.distance > prevPoint.distance) {
            uint32_t dt = point.timestamp - prevPoint.timestamp;
            double segMeters = point.distance - prevPoint.distance;
            if (dt > 0 && segMeters > 0.0) {
                pointSpeedMs = static_cast<float>(segMeters / static_cast<double>(dt));
                pointHasSpeed = true;
                speedWasDerived = true;
                // Reject single-sample GPS-distance glitches: a derived speed
                // implying impossible acceleration vs the previous accepted
                // sample is a position spike, not a real burst. Hold the prior
                // speed so the glitch neither inflates the session max nor
                // leaks into the per-record stream that consumers smooth.
                if (hasAcceptedSpeed) {
                    double accel = (static_cast<double>(pointSpeedMs) - prevAcceptedSpeedMs)
                                 / static_cast<double>(dt);
                    if (accel > kMaxPlausibleAccelMs2) {
                        pointSpeedMs = prevAcceptedSpeedMs;
                    }
                }
            }
        }
        if (pointHasSpeed) {
            record.SetSpeed(pointSpeedMs);
            totalSpeed += pointSpeedMs;
            speedCount++;
            if (pointSpeedMs > maxSpeed) maxSpeed = pointSpeedMs;
            if (speedWasDerived) {
                prevAcceptedSpeedMs = pointSpeedMs;
                hasAcceptedSpeed = true;
            }
        }

        encode.Write(record);
        prevPoint = point;
        hasPrevPoint = true;
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
        // gpsValid=false → hasPosition=false so the writer omits lat/lon.
        // Without this propagation, gps-stripper marked the trim zone but
        // the writer happily wrote the original coordinates back out, so
        // privacy stripping was a no-op for every archived ride.
        pt.hasPosition = coord.gpsValid;
        pt.elevation = coord.elevation;
        pt.hasElevation = (coord.elevation != 0.0);
        pt.timestamp = coord.timestamp;

        // Sensor fields — these were silently dropped by the previous
        // conversion (only lat/lon/elevation/timestamp copied), so the
        // gps-strip → re-write cycle wiped HR / power / cadence /
        // temperature / speed from every archived FIT.
        pt.heart_rate = coord.heartRate;
        pt.hasHeartRate = coord.hasHeartRate;
        pt.cadence = coord.cadence;
        pt.hasCadence = coord.hasCadence;
        pt.power = coord.power;
        pt.hasPower = coord.hasPower;
        pt.temperature = coord.temperature;
        pt.hasTemperature = coord.hasTemperature;
        // Coordinate.speed is km/h (computed from GPS deltas); TrackPoint.speed
        // is m/s (FIT-native). Convert so the FIT writer doesn't silently
        // serialise a 3.6× over-stated speed.
        pt.speed = static_cast<float>(coord.speed / 3.6);
        pt.hasSpeed = coord.hasSpeed;

        activity.points.push_back(pt);
    }

    write(activity, outputPath);
}
