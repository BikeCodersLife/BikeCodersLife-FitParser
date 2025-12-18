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
     * Extract GPS coordinates from FIT file
     * @return Vector of coordinates
     * @throws std::runtime_error if file cannot be opened or parsed
     */
    std::vector<Coordinate> extractCoordinates();
    
private:
    std::string filename_;
};

#endif // FIT_PARSER_H
