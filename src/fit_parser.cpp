#include "fit_parser.h"
#include <fstream>
#include <stdexcept>
#include <cmath>
#include <fit_decode.hpp>
#include <fit_mesg_broadcaster.hpp>
#include <fit_record_mesg.hpp>

/**
 * Listener for FIT record messages to extract coordinates
 */
class CoordinateListener : public fit::MesgListener {
public:
    std::vector<Coordinate> coordinates;
    
    /**
     * Called for each message in the FIT file
     */
    void OnMesg(fit::Mesg& mesg) override {
        // Check if this is a record message
        if (mesg.GetNum() == FIT_MESG_NUM_RECORD) {
            // Cast to RecordMesg
            fit::RecordMesg recordMesg(mesg);
            
            // Only process records with valid GPS coordinates
            if (recordMesg.IsPositionLatValid() && recordMesg.IsPositionLongValid()) {
                Coordinate coord;
                
                // Convert semicircles to degrees
                // FIT format stores coordinates as semicircles (2^31 semicircles = 180 degrees)
                coord.lat = recordMesg.GetPositionLat() * (180.0 / std::pow(2, 31));
                coord.lon = recordMesg.GetPositionLong() * (180.0 / std::pow(2, 31));
                
                // Elevation in meters (may be invalid)
                coord.elevation = recordMesg.IsAltitudeValid() ? recordMesg.GetAltitude() : 0.0;
                
                // Timestamp (FIT_DATE_TIME is uint32, seconds since FIT epoch)
                coord.timestamp = recordMesg.IsTimestampValid() ? recordMesg.GetTimestamp() : 0;
                
                coordinates.push_back(coord);
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

    if (stats.coordinates.empty()) {
        return stats;
    }

    // Calculate stats
    double totalDistanceMeters = 0.0;
    stats.startTime = stats.coordinates.front().timestamp;
    stats.endTime = stats.coordinates.back().timestamp;

    // Calculate duration in minutes (difference between start and end timestamps)
    // FIT timestamps are seconds since epoch
    if (stats.endTime > stats.startTime) {
        stats.durationMin = (stats.endTime - stats.startTime) / 60.0;
    }

    // Calculate distance
    for (size_t i = 1; i < stats.coordinates.size(); ++i) {
        const auto& p1 = stats.coordinates[i-1];
        const auto& p2 = stats.coordinates[i];
        
        totalDistanceMeters += calculateDistance(p1.lat, p1.lon, p2.lat, p2.lon);
    }

    stats.distanceKm = totalDistanceMeters / 1000.0;
    
    // Round to 2 decimal places for consistency
    stats.distanceKm = std::round(stats.distanceKm * 100.0) / 100.0;
    stats.durationMin = std::round(stats.durationMin * 100.0) / 100.0;

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
