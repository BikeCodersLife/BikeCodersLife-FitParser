#include <iostream>
#include <cstring>
#include <string>
#include <algorithm>
#include <cmath>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include "fit_parser.h"
#include "json_output.h"
#include "gpx_parser.h"
#include "tcx_parser.h"
#include "fit_writer.h"
#include "track_point.h"

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
    std::cerr << "Usage: " << programName << " <input_file> [--convert <output.fit>]" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Supported input formats: .fit, .gpx, .tcx" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Modes:" << std::endl;
    std::cerr << "  <input_file>                         Parse and output JSON to stdout" << std::endl;
    std::cerr << "  <input_file> --convert <output.fit>   Convert GPX/TCX to FIT format" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Options:" << std::endl;
    std::cerr << "  --version    Show version information" << std::endl;
    std::cerr << "  --help       Show this help message" << std::endl;
}

/**
 * Print version information
 */
void printVersion() {
    std::cout << "BikeCodersLife FIT Parser v2.0.0" << std::endl;
    std::cout << "Built with Garmin FIT SDK + pugixml" << std::endl;
    std::cout << "Supports: FIT, GPX, TCX input | FIT, JSON output" << std::endl;
}

/**
 * Validate file path and size for security
 */
bool validateFile(const char* filepath) {
    // Maximum file size: 100MB
    const off_t MAX_FILE_SIZE = 100 * 1024 * 1024;

    if (!filepath || filepath[0] == '\0') {
        std::cerr << "Error: Invalid file path" << std::endl;
        return false;
    }

    char realPath[PATH_MAX];
    if (!realpath(filepath, realPath)) {
        std::cerr << "Error: Cannot resolve file path" << std::endl;
        return false;
    }

    struct stat fileStat;
    if (stat(realPath, &fileStat) != 0) {
        std::cerr << "Error: Cannot access file" << std::endl;
        return false;
    }

    if (!S_ISREG(fileStat.st_mode)) {
        std::cerr << "Error: Not a regular file" << std::endl;
        return false;
    }

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
 * Get lowercase file extension from a path
 */
std::string getFileExtension(const std::string& path) {
    size_t dotPos = path.rfind('.');
    if (dotPos == std::string::npos) {
        return "";
    }
    std::string ext = path.substr(dotPos);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

/**
 * Convert a ParsedActivity to a RideStatistic for JSON output (backward compatibility)
 */
RideStatistic activityToRideStatistic(const ParsedActivity& activity) {
    RideStatistic stats;
    stats.distanceKm = activity.totalDistanceM / 1000.0;
    stats.durationMin = activity.durationSec / 60.0;
    stats.startTime = activity.startTime;
    stats.endTime = activity.endTime;

    // Round for consistency
    stats.distanceKm = std::round(stats.distanceKm * 100.0) / 100.0;
    stats.durationMin = std::round(stats.durationMin * 100.0) / 100.0;

    // Convert TrackPoints to Coordinates
    for (const auto& pt : activity.points) {
        Coordinate coord;
        coord.lat = pt.lat;
        coord.lon = pt.lon;
        coord.elevation = pt.elevation;
        coord.timestamp = pt.timestamp;
        stats.coordinates.push_back(coord);
    }

    return stats;
}

/**
 * Main entry point
 */
int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    if (std::strcmp(argv[1], "--version") == 0) {
        printVersion();
        return 0;
    }

    if (std::strcmp(argv[1], "--help") == 0) {
        printUsage(argv[0]);
        return 0;
    }

    // Set security limits
    setResourceLimits();

    // Validate input file
    if (!validateFile(argv[1])) {
        return 1;
    }

    std::string inputPath = argv[1];
    std::string ext = getFileExtension(inputPath);

    // Check for --convert mode
    std::string outputPath;
    bool convertMode = false;
    for (int i = 2; i < argc; i++) {
        if (std::strcmp(argv[i], "--convert") == 0) {
            if (i + 1 < argc) {
                outputPath = argv[i + 1];
                convertMode = true;
                i++;
            } else {
                std::cerr << "Error: --convert requires an output file path" << std::endl;
                return 1;
            }
        }
    }

    try {
        if (ext == ".fit") {
            if (convertMode) {
                std::cerr << "Error: Cannot convert FIT to FIT. Use GPX or TCX as input for conversion." << std::endl;
                return 1;
            }
            // Existing behavior: parse FIT, output JSON
            FitParser parser(inputPath);
            RideStatistic stats = parser.extractCoordinates();
            JsonOutput output;
            output.writeCoordinates(stats);

        } else if (ext == ".gpx") {
            GpxParser parser(inputPath);
            ParsedActivity activity = parser.parse();

            if (convertMode) {
                // Convert to FIT
                FitWriter writer;
                writer.write(activity, outputPath);
                std::cerr << "Converted " << activity.points.size() << " points to " << outputPath << std::endl;
                std::cerr << "Distance: " << std::round(activity.totalDistanceM / 10.0) / 100.0 << " km" << std::endl;
                std::cerr << "Duration: " << std::round(activity.durationSec / 0.6) / 100.0 << " min" << std::endl;
                std::cerr << "Ascent: " << std::round(activity.totalAscentM) << " m" << std::endl;
            } else {
                // Output JSON (same format as FIT)
                RideStatistic stats = activityToRideStatistic(activity);
                JsonOutput output;
                output.writeCoordinates(stats);
            }

        } else if (ext == ".tcx") {
            TcxParser parser(inputPath);
            ParsedActivity activity = parser.parse();

            if (convertMode) {
                // Convert to FIT
                FitWriter writer;
                writer.write(activity, outputPath);
                std::cerr << "Converted " << activity.points.size() << " points to " << outputPath << std::endl;
                std::cerr << "Distance: " << std::round(activity.totalDistanceM / 10.0) / 100.0 << " km" << std::endl;
                std::cerr << "Duration: " << std::round(activity.durationSec / 0.6) / 100.0 << " min" << std::endl;
                std::cerr << "Ascent: " << std::round(activity.totalAscentM) << " m" << std::endl;
            } else {
                // Output JSON (same format as FIT)
                RideStatistic stats = activityToRideStatistic(activity);
                JsonOutput output;
                output.writeCoordinates(stats);
            }

        } else {
            std::cerr << "Error: Unsupported file format '" << ext << "'. Supported: .fit, .gpx, .tcx" << std::endl;
            return 1;
        }

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Error: Unknown exception occurred" << std::endl;
        return 1;
    }
}
