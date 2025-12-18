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

std::vector<Coordinate> FitParser::extractCoordinates() {
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
    
    // Return extracted coordinates
    return listener.coordinates;
}
