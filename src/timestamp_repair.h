#ifndef TIMESTAMP_REPAIR_H
#define TIMESTAMP_REPAIR_H

#include <cmath>
#include <cstddef>
#include <cstdint>
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

/**
 * Keep one fix per second in a sub-second log (v2.6.2).
 *
 * Some apps log several fixes inside every second, but GPX and TCX stamps
 * reach the parser as whole seconds. Example, a Road Grand Tours export (dev
 * ride 613): 12:16:12 three times, then 12:16:13 four times, about 4.8 fixes
 * a second. A 5 Hz log with millisecond stamps (00.000, 00.200, ...) gives the
 * same, because the parser reads whole seconds. Speed is road over time
 * between two fixes, so only the first fix of each second got a speed: a fifth
 * of the road over 1 s (5.4 km/h instead of 27).
 *
 * A log is sub-second when it holds at least 1.75 timed fixes per distinct
 * second. A 1 Hz log with late stamps (see repairDoubledSeconds) stays below:
 * one late stamp in three, the worst seen (dev ride 50), gives 1.5. So such a
 * log is not touched, and neither is a normal 1 Hz log (1.0).
 *
 * In a sub-second log each run of fixes with the same second becomes its first
 * fix, the one nearest the start of that second. Heart rate, cadence, power
 * and temperature become the mean of the fixes in the run that carry them.
 * Its distance stays the distance at that first fix. Points without a time
 * (timestamp 0) are left alone.
 *
 * @return how many fixes were removed
 */
inline std::size_t collapseSharedSeconds(std::vector<TrackPoint>& points) {
    std::size_t timed = 0;
    std::size_t seconds = 0;
    uint32_t last = 0;
    for (const TrackPoint& p : points) {
        if (0 == p.timestamp) {
            continue;
        }
        ++timed;
        if (p.timestamp != last) {
            ++seconds;
            last = p.timestamp;
        }
    }
    if (0 == seconds || 4 * timed < 7 * seconds) { // fewer than 1.75 fixes per second
        return 0;
    }

    std::vector<TrackPoint> kept;
    kept.reserve(seconds + (points.size() - timed));
    std::size_t i = 0;
    while (i < points.size()) {
        if (0 == points[i].timestamp) {
            kept.push_back(points[i]);
            ++i;
            continue;
        }
        std::size_t end = i + 1;
        while (end < points.size() && points[end].timestamp == points[i].timestamp) {
            ++end;
        }

        TrackPoint p = points[i];
        double hr = 0.0, cad = 0.0, pwr = 0.0, temp = 0.0;
        std::size_t nHr = 0, nCad = 0, nPwr = 0, nTemp = 0;
        for (std::size_t m = i; m < end; ++m) {
            const TrackPoint& q = points[m];
            if (q.hasHeartRate) { hr += q.heart_rate; ++nHr; }
            if (q.hasCadence) { cad += q.cadence; ++nCad; }
            if (q.hasPower) { pwr += q.power; ++nPwr; }
            if (q.hasTemperature) { temp += q.temperature; ++nTemp; }
        }
        if (nHr > 0) { p.heart_rate = static_cast<uint8_t>(std::lround(hr / nHr)); p.hasHeartRate = true; }
        if (nCad > 0) { p.cadence = static_cast<uint8_t>(std::lround(cad / nCad)); p.hasCadence = true; }
        if (nPwr > 0) { p.power = static_cast<uint16_t>(std::lround(pwr / nPwr)); p.hasPower = true; }
        if (nTemp > 0) { p.temperature = static_cast<int8_t>(std::lround(temp / nTemp)); p.hasTemperature = true; }
        kept.push_back(p);
        i = end;
    }

    const std::size_t removed = points.size() - kept.size();
    points.swap(kept);
    return removed;
}

#endif // TIMESTAMP_REPAIR_H
