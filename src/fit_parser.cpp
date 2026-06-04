#include "fit_parser.h"
#include <fstream>
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <fit_decode.hpp>
#include <fit_mesg_broadcaster.hpp>
#include <fit_record_mesg.hpp>
#include <fit_session_mesg.hpp>
#include <fit_file_id_mesg.hpp>
#include <fit_profile.hpp>

/**
 * Holds the FIT session-message totals captured during decoding.
 * Each field's bool is independent because a session message can be
 * present without all fields being valid (e.g. an indoor trainer file
 * may carry avg_speed but no total_ascent).
 */
struct SessionTotals {
    bool hasDistance = false;
    double distanceM = 0.0;
    bool hasAscent = false;
    double ascentM = 0.0;
    bool hasDescent = false;
    double descentM = 0.0;
    bool hasMaxSpeed = false;
    double maxSpeedMs = 0.0;
    bool hasAvgSpeed = false;
    double avgSpeedMs = 0.0;
    bool hasElapsed = false;
    double elapsedSec = 0.0;
    bool hasMoving = false;
    double movingSec = 0.0;
    bool hasSport = false;
    uint8_t sport = 0;
    bool hasSubSport = false;
    uint8_t subSport = 0;
};

/**
 * FIT FileId metadata captured for indoor / source-app detection.
 * Populated once per file (FileId is the first message in every FIT file).
 */
struct FileIdInfo {
    bool hasManufacturer = false;
    uint16_t manufacturer = 0;
    bool hasGarminProduct = false;
    uint16_t garminProduct = 0;
    bool hasProductName = false;
    std::string productName;
};

/**
 * Lossy FIT_WSTRING (std::wstring) → UTF-8 std::string conversion. All
 * known indoor-app product_name values ("MyWhoosh", "Zwift", "Rouvy",
 * "Tacx Training Application", etc.) are pure ASCII, so a naive cast is
 * fine for matching. Non-ASCII characters get replaced with '?' rather
 * than producing mojibake.
 */
static std::string wstringToUtf8Lossy(const std::wstring& w) {
    std::string out;
    out.reserve(w.size());
    for (wchar_t c : w) {
        out.push_back((c >= 0 && c < 128) ? static_cast<char>(c) : '?');
    }
    return out;
}

/**
 * Single listener that captures both per-record coordinate data and the
 * (typically single) session-summary message in one pass through the FIT
 * file. Cheaper than two listeners + a MesgBroadcaster.
 */
class CoordinateListener : public fit::MesgListener {
public:
    std::vector<Coordinate> coordinates;
    SessionTotals session;
    FileIdInfo fileId;

    void OnMesg(fit::Mesg& mesg) override {
        const auto num = mesg.GetNum();

        if (num == FIT_MESG_NUM_FILE_ID) {
            fit::FileIdMesg fileIdMesg(mesg);
            if (fileIdMesg.IsManufacturerValid()) {
                fileId.hasManufacturer = true;
                fileId.manufacturer = fileIdMesg.GetManufacturer();
            }
            if (fileIdMesg.IsGarminProductValid()) {
                fileId.hasGarminProduct = true;
                fileId.garminProduct = fileIdMesg.GetGarminProduct();
            }
            if (fileIdMesg.IsProductNameValid()) {
                fileId.hasProductName = true;
                fileId.productName = wstringToUtf8Lossy(fileIdMesg.GetProductName());
            }
            return;
        }

        if (num == FIT_MESG_NUM_RECORD) {
            fit::RecordMesg recordMesg(mesg);

            // Always create a coordinate, even when position is absent.
            // FIT records may legitimately ship without lat/lon — pause
            // segments, indoor trainers, and (most importantly for us)
            // the start/end trim zones our own gps-stripper produces.
            // Previously this branch was gated on position validity and
            // silently discarded the entire record, taking HR / power /
            // cadence / temperature with it. We mark such points as
            // `gpsValid=false` so the writer can later choose to
            // re-emit them without position.
            Coordinate coord;

            bool hasLat = recordMesg.IsPositionLatValid();
            bool hasLon = recordMesg.IsPositionLongValid();
            if (hasLat && hasLon) {
                // FIT semicircles → degrees: 2^31 semicircles = 180°
                coord.lat = recordMesg.GetPositionLat() * (180.0 / std::pow(2, 31));
                coord.lon = recordMesg.GetPositionLong() * (180.0 / std::pow(2, 31));
                coord.gpsValid = true;
            } else {
                coord.lat = 0.0;
                coord.lon = 0.0;
                coord.gpsValid = false;
            }

            coord.elevation = recordMesg.IsAltitudeValid() ? recordMesg.GetAltitude() : 0.0;
            coord.timestamp = recordMesg.IsTimestampValid() ? recordMesg.GetTimestamp() : 0;

            // Health Data
            coord.hasHeartRate = recordMesg.IsHeartRateValid();
            coord.heartRate = coord.hasHeartRate ? recordMesg.GetHeartRate() : 0;

            coord.hasPower = recordMesg.IsPowerValid();
            coord.power = coord.hasPower ? recordMesg.GetPower() : 0;

            coord.hasCadence = recordMesg.IsCadenceValid();
            coord.cadence = coord.hasCadence ? recordMesg.GetCadence() : 0;

            coord.hasTemperature = recordMesg.IsTemperatureValid();
            coord.temperature = coord.hasTemperature ? recordMesg.GetTemperature() : 0;

            coordinates.push_back(coord);
            return;
        }

        if (num == FIT_MESG_NUM_SESSION) {
            fit::SessionMesg sessionMesg(mesg);

            if (sessionMesg.IsTotalDistanceValid()) {
                session.hasDistance = true;
                session.distanceM = sessionMesg.GetTotalDistance();
            }
            if (sessionMesg.IsTotalAscentValid()) {
                session.hasAscent = true;
                session.ascentM = sessionMesg.GetTotalAscent();
            }
            if (sessionMesg.IsTotalDescentValid()) {
                session.hasDescent = true;
                session.descentM = sessionMesg.GetTotalDescent();
            }
            if (sessionMesg.IsMaxSpeedValid()) {
                session.hasMaxSpeed = true;
                session.maxSpeedMs = sessionMesg.GetMaxSpeed();
            }
            if (sessionMesg.IsAvgSpeedValid()) {
                session.hasAvgSpeed = true;
                session.avgSpeedMs = sessionMesg.GetAvgSpeed();
            }
            if (sessionMesg.IsTotalElapsedTimeValid()) {
                session.hasElapsed = true;
                session.elapsedSec = sessionMesg.GetTotalElapsedTime();
            }
            if (sessionMesg.IsTotalTimerTimeValid()) {
                session.hasMoving = true;
                session.movingSec = sessionMesg.GetTotalTimerTime();
            }
            if (sessionMesg.IsSportValid()) {
                session.hasSport = true;
                session.sport = static_cast<uint8_t>(sessionMesg.GetSport());
            }
            if (sessionMesg.IsSubSportValid()) {
                session.hasSubSport = true;
                session.subSport = static_cast<uint8_t>(sessionMesg.GetSubSport());
            }
        }
    }
};

FitParser::FitParser(const std::string& filename) : filename_(filename) {}

RideStatistic FitParser::extractCoordinates() {
    // Open FIT file
    std::fstream file(filename_, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + filename_);
    }
    
    // Create FIT decoder
    fit::Decode decode;
    CoordinateListener listener;
    
    // Decode FIT file - SDK 21.158 API uses references
    if (!decode.Read(file, listener)) {
        file.close();
        throw std::runtime_error("Failed to decode FIT file");
    }
    
    file.close();

    RideStatistic stats;
    stats.coordinates = listener.coordinates;
    stats.distanceKm = 0.0;
    stats.durationMin = 0.0;
    stats.startTime = 0;
    stats.endTime = 0;

    // Initialize health stats
    stats.avgHeartRate = 0;
    stats.maxHeartRate = 0;
    stats.avgPower = 0;
    stats.maxPower = 0;
    stats.normalizedPower = 0;
    stats.avgCadence = 0;
    stats.maxCadence = 0;
    stats.avgSpeed = 0;
    stats.maxSpeed = 0;
    stats.smoothedMaxSpeed = 0;
    stats.coastingTimeSec = 0;
    stats.coastingDistanceKm = 0;
    stats.coastingPct = 0;
    stats.movingTimeSec = 0;
    stats.hasHeartRateData = false;
    stats.hasPowerData = false;
    stats.hasCadenceData = false;

    // Roadmap #156: copy session totals through to the result. PHP
    // prefers these over the Haversine sum below when present.
    if (listener.session.hasDistance) {
        stats.hasSessionDistance = true;
        stats.sessionDistanceKm = std::round((listener.session.distanceM / 1000.0) * 100.0) / 100.0;
    }
    if (listener.session.hasAscent) {
        stats.hasSessionAscent = true;
        stats.sessionElevationGainM = listener.session.ascentM;
    }
    if (listener.session.hasDescent) {
        stats.hasSessionDescent = true;
        stats.sessionElevationLossM = listener.session.descentM;
    }
    if (listener.session.hasMaxSpeed) {
        stats.hasSessionMaxSpeed = true;
        stats.sessionMaxSpeedKmh = std::round((listener.session.maxSpeedMs * 3.6) * 10.0) / 10.0;
    }
    if (listener.session.hasAvgSpeed) {
        stats.hasSessionAvgSpeed = true;
        stats.sessionAvgSpeedKmh = std::round((listener.session.avgSpeedMs * 3.6) * 10.0) / 10.0;
    }
    if (listener.session.hasElapsed) {
        stats.hasSessionElapsed = true;
        stats.sessionElapsedSec = listener.session.elapsedSec;
    }
    if (listener.session.hasMoving) {
        stats.hasSessionMoving = true;
        stats.sessionMovingSec = listener.session.movingSec;
    }

    // Indoor / source-app metadata captured from the FileId + Session
    // messages, with isIndoor derived from the most reliable signals only:
    //   - sub_sport == INDOOR_CYCLING (6) or VIRTUAL_ACTIVITY (58)
    //   - manufacturer == ZWIFT (260) / ZWIFT_BYTE (144) / MYWHOOSH (331)
    //     / BKOOL (67) — apps that ship *only* indoor experiences
    // Tacx is intentionally NOT in the manufacturer auto-flag list because
    // the same vendor id appears on Tacx outdoor head units; a Tacx file
    // still flags indoor when its sub_sport says so.
    if (listener.fileId.hasManufacturer) {
        stats.hasManufacturer = true;
        stats.manufacturer = listener.fileId.manufacturer;
    }
    if (listener.fileId.hasGarminProduct) {
        stats.hasGarminProduct = true;
        stats.garminProduct = listener.fileId.garminProduct;
    }
    if (listener.fileId.hasProductName) {
        stats.hasProductName = true;
        stats.productName = listener.fileId.productName;
    }
    if (listener.session.hasSport) {
        stats.hasSport = true;
        stats.sport = listener.session.sport;
    }
    if (listener.session.hasSubSport) {
        stats.hasSubSport = true;
        stats.subSport = listener.session.subSport;
    }

    if (stats.hasSubSport
        && (stats.subSport == FIT_SUB_SPORT_INDOOR_CYCLING
            || stats.subSport == FIT_SUB_SPORT_VIRTUAL_ACTIVITY)) {
        stats.isIndoor = true;
    }
    if (!stats.isIndoor && stats.hasManufacturer) {
        switch (stats.manufacturer) {
            case FIT_MANUFACTURER_ZWIFT:
            case FIT_MANUFACTURER_ZWIFT_BYTE:
            case FIT_MANUFACTURER_MYWHOOSH:
            case FIT_MANUFACTURER_BKOOL:
                stats.isIndoor = true;
                break;
            default:
                break;
        }
    }

    if (stats.coordinates.empty()) {
        return stats;
    }

    // GPS de-spike: drop fixes that don't fit the line between their
    // neighbours — a pre-GPS-lock (0,0) leading fix, or an isolated point only
    // reachable at an impossible speed from BOTH sides. A point that merely
    // follows a signal gap (far from the previous fix but continuing normally
    // to the next) is a real location and kept. Runs BEFORE the
    // distance/speed/geometry pass so every derived stat (and the coordinate
    // stream every consumer reads) uses the cleaned track. Without this, one
    // (0,0) fix makes the route line span from null-island to the real ride.
    if (stats.coordinates.size() >= 3) {
        const double glitchKmh = 120.0; // impossible between adjacent fixes for cycling
        auto segKmh = [this](const Coordinate& a, const Coordinate& b) -> double {
            if (b.timestamp <= a.timestamp) return 0.0; // no elapsed time → can't judge
            const double meters = calculateDistance(a.lat, a.lon, b.lat, b.lon);
            return (meters / 1000.0) / ((b.timestamp - a.timestamp) / 3600.0);
        };
        const std::vector<Coordinate> cs = stats.coordinates;
        std::vector<Coordinate> kept;
        kept.reserve(cs.size());
        for (size_t i = 0; i < cs.size(); ++i) {
            const bool hasPrev = i > 0;
            const bool hasNext = i + 1 < cs.size();
            const double sIn = hasPrev ? segKmh(cs[i - 1], cs[i]) : 0.0;
            const double sOut = hasNext ? segKmh(cs[i], cs[i + 1]) : 0.0;
            bool spike = false;
            if (hasPrev && hasNext) {
                spike = (sIn > glitchKmh && sOut > glitchKmh); // isolated interior
            } else if (!hasPrev) {
                spike = (sOut > glitchKmh);                    // leading (pre-lock 0,0)
            } else {
                spike = (sIn > glitchKmh);                     // trailing
            }
            if (!spike) {
                kept.push_back(cs[i]);
            }
        }
        stats.coordinates = kept;
        if (stats.coordinates.empty()) {
            return stats;
        }
    }

    // Calculate stats
    double totalDistanceMeters = 0.0;
    stats.startTime = stats.coordinates.front().timestamp;
    stats.endTime = stats.coordinates.back().timestamp;
    
    // Accumulators for averages
    double totalHeartRate = 0;
    long countHeartRate = 0;
    double totalPower = 0;
    long countPower = 0;
    double totalCadence = 0;
    long countCadence = 0;
    // Time-weighted power integration for a coasting-inclusive average power
    // that matches Strava / Garmin. See the integration in the loop below.
    double powerWork = 0.0;        // Σ power·dt (coasting samples contribute 0)
    double powerTimeWeight = 0.0;  // Σ dt over the same (capped) intervals

    // Calculate duration in minutes (difference between start and end timestamps)
    // FIT timestamps are seconds since epoch
    if (stats.endTime > stats.startTime) {
        stats.durationMin = (stats.endTime - stats.startTime) / 60.0;
    }
    
    // Speed accumulators
    double totalMovingSpeed = 0.0;
    long countMovingSpeed = 0;
    double movingTimeSec = 0.0;
    const double movingThreshold = 1.0; // km/h — below this = stopped
    double coastingTimeSec = 0.0;       // moving but not pedalling (freewheel)
    double coastingDistanceMeters = 0.0;// distance covered while coasting

    // Iterate through coordinates to calculate distance, speed, and health stats
    for (size_t i = 0; i < stats.coordinates.size(); ++i) {
        auto& point = stats.coordinates[i];

        // Distance and per-point speed — only when both endpoints actually
        // have a GPS fix. Without this, a record we now keep (paused
        // segment, indoor, or the start/end trim zone) with lat/lon=0
        // would Haversine a giant phantom segment from the equator and
        // either inflate total distance or zero it out.
        if (i > 0 && point.gpsValid && stats.coordinates[i-1].gpsValid) {
            const auto& prev = stats.coordinates[i-1];
            double segmentMeters = calculateDistance(prev.lat, prev.lon, point.lat, point.lon);
            totalDistanceMeters += segmentMeters;

            // Compute GPS-derived speed (km/h) from consecutive points
            uint32_t dt = point.timestamp - prev.timestamp;
            if (dt > 0) {
                double speedKmh = (segmentMeters / 1000.0) / (dt / 3600.0);
                point.speed = std::round(speedKmh * 10.0) / 10.0;
                point.hasSpeed = true;

                // Moving time and speed stats
                if (point.speed > movingThreshold) {
                    totalMovingSpeed += point.speed;
                    countMovingSpeed++;
                    movingTimeSec += dt;

                    // Coasting: moving but not pedalling (freewheel). Devices
                    // omit cadence/power while coasting rather than logging a
                    // zero, so "not pedalling" = no positive cadence AND no
                    // positive power. Track both time and distance covered.
                    const bool notPedalling = !((prev.hasCadence && prev.cadence > 0)
                        || (prev.hasPower && prev.power > 0));
                    if (notPedalling) {
                        coastingTimeSec += dt;
                        coastingDistanceMeters += segmentMeters;
                    }
                }
                if (point.speed > stats.maxSpeed) {
                    stats.maxSpeed = point.speed;
                }
            }
        }

        // Heart Rate
        if (point.hasHeartRate) {
            stats.hasHeartRateData = true;
            totalHeartRate += point.heartRate;
            countHeartRate++;
            if (point.heartRate > stats.maxHeartRate) {
                stats.maxHeartRate = point.heartRate;
            }
        }

        // Power
        if (point.hasPower) {
            stats.hasPowerData = true;
            totalPower += point.power;
            countPower++;
            if (point.power > stats.maxPower) {
                stats.maxPower = point.power;
            }
        }

        // Time-weighted power integration over the active stream. Each interval
        // bills the PREVIOUS sample's power (0 when coasting / no power) across
        // its dt, so freewheeling-while-moving correctly drags the average
        // power down to the elapsed-time value Strava / Garmin report — rather
        // than the higher pedalling-only mean. dt is capped at 20 s so a real
        // pause / recording dropout isn't billed as coasting-zero, and it's
        // time-weighted so sparse (e.g. 1-per-3-second) logs are handled too.
        if (i > 0) {
            const auto& pp = stats.coordinates[i - 1];
            const uint32_t pdt = point.timestamp - pp.timestamp;
            if (pdt > 0) {
                const double capped = pdt > 20 ? 20.0 : static_cast<double>(pdt);
                powerWork += (pp.hasPower ? static_cast<double>(pp.power) : 0.0) * capped;
                powerTimeWeight += capped;
            }
        }

        // Cadence
        if (point.hasCadence) {
            stats.hasCadenceData = true;
            totalCadence += point.cadence;
            countCadence++;
            if (point.cadence > stats.maxCadence) {
                stats.maxCadence = point.cadence;
            }
        }
    }

    // Copy speed from second point to first (no delta available for first point)
    if (stats.coordinates.size() >= 2 && stats.coordinates[1].hasSpeed) {
        stats.coordinates[0].speed = stats.coordinates[1].speed;
        stats.coordinates[0].hasSpeed = true;
    }

    stats.distanceKm = totalDistanceMeters / 1000.0;

    // Round to 2 decimal places for consistency
    stats.distanceKm = std::round(stats.distanceKm * 100.0) / 100.0;
    stats.durationMin = std::round(stats.durationMin * 100.0) / 100.0;
    stats.maxSpeed = std::round(stats.maxSpeed * 10.0) / 10.0;
    stats.movingTimeSec = movingTimeSec;
    stats.coastingTimeSec = std::round(coastingTimeSec);
    stats.coastingDistanceKm = std::round((coastingDistanceMeters / 1000.0) * 100.0) / 100.0;
    stats.coastingPct = movingTimeSec > 0
        ? std::round((coastingTimeSec / movingTimeSec) * 1000.0) / 10.0
        : 0.0;

    // Spike-resistant max speed: the peak of a rolling MEDIAN over the
    // per-point GPS-derived speed series. The window is TIME-based (~7 s, not a
    // fixed sample count) so it behaves consistently across recording rates.
    //
    // A MEDIAN, not a mean: a single GPS-distance glitch (e.g. a 700 m jump in
    // 1 s = 2500 km/h) only shifts the window's median by one rank, so it's
    // ignored, whereas a mean averages it in — a 5 s window with one 2500 km/h
    // sample and four 28 km/h samples means ~520 km/h, which is exactly the
    // bogus "517 km/h max" this replaces. A sustained real descent peak (the
    // majority of the window) still survives. Consumers should prefer this
    // over maxSpeed.
    {
        const uint32_t windowSec = 7;
        const auto& cs = stats.coordinates;
        const bool haveTimes = !cs.empty() && cs.back().timestamp > cs.front().timestamp;
        double best = 0.0;
        auto windowMedian = [](std::vector<double>& w) -> double {
            if (w.empty()) return 0.0;
            std::sort(w.begin(), w.end());
            const size_t mid = w.size() / 2;
            return (w.size() % 2 == 0) ? (w[mid - 1] + w[mid]) / 2.0 : w[mid];
        };
        if (haveTimes) {
            // Two-pointer sliding window keeping the time span within windowSec.
            size_t start = 0;
            for (size_t end = 0; end < cs.size(); ++end) {
                while (start < end && cs[end].timestamp - cs[start].timestamp > windowSec) {
                    ++start;
                }
                std::vector<double> speeds;
                speeds.reserve(end - start + 1);
                for (size_t k = start; k <= end; ++k) speeds.push_back(cs[k].speed);
                const double med = windowMedian(speeds);
                if (med > best) best = med;
            }
            stats.smoothedMaxSpeed = std::round(best * 10.0) / 10.0;
        } else if (cs.size() >= 5) {
            // No usable timestamps — fall back to a fixed 7-sample median window.
            const size_t win = 7;
            if (cs.size() >= win) {
                for (size_t i = 0; i + win <= cs.size(); ++i) {
                    std::vector<double> speeds;
                    speeds.reserve(win);
                    for (size_t k = i; k < i + win; ++k) speeds.push_back(cs[k].speed);
                    const double med = windowMedian(speeds);
                    if (med > best) best = med;
                }
                stats.smoothedMaxSpeed = std::round(best * 10.0) / 10.0;
            } else {
                stats.smoothedMaxSpeed = stats.maxSpeed;
            }
        } else {
            stats.smoothedMaxSpeed = stats.maxSpeed;
        }
    }

    // Normalized Power (Coggan): 30-second time-weighted rolling-mean power,
    // mean of the 4th powers, 4th root. Coasting counts as ZERO watts (the
    // reader stores power = 0 when the device omits it while freewheeling),
    // matching the coasting-inclusive average power and the standard NP
    // definition — rather than holding the prior power across the gap. Only
    // computed when the ride actually carries power data.
    if (stats.hasPowerData) {
        const auto& cs = stats.coordinates;
        const size_t n = cs.size();
        if (n >= 2) {
            std::vector<double> dur(n);
            for (size_t i = 0; i + 1 < n; ++i) {
                const double d = static_cast<double>(cs[i + 1].timestamp) - static_cast<double>(cs[i].timestamp);
                dur[i] = d > 0.001 ? d : 0.001;
            }
            dur[n - 1] = dur[n - 2];
            double totalDuration = 0.0;
            for (const double d : dur) totalDuration += d;
            if (totalDuration >= 30.0) {
                std::vector<double> avgs;
                size_t left = 0;
                double weightedSum = 0.0;
                double totalWeight = 0.0;
                for (size_t right = 0; right < n; ++right) {
                    const double p = cs[right].hasPower ? static_cast<double>(cs[right].power) : 0.0;
                    weightedSum += p * dur[right];
                    totalWeight += dur[right];
                    while (left < right
                        && (static_cast<double>(cs[right].timestamp) + dur[right] - static_cast<double>(cs[left].timestamp)) > 30.0) {
                        const double pl = cs[left].hasPower ? static_cast<double>(cs[left].power) : 0.0;
                        weightedSum -= pl * dur[left];
                        totalWeight -= dur[left];
                        ++left;
                    }
                    if (totalWeight + 1e-9 >= 30.0) {
                        avgs.push_back(weightedSum / totalWeight);
                    }
                }
                if (!avgs.empty()) {
                    double sumFourth = 0.0;
                    for (const double a : avgs) sumFourth += a * a * a * a;
                    stats.normalizedPower = std::round(std::pow(sumFourth / static_cast<double>(avgs.size()), 0.25) * 10.0) / 10.0;
                }
            }
        }
    }

    // Calculate Averages
    if (countHeartRate > 0) stats.avgHeartRate = totalHeartRate / countHeartRate;
    // Average power: prefer the coasting-inclusive time-weighted value (matches
    // Strava / Garmin, which average over elapsed/moving time with coasting as
    // zero). Fall back to the pedalling-only mean only when timestamps are
    // unusable for integration.
    if (stats.hasPowerData && powerTimeWeight > 0) {
        stats.avgPower = powerWork / powerTimeWeight;
    } else if (countPower > 0) {
        stats.avgPower = totalPower / countPower;
    }
    if (countCadence > 0) stats.avgCadence = totalCadence / countCadence;
    if (countMovingSpeed > 0) stats.avgSpeed = std::round((totalMovingSpeed / countMovingSpeed) * 10.0) / 10.0;

    return stats;
}

double FitParser::calculateDistance(double lat1, double lon1, double lat2, double lon2) {
    const double earthRadius = 6371003.0; // Mean Earth Radius in meters
    const double degToRad = M_PI / 180.0;

    double dLat = (lat2 - lat1) * degToRad;
    double dLon = (lon2 - lon1) * degToRad;

    double a = std::sin(dLat / 2) * std::sin(dLat / 2) +
               std::cos(lat1 * degToRad) * std::cos(lat2 * degToRad) *
               std::sin(dLon / 2) * std::sin(dLon / 2);

    double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));

    return earthRadius * c;
}
