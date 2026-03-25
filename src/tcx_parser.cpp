#include "tcx_parser.h"
#include <pugixml.hpp>
#include <stdexcept>
#include <cmath>
#include <ctime>
#include <cstring>

static const uint32_t FIT_EPOCH_OFFSET = 631065600;

TcxParser::TcxParser(const std::string& filename) : filename_(filename) {}

uint32_t TcxParser::iso8601ToFitTimestamp(const std::string& isoTime) {
    if (isoTime.empty()) {
        return 0;
    }

    struct tm tm = {};
    const char* result = strptime(isoTime.c_str(), "%Y-%m-%dT%H:%M:%S", &tm);
    if (!result) {
        result = strptime(isoTime.c_str(), "%Y-%m-%d %H:%M:%S", &tm);
        if (!result) {
            return 0;
        }
    }

    time_t unixTime = timegm(&tm);
    if (unixTime < 0) {
        return 0;
    }

    if (static_cast<uint64_t>(unixTime) < FIT_EPOCH_OFFSET) {
        return 0;
    }

    return static_cast<uint32_t>(unixTime - FIT_EPOCH_OFFSET);
}

double TcxParser::haversineDistance(double lat1, double lon1, double lat2, double lon2) {
    const double earthRadius = 6371003.0;
    const double degToRad = M_PI / 180.0;

    double dLat = (lat2 - lat1) * degToRad;
    double dLon = (lon2 - lon1) * degToRad;

    double a = std::sin(dLat / 2) * std::sin(dLat / 2) +
               std::cos(lat1 * degToRad) * std::cos(lat2 * degToRad) *
               std::sin(dLon / 2) * std::sin(dLon / 2);

    double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));

    return earthRadius * c;
}

ParsedActivity TcxParser::parse() {
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(filename_.c_str());

    if (!result) {
        throw std::runtime_error("Failed to parse TCX file: " + std::string(result.description()));
    }

    ParsedActivity activity;

    // TCX root: <TrainingCenterDatabase>
    pugi::xml_node root = doc.child("TrainingCenterDatabase");
    if (!root) {
        throw std::runtime_error("Invalid TCX file: missing <TrainingCenterDatabase> root element");
    }

    pugi::xml_node activities = root.child("Activities");
    if (!activities) {
        // Try courses as fallback
        pugi::xml_node courses = root.child("Courses");
        if (!courses) {
            throw std::runtime_error("TCX file contains no Activities or Courses");
        }
        // Handle Courses (simplified structure)
        pugi::xml_node course = courses.child("Course");
        if (!course) {
            throw std::runtime_error("TCX file contains no Course elements");
        }
        pugi::xml_node nameNode = course.child("Name");
        if (nameNode) {
            activity.name = nameNode.child_value();
        }
        // Courses use <Track><Trackpoint> same as activities
        activities = courses;
    }

    double cumulativeDistance = 0.0;
    double totalAscent = 0.0;
    double totalDescent = 0.0;
    TrackPoint prevPoint;
    bool hasPrevPoint = false;

    // Iterate over activities (or courses)
    for (pugi::xml_node act = activities.first_child(); act; act = act.next_sibling()) {
        // Get activity name/sport
        if (activity.name.empty()) {
            pugi::xml_node nameNode = act.child("Name");
            if (nameNode) {
                activity.name = nameNode.child_value();
            }
        }

        // Iterate over laps (activities) or directly over tracks (courses)
        // Activities have: Activity > Lap > Track > Trackpoint
        // Courses have: Course > Track > Trackpoint
        for (pugi::xml_node container = act.first_child(); container; container = container.next_sibling()) {
            // Find Track elements (may be inside Lap or directly)
            auto processTrack = [&](pugi::xml_node track) {
                for (pugi::xml_node tp = track.child("Trackpoint"); tp; tp = tp.next_sibling("Trackpoint")) {
                    TrackPoint point;

                    // Time
                    pugi::xml_node timeNode = tp.child("Time");
                    if (timeNode) {
                        point.timestamp = iso8601ToFitTimestamp(timeNode.child_value());
                    }

                    // Position (lat/lon)
                    pugi::xml_node posNode = tp.child("Position");
                    if (posNode) {
                        pugi::xml_node latNode = posNode.child("LatitudeDegrees");
                        pugi::xml_node lonNode = posNode.child("LongitudeDegrees");
                        if (latNode && lonNode) {
                            point.lat = latNode.text().as_double(0.0);
                            point.lon = lonNode.text().as_double(0.0);
                        }
                    }

                    // Skip points without valid coordinates
                    if (point.lat == 0.0 && point.lon == 0.0) {
                        continue;
                    }

                    // Altitude
                    pugi::xml_node altNode = tp.child("AltitudeMeters");
                    if (altNode) {
                        point.elevation = altNode.text().as_double(0.0);
                        point.hasElevation = true;
                    }

                    // Distance (TCX provides cumulative distance)
                    pugi::xml_node distNode = tp.child("DistanceMeters");
                    if (distNode) {
                        point.distance = distNode.text().as_double(0.0);
                    }

                    // Heart rate
                    pugi::xml_node hrNode = tp.child("HeartRateBpm");
                    if (hrNode) {
                        pugi::xml_node hrValue = hrNode.child("Value");
                        if (hrValue) {
                            int hr = hrValue.text().as_int(0);
                            if (hr > 0 && hr <= 255) {
                                point.heart_rate = static_cast<uint8_t>(hr);
                                point.hasHeartRate = true;
                            }
                        }
                    }

                    // Cadence (directly in Trackpoint for bike activities)
                    pugi::xml_node cadNode = tp.child("Cadence");
                    if (cadNode) {
                        int cad = cadNode.text().as_int(0);
                        if (cad > 0 && cad <= 255) {
                            point.cadence = static_cast<uint8_t>(cad);
                            point.hasCadence = true;
                        }
                    }

                    // Speed from extensions
                    pugi::xml_node extensions = tp.child("Extensions");
                    if (extensions) {
                        // Garmin ActivityTrackpointExtension
                        pugi::xml_node tpx = extensions.child("TPX");
                        if (!tpx) tpx = extensions.child("ns3:TPX");
                        if (!tpx) tpx = extensions.child("ActivityTrackpointExtension");

                        if (tpx) {
                            pugi::xml_node spdNode = tpx.child("Speed");
                            if (!spdNode) spdNode = tpx.child("ns3:Speed");
                            if (spdNode) {
                                point.speed = spdNode.text().as_float(0.0f);
                                point.hasSpeed = true;
                            }

                            pugi::xml_node pwrNode = tpx.child("Watts");
                            if (!pwrNode) pwrNode = tpx.child("ns3:Watts");
                            if (pwrNode) {
                                int pwr = pwrNode.text().as_int(0);
                                if (pwr > 0 && pwr <= 65535) {
                                    point.power = static_cast<uint16_t>(pwr);
                                    point.hasPower = true;
                                }
                            }

                            // Cadence from extension (running cadence)
                            if (!point.hasCadence) {
                                pugi::xml_node runCadNode = tpx.child("RunCadence");
                                if (runCadNode) {
                                    int cad = runCadNode.text().as_int(0);
                                    if (cad > 0 && cad <= 255) {
                                        point.cadence = static_cast<uint8_t>(cad);
                                        point.hasCadence = true;
                                    }
                                }
                            }
                        }
                    }

                    // Calculate distance if TCX did not provide it
                    if (hasPrevPoint && point.distance == 0.0) {
                        double segDist = haversineDistance(prevPoint.lat, prevPoint.lon, point.lat, point.lon);
                        cumulativeDistance += segDist;
                        point.distance = cumulativeDistance;
                    } else if (point.distance > 0.0) {
                        cumulativeDistance = point.distance;
                    }

                    // Calculate ascent/descent
                    if (hasPrevPoint && point.hasElevation && prevPoint.hasElevation) {
                        double eleDiff = point.elevation - prevPoint.elevation;
                        if (eleDiff > 0) {
                            totalAscent += eleDiff;
                        } else {
                            totalDescent += (-eleDiff);
                        }
                    }

                    activity.points.push_back(point);
                    prevPoint = point;
                    hasPrevPoint = true;
                }
            };

            // Check if this node is a Track directly
            if (std::string(container.name()) == "Track") {
                processTrack(container);
            }
            // Check if this is a Lap containing Track(s)
            else if (std::string(container.name()) == "Lap") {
                for (pugi::xml_node track = container.child("Track"); track; track = track.next_sibling("Track")) {
                    processTrack(track);
                }
            }
        }
    }

    if (activity.points.empty()) {
        throw std::runtime_error("TCX file contains no valid track points");
    }

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
