#ifndef FIT_PARSER_H
#define FIT_PARSER_H

#include <string>
#include <vector>
#include <cstdint>

/**
 * Coordinate structure representing a GPS point
 */
struct Coordinate {
    double lat;
    double lon;
    double elevation;
    uint32_t timestamp;
    double speed = 0.0;       // km/h, computed from GPS
    uint8_t heartRate;
    uint16_t power;
    uint8_t cadence;
    int8_t temperature;
    bool hasHeartRate;
    bool hasPower;
    bool hasCadence;
    bool hasTemperature;
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
    double distanceKm;
    double durationMin;
    uint32_t startTime;
    uint32_t endTime;

    // Health Stats
    double avgHeartRate;
    double maxHeartRate;
    double avgPower;
    double maxPower;
    double avgCadence;
    double avgSpeed;        // km/h, moving speed (excludes stops)
    double maxSpeed;        // km/h
    double movingTimeSec;   // seconds where speed > threshold

    // Data availability flags
    bool hasHeartRateData;
    bool hasPowerData;
    bool hasCadenceData;

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
};

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
