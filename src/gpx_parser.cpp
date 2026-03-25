#include "gpx_parser.h"
#include <pugixml.hpp>
#include <stdexcept>
#include <cmath>
#include <ctime>
#include <cstring>
#include <sstream>

// FIT epoch offset: seconds between Unix epoch (1970-01-01) and FIT epoch (1989-12-31)
static const uint32_t FIT_EPOCH_OFFSET = 631065600;

GpxParser::GpxParser(const std::string& filename) : filename_(filename) {}

uint32_t GpxParser::iso8601ToFitTimestamp(const std::string& isoTime) {
    if (isoTime.empty()) {
        return 0;
    }

    struct tm tm = {};
    // Try parsing "YYYY-MM-DDTHH:MM:SSZ" or "YYYY-MM-DDTHH:MM:SS+00:00"
    // strptime is POSIX, available on Linux
    const char* result = strptime(isoTime.c_str(), "%Y-%m-%dT%H:%M:%S", &tm);
    if (!result) {
        // Try alternative format without T separator
        result = strptime(isoTime.c_str(), "%Y-%m-%d %H:%M:%S", &tm);
        if (!result) {
            return 0;
        }
    }

    // timegm converts struct tm (UTC) to time_t
    time_t unixTime = timegm(&tm);
    if (unixTime < 0) {
        return 0;
    }

    // Convert Unix epoch to FIT epoch
    if (static_cast<uint64_t>(unixTime) < FIT_EPOCH_OFFSET) {
        return 0; // Before FIT epoch
    }

    return static_cast<uint32_t>(unixTime - FIT_EPOCH_OFFSET);
}

double GpxParser::haversineDistance(double lat1, double lon1, double lat2, double lon2) {
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

ParsedActivity GpxParser::parse() {
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(filename_.c_str());

    if (!result) {
        throw std::runtime_error("Failed to parse GPX file: " + std::string(result.description()));
    }

    ParsedActivity activity;

    // GPX root element — handle both with and without namespace
    pugi::xml_node gpxNode = doc.child("gpx");
    if (!gpxNode) {
        throw std::runtime_error("Invalid GPX file: missing <gpx> root element");
    }

    // Try to get track name from metadata or first track
    pugi::xml_node metadata = gpxNode.child("metadata");
    if (metadata) {
        pugi::xml_node nameNode = metadata.child("name");
        if (nameNode) {
            activity.name = nameNode.child_value();
        }
    }

    double cumulativeDistance = 0.0;
    double totalAscent = 0.0;
    double totalDescent = 0.0;
    TrackPoint prevPoint;
    bool hasPrevPoint = false;

    // Iterate over all tracks
    for (pugi::xml_node trk = gpxNode.child("trk"); trk; trk = trk.next_sibling("trk")) {
        // Get track name if activity name not set yet
        if (activity.name.empty()) {
            // Try both <name> and <Name> (GPX 1.0 sometimes uses <Name>)
            pugi::xml_node trkName = trk.child("name");
            if (!trkName) {
                trkName = trk.child("Name");
            }
            if (trkName) {
                activity.name = trkName.child_value();
            }
        }

        // Iterate over track segments
        for (pugi::xml_node trkseg = trk.child("trkseg"); trkseg; trkseg = trkseg.next_sibling("trkseg")) {
            // Iterate over track points
            for (pugi::xml_node trkpt = trkseg.child("trkpt"); trkpt; trkpt = trkpt.next_sibling("trkpt")) {
                TrackPoint point;

                // Lat/lon are attributes
                point.lat = trkpt.attribute("lat").as_double(0.0);
                point.lon = trkpt.attribute("lon").as_double(0.0);

                // Skip points without valid coordinates
                if (point.lat == 0.0 && point.lon == 0.0) {
                    continue;
                }

                // Elevation
                pugi::xml_node eleNode = trkpt.child("ele");
                if (eleNode) {
                    point.elevation = eleNode.text().as_double(0.0);
                    point.hasElevation = true;
                }

                // Timestamp
                pugi::xml_node timeNode = trkpt.child("time");
                if (timeNode) {
                    point.timestamp = iso8601ToFitTimestamp(timeNode.child_value());
                }

                // GPX 1.0: <speed> element directly in trkpt
                pugi::xml_node speedNode = trkpt.child("speed");
                if (speedNode) {
                    point.speed = speedNode.text().as_float(0.0f);
                    point.hasSpeed = true;
                }

                // Parse extensions (Garmin TrackPointExtension and others)
                pugi::xml_node extensions = trkpt.child("extensions");
                if (extensions) {
                    // Garmin TrackPointExtension v1/v2:
                    // <gpxtpx:TrackPointExtension> or <TrackPointExtension>
                    // Try multiple namespace prefixes and no prefix
                    pugi::xml_node tpExt = extensions.child("gpxtpx:TrackPointExtension");
                    if (!tpExt) tpExt = extensions.child("TrackPointExtension");
                    if (!tpExt) tpExt = extensions.child("ns3:TrackPointExtension");

                    if (tpExt) {
                        // Heart rate: <gpxtpx:hr> or <hr>
                        pugi::xml_node hrNode = tpExt.child("gpxtpx:hr");
                        if (!hrNode) hrNode = tpExt.child("hr");
                        if (!hrNode) hrNode = tpExt.child("ns3:hr");
                        if (hrNode) {
                            int hr = hrNode.text().as_int(0);
                            if (hr > 0 && hr <= 255) {
                                point.heart_rate = static_cast<uint8_t>(hr);
                                point.hasHeartRate = true;
                            }
                        }

                        // Cadence: <gpxtpx:cad> or <cad>
                        pugi::xml_node cadNode = tpExt.child("gpxtpx:cad");
                        if (!cadNode) cadNode = tpExt.child("cad");
                        if (!cadNode) cadNode = tpExt.child("ns3:cad");
                        if (cadNode) {
                            int cad = cadNode.text().as_int(0);
                            if (cad > 0 && cad <= 255) {
                                point.cadence = static_cast<uint8_t>(cad);
                                point.hasCadence = true;
                            }
                        }

                        // Temperature: <gpxtpx:atemp> or <atemp>
                        pugi::xml_node tempNode = tpExt.child("gpxtpx:atemp");
                        if (!tempNode) tempNode = tpExt.child("atemp");
                        if (!tempNode) tempNode = tpExt.child("ns3:atemp");
                        if (tempNode) {
                            int temp = tempNode.text().as_int(0);
                            point.temperature = static_cast<int8_t>(temp);
                            point.hasTemperature = true;
                        }

                        // Speed from extension
                        pugi::xml_node spdNode = tpExt.child("gpxtpx:speed");
                        if (!spdNode) spdNode = tpExt.child("speed");
                        if (spdNode && !point.hasSpeed) {
                            point.speed = spdNode.text().as_float(0.0f);
                            point.hasSpeed = true;
                        }
                    }

                    // Power: often in a separate extension namespace
                    // <power> directly in extensions (common with power meters)
                    pugi::xml_node powerNode = extensions.child("power");
                    if (!powerNode) powerNode = extensions.child("gpxtpx:power");
                    if (powerNode) {
                        int pwr = powerNode.text().as_int(0);
                        if (pwr > 0 && pwr <= 65535) {
                            point.power = static_cast<uint16_t>(pwr);
                            point.hasPower = true;
                        }
                    }
                }

                // Calculate cumulative distance
                if (hasPrevPoint) {
                    double segDist = haversineDistance(prevPoint.lat, prevPoint.lon, point.lat, point.lon);
                    cumulativeDistance += segDist;

                    // Calculate ascent/descent
                    if (point.hasElevation && prevPoint.hasElevation) {
                        double eleDiff = point.elevation - prevPoint.elevation;
                        if (eleDiff > 0) {
                            totalAscent += eleDiff;
                        } else {
                            totalDescent += (-eleDiff);
                        }
                    }
                }

                point.distance = cumulativeDistance;
                activity.points.push_back(point);

                prevPoint = point;
                hasPrevPoint = true;
            }
        }
    }

    if (activity.points.empty()) {
        throw std::runtime_error("GPX file contains no valid track points");
    }

    // Fill in activity summary
    activity.totalDistanceM = cumulativeDistance;
    activity.totalAscentM = totalAscent;
    activity.totalDescentM = totalDescent;
    activity.startTime = activity.points.front().timestamp;
    activity.endTime = activity.points.back().timestamp;

    if (activity.endTime > activity.startTime) {
        activity.durationSec = static_cast<double>(activity.endTime - activity.startTime);
    }

    return activity;
}
