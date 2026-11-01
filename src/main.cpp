#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <string>
#include <algorithm>
#include <cmath>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include "fit_parser.h"
#include "json_output.h"
#include "gpx_parser.h"
#include "tcx_parser.h"
#include "fit_writer.h"
#include "health_stripper.h"
#include "track_point.h"

/**
 * Set resource limits for security
 */
void setResourceLimits() {
    struct rlimit limit;

    // Memory limit: 256MB
    limit.rlim_cur = 256 * 1024 * 1024;
    limit.rlim_max = 256 * 1024 * 1024;
    setrlimit(RLIMIT_AS, &limit);

    // CPU time limit: 30 seconds
    limit.rlim_cur = 30;
    limit.rlim_max = 30;
    setrlimit(RLIMIT_CPU, &limit);

    // No core dumps
    limit.rlim_cur = 0;
    limit.rlim_max = 0;
    setrlimit(RLIMIT_CORE, &limit);

    // Limit number of open file descriptors
    limit.rlim_cur = 10;
    limit.rlim_max = 10;
    setrlimit(RLIMIT_NOFILE, &limit);
}

/**
 * Print usage information
 */
void printUsage(const char* programName) {
    std::cerr << "Usage: " << programName << " <input_file> [--convert <output.fit> | --strip-health <output.fit> | --strip-hr <output.fit>]" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Supported input formats: .fit, .gpx, .tcx" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Modes:" << std::endl;
    std::cerr << "  <input_file>                         Parse and output JSON to stdout" << std::endl;
    std::cerr << "  <input_file> --convert <output.fit>   Convert GPX/TCX to FIT format" << std::endl;
    std::cerr << "  <input_file> --strip-health <out.fit> Remove health data (heart rate, power, cadence, ...) and all developer data;" << std::endl;
    std::cerr << "                                       FIT/GPX/TCX in, valid FIT out, JSON counters on stdout" << std::endl;
    std::cerr << "  <input_file> --strip-hr <out.fit>     Remove heart rate and other body data and all developer data;" << std::endl;
    std::cerr << "                                       keeps power and cadence (same table, same output rules)" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Options:" << std::endl;
    std::cerr << "  --version    Show version information" << std::endl;
    std::cerr << "  --help       Show this help message" << std::endl;
}

/**
 * Print version information
 */
void printVersion() {
    std::cout << "BikeCodersLife FIT Parser v2.5.0" << std::endl;
    std::cout << "Built with Garmin FIT SDK + pugixml" << std::endl;
    std::cout << "Supports: FIT, GPX, TCX input | FIT, JSON output" << std::endl;
    std::cout << "v2.5.0: --strip-hr <out.fit>: removes heart rate and other body data (HRV, respiration, SpO2, body temperature, calories, training load, zones, user profile, sleep, ...) and ALL developer data, but keeps power, cadence, pedalling dynamics, work, e-bike motor power, standing time and trainer resistance (bike part wear); same table and same fail-closed rules as --strip-health (docs/health-strip.md)" << std::endl;
    std::cout << "v2.4.0 (structure check): the plain parse mode refuses FIT files with an invalid structure (zero sized field, unknown base type, bad architecture byte, duplicate field numbers, data without definition) with exit 1 before the vendor SDK decoder sees them" << std::endl;
    std::cout << "v2.4.0: --strip-health <out.fit>: removes health data from FIT, GPX and TCX (heart rate, HRV, power and derived values, cadence, respiration, body temperature, running dynamics, pedalling dynamics, muscle oxygen, calories, training load, zones, user profile, sleep, ...) and ALL developer data; allow-list decided from the FIT SDK profile (docs/health-strip.md); output is always a valid FIT; fails closed (non-zero exit, no output file)" << std::endl;
    std::cout << "v2.3.3: GPX/TCX points without position (GlobalSat lat=0 lon=0, TCX without <Position>) are kept with their time and sensors instead of dropped; a trainer ride no longer fails with \"no valid track points\"; a ride without any position takes its distance from the device (GPX <trk><extensions><length>, TCX <DistanceMeters>)" << std::endl;
    std::cout << "v2.3.2: GPS spike filter for smoothedMaxSpeedKmh (all formats): segments > 150 km/h never count, the 7 s median window holds >= 4 samples on sparse logs, and the result never exceeds the device's own recorded max speed (FIT session, GlobalSat GPX <maxspeed>, TCX Lap MaximumSpeed); GPX/TCX get the FIT de-spike of isolated fixes (distance/ascent re-summed over the kept fixes only when one was dropped)" << std::endl;
    std::cout << "v2.3.1: GPX/TCX JSON no longer emits uninitialised power/cadence/heartRate on every point; copies the real HR/power/cadence/temperature/speed from the file; GPX/TCX summary uses the same stream-stats pass as FIT; GPX power inside TrackPointExtension is read" << std::endl;
    std::cout << "v2.3.0: emit Session.start_time + FileId.serial_number/time_created (cross-provider ride identity)" << std::endl;
    std::cout << "v2.2.0: decode device_info (per-sensor identity + battery voltage/status/level) into a top-level devices[] JSON array; GPS strip passes device_info through so archived rides keep battery telemetry (battery two-track design 2026-07-26)" << std::endl;
    std::cout << "v2.1.6: smoothedMaxSpeedKmh uses a rolling MEDIAN (was mean — a single GPS jump inflated it to 500+ km/h); de-spike GPS fixes that don't fit the line between neighbours (drops the pre-lock (0,0) fix that otherwise stretches the route to null-island)" << std::endl;
    std::cout << "v2.1.5: emit normalizedPower (Coggan 30s rolling, coasting = 0 W) — consistent with coasting-inclusive avgPower instead of the held-power-across-gaps inflation" << std::endl;
    std::cout << "v2.1.4: emit maxCadence; spike-resistant time-windowed smoothedMaxSpeedKmh; coasting-inclusive time-weighted avgPower (matches Strava/Garmin); emit coastingTimeSec/coastingDistanceKm/coastingPct" << std::endl;
    std::cout << "v2.1.3: reject single-sample GPS-distance glitches in derived per-record speed (acceleration cap) so session max-speed isn't spiked above the real peak on Strava-synth rides" << std::endl;
    std::cout << "v2.1.2: gps-strip actually redacts position + preserves HR/power/cadence/temp/speed; derive per-record speed from GPS deltas so session max-speed isn't collapsed to avg; keep records without GPS (indoor pauses, trim zones)" << std::endl;
    std::cout << "v2.1.1: emit FileId.manufacturer + Session.sub_sport + derived isIndoor flag (indoor trainer / virtual-activity detection)" << std::endl;
    std::cout << "v2.1.0 (#156): emit FIT session totals (sessionDistanceKm, sessionElevationGainM/LossM, sessionMaxSpeedKmh, sessionAvgSpeedKmh, sessionElapsedSec, sessionMovingSec)" << std::endl;
}

/**
 * Validate file path and size for security
 */
bool validateFile(const char* filepath) {
    // Maximum file size: 100MB
    const off_t MAX_FILE_SIZE = 100 * 1024 * 1024;

    if (!filepath || filepath[0] == '\0') {
        std::cerr << "Error: Invalid file path" << std::endl;
        return false;
    }

    char realPath[PATH_MAX];
    if (!realpath(filepath, realPath)) {
        std::cerr << "Error: Cannot resolve file path" << std::endl;
        return false;
    }

    struct stat fileStat;
    if (stat(realPath, &fileStat) != 0) {
        std::cerr << "Error: Cannot access file" << std::endl;
        return false;
    }

    if (!S_ISREG(fileStat.st_mode)) {
        std::cerr << "Error: Not a regular file" << std::endl;
        return false;
    }

    if (fileStat.st_size > MAX_FILE_SIZE) {
        std::cerr << "Error: File too large (max 100MB)" << std::endl;
        return false;
    }

    if (fileStat.st_size == 0) {
        std::cerr << "Error: File is empty" << std::endl;
        return false;
    }

    return true;
}

/**
 * Get lowercase file extension from a path
 */
std::string getFileExtension(const std::string& path) {
    size_t dotPos = path.rfind('.');
    if (dotPos == std::string::npos) {
        return "";
    }
    std::string ext = path.substr(dotPos);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

/**
 * Convert a ParsedActivity (GPX/TCX) to a RideStatistic for JSON output.
 *
 * GPX/TCX have no FIT-style "session" message but the parsers compute the
 * same totals from track data + explicit elevation tags. Surface them as
 * the session-summary fields so PHP can apply identical preference rules
 * across sources (#156).
 */
RideStatistic activityToRideStatistic(const ParsedActivity& activity) {
    RideStatistic stats;

    // GPS de-spike (v2.3.2): the same isolated-fix rule the FIT path has
    // applied since v2.1.6 (gpsDespikeKeepMask), so a GPX/TCX parsed
    // directly reports the same track as the same file converted to FIT.
    // Up to v2.3.1 the GPX/TCX path kept every fix, so one teleported point
    // stayed in the route line, the replay and the speed series.
    std::vector<Coordinate> allCoordinates;
    allCoordinates.reserve(activity.points.size());
    for (const auto& pt : activity.points) {
        Coordinate c;
        c.lat = pt.lat;
        c.lon = pt.lon;
        c.timestamp = pt.timestamp;
        c.gpsValid = pt.hasPosition;
        allCoordinates.push_back(c);
    }
    const std::vector<bool> keep = gpsDespikeKeepMask(allCoordinates);
    std::vector<const TrackPoint*> kept;
    kept.reserve(activity.points.size());
    for (size_t i = 0; i < activity.points.size(); ++i) {
        if (keep[i]) kept.push_back(&activity.points[i]);
    }

    // Reader totals. When the de-spike dropped a fix, the Haversine distance
    // and the ascent / descent are summed again over the kept fixes, with the
    // readers' own arithmetic: a teleported fix adds a detour that was never
    // ridden (e.g. 234 m out and 58 m back in 2 s). When nothing was dropped
    // (every clean ride) the reader totals are used as they are, so the
    // output is byte-identical to v2.3.1. A TCX device distance
    // (<DistanceMeters>, the odometer) is kept either way: a bad GPS fix
    // does not move the odometer.
    double totalDistanceM = activity.totalDistanceM;
    double totalAscentM = activity.totalAscentM;
    double totalDescentM = activity.totalDescentM;
    if (kept.size() != activity.points.size() && !kept.empty()) {
        double distance = 0.0;
        double ascent = 0.0;
        double descent = 0.0;
        for (size_t i = 1; i < kept.size(); ++i) {
            const TrackPoint& a = *kept[i - 1];
            const TrackPoint& b = *kept[i];
            distance += haversineMeters(a.lat, a.lon, b.lat, b.lon);
            if (a.hasElevation && b.hasElevation) {
                const double diff = b.elevation - a.elevation;
                if (diff > 0) {
                    ascent += diff;
                } else {
                    descent += (-diff);
                }
            }
        }
        if (!activity.hasDeviceDistance) {
            totalDistanceM = distance;
        }
        totalAscentM = ascent;
        totalDescentM = descent;
    }

    stats.distanceKm = totalDistanceM / 1000.0;
    stats.durationMin = activity.durationSec / 60.0;
    stats.startTime = activity.startTime;
    stats.endTime = activity.endTime;

    // Round for consistency
    stats.distanceKm = std::round(stats.distanceKm * 100.0) / 100.0;
    stats.durationMin = std::round(stats.durationMin * 100.0) / 100.0;

    // Surface parsed totals as session-summary fields for #156. GPX/TCX
    // always have distance + duration; ascent/descent are populated only
    // when the source carried an <ele> stream the parser could
    // differentiate.
    if (totalDistanceM > 0.0) {
        stats.hasSessionDistance = true;
        stats.sessionDistanceKm = stats.distanceKm;
    }
    if (totalAscentM > 0.0) {
        stats.hasSessionAscent = true;
        stats.sessionElevationGainM = totalAscentM;
    }
    if (totalDescentM > 0.0) {
        stats.hasSessionDescent = true;
        stats.sessionElevationLossM = totalDescentM;
    }
    if (activity.durationSec > 0.0) {
        stats.hasSessionElapsed = true;
        stats.sessionElapsedSec = activity.durationSec;
        // GPX/TCX don't separate moving-time from elapsed-time; treat
        // elapsed as moving so the field is populated for downstream
        // averageSpeed math. FIT files override this with the real
        // total_timer_time when present.
        stats.hasSessionMoving = true;
        stats.sessionMovingSec = activity.durationSec;
    }
    // The device's own recorded max speed, when the file carries one
    // (v2.3.2): emitted like a FIT session.max_speed, and the upper bound
    // for the GPS-derived smoothed max (see computeStreamStatistics).
    if (activity.hasDeviceMaxSpeed && activity.deviceMaxSpeedMs > 0.0) {
        stats.hasSessionMaxSpeed = true;
        stats.sessionMaxSpeedKmh = std::round((activity.deviceMaxSpeedMs * 3.6) * 10.0) / 10.0;
    }

    // Convert TrackPoints to Coordinates, sensor fields included. Up to
    // v2.3.0 only position/elevation/time were copied and the rest of the
    // Coordinate was left uninitialised, so every GPX/TCX ride emitted one
    // garbage power/cadence/heartRate value on every point while the real
    // sensor streams the readers had parsed were dropped.
    stats.coordinates.reserve(kept.size());
    for (const TrackPoint* p : kept) {
        const TrackPoint& pt = *p;
        Coordinate coord;
        coord.lat = pt.lat;
        coord.lon = pt.lon;
        coord.gpsValid = pt.hasPosition;
        coord.elevation = pt.elevation;
        coord.timestamp = pt.timestamp;
        coord.heartRate = pt.heart_rate;
        coord.hasHeartRate = pt.hasHeartRate;
        coord.power = pt.power;
        coord.hasPower = pt.hasPower;
        coord.cadence = pt.cadence;
        coord.hasCadence = pt.hasCadence;
        coord.temperature = pt.temperature;
        coord.hasTemperature = pt.hasTemperature;
        // TrackPoint.speed is m/s (FIT-native); Coordinate.speed is km/h.
        coord.speed = static_cast<double>(pt.speed) * 3.6;
        coord.hasSpeed = pt.hasSpeed;
        stats.coordinates.push_back(coord);
    }

    // Same record-stream summary as the FIT path (speed, moving time,
    // coasting, HR / power / cadence aggregates, NP), so a GPX parsed
    // directly reports what the same GPX converted to FIT reports. The
    // GPX/TCX readers already summed distance / duration over the full
    // track (TCX may carry the device's own DistanceMeters), so their
    // totals set above win over the pass's Haversine recomputation.
    const double parsedDistanceKm = stats.distanceKm;
    const double parsedDurationMin = stats.durationMin;
    computeStreamStatistics(stats);
    stats.distanceKm = parsedDistanceKm;
    stats.durationMin = parsedDurationMin;
    stats.startTime = activity.startTime;
    stats.endTime = activity.endTime;

    return stats;
}

/**
 * --strip-health / --strip-hr: write a FIT without health data
 * (StripMode::Health) or without heart rate and body data (StripMode::HeartRate).
 *
 * FIT in: decoded and re-encoded through the allow-list (HealthStripper).
 * GPX/TCX in: the health values are dropped from the parsed points, the
 * activity is written as FIT, and that FIT goes through the same allow-list,
 * so every source ends up behind one filter.
 *
 * Prints one JSON line with the counters. Throws on any failure; the output
 * file only appears (atomic rename) when everything succeeded.
 */
int runStripHealth(const std::string& inputPath, const std::string& ext, const std::string& outputPath, StripMode mode) {
    HealthStripStats stats;

    if (ext == ".fit") {
        HealthStripper::stripFile(inputPath, outputPath, stats, mode);
    } else if (ext == ".gpx" || ext == ".tcx") {
        ParsedActivity activity;
        if (ext == ".gpx") {
            GpxParser parser(inputPath);
            activity = parser.parse();
        } else {
            TcxParser parser(inputPath);
            activity = parser.parse();
        }
        for (auto& pt : activity.points) {
            pt.heart_rate = 0;
            pt.hasHeartRate = false;
            if (mode == StripMode::HeartRate) {
                continue; // --strip-hr keeps cadence and power
            }
            pt.cadence = 0;
            pt.hasCadence = false;
            pt.power = 0;
            pt.hasPower = false;
        }

        // unpredictable temp name next to the output (mkstemp), removed on every handled failure
        std::string tmpFit = outputPath + ".conv.XXXXXX";
        {
            std::vector<char> nm(tmpFit.begin(), tmpFit.end());
            nm.push_back('\0');
            const int fd = mkstemp(nm.data());
            if (fd < 0) {
                throw std::runtime_error("Cannot create temporary file next to: " + outputPath);
            }
            close(fd);
            tmpFit.assign(nm.data());
        }
        std::string converted;
        try {
            FitWriter writer;
            writer.write(activity, tmpFit);
            std::ifstream in(tmpFit, std::ios::in | std::ios::binary);
            if (!in.is_open()) {
                throw std::runtime_error("Cannot read back the converted FIT");
            }
            std::ostringstream buffer;
            buffer << in.rdbuf();
            converted = buffer.str();
        } catch (...) {
            std::remove(tmpFit.c_str());
            throw;
        }
        std::remove(tmpFit.c_str());

        HealthStripper::writeAtomically(outputPath, HealthStripper::stripBytes(converted, stats, mode));
    } else {
        throw std::runtime_error("Unsupported file format '" + ext + "'. Supported: .fit, .gpx, .tcx");
    }

    std::cout << "{\"droppedMessages\": " << stats.droppedMessages
              << ", \"strippedFields\": " << stats.strippedFields
              << ", \"developerFieldsRemoved\": " << stats.developerFieldsRemoved
              << ", \"trailingBytesDropped\": " << stats.trailingBytesDropped << "}" << std::endl;
    if (stats.trailingBytesDropped > 0) {
        std::cerr << "Warning: " << stats.trailingBytesDropped
                  << " byte(s) after the last valid FIT file were not copied (junk, padding or a damaged chained file)" << std::endl;
    }
    return 0;
}

/**
 * Main entry point
 */
int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    if (std::strcmp(argv[1], "--version") == 0) {
        printVersion();
        return 0;
    }

    if (std::strcmp(argv[1], "--help") == 0) {
        printUsage(argv[0]);
        return 0;
    }

    // Set security limits
    setResourceLimits();

    // Validate input file
    if (!validateFile(argv[1])) {
        return 1;
    }

    std::string inputPath = argv[1];
    std::string ext = getFileExtension(inputPath);

    // Check for --convert mode
    std::string outputPath;
    bool convertMode = false;
    bool stripHealthMode = false;
    bool stripHrMode = false;
    for (int i = 2; i < argc; i++) {
        const bool health = std::strcmp(argv[i], "--strip-health") == 0;
        const bool hr = std::strcmp(argv[i], "--strip-hr") == 0;
        if (health || hr) {
            if (i + 1 < argc) {
                outputPath = argv[i + 1];
                (health ? stripHealthMode : stripHrMode) = true;
                i++;
            } else {
                std::cerr << "Error: " << argv[i] << " requires an output file path" << std::endl;
                return 1;
            }
            continue;
        }
        if (std::strcmp(argv[i], "--convert") == 0) {
            if (i + 1 < argc) {
                outputPath = argv[i + 1];
                convertMode = true;
                i++;
            } else {
                std::cerr << "Error: --convert requires an output file path" << std::endl;
                return 1;
            }
        }
    }

    if (stripHealthMode && stripHrMode) {
        std::cerr << "Error: --strip-health and --strip-hr cannot be combined" << std::endl;
        return 1;
    }
    if ((stripHealthMode || stripHrMode) && convertMode) {
        std::cerr << "Error: " << (stripHealthMode ? "--strip-health" : "--strip-hr") << " and --convert cannot be combined" << std::endl;
        return 1;
    }

    try {
        if (stripHealthMode || stripHrMode) {
            return runStripHealth(inputPath, ext, outputPath, stripHrMode ? StripMode::HeartRate : StripMode::Health);
        }

        if (ext == ".fit") {
            if (convertMode) {
                std::cerr << "Error: Cannot convert FIT to FIT. Use GPX or TCX as input for conversion." << std::endl;
                return 1;
            }
            // Existing behavior: parse FIT, output JSON
            FitParser parser(inputPath);
            RideStatistic stats = parser.extractCoordinates();
            JsonOutput output;
            output.writeCoordinates(stats);

        } else if (ext == ".gpx") {
            GpxParser parser(inputPath);
            ParsedActivity activity = parser.parse();

            if (convertMode) {
                // Convert to FIT
                FitWriter writer;
                writer.write(activity, outputPath);
                std::cerr << "Converted " << activity.points.size() << " points to " << outputPath << std::endl;
                std::cerr << "Distance: " << std::round(activity.totalDistanceM / 10.0) / 100.0 << " km" << std::endl;
                std::cerr << "Duration: " << std::round(activity.durationSec / 0.6) / 100.0 << " min" << std::endl;
                std::cerr << "Ascent: " << std::round(activity.totalAscentM) << " m" << std::endl;
            } else {
                // Output JSON (same format as FIT)
                RideStatistic stats = activityToRideStatistic(activity);
                JsonOutput output;
                output.writeCoordinates(stats);
            }

        } else if (ext == ".tcx") {
            TcxParser parser(inputPath);
            ParsedActivity activity = parser.parse();

            if (convertMode) {
                // Convert to FIT
                FitWriter writer;
                writer.write(activity, outputPath);
                std::cerr << "Converted " << activity.points.size() << " points to " << outputPath << std::endl;
                std::cerr << "Distance: " << std::round(activity.totalDistanceM / 10.0) / 100.0 << " km" << std::endl;
                std::cerr << "Duration: " << std::round(activity.durationSec / 0.6) / 100.0 << " min" << std::endl;
                std::cerr << "Ascent: " << std::round(activity.totalAscentM) << " m" << std::endl;
            } else {
                // Output JSON (same format as FIT)
                RideStatistic stats = activityToRideStatistic(activity);
                JsonOutput output;
                output.writeCoordinates(stats);
            }

        } else {
            std::cerr << "Error: Unsupported file format '" << ext << "'. Supported: .fit, .gpx, .tcx" << std::endl;
            return 1;
        }

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Error: Unknown exception occurred" << std::endl;
        return 1;
    }
}
