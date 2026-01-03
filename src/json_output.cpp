#include "json_output.h"
#include <iomanip>
#include <sstream>
#include <ctime>

std::string JsonOutput::timestampToIso8601(uint32_t timestamp) {
    // FIT epoch: 1989-12-31 00:00:00 UTC
    // Unix epoch: 1970-01-01 00:00:00 UTC
    // Difference: 631065600 seconds
    const uint32_t FIT_EPOCH_OFFSET = 631065600;
    
    time_t unixTime = timestamp + FIT_EPOCH_OFFSET;
    struct tm* timeinfo = gmtime(&unixTime);
    
    std::ostringstream oss;
    oss << std::put_time(timeinfo, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::string JsonOutput::escapeJson(const std::string& str) {
    std::ostringstream oss;
    for (char c : str) {
        switch (c) {
            case '"':  oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\b': oss << "\\b";  break;
            case '\f': oss << "\\f";  break;
            case '\n': oss << "\\n";  break;
            case '\r': oss << "\\r";  break;
            case '\t': oss << "\\t";  break;
            default:   oss << c;      break;
        }
    }
    return oss.str();
}

void JsonOutput::writeCoordinates(const RideStatistic& stats) {
    std::cout << "{" << std::endl;
    std::cout << "  \"coordinates\": [" << std::endl;
    
    const auto& coordinates = stats.coordinates;
    for (size_t i = 0; i < coordinates.size(); ++i) {
        const Coordinate& coord = coordinates[i];
        
        std::cout << "    {";
        std::cout << "\"lat\": " << std::fixed << std::setprecision(6) << coord.lat << ", ";
        std::cout << "\"lon\": " << std::fixed << std::setprecision(6) << coord.lon << ", ";
        std::cout << "\"elevation\": " << std::fixed << std::setprecision(1) << coord.elevation << ", ";
        
        // Health Data
        if (coord.hasHeartRate) std::cout << "\"heartRate\": " << (int)coord.heartRate << ", ";
        if (coord.hasPower) std::cout << "\"power\": " << coord.power << ", ";
        if (coord.hasCadence) std::cout << "\"cadence\": " << (int)coord.cadence << ", ";
        if (coord.hasTemperature) std::cout << "\"temperature\": " << (int)coord.temperature << ", ";
        
        std::cout << "\"timestamp\": \"" << timestampToIso8601(coord.timestamp) << "\"";
        std::cout << "}";
        
        if (i < coordinates.size() - 1) {
            std::cout << ",";
        }
        std::cout << std::endl;
    }
    
    std::cout << "  ]," << std::endl;
    std::cout << "  \"summary\": {" << std::endl;
    std::cout << "    \"points\": " << coordinates.size() << "," << std::endl;
    std::cout << "    \"distanceKm\": " << std::fixed << std::setprecision(2) << stats.distanceKm << "," << std::endl;
    std::cout << "    \"durationMin\": " << std::fixed << std::setprecision(2) << stats.durationMin << "," << std::endl;
    
    // Health Stats
    if (stats.hasHeartRateData) {
        std::cout << "    \"avgHeartRate\": " << std::fixed << std::setprecision(1) << stats.avgHeartRate << "," << std::endl;
        std::cout << "    \"maxHeartRate\": " << std::fixed << std::setprecision(1) << stats.maxHeartRate << "," << std::endl;
    }
    if (stats.hasPowerData) {
        std::cout << "    \"avgPower\": " << std::fixed << std::setprecision(1) << stats.avgPower << "," << std::endl;
        std::cout << "    \"maxPower\": " << std::fixed << std::setprecision(1) << stats.maxPower << "," << std::endl;
    }
    if (stats.hasCadenceData) {
        std::cout << "    \"avgCadence\": " << std::fixed << std::setprecision(1) << stats.avgCadence << "," << std::endl;
    }
    
    std::cout << "    \"startTime\": \"" << timestampToIso8601(stats.startTime) << "\"," << std::endl;
    std::cout << "    \"endTime\": \"" << timestampToIso8601(stats.endTime) << "\"" << std::endl;
    std::cout << "  }" << std::endl;
    std::cout << "}" << std::endl;
}
