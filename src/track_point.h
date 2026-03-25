#ifndef TRACK_POINT_H
#define TRACK_POINT_H

#include <cstdint>
#include <vector>
#include <string>

/**
 * Extended track point with full sensor data.
 * Used as the common interchange format between GPX/TCX parsers and FIT writer.
 */
struct TrackPoint {
    double lat = 0.0;
    double lon = 0.0;
    double elevation = 0.0;
    uint32_t timestamp = 0;      // FIT epoch seconds (since 1989-12-31 00:00:00 UTC)
    uint8_t heart_rate = 0;      // bpm, 0 = not available
    uint8_t cadence = 0;         // rpm, 0 = not available
    uint16_t power = 0;          // watts, 0 = not available
    int8_t temperature = 0;      // celsius, 0 = not available
    float speed = 0.0F;          // m/s, 0 = not available
    double distance = 0.0;       // cumulative meters

    bool hasHeartRate = false;
    bool hasCadence = false;
    bool hasPower = false;
    bool hasTemperature = false;
    bool hasSpeed = false;
    bool hasElevation = false;
};

/**
 * Parsed activity data from any format (GPX, TCX, FIT).
 */
struct ParsedActivity {
    std::vector<TrackPoint> points;
    std::string name;            // Activity/track name if available
    double totalDistanceM = 0.0; // Total distance in meters
    double totalAscentM = 0.0;   // Total ascent in meters
    double totalDescentM = 0.0;  // Total descent in meters
    uint32_t startTime = 0;      // FIT epoch
    uint32_t endTime = 0;        // FIT epoch
    double durationSec = 0.0;    // Duration in seconds
};

#endif // TRACK_POINT_H
