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
    std::size_t trailingBytesDropped = 0;   // bytes after the last valid FIT file (junk, padding, damaged chained file) not copied
};

/**
 * Removes health data (heart rate, power, cadence and every other
 * physiological or biomechanical value) and ALL developer data from a FIT file.
 *
 * The FIT bytes are rewritten message by message in the original order (definitions
 * reduced to the kept fields), so the output is always a valid FIT (header and CRCs
 * rebuilt). The decision table lives in docs/health-strip-table.tsv and is
 * compiled into src/health_table.inc (see docs/health-strip.md).
 *
 * A linear byte-level rewriter (~100 MB/s); the vendor SDK decoder is not used.
 * Compressed-timestamp headers are expanded to full timestamps.
 *
 * Fails closed: anything that cannot be decoded or cleaned throws, and the
 * caller must not use any output.
 *
 * Output path: pass a fresh path. The output replaces an existing file only on
 * success (atomic rename); on failure an existing file is left as it was (it is
 * never unlinked, in == out is legal). Handled failures remove the temporary
 * files (`<out>.tmp.<pid>`, `<out>.conv.<pid>`); a SIGKILL (for instance the
 * 30 s CPU limit) cannot be handled, so callers delete the output directory
 * contents in a finally block.
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
