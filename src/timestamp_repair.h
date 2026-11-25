#ifndef TIMESTAMP_REPAIR_H
#define TIMESTAMP_REPAIR_H

#include <cstddef>
#include <vector>

#include "track_point.h"

/**
 * Give doubled seconds back their missing second (v2.6.1).
 *
 * Some 1 Hz loggers write a stamp a second late now and then. Example, a 2014
 * GlobalSat GS-Sport log (dev ride 50): ..., 18:08:05, 18:08:07, 18:08:07,
 * 18:08:08, ... One fix per second was recorded, but 18:08:06 is missing and
 * 18:08:07 is there twice. Unrepaired, the GPS speed of the first 18:08:07 is
 * 1 s of road over 2 s (half the speed) and its twin gets none, so speed,
 * moving time and the per-second replay all read low (ride 50: 20.8 km/h
 * instead of 25.4).
 *
 * For each doubled second the repair finds the nearest EMPTY second within
 * kMaxShiftPoints fixes, across a run of fixes one second apart, and moves
 * the fixes between it and the doubled second one second towards it:
 *   5, 7, 7, 8        -> 5, 6, 7, 8   (the empty second is next to it)
 *   3, 5, 6, 6, 7     -> 3, 4, 5, 6, 7 (two fixes back)
 *   47, 47, 48, 50    -> 47, 48, 49, 50 (forward)
 * Every fix moves at most one second, order stays strictly increasing, no fix
 * is added or dropped, and a log without doubled seconds is not touched.
 * Points without a time (timestamp 0) are left alone.
 *
 * @return how many stamps moved
 */
inline std::size_t repairDoubledSeconds(std::vector<TrackPoint>& points) {
    constexpr std::size_t kMaxShiftPoints = 4;
    constexpr std::size_t kNone = static_cast<std::size_t>(-1);
    std::size_t moved = 0;

    for (std::size_t i = 1; i < points.size(); ++i) {
        if (0 == points[i - 1].timestamp || points[i].timestamp != points[i - 1].timestamp) {
            continue;
        }

        // Back: walk from the first twin over fixes one second apart until a
        // gap of two seconds or more (an empty second before fix j).
        std::size_t back = kNone;
        for (std::size_t j = i - 1; j >= 1 && (i - 1) - j <= kMaxShiftPoints; --j) {
            const uint32_t before = points[j - 1].timestamp;
            const uint32_t at = points[j].timestamp;
            if (0 == before) {
                break;
            }
            if (at > before + 1) {
                back = j;
                break;
            }
            if (at != before + 1) {
                break; // another doubled second or out of order
            }
        }

        // Forward: the same from the second twin (an empty second after fix k).
        std::size_t forward = kNone;
        for (std::size_t k = i; k + 1 < points.size() && k - i <= kMaxShiftPoints; ++k) {
            const uint32_t at = points[k].timestamp;
            const uint32_t after = points[k + 1].timestamp;
            if (after > at + 1) {
                forward = k;
                break;
            }
            if (after != at + 1) {
                break;
            }
        }

        const bool useBack = kNone != back && (kNone == forward || (i - 1) - back <= forward - i);
        if (useBack) {
            for (std::size_t m = back; m <= i - 1; ++m) {
                --points[m].timestamp;
                ++moved;
            }
        } else if (kNone != forward) {
            for (std::size_t m = i; m <= forward; ++m) {
                ++points[m].timestamp;
                ++moved;
            }
        }
    }
    return moved;
}

#endif // TIMESTAMP_REPAIR_H
