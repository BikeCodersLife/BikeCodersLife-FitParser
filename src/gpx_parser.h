#ifndef GPX_PARSER_H
#define GPX_PARSER_H

#include "track_point.h"
#include <string>
#include <cstdint>

/**
 * GPX file parser.
 * Supports GPX 1.0 and 1.1, including Garmin TrackPointExtension for HR/cadence/power.
 */
class GpxParser {
public:
    explicit GpxParser(const std::string& filename);

    /**
     * Parse the GPX file and return a ParsedActivity.
     * @throws std::runtime_error on parse failure
     */
    ParsedActivity parse();

private:
    std::string filename_;

    /**
     * Convert ISO 8601 timestamp string to FIT epoch seconds.
     * FIT epoch = 1989-12-31 00:00:00 UTC
     */
    uint32_t iso8601ToFitTimestamp(const std::string& isoTime);

    /**
     * Calculate Haversine distance between two points in meters.
     */
    double haversineDistance(double lat1, double lon1, double lat2, double lon2);
};

#endif // GPX_PARSER_H
