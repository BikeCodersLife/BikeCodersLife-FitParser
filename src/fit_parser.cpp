#include "fit_parser.h"
#include <fstream>
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <fit_decode.hpp>
#include <fit_mesg_broadcaster.hpp>
#include <fit_record_mesg.hpp>
#include <fit_session_mesg.hpp>
#include <fit_file_id_mesg.hpp>
#include <fit_profile.hpp>

/**
 * Holds the FIT session-message totals captured during decoding.
 * Each field's bool is independent because a session message can be
 * present without all fields being valid (e.g. an indoor trainer file
 * may carry avg_speed but no total_ascent).
 */
struct SessionTotals {
    bool hasDistance = false;
    double distanceM = 0.0;
    bool hasAscent = false;
    double ascentM = 0.0;
    bool hasDescent = false;
    double descentM = 0.0;
    bool hasMaxSpeed = false;
    double maxSpeedMs = 0.0;
    bool hasAvgSpeed = false;
    double avgSpeedMs = 0.0;
    bool hasElapsed = false;
    double elapsedSec = 0.0;
    bool hasMoving = false;
    double movingSec = 0.0;
    bool hasSport = false;
    uint8_t sport = 0;
    bool hasSubSport = false;
    uint8_t subSport = 0;
};

/**
 * FIT FileId metadata captured for indoor / source-app detection.
 * Populated once per file (FileId is the first message in every FIT file).
 */
struct FileIdInfo {
    bool hasManufacturer = false;
    uint16_t manufacturer = 0;
    bool hasGarminProduct = false;
    uint16_t garminProduct = 0;
    bool hasProductName = false;
    std::string productName;
};

/**
 * Lossy FIT_WSTRING (std::wstring) → UTF-8 std::string conversion. All
 * known indoor-app product_name values ("MyWhoosh", "Zwift", "Rouvy",
 * "Tacx Training Application", etc.) are pure ASCII, so a naive cast is
 * fine for matching. Non-ASCII characters get replaced with '?' rather
 * than producing mojibake.
 */
static std::string wstringToUtf8Lossy(const std::wstring& w) {
    std::string out;
    out.reserve(w.size());
    for (wchar_t c : w) {
        out.push_back((c >= 0 && c < 128) ? static_cast<char>(c) : '?');
    }
    return out;
}

/**
 * Single listener that captures both per-record coordinate data and the
 * (typically single) session-summary message in one pass through the FIT
 * file. Cheaper than two listeners + a MesgBroadcaster.
 */
class CoordinateListener : public fit::MesgListener {
public:
    std::vector<Coordinate> coordinates;
    SessionTotals session;
    FileIdInfo fileId;

    void OnMesg(fit::Mesg& mesg) override {
        const auto num = mesg.GetNum();

        if (num == FIT_MESG_NUM_FILE_ID) {
            fit::FileIdMesg fileIdMesg(mesg);
            if (fileIdMesg.IsManufacturerValid()) {
                fileId.hasManufacturer = true;
                fileId.manufacturer = fileIdMesg.GetManufacturer();
            }
            if (fileIdMesg.IsGarminProductValid()) {
                fileId.hasGarminProduct = true;
                fileId.garminProduct = fileIdMesg.GetGarminProduct();
            }
            if (fileIdMesg.IsProductNameValid()) {
                fileId.hasProductName = true;
                fileId.productName = wstringToUtf8Lossy(fileIdMesg.GetProductName());
            }
            return;
        }

        if (num == FIT_MESG_NUM_RECORD) {
            fit::RecordMesg recordMesg(mesg);

            if (recordMesg.IsPositionLatValid() && recordMesg.IsPositionLongValid()) {
                Coordinate coord;

                // FIT semicircles → degrees: 2^31 semicircles = 180°
                coord.lat = recordMesg.GetPositionLat() * (180.0 / std::pow(2, 31));
                coord.lon = recordMesg.GetPositionLong() * (180.0 / std::pow(2, 31));

                coord.elevation = recordMesg.IsAltitudeValid() ? recordMesg.GetAltitude() : 0.0;
                coord.timestamp = recordMesg.IsTimestampValid() ? recordMesg.GetTimestamp() : 0;

                // Health Data
                coord.hasHeartRate = recordMesg.IsHeartRateValid();
                coord.heartRate = coord.hasHeartRate ? recordMesg.GetHeartRate() : 0;

                coord.hasPower = recordMesg.IsPowerValid();
                coord.power = coord.hasPower ? recordMesg.GetPower() : 0;

                coord.hasCadence = recordMesg.IsCadenceValid();
                coord.cadence = coord.hasCadence ? recordMesg.GetCadence() : 0;

                coord.hasTemperature = recordMesg.IsTemperatureValid();
                coord.temperature = coord.hasTemperature ? recordMesg.GetTemperature() : 0;

                coordinates.push_back(coord);
            }
            return;
        }

        if (num == FIT_MESG_NUM_SESSION) {
            fit::SessionMesg sessionMesg(mesg);

            if (sessionMesg.IsTotalDistanceValid()) {
                session.hasDistance = true;
                session.distanceM = sessionMesg.GetTotalDistance();
            }
            if (sessionMesg.IsTotalAscentValid()) {
                session.hasAscent = true;
                session.ascentM = sessionMesg.GetTotalAscent();
            }
            if (sessionMesg.IsTotalDescentValid()) {
                session.hasDescent = true;
                session.descentM = sessionMesg.GetTotalDescent();
            }
            if (sessionMesg.IsMaxSpeedValid()) {
                session.hasMaxSpeed = true;
                session.maxSpeedMs = sessionMesg.GetMaxSpeed();
            }
            if (sessionMesg.IsAvgSpeedValid()) {
                session.hasAvgSpeed = true;
                session.avgSpeedMs = sessionMesg.GetAvgSpeed();
            }
            if (sessionMesg.IsTotalElapsedTimeValid()) {
                session.hasElapsed = true;
                session.elapsedSec = sessionMesg.GetTotalElapsedTime();
            }
            if (sessionMesg.IsTotalTimerTimeValid()) {
                session.hasMoving = true;
                session.movingSec = sessionMesg.GetTotalTimerTime();
            }
            if (sessionMesg.IsSportValid()) {
                session.hasSport = true;
                session.sport = static_cast<uint8_t>(sessionMesg.GetSport());
            }
            if (sessionMesg.IsSubSportValid()) {
                session.hasSubSport = true;
                session.subSport = static_cast<uint8_t>(sessionMesg.GetSubSport());
            }
        }
    }
};

FitParser::FitParser(const std::string& filename) : filename_(filename) {}

RideStatistic FitParser::extractCoordinates() {
    // Open FIT file
    std::fstream file(filename_, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + filename_);
    }
    
    // Create FIT decoder
    fit::Decode decode;
    CoordinateListener listener;
    
    // Decode FIT file - SDK 21.158 API uses references
    if (!decode.Read(file, listener)) {
        file.close();
        throw std::runtime_error("Failed to decode FIT file");
    }
    
    file.close();

    RideStatistic stats;
    stats.coordinates = listener.coordinates;
    stats.distanceKm = 0.0;
    stats.durationMin = 0.0;
    stats.startTime = 0;
    stats.endTime = 0;

    // Initialize health stats
    stats.avgHeartRate = 0;
    stats.maxHeartRate = 0;
    stats.avgPower = 0;
    stats.maxPower = 0;
    stats.avgCadence = 0;
    stats.avgSpeed = 0;
    stats.maxSpeed = 0;
    stats.movingTimeSec = 0;
    stats.hasHeartRateData = false;
    stats.hasPowerData = false;
    stats.hasCadenceData = false;

    // Roadmap #156: copy session totals through to the result. PHP
    // prefers these over the Haversine sum below when present.
    if (listener.session.hasDistance) {
        stats.hasSessionDistance = true;
        stats.sessionDistanceKm = std::round((listener.session.distanceM / 1000.0) * 100.0) / 100.0;
    }
    if (listener.session.hasAscent) {
        stats.hasSessionAscent = true;
        stats.sessionElevationGainM = listener.session.ascentM;
    }
    if (listener.session.hasDescent) {
        stats.hasSessionDescent = true;
        stats.sessionElevationLossM = listener.session.descentM;
    }
    if (listener.session.hasMaxSpeed) {
        stats.hasSessionMaxSpeed = true;
        stats.sessionMaxSpeedKmh = std::round((listener.session.maxSpeedMs * 3.6) * 10.0) / 10.0;
    }
    if (listener.session.hasAvgSpeed) {
        stats.hasSessionAvgSpeed = true;
        stats.sessionAvgSpeedKmh = std::round((listener.session.avgSpeedMs * 3.6) * 10.0) / 10.0;
    }
    if (listener.session.hasElapsed) {
        stats.hasSessionElapsed = true;
        stats.sessionElapsedSec = listener.session.elapsedSec;
    }
    if (listener.session.hasMoving) {
        stats.hasSessionMoving = true;
        stats.sessionMovingSec = listener.session.movingSec;
    }

    // Indoor / source-app metadata captured from the FileId + Session
    // messages, with isIndoor derived from the most reliable signals only:
    //   - sub_sport == INDOOR_CYCLING (6) or VIRTUAL_ACTIVITY (58)
    //   - manufacturer == ZWIFT (260) / ZWIFT_BYTE (144) / MYWHOOSH (331)
    //     / BKOOL (67) — apps that ship *only* indoor experiences
    // Tacx is intentionally NOT in the manufacturer auto-flag list because
    // the same vendor id appears on Tacx outdoor head units; a Tacx file
    // still flags indoor when its sub_sport says so.
    if (listener.fileId.hasManufacturer) {
        stats.hasManufacturer = true;
        stats.manufacturer = listener.fileId.manufacturer;
    }
    if (listener.fileId.hasGarminProduct) {
        stats.hasGarminProduct = true;
        stats.garminProduct = listener.fileId.garminProduct;
    }
    if (listener.fileId.hasProductName) {
        stats.hasProductName = true;
        stats.productName = listener.fileId.productName;
    }
    if (listener.session.hasSport) {
        stats.hasSport = true;
        stats.sport = listener.session.sport;
    }
    if (listener.session.hasSubSport) {
        stats.hasSubSport = true;
        stats.subSport = listener.session.subSport;
    }

    if (stats.hasSubSport
        && (stats.subSport == FIT_SUB_SPORT_INDOOR_CYCLING
            || stats.subSport == FIT_SUB_SPORT_VIRTUAL_ACTIVITY)) {
        stats.isIndoor = true;
    }
    if (!stats.isIndoor && stats.hasManufacturer) {
        switch (stats.manufacturer) {
            case FIT_MANUFACTURER_ZWIFT:
            case FIT_MANUFACTURER_ZWIFT_BYTE:
            case FIT_MANUFACTURER_MYWHOOSH:
            case FIT_MANUFACTURER_BKOOL:
                stats.isIndoor = true;
                break;
            default:
                break;
        }
    }

    if (stats.coordinates.empty()) {
        return stats;
    }

    // Calculate stats
    double totalDistanceMeters = 0.0;
    stats.startTime = stats.coordinates.front().timestamp;
    stats.endTime = stats.coordinates.back().timestamp;
    
    // Accumulators for averages
    double totalHeartRate = 0;
    long countHeartRate = 0;
    double totalPower = 0;
    long countPower = 0;
    double totalCadence = 0;
    long countCadence = 0;

    // Calculate duration in minutes (difference between start and end timestamps)
    // FIT timestamps are seconds since epoch
    if (stats.endTime > stats.startTime) {
        stats.durationMin = (stats.endTime - stats.startTime) / 60.0;
    }
    
    // Speed accumulators
    double totalMovingSpeed = 0.0;
    long countMovingSpeed = 0;
    double movingTimeSec = 0.0;
    const double movingThreshold = 1.0; // km/h — below this = stopped

    // Iterate through coordinates to calculate distance, speed, and health stats
    for (size_t i = 0; i < stats.coordinates.size(); ++i) {
        auto& point = stats.coordinates[i];

        // Distance and per-point speed
        if (i > 0) {
            const auto& prev = stats.coordinates[i-1];
            double segmentMeters = calculateDistance(prev.lat, prev.lon, point.lat, point.lon);
            totalDistanceMeters += segmentMeters;

            // Compute GPS-derived speed (km/h) from consecutive points
            uint32_t dt = point.timestamp - prev.timestamp;
            if (dt > 0) {
                double speedKmh = (segmentMeters / 1000.0) / (dt / 3600.0);
                point.speed = std::round(speedKmh * 10.0) / 10.0;
                point.hasSpeed = true;

                // Moving time and speed stats
                if (point.speed > movingThreshold) {
                    totalMovingSpeed += point.speed;
                    countMovingSpeed++;
                    movingTimeSec += dt;
                }
                if (point.speed > stats.maxSpeed) {
                    stats.maxSpeed = point.speed;
                }
            }
        }

        // Heart Rate
        if (point.hasHeartRate) {
            stats.hasHeartRateData = true;
            totalHeartRate += point.heartRate;
            countHeartRate++;
            if (point.heartRate > stats.maxHeartRate) {
                stats.maxHeartRate = point.heartRate;
            }
        }

        // Power
        if (point.hasPower) {
            stats.hasPowerData = true;
            totalPower += point.power;
            countPower++;
            if (point.power > stats.maxPower) {
                stats.maxPower = point.power;
            }
        }

        // Cadence
        if (point.hasCadence) {
            stats.hasCadenceData = true;
            totalCadence += point.cadence;
            countCadence++;
        }
    }

    // Copy speed from second point to first (no delta available for first point)
    if (stats.coordinates.size() >= 2 && stats.coordinates[1].hasSpeed) {
        stats.coordinates[0].speed = stats.coordinates[1].speed;
        stats.coordinates[0].hasSpeed = true;
    }

    stats.distanceKm = totalDistanceMeters / 1000.0;

    // Round to 2 decimal places for consistency
    stats.distanceKm = std::round(stats.distanceKm * 100.0) / 100.0;
    stats.durationMin = std::round(stats.durationMin * 100.0) / 100.0;
    stats.maxSpeed = std::round(stats.maxSpeed * 10.0) / 10.0;
    stats.movingTimeSec = movingTimeSec;

    // Calculate Averages
    if (countHeartRate > 0) stats.avgHeartRate = totalHeartRate / countHeartRate;
    if (countPower > 0) stats.avgPower = totalPower / countPower;
    if (countCadence > 0) stats.avgCadence = totalCadence / countCadence;
    if (countMovingSpeed > 0) stats.avgSpeed = std::round((totalMovingSpeed / countMovingSpeed) * 10.0) / 10.0;

    return stats;
}

double FitParser::calculateDistance(double lat1, double lon1, double lat2, double lon2) {
    const double earthRadius = 6371003.0; // Mean Earth Radius in meters
    const double degToRad = M_PI / 180.0;

    double dLat = (lat2 - lat1) * degToRad;
    double dLon = (lon2 - lon1) * degToRad;

    double a = std::sin(dLat / 2) * std::sin(dLat / 2) +
               std::cos(lat1 * degToRad) * std::cos(lat2 * degToRad) *
               std::sin(dLon / 2) * std::sin(dLon / 2);

    double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));

    return earthRadius * c;
}
