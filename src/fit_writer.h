#ifndef FIT_WRITER_H
#define FIT_WRITER_H

#include "fit_parser.h"
#include <string>

/**
 * Writes ride data to FIT file format
 */
class FitWriter {
public:
    FitWriter();
    
    /**
     * Write ride statistics to a FIT file
     * @param filename Output filename
     * @param stats Ride statistics to write
     * @throws std::runtime_error on failure
     */
    void write(const std::string& filename, const RideStatistic& stats);
};

#endif // FIT_WRITER_H
