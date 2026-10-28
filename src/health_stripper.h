#ifndef HEALTH_STRIPPER_H
#define HEALTH_STRIPPER_H

#include <cstddef>
#include <string>

/**
 * Counters reported by the --strip-health mode.
 */
struct HealthStripStats {
    std::size_t droppedMessages = 0;        // whole messages removed (data messages, not definitions)
    std::size_t strippedFields = 0;         // fields removed from messages that were kept
    std::size_t developerFieldsRemoved = 0; // developer fields seen on any message (all are removed)
};

/**
 * Removes health data (heart rate, power, cadence and every other
 * physiological or biomechanical value) and ALL developer data from a FIT file.
 *
 * The FIT is decoded with the Garmin SDK and re-encoded message by message in
 * the original order, so the output is always a valid FIT (header and CRC
 * rebuilt). The decision table lives in docs/health-strip-table.tsv and is
 * compiled into src/health_table.inc (see docs/health-strip.md).
 *
 * Fails closed: anything that cannot be decoded or cleaned throws, and the
 * caller must not use any output.
 */
class HealthStripper {
public:
    /**
     * Clean a FIT file held in memory.
     * A FIT file can hold several chained files; each is cleaned on its own.
     * Bytes after the last valid file (padding, junk) are not copied.
     * @throws std::runtime_error when the first file is not a valid FIT
     */
    static std::string stripBytes(const std::string& input, HealthStripStats& stats);

    /**
     * Clean a FIT file on disk. The output is written to a temporary name next
     * to outputPath and renamed only when everything succeeded.
     * @throws std::runtime_error on any failure (no output file is left)
     */
    static void stripFile(const std::string& inputPath, const std::string& outputPath, HealthStripStats& stats);

    /** Write already encoded FIT bytes atomically (temp name + rename). */
    static void writeAtomically(const std::string& outputPath, const std::string& bytes);
};

#endif // HEALTH_STRIPPER_H
