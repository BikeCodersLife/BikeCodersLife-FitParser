#ifndef GPS_STRIPPER_H
#define GPS_STRIPPER_H

#include <string>
#include <utility>

/**
 * Strips GPS position data (lat/lon) from the start and end trim zones of a FIT file.
 * All other data (HR, cadence, power, elevation, timestamps) is preserved.
 * Stripped positions are omitted from RECORD messages, compliant with the FIT protocol.
 */
class GpsStripper {
public:
    /**
     * @param inputFile Path to the input .fit file
     */
    explicit GpsStripper(const std::string& inputFile);

    /**
     * Strip GPS from trim zones and write the result to outputFile.
     *
     * @param outputFile       Path to write the stripped .fit file
     * @param trimStartMeters  Distance (m) from start within which GPS is removed
     * @param trimEndMeters    Distance (m) from end within which GPS is removed
     * @return {stripped_start_count, stripped_end_count}
     */
    std::pair<int, int> strip(const std::string& outputFile,
                               double trimStartMeters,
                               double trimEndMeters);

private:
    std::string inputFile_;

    /** Haversine distance between two GPS points in metres. */
    static double haversineMeters(double lat1, double lon1,
                                   double lat2, double lon2);
};

#endif // GPS_STRIPPER_H
