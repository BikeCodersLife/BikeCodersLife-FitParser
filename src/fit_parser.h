#ifndef FIT_PARSER_H
#define FIT_PARSER_H

#include <string>
#include <vector>
#include <cstdint>

#include "device_info_record.h"

/**
 * Coordinate structure representing a GPS point
 */
struct Coordinate {
    // Every field carries a default: a Coordinate built field-by-field (the
    // GPX/TCX path in main.cpp) must never emit stack garbage. v2.3.0 and
    // older left the sensor fields + has* flags uninitialised there, so every
    // GPX/TCX ride came out with one random power/cadence value on every
    // point (e.g. 59820 W / 176 rpm).
    double lat = 0.0;
    double lon = 0.0;
    double elevation = 0.0;
    uint32_t timestamp = 0;
    double speed = 0.0;       // km/h, computed from GPS
    uint8_t heartRate = 0;
    uint16_t power = 0;
    uint8_t cadence = 0;
    int8_t temperature = 0;
    bool hasHeartRate = false;
    bool hasPower = false;
    bool hasCadence = false;
    bool hasTemperature = false;
    bool hasSpeed = false;
    /** False when GPS has been stripped for privacy (start/end trim zone). */
    bool gpsValid = true;
};

/**
 * Ride statistics extracted from FIT file.
 *
 * `distanceKm` / `durationMin` are derived from the per-coordinate record
 * stream. Health stats (HR / power / cadence) and per-second speed come
 * from the same RECORD messages.
 *
 * The `session*` fields below carry the FIT session-message totals that
 * the cycle computer / head unit recorded — its odometer reading,
 * barometric ascent, sustained-speed peak, total moving / elapsed time.
 * Populated only when the FIT file contains a SESSION message AND the
 * field passed FIT SDK validity. Roadmap #156: PHP prefers these over
 * the Haversine sum because recomputed distance from a 1 Hz GPS stream
 * is structurally short by 2-3% on rides with curvy roads.
 */
struct RideStatistic {
    std::vector<Coordinate> coordinates;
    double distanceKm = 0.0;
    double durationMin = 0.0;
    uint32_t startTime = 0;
    uint32_t endTime = 0;

    // Health Stats
    double avgHeartRate = 0.0;
    double maxHeartRate = 0.0;
    double avgPower = 0.0;
    double maxPower = 0.0;
    double normalizedPower = 0.0; // Coggan NP: 30s rolling-mean power^4 mean, 4th root (coasting = 0 W)
    double avgCadence = 0.0;
    double maxCadence = 0.0;
    double avgSpeed = 0.0;        // km/h, moving speed (excludes stops)
    double maxSpeed = 0.0;        // km/h, raw single-sample peak (spike-prone)
    double smoothedMaxSpeed = 0.0; // km/h, spike-resistant peak (5-sample rolling mean)
    double movingTimeSec = 0.0;   // seconds where speed > threshold
    double coastingTimeSec = 0.0; // seconds moving but not pedalling (freewheel)
    double coastingDistanceKm = 0.0; // distance covered while coasting
    double coastingPct = 0.0;     // coastingTimeSec as % of moving time

    // Data availability flags. Defaults matter: the GPX/TCX path used to
    // leave these uninitialised, so the summary emitted avg/max health stats
    // for files that carry no such sensor at all.
    bool hasHeartRateData = false;
    bool hasPowerData = false;
    bool hasCadenceData = false;

    // FIT session-message totals (#156).
    bool hasSessionDistance = false;
    bool hasSessionAscent = false;
    bool hasSessionDescent = false;
    bool hasSessionMaxSpeed = false;
    bool hasSessionAvgSpeed = false;
    bool hasSessionElapsed = false;
    bool hasSessionMoving = false;

    double sessionDistanceKm = 0.0;      // session.total_distance / 1000
    double sessionElevationGainM = 0.0;  // session.total_ascent
    double sessionElevationLossM = 0.0;  // session.total_descent
    double sessionMaxSpeedKmh = 0.0;     // session.max_speed * 3.6
    double sessionAvgSpeedKmh = 0.0;     // session.avg_speed * 3.6
    double sessionElapsedSec = 0.0;      // session.total_elapsed_time
    double sessionMovingSec = 0.0;       // session.total_timer_time

    // FIT FileId + Session metadata used for indoor / trainer detection.
    // PHP previously had dead code looking for these fields in the JSON —
    // the binary now emits them so trainer rides (MyWhoosh / Zwift / Rouvy
    // / Tacx Training / BKOOL etc.) auto-flag as indoor instead of needing
    // the manual chip toggle on the activity log.
    bool hasManufacturer = false;
    uint16_t manufacturer = 0;       // FIT_MANUFACTURER enum (e.g. 260=Zwift, 331=MyWhoosh)
    bool hasGarminProduct = false;
    uint16_t garminProduct = 0;      // FIT_GARMIN_PRODUCT enum (e.g. 20533=Tacx Training App Win)
    bool hasProductName = false;
    std::string productName;         // Raw product_name string (ASCII subset of UTF-8). May be empty.
    bool hasSessionStartTime = false;
    uint32_t sessionStartTime = 0;   // Session.start_time, FIT epoch seconds
    bool hasFileIdSerialNumber = false;
    uint32_t fileIdSerialNumber = 0; // FileId.serial_number — the creator device
    bool hasFileIdTimeCreated = false;
    uint32_t fileIdTimeCreated = 0;  // FileId.time_created, FIT epoch seconds
    bool hasSport = false;
    uint8_t sport = 0;               // FIT_SPORT enum (2=CYCLING, 10=TRAINING)
    bool hasSubSport = false;
    uint8_t subSport = 0;            // FIT_SUB_SPORT enum (6=INDOOR_CYCLING, 58=VIRTUAL_ACTIVITY)
    bool isIndoor = false;           // Computed flag — true when sub_sport or manufacturer marks the ride as indoor.

    // Per-sensor device_info messages, verbatim decode order (battery design
    // 2026-07-26). Empty for files without device_info (Strava exports, Zwift).
    std::vector<DeviceInfoRecord> deviceInfos;
};

/**
 * Derive the record-stream statistics from `stats.coordinates`: per-point
 * GPS speed, avg / max / smoothed-max speed, moving time, coasting, the
 * heart-rate / power / cadence aggregates and Normalized Power. Also sets
 * startTime / endTime / durationMin / distanceKm (Haversine sum over the
 * GPS-valid segments).
 *
 * Shared by the FIT path (FitParser::extractCoordinates) and the GPX/TCX
 * path (activityToRideStatistic in main.cpp) so a GPX parsed directly
 * reports the same summary as the same GPX converted to FIT first.
 * Callers that carry their own totals (the GPX/TCX parsers) overwrite the
 * distance / duration / time fields afterwards.
 */
void computeStreamStatistics(RideStatistic& stats);

/**
 * FIT file parser using Garmin FIT SDK
 */
class FitParser {
public:
    /**
     * Constructor
     * @param filename Path to FIT file
     */
    explicit FitParser(const std::string& filename);
    
    /**
     * Extract GPS coordinates and stats from FIT file
     * @return RideStatistic with coordinates and summary
     * @throws std::runtime_error if file cannot be opened or parsed
     */
    RideStatistic extractCoordinates();
    
private:
    std::string filename_;
    
    /**
     * Calculate Haversine distance between two points
     */
    double calculateDistance(double lat1, double lon1, double lat2, double lon2);
};

#endif // FIT_PARSER_H
