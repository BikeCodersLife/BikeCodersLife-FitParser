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
 * Ride statistics extracted from FIT file
 */
struct RideStatistic {
    std::vector<Coordinate> coordinates;
    double distanceKm;
    double durationMin;
    uint32_t startTime;
    uint32_t endTime;
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
