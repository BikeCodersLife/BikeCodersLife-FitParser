#include "gps_stripper.h"
#include "fit_parser.h"
#include "fit_writer.h"

#include <cmath>
#include <stdexcept>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

GpsStripper::GpsStripper(const std::string& inputFile)
    : inputFile_(inputFile)
{
}

double GpsStripper::haversineMeters(double lat1, double lon1,
                                     double lat2, double lon2)
{
    const double R = 6371000.0; // Earth radius in metres
    const double toRad = M_PI / 180.0;
    double dLat = (lat2 - lat1) * toRad;
    double dLon = (lon2 - lon1) * toRad;
    double a = std::sin(dLat / 2.0) * std::sin(dLat / 2.0)
             + std::cos(lat1 * toRad) * std::cos(lat2 * toRad)
             * std::sin(dLon / 2.0) * std::sin(dLon / 2.0);
    double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
    return R * c;
}

std::pair<int, int> GpsStripper::strip(const std::string& outputFile,
                                        double trimStartMeters,
                                        double trimEndMeters)
{
    FitParser parser(inputFile_);
    RideStatistic stats = parser.extractCoordinates();

    if (stats.coordinates.empty()) {
        throw std::runtime_error("No coordinates found in FIT file: " + inputFile_);
    }

    // Build cumulative distance array
    std::vector<double> cumDist(stats.coordinates.size(), 0.0);
    for (std::size_t i = 1; i < stats.coordinates.size(); ++i) {
        const auto& prev = stats.coordinates[i - 1];
        const auto& curr = stats.coordinates[i];
        cumDist[i] = cumDist[i - 1] + haversineMeters(prev.lat, prev.lon, curr.lat, curr.lon);
    }

    double totalDist = cumDist.back();

    int strippedStart = 0;
    int strippedEnd   = 0;

    for (std::size_t i = 0; i < stats.coordinates.size(); ++i) {
        bool inStartZone = (cumDist[i] < trimStartMeters);
        bool inEndZone   = (totalDist - cumDist[i] < trimEndMeters);

        if (inStartZone || inEndZone) {
            stats.coordinates[i].gpsValid = false;

            if (inStartZone) {
                ++strippedStart;
            } else {
                ++strippedEnd;
            }
        }
    }

    FitWriter writer;
    writer.write(outputFile, stats);

    return {strippedStart, strippedEnd};
}
