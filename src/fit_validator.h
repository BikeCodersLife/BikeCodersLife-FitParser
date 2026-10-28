#ifndef FIT_VALIDATOR_H
#define FIT_VALIDATOR_H

#include <cstddef>
#include <cstdint>
#include <string>

/**
 * Structural validation of FIT bytes, run BEFORE the vendor SDK decoder sees an
 * uploaded file (the SDK decoder is memory-unsafe on hostile definitions:
 * base type numbers >= 17, zero sized fields, bad developer field tables).
 *
 * Walks the first FIT file (header, every definition, every data message) and
 * refuses anything the SDK would refuse or that cannot be valid:
 *   - bad header / ".FIT" tag / sizes that do not fit the input
 *   - architecture byte > 1, data message without a definition
 *   - field size 0, size not a multiple of the base type size, base type number >= 17
 *   - duplicate field numbers inside one definition, developer field size 0
 *   - a record stream that does not end exactly at the data size
 * The CRC is not checked here (the SDK and the strip mode do that themselves).
 */

/** Base type sizes of the 17 FIT base types (0..16). */
inline unsigned fitBaseTypeSize(unsigned baseTypeByte) {
    static const unsigned sizes[17] = {1, 1, 1, 2, 2, 4, 4, 1, 4, 8, 1, 2, 4, 1, 8, 8, 8};
    const unsigned n = baseTypeByte & 0x1F;
    return n < 17 ? sizes[n] : 0;
}

/** Reason a field definition is invalid, or nullptr when it is fine. */
inline const char* fitFieldDefError(unsigned size, unsigned baseTypeByte) {
    if (size == 0) return "field size 0";
    const unsigned ts = fitBaseTypeSize(baseTypeByte);
    if (ts == 0) return "unknown base type";
    if (size % ts != 0) return "field size is not a multiple of the base type size";
    return nullptr;
}

/** Empty string when valid, else the reason. */
std::string validateFitStructure(const unsigned char* data, std::size_t size);

/** Same for a file on disk (memory mapped, no copy). Empty string when valid. */
std::string validateFitFile(const std::string& path);

#endif // FIT_VALIDATOR_H
