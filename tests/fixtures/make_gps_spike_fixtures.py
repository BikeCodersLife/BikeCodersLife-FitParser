#!/usr/bin/env python3
"""Generate the synthetic GPS-spike fixtures for the v2.3.2 spike filter tests.

Deterministic: running it again rewrites byte-identical files. Every track
runs due north from Hoorn (52.64 N, 5.06 E) so a distance along the track is
a pure latitude offset (1 degree = 111195.08 m with the parser's Earth radius
6371003 m), and a sideways teleport is a pure longitude offset.

  spike-teleport.gpx        1 Hz, 30 km/h, ONE fix teleported 400 m east and
                            back (out-and-back). The de-spike must drop it,
                            and the distance must not include the detour.
  spike-teleport.tcx        The same track as TCX with the device's own
                            <DistanceMeters>: the fix is dropped, the device
                            distance is kept.
  spike-sparse-catchup.gpx  One fix every 10 s at 25 km/h; the position freezes
                            for two fixes, then catches up 362 m in 11 s
                            (119 km/h), like dev ride 1256.
  fast-descent.gpx          1 Hz: 30 km/h, +0.8 m/s^2 to 95 km/h, 10 s at
                            95 +- 1.5 km/h, -3 m/s^2 back to 30. A real descent:
                            its ~95 km/h must survive.
  globalsat-catchup.gpx     GlobalSat-style log (one fix every 2 s, track summary
                            <maxspeed> 9.0 m/s = 32.4 km/h) with a lag-then-
                            catch-up burst 40, 60, 72, 84, 84, 96, 171 km/h, then
                            24 km/h, like dev rides 1010 / 1117.
  globalsat-doubled-seconds.gpx  GlobalSat-style 1 Hz log at 27 km/h where some
                            stamps are a second late (5, 7, 7, 8, ... and
                            300, 302, 303, 303, ...), like dev ride 50.
  subsecond-shared-seconds.gpx  Road Grand Tours-style log at 27 km/h: 4 or 5
                            fixes inside every second, all with the same whole
                            second stamp (12, 12, 12, 12, 13, ...), like dev
                            ride 613.
  subsecond-fractional.gpx  5 Hz log at 27 km/h with millisecond stamps
                            (00.000, 00.200, 00.400, ...).
"""
import math
import os
from datetime import datetime, timedelta, timezone

M_PER_DEG_LAT = 2 * math.pi * 6371003.0 / 360.0
LAT0, LON0 = 52.64, 5.06
T0 = datetime(2025, 6, 1, 8, 0, 0, tzinfo=timezone.utc)
HERE = os.path.dirname(os.path.abspath(__file__))


def m_per_deg_lon(lat):
    return M_PER_DEG_LAT * math.cos(math.radians(lat))


def iso(sec):
    return (T0 + timedelta(seconds=sec)).strftime('%Y-%m-%dT%H:%M:%SZ')


def iso_ms(sec):
    return (T0 + timedelta(seconds=sec)).strftime('%Y-%m-%dT%H:%M:%S.%f')[:-3] + 'Z'


def track(segments):
    """segments: list of (dt_s, metres) steps north. Returns [(t, lat, lon, cum_m)]."""
    pts = [(0, LAT0, LON0, 0.0)]
    t, d = 0, 0.0
    for dt, m in segments:
        t += dt
        d += m
        pts.append((t, LAT0 + d / M_PER_DEG_LAT, LON0, d))
    return pts


def write_gpx(name, pts, creator='BikeCodersLife fixture generator', trk_ext='', stamp=iso):
    lines = ['<?xml version="1.0" encoding="UTF-8"?>',
             f'<gpx xmlns="http://www.topografix.com/GPX/1/1" version="1.1" creator="{creator}">',
             '  <trk>',
             f'    <name>{name}</name>']
    if trk_ext:
        lines.append(f'    <extensions>{trk_ext}</extensions>')
    lines.append('    <trkseg>')
    for t, lat, lon, _ in pts:
        lines.append(f'      <trkpt lat="{lat:.7f}" lon="{lon:.7f}"><ele>5</ele><time>{stamp(t)}</time></trkpt>')
    lines += ['    </trkseg>', '  </trk>', '</gpx>', '']
    with open(os.path.join(HERE, name), 'w') as f:
        f.write('\n'.join(lines))


def write_tcx(name, pts):
    lines = ['<?xml version="1.0" encoding="UTF-8"?>',
             '<TrainingCenterDatabase xmlns="http://www.garmin.com/xmlschemas/TrainingCenterDatabase/v2">',
             '  <Activities>',
             '    <Activity Sport="Biking">',
             f'      <Id>{iso(0)}</Id>',
             f'      <Lap StartTime="{iso(0)}">',
             '        <Track>']
    for t, lat, lon, cum in pts:
        lines.append(f'          <Trackpoint><Time>{iso(t)}</Time><Position><LatitudeDegrees>{lat:.7f}</LatitudeDegrees>'
                     f'<LongitudeDegrees>{lon:.7f}</LongitudeDegrees></Position><AltitudeMeters>5</AltitudeMeters>'
                     f'<DistanceMeters>{cum:.1f}</DistanceMeters></Trackpoint>')
    lines += ['        </Track>', '      </Lap>', '    </Activity>', '  </Activities>', '</TrainingCenterDatabase>', '']
    with open(os.path.join(HERE, name), 'w') as f:
        f.write('\n'.join(lines))


def kmh(v):
    return v / 3.6


# 1. One-fix teleport: 600 s at 30 km/h, fix 300 moved 400 m east (out and back).
pts = track([(1, kmh(30))] * 600)
t, lat, lon, cum = pts[300]
pts[300] = (t, lat, lon + 400.0 / m_per_deg_lon(lat), cum)
write_gpx('spike-teleport.gpx', pts)
# TCX twin: the device's cumulative distance follows the road, not the glitch.
write_tcx('spike-teleport.tcx', pts)

# 2. Sparse log with a GPS freeze and a catch-up: 10 s fixes at 25 km/h.
steps = [(10, kmh(25) * 10)] * 40
steps += [(23, 9.0), (11, 15.0), (11, 362.0)]   # freeze (near-zero), then the catch-up
steps += [(10, kmh(25) * 10)] * 40
write_gpx('spike-sparse-catchup.gpx', track(steps))

# 3. A real fast descent at 1 Hz.
steps = [(1, kmh(30))] * 60
v = kmh(30)
while v < kmh(95):
    v = min(kmh(95), v + 0.8)
    steps.append((1, v))
noise = [0.0, 1.5, -1.0, 0.5, -1.5, 1.0, -0.5, 1.5, -1.0, 0.0]
steps += [(1, kmh(95 + n)) for n in noise]
v = kmh(95)
while v > kmh(30):
    v = max(kmh(30), v - 3.0)
    steps.append((1, v))
steps += [(1, kmh(30))] * 60
write_gpx('fast-descent.gpx', track(steps))

# 4. GlobalSat lag-then-catch-up burst, device max 9.0 m/s in the track summary.
cruise = [(2, kmh(30) * 2)] * 60
burst = [(2, kmh(v) * 2) for v in (40, 60, 72, 84, 84, 96, 171)] + [(2, kmh(24) * 2)] * 5
write_gpx('globalsat-catchup.gpx', track(cruise + burst + cruise),
          creator='GlobalSat GS-Sport PC Software', trk_ext='<maxspeed>9.000000</maxspeed>')

# 5. GlobalSat doubled seconds (dev ride 50, a 2014 GS-Sport log): a fix every
#    second at 27 km/h, but some stamps are written a second late. First half:
#    one in three, so the log reads ..., 5, 7, 7, 8, 10, 10, ... (a skipped
#    second, then the same second twice). Second half: two late in a row, so
#    the free second is two fixes back: ..., 300, 302, 303, 303, 304, ...
pts = []
for i in range(600):
    late = i % 3 == 1 if i < 300 else i % 5 in (1, 2)
    written = i + 1 if late else i
    pts.append((written, LAT0 + i * kmh(27) / M_PER_DEG_LAT, LON0, i * kmh(27)))
write_gpx('globalsat-doubled-seconds.gpx', pts, creator='GlobalSat GS-Sport PC Software')

# 6. Sub-second log with whole-second stamps (dev ride 613, a Road Grand Tours
#    export): 4 or 5 fixes inside every second at 27 km/h, and every fix of a
#    second carries that same whole second (12, 12, 12, 12, 13, 13, ...).
pts = []
for sec in range(600):
    n = 4 if sec % 5 == 0 else 5
    for k in range(n):
        t = sec + k / n
        pts.append((sec, LAT0 + t * kmh(27) / M_PER_DEG_LAT, LON0, t * kmh(27)))
write_gpx('subsecond-shared-seconds.gpx', pts, creator='Road Grand Tours - https://www.roadgrandtours.com')

# 7. 5 Hz log with millisecond stamps: 00.000, 00.200, 00.400, ... at 27 km/h.
pts = [(i / 5, LAT0 + (i / 5) * kmh(27) / M_PER_DEG_LAT, LON0, (i / 5) * kmh(27)) for i in range(3000)]
write_gpx('subsecond-fractional.gpx', pts, stamp=iso_ms)
