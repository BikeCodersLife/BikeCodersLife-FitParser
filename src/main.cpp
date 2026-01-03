#include <iostream>
#include <cstring>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <fstream>
#include <sstream>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include "fit_parser.h"
#include "fit_writer.h"
#include "json_output.h"

/**
 * Set resource limits for security
 */
void setResourceLimits() {
    struct rlimit limit;
    
    // Memory limit: 256MB
    limit.rlim_cur = 256 * 1024 * 1024;
    limit.rlim_max = 256 * 1024 * 1024;
    setrlimit(RLIMIT_AS, &limit);
    
    // CPU time limit: 30 seconds
    limit.rlim_cur = 30;
    limit.rlim_max = 30;
    setrlimit(RLIMIT_CPU, &limit);
    
    // No core dumps
    limit.rlim_cur = 0;
    limit.rlim_max = 0;
    setrlimit(RLIMIT_CORE, &limit);
    
    // Limit number of open file descriptors
    limit.rlim_cur = 10;
    limit.rlim_max = 10;
    setrlimit(RLIMIT_NOFILE, &limit);
}

/**
 * Print usage information
 */
void printUsage(const char* programName) {
    std::cerr << "Usage: " << programName << " <fit_file>" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Parses a FIT file and outputs GPS coordinates as JSON to stdout." << std::endl;
    std::cerr << std::endl;
    std::cerr << "Options:" << std::endl;
    std::cerr << "  --version    Show version information" << std::endl;
    std::cerr << "  --help       Show this help message" << std::endl;
    std::cerr << "  --convert-csv <in.csv> <out.fit>  Convert CSV to FIT file" << std::endl;
}

/**
 * Print version information
 */
void printVersion() {
    std::cout << "BikeCodersLife FIT Parser v1.1.0" << std::endl;
    std::cout << "Built with Garmin FIT SDK" << std::endl;
}

/**
 * Validate file path and size for security
 */
bool validateFile(const char* filepath) {
    // Maximum file size: 100MB (FIT files are typically <10MB)
    const off_t MAX_FILE_SIZE = 100 * 1024 * 1024;
    
    // Check for null or empty path
    if (!filepath || filepath[0] == '\0') {
        std::cerr << "Error: Invalid file path" << std::endl;
        return false;
    }
    
    // Resolve to absolute path to prevent directory traversal
    char realPath[PATH_MAX];
    if (!realpath(filepath, realPath)) {
        std::cerr << "Error: Cannot resolve file path" << std::endl;
        return false;
    }
    
    // Check if file exists and get stats
    struct stat fileStat;
    if (stat(realPath, &fileStat) != 0) {
        std::cerr << "Error: Cannot access file" << std::endl;
        return false;
    }
    
    // Check if it's a regular file (not a directory, symlink, device, etc.)
    if (!S_ISREG(fileStat.st_mode)) {
        std::cerr << "Error: Not a regular file" << std::endl;
        return false;
    }
    
    // Check file size
    if (fileStat.st_size > MAX_FILE_SIZE) {
        std::cerr << "Error: File too large (max 100MB)" << std::endl;
        return false;
    }
    
    if (fileStat.st_size == 0) {
        std::cerr << "Error: File is empty" << std::endl;
        return false;
    }
    
    return true;
}

/**
 * Main entry point
 */
int main(int argc, char* argv[]) {
    // Check arguments
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    // Handle --version flag
    if (std::strcmp(argv[1], "--version") == 0) {
        printVersion();
        return 0;
    }

    // Handle --help flag
    if (std::strcmp(argv[1], "--help") == 0) {
        printUsage(argv[0]);
        return 0;
    }
    
    // Handle --convert-csv flag
    if (std::strcmp(argv[1], "--convert-csv") == 0) {
        if (argc < 4) {
            std::cerr << "Usage: " << argv[0] << " --convert-csv <input.csv> <output.fit>" << std::endl;
            return 1;
        }
        
        std::string inputCsv = argv[2];
        std::string outputFit = argv[3];
        
        try {
            RideStatistic stats;
            stats.startTime = 0;
            stats.endTime = 0;
            stats.distanceKm = 0;
            stats.durationMin = 0;
            stats.avgHeartRate = 0;
            stats.maxHeartRate = 0;
            stats.avgPower = 0;
            stats.maxPower = 0;
            stats.avgCadence = 0;
            stats.hasHeartRateData = false;
            stats.hasPowerData = false;
            stats.hasCadenceData = false;
            
            std::ifstream file(inputCsv);
            if (!file.is_open()) {
                throw std::runtime_error("Cannot open CSV file: " + inputCsv);
            }
            
            std::string line;
            // Skip header
            std::getline(file, line);
            
            double totalHeartRate = 0;
            long countHeartRate = 0;
            double totalPower = 0;
            long countPower = 0;
            double totalCadence = 0;
            long countCadence = 0;

            while (std::getline(file, line)) {
                std::stringstream ss(line);
                std::string cell;
                std::vector<std::string> row;
                
                while (std::getline(ss, cell, ',')) {
                    row.push_back(cell);
                }
                
                if (row.size() >= 4) {
                    Coordinate coord;
                    coord.lat = std::stod(row[0]);
                    coord.lon = std::stod(row[1]);
                    coord.elevation = std::stod(row[2]);
                    coord.timestamp = std::stoul(row[3]);
                    
                    // Health Data
                    coord.hasHeartRate = (row.size() > 4 && !row[4].empty());
                    coord.heartRate = coord.hasHeartRate ? std::stoi(row[4]) : 0;
                    
                    coord.hasPower = (row.size() > 5 && !row[5].empty());
                    coord.power = coord.hasPower ? std::stoi(row[5]) : 0;
                    
                    coord.hasCadence = (row.size() > 6 && !row[6].empty());
                    coord.cadence = coord.hasCadence ? std::stoi(row[6]) : 0;
                    
                    coord.hasTemperature = (row.size() > 7 && !row[7].empty());
                    coord.temperature = coord.hasTemperature ? std::stoi(row[7]) : 0;
                    
                    stats.coordinates.push_back(coord);
                    
                    // Accumulate stats
                    if (coord.hasHeartRate) {
                        stats.hasHeartRateData = true;
                        totalHeartRate += coord.heartRate;
                        countHeartRate++;
                        if (coord.heartRate > stats.maxHeartRate) stats.maxHeartRate = coord.heartRate;
                    }
                    if (coord.hasPower) {
                        stats.hasPowerData = true;
                        totalPower += coord.power;
                        countPower++;
                        if (coord.power > stats.maxPower) stats.maxPower = coord.power;
                    }
                    if (coord.hasCadence) {
                        stats.hasCadenceData = true;
                        totalCadence += coord.cadence;
                        countCadence++;
                    }
                }
            }
            
            if (stats.coordinates.empty()) {
                throw std::runtime_error("No valid coordinates found in CSV");
            }
            
            stats.startTime = stats.coordinates.front().timestamp;
            stats.endTime = stats.coordinates.back().timestamp;
            
            if (stats.endTime > stats.startTime) {
                stats.durationMin = (stats.endTime - stats.startTime) / 60.0;
            }
            
            // Calculate distance
            FitParser dummyParser(""); // Use dummy parser to access calculateDistance 
            // Or make calculateDistance static. It's private logic.
            // Let's just re-implement simple haversine here or make public static. 
            // Better: copy-paste haversine for simplicity or use FitParser instance.
            // Ideally FitParser should provide static util.
            // For now, let's just do simple calculation loop.
            
             for (size_t i = 1; i < stats.coordinates.size(); ++i) {
                const auto& p1 = stats.coordinates[i-1];
                const auto& p2 = stats.coordinates[i];
                
                // Simple Haversine
                const double R = 6371000.0;
                const double dLat = (p2.lat - p1.lat) * M_PI / 180.0;
                const double dLon = (p2.lon - p1.lon) * M_PI / 180.0;
                const double a = std::sin(dLat/2) * std::sin(dLat/2) +
                                std::cos(p1.lat * M_PI / 180.0) * std::cos(p2.lat * M_PI / 180.0) *
                                std::sin(dLon/2) * std::sin(dLon/2);
                const double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1-a));
                stats.distanceKm += (R * c) / 1000.0;
            }
            
            if (countHeartRate > 0) stats.avgHeartRate = totalHeartRate / countHeartRate;
            if (countPower > 0) stats.avgPower = totalPower / countPower;
            if (countCadence > 0) stats.avgCadence = totalCadence / countCadence;
            
            // Round stats
            stats.distanceKm = std::round(stats.distanceKm * 100.0) / 100.0;
            stats.durationMin = std::round(stats.durationMin * 100.0) / 100.0;
            
            // Write FIT file
            FitWriter writer;
            writer.write(outputFit, stats);
            
            std::cout << "Successfully converted CSV to FIT: " << outputFit << std::endl;
            return 0;
            
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            return 1;
        }
    }
    
    // Set security limits
    setResourceLimits();
    
    // Validate file before parsing
    if (!validateFile(argv[1])) {
        return 1;
    }
    
    try {
        // Parse FIT file
        FitParser parser(argv[1]);
        RideStatistic stats = parser.extractCoordinates();
        
        // Output JSON
        JsonOutput output;
        output.writeCoordinates(stats);
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Error: Unknown exception occurred" << std::endl;
        return 1;
    }
}
