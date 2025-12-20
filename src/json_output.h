#ifndef JSON_OUTPUT_H
#define JSON_OUTPUT_H

#include "fit_parser.h"
#include <vector>
#include <iostream>
#include <cstdint>

/**
 * JSON output formatter for coordinates
 */
class JsonOutput {
public:
    /**
     * Write coordinates and stats to JSON
     * @param stats Ride statistics
     */
    void writeCoordinates(const RideStatistic& stats);
    
private:
    /**
     * Convert FIT timestamp to ISO 8601 string
     * @param timestamp FIT timestamp (seconds since 1989-12-31 00:00:00 UTC)
     * @return ISO 8601 formatted string
     */
    std::string timestampToIso8601(uint32_t timestamp);
    
    /**
     * Escape string for JSON
     * @param str String to escape
     * @return Escaped string
     */
    std::string escapeJson(const std::string& str);
};

#endif // JSON_OUTPUT_H
