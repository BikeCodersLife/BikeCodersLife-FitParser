#ifndef TCX_PARSER_H
#define TCX_PARSER_H

#include "track_point.h"
#include <string>
#include <cstdint>

/**
 * TCX (Training Center XML) file parser.
 * Supports Garmin TCX format with all sensor data.
 */
class TcxParser {
public:
    explicit TcxParser(const std::string& filename);

    /**
     * Parse the TCX file and return a ParsedActivity.
     * @throws std::runtime_error on parse failure
     */
    ParsedActivity parse();

private:
    std::string filename_;

    /**
     * Convert ISO 8601 timestamp string to FIT epoch seconds.
     */
    uint32_t iso8601ToFitTimestamp(const std::string& isoTime);

    /**
     * Calculate Haversine distance between two points in meters.
     */
    double haversineDistance(double lat1, double lon1, double lat2, double lon2);
};

#endif // TCX_PARSER_H
