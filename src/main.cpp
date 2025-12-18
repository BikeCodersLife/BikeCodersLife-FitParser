#include <iostream>
#include <cstring>
#include <sys/resource.h>
#include "fit_parser.h"
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
}

/**
 * Print version information
 */
void printVersion() {
    std::cout << "BikeCodersLife FIT Parser v1.0.0" << std::endl;
    std::cout << "Built with Garmin FIT SDK" << std::endl;
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
    
    // Set security limits
    setResourceLimits();
    
    try {
        // Parse FIT file
        FitParser parser(argv[1]);
        std::vector<Coordinate> coordinates = parser.extractCoordinates();
        
        // Output JSON
        JsonOutput output;
        output.writeCoordinates(coordinates);
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Error: Unknown exception occurred" << std::endl;
        return 1;
    }
}
