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
class CoordinateListener : public fit::RecordMesgListener {
public:
    std::vector<Coordinate> coordinates;
    
    /**
     * Called for each record message in the FIT file
     */
    void OnMesg(fit::RecordMesg& mesg) override {
        // Only process records with valid GPS coordinates
        if (mesg.IsLatitudeValid() && mesg.IsLongitudeValid()) {
            Coordinate coord;
            
            // Convert semicircles to degrees
            // FIT format stores coordinates as semicircles (2^31 semicircles = 180 degrees)
            coord.lat = mesg.GetLatitude() * (180.0 / std::pow(2, 31));
            coord.lon = mesg.GetLongitude() * (180.0 / std::pow(2, 31));
            
            // Elevation in meters (may be invalid)
            coord.elevation = mesg.IsAltitudeValid() ? mesg.GetAltitude() : 0.0;
            
            // Timestamp (seconds since FIT epoch: 1989-12-31 00:00:00 UTC)
            coord.timestamp = mesg.IsTimestampValid() ? mesg.GetTimestamp().GetTimeStamp() : 0;
            
            coordinates.push_back(coord);
        }
    }
};

FitParser::FitParser(const std::string& filename) : filename_(filename) {}

std::vector<Coordinate> FitParser::extractCoordinates() {
    // Open FIT file
    std::fstream file(filename_, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + filename_);
    }
    
    // Create FIT decoder
    fit::Decode decode;
    fit::MesgBroadcaster broadcaster;
    CoordinateListener listener;
    
    // Register listener for record messages
    broadcaster.AddListener(listener);
    
    // Decode FIT file
    if (!decode.Read(&file, &broadcaster)) {
        file.close();
        throw std::runtime_error("Failed to decode FIT file");
    }
    
    file.close();
    
    // Return extracted coordinates
    return listener.coordinates;
}
