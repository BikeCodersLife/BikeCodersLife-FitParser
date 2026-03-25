#ifndef FIT_WRITER_H
#define FIT_WRITER_H

#include "track_point.h"
#include <string>

/**
 * FIT file writer using the Garmin FIT SDK encoder.
 * Creates valid FIT activity files from parsed track data.
 *
 * Privacy by design: no device serial, no user name, no author info in output.
 */
class FitWriter {
public:
    /**
     * Write a ParsedActivity to a FIT file.
     * @param activity The parsed activity data
     * @param outputPath Path for the output FIT file
     * @throws std::runtime_error on write failure
     */
    void write(const ParsedActivity& activity, const std::string& outputPath);

private:
    /**
     * Convert degrees to FIT semicircles.
     * FIT stores coordinates as semicircles: 2^31 semicircles = 180 degrees
     */
    int32_t degreesToSemicircles(double degrees);
};

#endif // FIT_WRITER_H
