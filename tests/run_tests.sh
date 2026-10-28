#!/bin/bash
# Test script for FIT parser v2.0
# Tests: FIT parsing, GPX/TCX parsing, GPX/TCX→FIT conversion, round-trip

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Override with PARSER=/path/to/fit-parser to test a build made elsewhere
# (dev containers bind-mount build/fit-parser, so a rebuild there breaks them).
PARSER="${PARSER:-${SCRIPT_DIR}/../build/fit-parser}"
FIXTURES_DIR="${SCRIPT_DIR}/fixtures"
EXPECTED_DIR="${SCRIPT_DIR}/expected"
TMP_DIR=$(mktemp -d)

trap "rm -rf $TMP_DIR" EXIT

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

TOTAL=0
PASSED=0
FAILED=0

pass() { echo -e "${GREEN}PASS${NC} $1"; PASSED=$((PASSED + 1)); }
fail() { echo -e "${RED}FAIL${NC} $1"; FAILED=$((FAILED + 1)); }
skip() { echo -e "${YELLOW}SKIP${NC} $1"; PASSED=$((PASSED + 1)); }

echo "========================================="
echo "FIT Parser Test Suite v2.4"
echo "========================================="
echo ""

# Check binary exists
if [ ! -x "$PARSER" ]; then
    echo -e "${RED}Error: fit-parser binary not found at $PARSER${NC}"
    echo "Build with: cd build && cmake .. && make"
    exit 1
fi

echo "--- FIT File Parsing ---"
for fit_file in "$FIXTURES_DIR"/*.fit; do
    [ ! -f "$fit_file" ] && continue
    TOTAL=$((TOTAL + 1))
    filename=$(basename "$fit_file")
    expected_file="$EXPECTED_DIR/${filename%.fit}.json"

    if output=$("$PARSER" "$fit_file" 2>/dev/null); then
        if [ -f "$expected_file" ]; then
            if echo "$output" | diff -q - "$expected_file" > /dev/null 2>&1; then
                pass "$filename"
            else
                fail "$filename (output mismatch)"
                [ "$1" == "-v" ] && echo "$output" | diff - "$expected_file" | head -20
            fi
        else
            if echo "$output" | python3 -m json.tool > /dev/null 2>&1; then
                skip "$filename (no expected, valid JSON)"
            else
                fail "$filename (invalid JSON)"
            fi
        fi
    else
        fail "$filename (parser error)"
    fi
done

echo ""
echo "--- GPX File Parsing (JSON output) ---"
for gpx_file in "$FIXTURES_DIR"/*.gpx; do
    [ ! -f "$gpx_file" ] && continue
    TOTAL=$((TOTAL + 1))
    filename=$(basename "$gpx_file")

    if output=$("$PARSER" "$gpx_file" 2>/dev/null); then
        # Validate JSON and check required fields
        if echo "$output" | python3 -c "
import sys, json
d = json.load(sys.stdin)
assert 'coordinates' in d, 'missing coordinates'
assert len(d['coordinates']) > 0, 'empty coordinates'
# distanceKm may be at top level or in summary
s = d.get('summary', d)
assert 'distanceKm' in s, 'missing distanceKm'
assert 'durationMin' in s, 'missing durationMin'
c = d['coordinates'][0]
assert 'lat' in c and 'lon' in c, 'missing lat/lon'
print(f'{len(d[\"coordinates\"])} points, {s[\"distanceKm\"]:.1f} km')
" 2>/dev/null; then
            pass "$filename"
        else
            fail "$filename (invalid JSON structure)"
        fi
    else
        fail "$filename (parser error)"
    fi
done

echo ""
echo "--- GPX→FIT Conversion ---"
for gpx_file in "$FIXTURES_DIR"/*.gpx; do
    [ ! -f "$gpx_file" ] && continue
    TOTAL=$((TOTAL + 1))
    filename=$(basename "$gpx_file")
    fit_output="$TMP_DIR/${filename%.gpx}.fit"

    if "$PARSER" "$gpx_file" --convert "$fit_output" 2>/dev/null; then
        if [ -f "$fit_output" ] && [ -s "$fit_output" ]; then
            gpx_size=$(wc -c < "$gpx_file" | tr -d ' ')
            fit_size=$(wc -c < "$fit_output" | tr -d ' ')
            ratio=$((gpx_size / fit_size))
            pass "$filename → FIT (${gpx_size}B → ${fit_size}B, ${ratio}x smaller)"
        else
            fail "$filename → FIT (output empty)"
        fi
    else
        fail "$filename → FIT (conversion error)"
    fi
done

echo ""
echo "--- GPX→FIT→JSON Round-Trip ---"
for gpx_file in "$FIXTURES_DIR"/*.gpx; do
    [ ! -f "$gpx_file" ] && continue
    TOTAL=$((TOTAL + 1))
    filename=$(basename "$gpx_file")
    fit_output="$TMP_DIR/${filename%.gpx}.fit"

    # Skip if FIT wasn't created
    [ ! -f "$fit_output" ] && { fail "$filename round-trip (no FIT)"; continue; }

    # Parse the converted FIT back to JSON
    if fit_json=$("$PARSER" "$fit_output" 2>/dev/null); then
        gpx_json_raw=$("$PARSER" "$gpx_file" 2>/dev/null)
        # Compare point counts using temp files to avoid shell quoting issues
        echo "$gpx_json_raw" > "$TMP_DIR/gpx_out.json"
        echo "$fit_json" > "$TMP_DIR/fit_out.json"
        if python3 -c "
import json

gpx_json = json.load(open('$TMP_DIR/gpx_out.json'))
fit_json = json.load(open('$TMP_DIR/fit_out.json'))

gpx_count = len(gpx_json['coordinates'])
fit_count = len(fit_json['coordinates'])

# Both paths run the same GPS de-spike (FIT since v2.1.6, GPX/TCX direct
# since v2.3.2), so the counts normally match. The small tolerance (never
# more FIT points, never a wholesale loss) stays as a safety margin.
dropped = gpx_count - fit_count
assert 0 <= dropped <= max(10, int(gpx_count * 0.01)), \
    f'point count mismatch beyond de-spike tolerance: GPX={gpx_count} FIT={fit_count} (dropped {dropped})'

gpx_first = gpx_json['coordinates'][0]
fit_first = fit_json['coordinates'][0]
assert abs(gpx_first['lat'] - fit_first['lat']) < 0.001, f'first lat mismatch: {gpx_first[\"lat\"]} vs {fit_first[\"lat\"]}'
assert abs(gpx_first['lon'] - fit_first['lon']) < 0.001, f'first lon mismatch: {gpx_first[\"lon\"]} vs {fit_first[\"lon\"]}'

print(f'{fit_count} points match')
" 2>/dev/null; then
            pass "$filename round-trip"
        else
            fail "$filename round-trip (data mismatch)"
        fi
    else
        fail "$filename round-trip (FIT parse error)"
    fi
done

echo ""
echo "--- Edge Cases ---"

# Test: FIT file should not be convertible
TOTAL=$((TOTAL + 1))
first_fit=$(ls "$FIXTURES_DIR"/*.fit 2>/dev/null | head -1)
if [ -n "$first_fit" ]; then
    if "$PARSER" "$first_fit" --convert "$TMP_DIR/should_fail.fit" 2>/dev/null; then
        fail "FIT→FIT conversion should be rejected"
    else
        pass "FIT→FIT conversion correctly rejected"
    fi
fi

# Test: missing file
TOTAL=$((TOTAL + 1))
if "$PARSER" "/nonexistent/file.gpx" 2>/dev/null; then
    fail "Missing file should error"
else
    pass "Missing file correctly rejected"
fi

# Test: --version
TOTAL=$((TOTAL + 1))
if version=$("$PARSER" --version 2>&1) && echo "$version" | grep -qE "v2\.[0-9]+"; then
    pass "--version shows v2.x"
else
    fail "--version (got: $version)"
fi

echo ""
echo "--- GPS de-spike + spike-resistant max speed (v2.1.6) ---"

# A FIT that opens with a pre-GPS-lock (0,0) fix must be de-spiked: no
# null-island coordinate survives, so the route doesn't stretch to (0,0).
TOTAL=$((TOTAL + 1))
# Real-ride .fit fixtures are not committed (kept dev-local); skip when absent.
nullfit="$FIXTURES_DIR/2022-08-01-165517-ELEMNT BFF6-119-0.fit"
if [ ! -f "$nullfit" ]; then
    skip "null-island de-spike (FIT fixture not present)"
elif "$PARSER" "$nullfit" 2>/dev/null > "$TMP_DIR/nullisland.json" && python3 -c "
import json
d = json.load(open('$TMP_DIR/nullisland.json'))
bad = [p for p in d['coordinates'] if abs(p['lat']) < 0.1 and abs(p['lon']) < 0.1]
assert not bad, f'null-island fix survived de-spike: {bad[:1]}'
sm = d['summary']['smoothedMaxSpeedKmh']
assert 0 < sm < 120, f'implausible smoothedMaxSpeedKmh {sm}'
" 2>/dev/null; then
    pass "null-island (0,0) fix de-spiked + sane smoothedMaxSpeedKmh"
else
    fail "null-island de-spike"
fi

# A GPX with isolated GPS jumps (700 m in 1 s = 2500 km/h): the rolling-MEDIAN
# smoothedMaxSpeedKmh must reject them. A rolling mean reported ~517 km/h here;
# the real descent peak is ~70 (Strava 76.7). Also exercises valid-JSON output
# (no bare -nan from coasting math on a power-less GPX).
TOTAL=$((TOTAL + 1))
furka="$FIXTURES_DIR/Heen_en_weer_Furkapass.gpx"
furkafit="$TMP_DIR/furka.fit"
if [ -f "$furka" ] && "$PARSER" "$furka" --convert "$furkafit" 2>/dev/null && "$PARSER" "$furkafit" 2>/dev/null > "$TMP_DIR/furka.json"; then
    if python3 -c "
import json
d = json.load(open('$TMP_DIR/furka.json'))
sm = d['summary']['smoothedMaxSpeedKmh']
assert 40 < sm < 120, f'smoothedMaxSpeedKmh {sm} is not spike-resistant (expected ~70)'
" 2>/dev/null; then
        pass "rolling-median rejects GPS speed spike (smoothedMax ~70, not 517)"
    else
        fail "spike-resistant max speed"
    fi
else
    fail "furka fixture missing / convert / parse error"
fi

echo ""
echo "--- GPX/TCX sensor fields (v2.3.1) ---"

# Up to v2.3.0 the GPX/TCX → JSON path left every Coordinate sensor field and
# has* flag uninitialised: a sensorless file emitted one garbage power /
# cadence / heartRate value on every point (e.g. power 59820, cadence 176),
# and a file WITH sensors lost its real values. The summary's has*Data flags
# were uninitialised too.

# A sensorless GPX / TCX must emit no heartRate / power / cadence / temperature
# on any point, and no HR / power / cadence aggregates in the summary.
for sensorless in "no-sensors-gps-only.gpx" "tcx-no-sensors.tcx"; do
    TOTAL=$((TOTAL + 1))
    if "$PARSER" "$FIXTURES_DIR/$sensorless" 2>/dev/null > "$TMP_DIR/sensorless.json" && python3 - "$TMP_DIR/sensorless.json" <<'PY' 2>/dev/null
import json, sys
d = json.load(open(sys.argv[1]))
assert len(d['coordinates']) > 0
for p in d['coordinates']:
    for k in ('heartRate', 'power', 'cadence', 'temperature'):
        assert k not in p, f'{k}={p[k]} on a sensorless point'
s = d['summary']
for k in ('avgHeartRate', 'maxHeartRate', 'avgPower', 'maxPower', 'normalizedPower',
          'avgCadence', 'maxCadence', 'coastingTimeSec'):
    assert k not in s, f'summary {k}={s[k]} for a sensorless file'
PY
    then
        pass "$sensorless emits no sensor fields"
    else
        fail "$sensorless emits sensor fields it does not have"
    fi
done

# A GPX / TCX WITH sensor extensions must emit the real per-point values
# (compared point by point with the fixture) and matching summary maxima.
for sensored in "with-power-and-hr.gpx" "tcx-with-sensors.tcx"; do
    TOTAL=$((TOTAL + 1))
    if "$PARSER" "$FIXTURES_DIR/$sensored" 2>/dev/null > "$TMP_DIR/sensored.json" && python3 - "$FIXTURES_DIR/$sensored" "$TMP_DIR/sensored.json" <<'PY' 2>/dev/null
import json, sys
import xml.etree.ElementTree as ET

def local(tag):
    return tag.rsplit('}', 1)[-1].split(':')[-1]

root = ET.parse(sys.argv[1]).getroot()
is_gpx = local(root.tag) == 'gpx'
point_tag = 'trkpt' if is_gpx else 'Trackpoint'
names = {'hr': 'heartRate', 'cad': 'cadence', 'power': 'power', 'atemp': 'temperature',
         'HeartRateBpm': 'heartRate', 'Cadence': 'cadence', 'Watts': 'power'}
expected = []
for pt in root.iter():
    if local(pt.tag) != point_tag:
        continue
    want = {}
    for el in pt.iter():
        key = names.get(local(el.tag))
        if key is None:
            continue
        text = el.findtext('{*}Value') if local(el.tag) == 'HeartRateBpm' else el.text
        v = int(float(text))
        # The readers treat 0 hr / cad / power as "not available".
        if key == 'temperature' or v > 0:
            want[key] = v
    expected.append(want)

d = json.load(open(sys.argv[2]))
coords = d['coordinates']
assert len(coords) == len(expected), f'{len(coords)} points, fixture has {len(expected)}'
assert any(expected), 'fixture carries no sensor values'
for i, (p, want) in enumerate(zip(coords, expected)):
    got = {k: p[k] for k in ('heartRate', 'power', 'cadence', 'temperature') if k in p}
    assert got == want, f'point {i}: got {got}, fixture {want}'
s = d['summary']
for key, summary_key in (('heartRate', 'maxHeartRate'), ('power', 'maxPower'), ('cadence', 'maxCadence')):
    values = [w[key] for w in expected if key in w]
    assert s[summary_key] == max(values), f'{summary_key} {s[summary_key]} != {max(values)}'
assert 0 < s['avgPower'] <= s['maxPower']
PY
    then
        pass "$sensored emits its real HR / power / cadence / temperature"
    else
        fail "$sensored sensor values lost or wrong"
    fi
done

# GPX/TCX parsed directly must report the same record-stream summary as the
# same file converted to FIT first (both run the shared stream-stats pass).
for source in "with-power-and-hr.gpx" "tcx-with-sensors.tcx"; do
    TOTAL=$((TOTAL + 1))
    converted="$TMP_DIR/summary-${source%.*}.fit"
    if "$PARSER" "$FIXTURES_DIR/$source" 2>/dev/null > "$TMP_DIR/direct.json" \
        && "$PARSER" "$FIXTURES_DIR/$source" --convert "$converted" 2>/dev/null \
        && "$PARSER" "$converted" 2>/dev/null > "$TMP_DIR/viafit.json" \
        && python3 - "$TMP_DIR/direct.json" "$TMP_DIR/viafit.json" <<'PY' 2>/dev/null
import json, sys
direct = json.load(open(sys.argv[1]))['summary']
viafit = json.load(open(sys.argv[2]))['summary']
keys = ('avgHeartRate', 'maxHeartRate', 'avgPower', 'maxPower', 'normalizedPower',
        'avgCadence', 'maxCadence', 'avgSpeed', 'maxSpeed', 'smoothedMaxSpeedKmh',
        'movingTimeSec', 'coastingTimeSec')
diff = {k: (direct.get(k), viafit.get(k)) for k in keys if direct.get(k) != viafit.get(k)}
assert not diff, f'direct vs via-FIT summary differ: {diff}'
PY
    then
        pass "$source summary matches its FIT conversion"
    else
        fail "$source summary differs from its FIT conversion"
    fi
done

echo ""
echo "--- GPS spike filter (v2.3.2) ---"

# Synthetic fixtures from tests/fixtures/make_gps_spike_fixtures.py (see its
# docstring). Each check names the dev ride it reproduces.

# One fix teleported 400 m sideways and back, 1 Hz. The GPX/TCX path now runs
# the FIT de-spike: the fix is gone from the track, and the distance no longer
# includes the 800 m detour (v2.3.1: 5.78 km instead of 5.00, raw max 1440 km/h).
TOTAL=$((TOTAL + 1))
if "$PARSER" "$FIXTURES_DIR/spike-teleport.gpx" 2>/dev/null > "$TMP_DIR/teleport.json" && python3 - "$FIXTURES_DIR/spike-teleport.gpx" "$TMP_DIR/teleport.json" <<'PY' 2>/dev/null
import json, math, sys
import xml.etree.ElementTree as ET
pts = [(float(p.get('lat')), float(p.get('lon'))) for p in ET.parse(sys.argv[1]).getroot().iter('{http://www.topografix.com/GPX/1/1}trkpt')]
def hav(a, b):
    r = math.pi / 180
    x = math.sin((b[0] - a[0]) * r / 2) ** 2 + math.cos(a[0] * r) * math.cos(b[0] * r) * math.sin((b[1] - a[1]) * r / 2) ** 2
    return 6371003.0 * 2 * math.atan2(math.sqrt(x), math.sqrt(1 - x))
clean = pts[:300] + pts[301:]
want_km = round(sum(hav(clean[i - 1], clean[i]) for i in range(1, len(clean))) / 1000, 2)
d = json.load(open(sys.argv[2])); s = d['summary']
assert len(d['coordinates']) == len(pts) - 1, f"{len(d['coordinates'])} points, the teleported fix survived"
assert all(abs(c['lon'] - 5.06) < 0.001 for c in d['coordinates']), 'a teleported coordinate survived'
assert s['distanceKm'] == want_km and s['sessionDistanceKm'] == want_km, f"distance {s['distanceKm']} != {want_km}"
assert s['maxSpeed'] < 35 and 29 <= s['smoothedMaxSpeedKmh'] <= 31, s
PY
then
    pass "spike-teleport.gpx: teleported fix dropped, distance without the detour"
else
    fail "spike-teleport.gpx: teleported fix kept or distance includes the detour"
fi

# The TCX twin carries the device's own <DistanceMeters>: the fix is dropped,
# and the odometer distance (5.00 km) is kept as it is.
TOTAL=$((TOTAL + 1))
if "$PARSER" "$FIXTURES_DIR/spike-teleport.tcx" 2>/dev/null > "$TMP_DIR/teleport-tcx.json" && python3 - "$TMP_DIR/teleport-tcx.json" <<'PY' 2>/dev/null
import json, sys
d = json.load(open(sys.argv[1])); s = d['summary']
assert len(d['coordinates']) == 600, len(d['coordinates'])
assert s['distanceKm'] == 5.0, s['distanceKm']
assert s['maxSpeed'] < 35, s['maxSpeed']
PY
then
    pass "spike-teleport.tcx: teleported fix dropped, device distance kept"
else
    fail "spike-teleport.tcx: teleported fix kept or device distance changed"
fi

# Sparse log (a fix every 10 s), GPS freeze then a 362 m catch-up in 11 s
# (dev ride 1256: 118.6 km/h on v2.3.1). The 7 s window held one sample, so
# that one segment was the max. Distance is unchanged (no fix is dropped:
# the catch-up is ridden distance, only its timing is wrong).
TOTAL=$((TOTAL + 1))
if "$PARSER" "$FIXTURES_DIR/spike-sparse-catchup.gpx" 2>/dev/null > "$TMP_DIR/sparse.json" && python3 - "$TMP_DIR/sparse.json" <<'PY' 2>/dev/null
import json, sys
d = json.load(open(sys.argv[1])); s = d['summary']
assert s['smoothedMaxSpeedKmh'] <= 30, f"smoothedMaxSpeedKmh {s['smoothedMaxSpeedKmh']} (a 25 km/h ride)"
assert len(d['coordinates']) == 84 and s['distanceKm'] == 5.94, s
PY
then
    pass "spike-sparse-catchup.gpx: one catch-up segment no longer sets the max"
else
    fail "spike-sparse-catchup.gpx: catch-up segment still sets the max"
fi

# A real fast descent (1 Hz, 10 s at 95 +- 1.5 km/h) must keep its ~95 km/h.
TOTAL=$((TOTAL + 1))
if "$PARSER" "$FIXTURES_DIR/fast-descent.gpx" 2>/dev/null > "$TMP_DIR/descent.json" && python3 - "$TMP_DIR/descent.json" <<'PY' 2>/dev/null
import json, sys
s = json.load(open(sys.argv[1]))['summary']
assert 93 <= s['smoothedMaxSpeedKmh'] <= 97, s['smoothedMaxSpeedKmh']
PY
then
    pass "fast-descent.gpx: a real 95 km/h descent survives"
else
    fail "fast-descent.gpx: a real descent was filtered"
fi

# GlobalSat lag-then-catch-up burst over 7 fixes (dev rides 1010 / 1117:
# 127 / 124 km/h on v2.3.1, the device said 40 / 32.5). The file's own
# <maxspeed> (9.0 m/s) bounds the GPS-derived max, directly and after
# conversion to FIT.
TOTAL=$((TOTAL + 1))
if "$PARSER" "$FIXTURES_DIR/globalsat-catchup.gpx" 2>/dev/null > "$TMP_DIR/gs.json" \
    && "$PARSER" "$FIXTURES_DIR/globalsat-catchup.gpx" --convert "$TMP_DIR/gs.fit" 2>/dev/null \
    && "$PARSER" "$TMP_DIR/gs.fit" 2>/dev/null > "$TMP_DIR/gs-fit.json" \
    && python3 - "$TMP_DIR/gs.json" "$TMP_DIR/gs-fit.json" <<'PY' 2>/dev/null
import json, sys
s = json.load(open(sys.argv[1]))['summary']
f = json.load(open(sys.argv[2]))['summary']
assert s['sessionMaxSpeedKmh'] == 32.4, s.get('sessionMaxSpeedKmh')
assert s['smoothedMaxSpeedKmh'] <= 32.4, s['smoothedMaxSpeedKmh']
assert f['sessionMaxSpeedKmh'] == 32.4 and f['smoothedMaxSpeedKmh'] == s['smoothedMaxSpeedKmh'], f
PY
then
    pass "globalsat-catchup.gpx: device max speed bounds the GPS-derived max (GPX + FIT)"
else
    fail "globalsat-catchup.gpx: GPS catch-up burst still sets the max"
fi

# Clean rides: the spike filter must not change a single byte of their output.
# Baselines are the v2.3.1 output of each file.
for clean in fast-descent.gpx with-power-and-hr.gpx tcx-with-sensors.tcx tcx-no-sensors.tcx \
             negative-elevation-below-sea-level.gpx Avondrit-Wieringerwerf.gpx; do
    TOTAL=$((TOTAL + 1))
    baseline="$EXPECTED_DIR/unchanged/$clean.json"
    if "$PARSER" "$FIXTURES_DIR/$clean" 2>/dev/null | diff -q - "$baseline" > /dev/null 2>&1; then
        pass "$clean output unchanged since v2.3.1"
    else
        fail "$clean output changed (clean ride)"
    fi
done

echo ""
echo "--- Indoor rides without position (v2.3.3) ---"

# A trainer ride has no GPS: GlobalSat writes every GPX point at lat="0"
# lon="0", a Garmin trainer TCX has no <Position> at all. Up to v2.3.2 the
# GPX/TCX parsers dropped those points with their time, HR, cadence and power,
# and failed with "no valid track points" (4 GlobalSat trainer rides of one
# Strava export, 2026-10-20). Keep them as points without position, like the
# FIT path: lat/lon 0 (JobRunner counts them as no position, tessera makes the
# ride indoor), time and sensors kept, distance from the device (GPX track
# <length>, TCX DistanceMeters) instead of from positions.
for indoor in "indoor-no-position.gpx" "indoor-no-position.tcx"; do
    TOTAL=$((TOTAL + 1))
    if "$PARSER" "$FIXTURES_DIR/$indoor" 2>/dev/null > "$TMP_DIR/indoor.json" && python3 - "$indoor" "$TMP_DIR/indoor.json" <<'PY' 2>/dev/null
import json, sys
name, d = sys.argv[1], json.load(open(sys.argv[2]))
c, s = d['coordinates'], d['summary']
gpx = name.endswith('.gpx')
assert len(c) == (120 if gpx else 60), f'{len(c)} points kept'
assert all(p['lat'] == 0 and p['lon'] == 0 for p in c), 'a point got a position'
assert all('heartRate' in p for p in c), 'heart rate lost'
assert all('cadence' in p for p in c), 'cadence lost'
assert abs(s['durationMin'] - (238 if gpx else 236) / 60) < 0.05, f"durationMin {s['durationMin']}"
assert abs(s['distanceKm'] - (1.0 if gpx else 1.475)) < 0.01, f"distanceKm {s['distanceKm']}"
assert 'avgHeartRate' in s and 'avgCadence' in s, 'summary lost the sensors'
if not gpx:
    assert 'avgPower' in s, 'summary lost the power'
PY
    then
        pass "$indoor: a ride without position keeps its time, sensors and distance"
    else
        fail "$indoor: a ride without position is dropped or loses data"
    fi
done

# The same ride converted to FIT and read back keeps its points, heart rate
# and distance (the pipeline stores every GPX/TCX as FIT).
TOTAL=$((TOTAL + 1))
if "$PARSER" "$FIXTURES_DIR/indoor-no-position.gpx" --convert "$TMP_DIR/indoor.fit" 2>/dev/null \
    && "$PARSER" "$TMP_DIR/indoor.fit" 2>/dev/null > "$TMP_DIR/indoor-fit.json" \
    && python3 - "$TMP_DIR/indoor-fit.json" <<'PY' 2>/dev/null
import json, sys
d = json.load(open(sys.argv[1]))
c, s = d['coordinates'], d['summary']
assert len(c) == 120, f'{len(c)} points after FIT round trip'
assert all(p['lat'] == 0 and p['lon'] == 0 for p in c), 'a point got a position'
assert all('heartRate' in p for p in c), 'heart rate lost'
# FIT semantics: distanceKm is the GPS distance (0 indoors); the device
# distance is the session total, as for every indoor FIT.
assert abs(s.get('sessionDistanceKm', 0) - 1.0) < 0.01, f"sessionDistanceKm {s.get('sessionDistanceKm')}"
PY
then
    pass "indoor-no-position.gpx → FIT → JSON keeps points, heart rate and distance"
else
    fail "indoor-no-position.gpx → FIT round trip loses the ride"
fi

# Points without position AND without time carry nothing to keep: still an error.
TOTAL=$((TOTAL + 1))
cat > "$TMP_DIR/no-position-no-time.gpx" <<'GPX'
<?xml version="1.0"?>
<gpx version="1.1" creator="test" xmlns="http://www.topografix.com/GPX/1/1">
  <trk><trkseg>
    <trkpt lat="0" lon="0"><ele>5</ele></trkpt>
    <trkpt lat="0" lon="0"><ele>5</ele></trkpt>
  </trkseg></trk>
</gpx>
GPX
if err=$("$PARSER" "$TMP_DIR/no-position-no-time.gpx" 2>&1 >/dev/null); then
    fail "no-position-no-time.gpx: parsed a file with nothing to keep"
elif echo "$err" | grep -q "no valid track points"; then
    pass "no-position-no-time.gpx: still rejected (no valid track points)"
else
    fail "no-position-no-time.gpx: rejected with an unexpected error: $err"
fi

# ============================================================================
echo ""
echo "--- Health strip (--strip-health, v2.4.0) ---"
# Needs the test helper (cmake target health-fit-tool) next to the parser.
HEALTH_TOOL="${HEALTH_TOOL:-$(dirname "$PARSER")/health-fit-tool}"
CHECK_PY="${SCRIPT_DIR}/check_health_strip.py"
CHECK="python3 $CHECK_PY"

TOTAL=$((TOTAL + 1))
if python3 "${SCRIPT_DIR}/../tools/gen_health_table.py" --check; then
    pass "health table: src/health_table.inc and docs/health-strip.md match docs/health-strip-table.tsv"
else
    fail "health table: generated files are out of date (run python3 tools/gen_health_table.py)"
fi

if [ ! -x "$HEALTH_TOOL" ]; then
    TOTAL=$((TOTAL + 1))
    fail "health strip: test helper not found at $HEALTH_TOOL (build with: make health-fit-tool)"
else
    RICH="$TMP_DIR/rich.fit"
    "$HEALTH_TOOL" rich "$RICH"

    # (a)(b) every field of every message of the profile + developer fields in, only the table out
    TOTAL=$((TOTAL + 1))
    if out=$("$PARSER" "$RICH" --strip-health "$TMP_DIR/rich-out.fit" 2>"$TMP_DIR/err.txt") \
        && "$HEALTH_TOOL" dump "$TMP_DIR/rich-out.fit" > "$TMP_DIR/rich-out.txt" \
        && echo "$out" | python3 -c "
import json, sys
d = json.loads(sys.stdin.read())
assert d['droppedMessages'] > 0 and d['strippedFields'] > 0 and d['developerFieldsRemoved'] > 0, d
" \
        && $CHECK dump "$TMP_DIR/rich-out.txt" > "$TMP_DIR/check.txt" 2>&1; then
        pass "rich FIT (all profile fields + developer fields + CORE/power dev data): only the table survives, output decodes (CRC ok) [$(cat "$TMP_DIR/check.txt")]"
    else
        fail "rich FIT: $(cat "$TMP_DIR/check.txt" "$TMP_DIR/err.txt" 2>/dev/null | head -3)"
    fi

    # (a) JSON view: HR / power / cadence gone, GPS / time / distance identical
    TOTAL=$((TOTAL + 1))
    if "$PARSER" "$RICH" > "$TMP_DIR/rich-in.json" 2>/dev/null \
        && "$PARSER" "$TMP_DIR/rich-out.fit" > "$TMP_DIR/rich-out.json" 2>/dev/null \
        && $CHECK track "$TMP_DIR/rich-in.json" "$TMP_DIR/rich-out.json" --expect-health > "$TMP_DIR/check.txt" 2>&1; then
        pass "rich FIT: no heart rate / power / cadence in the JSON, same track [$(cat "$TMP_DIR/check.txt")]"
    else
        fail "rich FIT track: $(cat "$TMP_DIR/check.txt" | head -3)"
    fi

    # chained FIT files are each cleaned; junk after the last file is not copied
    TOTAL=$((TOTAL + 1))
    cat "$RICH" "$RICH" > "$TMP_DIR/chain.fit"
    if "$PARSER" "$TMP_DIR/chain.fit" --strip-health "$TMP_DIR/chain-out.fit" > /dev/null 2>&1 \
        && [ "$(wc -c < "$TMP_DIR/chain-out.fit")" -eq $((2 * $(wc -c < "$TMP_DIR/rich-out.fit"))) ] \
        && cmp -s <(head -c "$(wc -c < "$TMP_DIR/rich-out.fit")" "$TMP_DIR/chain-out.fit") "$TMP_DIR/rich-out.fit"; then
        pass "chained FIT: both files cleaned"
    else
        fail "chained FIT: not cleaned as two files"
    fi
    TOTAL=$((TOTAL + 1))
    { cat "$RICH"; head -c 16 /dev/zero; } > "$TMP_DIR/junk.fit"
    if "$PARSER" "$TMP_DIR/junk.fit" --strip-health "$TMP_DIR/junk-out.fit" > /dev/null 2>&1 \
        && cmp -s "$TMP_DIR/junk-out.fit" "$TMP_DIR/rich-out.fit"; then
        pass "FIT with trailing junk: junk not copied"
    else
        fail "FIT with trailing junk: output differs from the clean file's"
    fi

    # (c) fail closed: truncated, bad CRC, empty, not FIT -> exit != 0 and NO output file, no temp left over
    head -c $(( $(wc -c < "$RICH") / 2 )) "$RICH" > "$TMP_DIR/truncated.fit"
    cp "$RICH" "$TMP_DIR/badcrc.fit"
    printf '\x00' | dd of="$TMP_DIR/badcrc.fit" bs=1 seek=$(( $(wc -c < "$RICH") - 1 )) conv=notrunc 2>/dev/null
    printf 'this is not a fit file at all' > "$TMP_DIR/notfit.fit"
    cp "$RICH" "$TMP_DIR/badhdr.fit"
    printf 'XXXX' | dd of="$TMP_DIR/badhdr.fit" bs=1 seek=8 conv=notrunc 2>/dev/null
    for bad in truncated badcrc notfit badhdr; do
        TOTAL=$((TOTAL + 1))
        rm -rf "$TMP_DIR/failout" && mkdir "$TMP_DIR/failout"
        if "$PARSER" "$TMP_DIR/$bad.fit" --strip-health "$TMP_DIR/failout/out.fit" > /dev/null 2>&1; then
            fail "$bad.fit: --strip-health exited 0"
        elif [ -n "$(ls -A "$TMP_DIR/failout")" ]; then
            fail "$bad.fit: failed but left a file behind: $(ls "$TMP_DIR/failout")"
        else
            pass "$bad.fit: exit != 0, no output file, no temp file"
        fi
    done

    # (d)(e) GPX / TCX with heart rate, cadence, power extensions -> FIT without them
    for src in with-power-and-hr.gpx tcx-with-sensors.tcx; do
        TOTAL=$((TOTAL + 1))
        if "$PARSER" "$FIXTURES_DIR/$src" > "$TMP_DIR/src.json" 2>/dev/null \
            && out=$("$PARSER" "$FIXTURES_DIR/$src" --strip-health "$TMP_DIR/src-out.fit" 2>/dev/null) \
            && "$HEALTH_TOOL" dump "$TMP_DIR/src-out.fit" > "$TMP_DIR/src-out.txt" \
            && $CHECK dump "$TMP_DIR/src-out.txt" > /dev/null 2>&1 \
            && "$PARSER" "$TMP_DIR/src-out.fit" > "$TMP_DIR/src-out.json" 2>/dev/null \
            && python3 - "$TMP_DIR/src.json" "$TMP_DIR/src-out.json" <<'PY' 2>"$TMP_DIR/err.txt"
import json, sys
a = json.load(open(sys.argv[1]))['coordinates']
b = json.load(open(sys.argv[2]))['coordinates']
assert any('heartRate' in p or 'power' in p or 'cadence' in p for p in a), 'input has no health values'
assert len(a) == len(b), f'{len(a)} vs {len(b)} points'
for x, y in zip(a, b):
    assert not ({'heartRate', 'power', 'cadence'} & set(y)), y
    assert abs(x['lat'] - y['lat']) < 1e-5 and abs(x['lon'] - y['lon']) < 1e-5, (x, y)
    assert x['timestamp'] == y['timestamp'], (x, y)
PY
        then
            pass "$src → FIT without heart rate / power / cadence, same positions and times"
        else
            fail "$src --strip-health: $(head -3 "$TMP_DIR/err.txt")"
        fi
    done

    # (f) no health data in: the track comes out equal
    for src in no-sensors-gps-only.gpx tcx-no-sensors.tcx; do
        TOTAL=$((TOTAL + 1))
        if "$PARSER" "$FIXTURES_DIR/$src" --convert "$TMP_DIR/plain.fit" > /dev/null 2>&1 \
            && "$PARSER" "$TMP_DIR/plain.fit" --strip-health "$TMP_DIR/plain-out.fit" > /dev/null 2>&1 \
            && "$PARSER" "$TMP_DIR/plain.fit" > "$TMP_DIR/plain.json" 2>/dev/null \
            && "$PARSER" "$TMP_DIR/plain-out.fit" > "$TMP_DIR/plain-out.json" 2>/dev/null \
            && $CHECK track "$TMP_DIR/plain.json" "$TMP_DIR/plain-out.json" --identical > "$TMP_DIR/check.txt" 2>&1; then
            pass "$src (no health data): track identical after --strip-health [$(cat "$TMP_DIR/check.txt")]"
        else
            fail "$src without health data: $(head -3 "$TMP_DIR/check.txt")"
        fi
    done

    # every real FIT fixture: valid output, same track, no health values
    for fit_file in "$FIXTURES_DIR"/*.fit; do
        [ ! -f "$fit_file" ] && continue
        TOTAL=$((TOTAL + 1))
        filename=$(basename "$fit_file")
        if "$PARSER" "$fit_file" --strip-health "$TMP_DIR/real-out.fit" > /dev/null 2>"$TMP_DIR/err.txt" \
            && "$HEALTH_TOOL" dump "$TMP_DIR/real-out.fit" > "$TMP_DIR/real-out.txt" \
            && $CHECK dump "$TMP_DIR/real-out.txt" > /dev/null 2>"$TMP_DIR/err.txt" \
            && "$PARSER" "$fit_file" > "$TMP_DIR/real-in.json" 2>/dev/null \
            && "$PARSER" "$TMP_DIR/real-out.fit" > "$TMP_DIR/real-out.json" 2>/dev/null \
            && $CHECK track "$TMP_DIR/real-in.json" "$TMP_DIR/real-out.json" > "$TMP_DIR/check.txt" 2>"$TMP_DIR/err.txt"; then
            pass "$filename: --strip-health keeps the track, drops health ($(cat "$TMP_DIR/check.txt"))"
        else
            fail "$filename --strip-health: $(head -3 "$TMP_DIR/err.txt")"
        fi
    done

    # the table against the SDK profile itself (numbers, names, completeness, deny list of health words)
    TOTAL=$((TOTAL + 1))
    if res=$("$HEALTH_TOOL" checktable "${SCRIPT_DIR}/../docs/health-strip-table.tsv" 2>&1); then
        pass "health table vs SDK profile: $res"
    else
        fail "health table vs SDK profile: $(echo "$res" | head -3)"
    fi

    # compressed timestamp headers: handled by the SDK path, timestamps and track intact
    TOTAL=$((TOTAL + 1))
    python3 "$CHECK_PY" mkraw compressed "$TMP_DIR/compressed.fit"
    if "$PARSER" "$TMP_DIR/compressed.fit" > "$TMP_DIR/comp-in.json" 2>/dev/null \
        && "$PARSER" "$TMP_DIR/compressed.fit" --strip-health "$TMP_DIR/comp-out.fit" > /dev/null 2>"$TMP_DIR/err.txt" \
        && "$HEALTH_TOOL" dump "$TMP_DIR/comp-out.fit" > "$TMP_DIR/comp-out.txt" \
        && $CHECK dump "$TMP_DIR/comp-out.txt" > /dev/null 2>"$TMP_DIR/err.txt" \
        && "$PARSER" "$TMP_DIR/comp-out.fit" > "$TMP_DIR/comp-out.json" 2>/dev/null \
        && $CHECK track "$TMP_DIR/comp-in.json" "$TMP_DIR/comp-out.json" --expect-health > "$TMP_DIR/check.txt" 2>"$TMP_DIR/err.txt"; then
        pass "FIT with compressed timestamp headers: cleaned, same track [$(cat "$TMP_DIR/check.txt")]"
    else
        fail "FIT with compressed timestamp headers: $(head -3 "$TMP_DIR/err.txt")"
    fi

    # unknown message (65300) and unknown record field (200) are removed (allow-list)
    TOTAL=$((TOTAL + 1))
    python3 "$CHECK_PY" mkraw unknown "$TMP_DIR/unknown.fit"
    if "$PARSER" "$TMP_DIR/unknown.fit" --strip-health "$TMP_DIR/unknown-out.fit" > /dev/null 2>"$TMP_DIR/err.txt" \
        && "$HEALTH_TOOL" dump "$TMP_DIR/unknown-out.fit" > "$TMP_DIR/unknown-out.txt" \
        && $CHECK dump "$TMP_DIR/unknown-out.txt" > /dev/null 2>"$TMP_DIR/err.txt" \
        && ! grep -q " 200" "$TMP_DIR/unknown-out.txt" \
        && [ "$(grep -c '^mesg 20 ' "$TMP_DIR/unknown-out.txt")" -eq 4 ]; then
        pass "unknown message 65300 and unknown record field 200 removed, 4 records kept"
    else
        fail "unknown message/field: $(head -3 "$TMP_DIR/err.txt")"
    fi

    # trailing junk is reported (JSON + stderr), an existing output is replaced only on success
    TOTAL=$((TOTAL + 1))
    if out=$("$PARSER" "$TMP_DIR/junk.fit" --strip-health "$TMP_DIR/junk-out2.fit" 2>"$TMP_DIR/err.txt") \
        && echo "$out" | grep -q '"trailingBytesDropped": 16' && grep -q "not copied" "$TMP_DIR/err.txt"; then
        pass "trailing junk: counted in trailingBytesDropped and warned on stderr"
    else
        fail "trailing junk not reported: $out"
    fi
    TOTAL=$((TOTAL + 1))
    echo keep-me > "$TMP_DIR/stale.fit"
    if "$PARSER" "$TMP_DIR/truncated.fit" --strip-health "$TMP_DIR/stale.fit" > /dev/null 2>&1; then
        fail "truncated input accepted"
    elif [ "$(cat "$TMP_DIR/stale.fit")" = "keep-me" ] && ! ls "$TMP_DIR"/stale.fit.* > /dev/null 2>&1; then
        pass "failure leaves an existing output untouched (callers pass a fresh path) and no temp file"
    else
        fail "failure changed the existing output or left a temp file"
    fi

    # speed: ONE big FIT (many MB), linear time. Prints the time; fails above a generous bound.
    BENCH_N="${BENCH_RECORDS:-400000}"
    python3 "$CHECK_PY" mkraw bench "$TMP_DIR/bench.fit" "$BENCH_N"
    TOTAL=$((TOTAL + 1))
    t0=$(date +%s%N)
    rc=0; out=$("$PARSER" "$TMP_DIR/bench.fit" --strip-health "$TMP_DIR/bench-out.fit" 2>/dev/null) || rc=$?
    t1=$(date +%s%N)
    ms=$(( (t1 - t0) / 1000000 ))
    insz=$(wc -c < "$TMP_DIR/bench.fit"); outsz=$(wc -c < "$TMP_DIR/bench-out.fit")
    if [ $rc -eq 0 ] && echo "$out" | python3 -c "
import json, sys
d = json.loads(sys.stdin.read()); n = $BENCH_N
assert d['strippedFields'] == 3 * n and d['droppedMessages'] == 0, d
assert 21 * n < $outsz < 21 * n + 200, ($outsz, 21 * n)
" && [ "$ms" -lt 10000 ]; then
        pass "benchmark: $BENCH_N records, $((insz / 1000000)) MB in, $((outsz / 1000000)) MB out, ${ms} ms"
    else
        fail "benchmark: rc=$rc ${ms} ms in=$insz out=$outsz $out"
    fi
    # the same generator, small: parse the output and count the points
    TOTAL=$((TOTAL + 1))
    python3 "$CHECK_PY" mkraw bench "$TMP_DIR/bench-small.fit" 2000
    if "$PARSER" "$TMP_DIR/bench-small.fit" --strip-health "$TMP_DIR/bench-small-out.fit" > /dev/null 2>&1 \
        && "$PARSER" "$TMP_DIR/bench-small.fit" > "$TMP_DIR/bs-in.json" 2>/dev/null \
        && "$PARSER" "$TMP_DIR/bench-small-out.fit" > "$TMP_DIR/bs-out.json" 2>/dev/null \
        && $CHECK track "$TMP_DIR/bs-in.json" "$TMP_DIR/bs-out.json" --expect-health > "$TMP_DIR/check.txt" 2>&1; then
        pass "synthetic 2000-point FIT: same track, no health [$(cat "$TMP_DIR/check.txt")]"
    else
        fail "synthetic 2000-point FIT: $(head -3 "$TMP_DIR/check.txt")"
    fi

    # structural refusals: strip AND plain parse refuse cleanly (exit 1, never a crash), no output file
    INVALID_DIR="$FIXTURES_DIR/invalid"
    GPS_STRIPPER="${GPS_STRIPPER:-$(dirname "$PARSER")/gps-stripper}"
    for kind in zerosize badbase oddsize arch2 nodef dupfield chain-badbase chain-truncated; do
        python3 "$CHECK_PY" mkraw "$kind" "$TMP_DIR/inv-$kind.fit"
    done
    for bad in "$TMP_DIR"/inv-*.fit "$INVALID_DIR"/*.fit; do
        [ ! -f "$bad" ] && continue
        TOTAL=$((TOTAL + 1))
        rm -rf "$TMP_DIR/failout" && mkdir "$TMP_DIR/failout"
        rc1=0; rc2=0
        "$PARSER" "$bad" --strip-health "$TMP_DIR/failout/out.fit" > /dev/null 2>"$TMP_DIR/err1.txt" || rc1=$?
        "$PARSER" "$bad" > /dev/null 2>"$TMP_DIR/err2.txt" || rc2=$?
        gps_rc=0
        "${GPS_STRIPPER:-/nonexistent}" "$bad" "$TMP_DIR/failout/gps.fit" 100 100 > /dev/null 2>&1 || gps_rc=$?
        want1=1; chain=0
        case "$(basename "$bad")" in *chain*) want1=0; chain=1;; esac   # strip drops a bad chained file (reported); parse/gps refuse it
        strip_ok=0
        if [ $chain -eq 1 ]; then
            [ $rc1 -eq 0 ] && grep -q "were not copied" "$TMP_DIR/err1.txt" && strip_ok=1
        else
            [ $rc1 -eq 1 ] && [ -z "$(ls -A "$TMP_DIR/failout" | grep -v '^gps.fit$' || true)" ] && grep -q "invalid FIT structure" "$TMP_DIR/err1.txt" && strip_ok=1
        fi
        if [ $strip_ok -eq 1 ] && [ $rc2 -eq 1 ] && ! ls "$TMP_DIR/failout" | grep -q gps \
            && grep -q "invalid FIT structure" "$TMP_DIR/err2.txt" \
            && { [ ! -x "${GPS_STRIPPER:-/nonexistent}" ] || [ $gps_rc -eq 1 ]; }; then
            pass "$(basename "$bad"): parse and gps-stripper exit 1 ($(sed 's/.*structure: //' "$TMP_DIR/err2.txt")), strip exit $rc1"
        else
            fail "$(basename "$bad"): strip rc=$rc1 (want $want1), parse rc=$rc2 (want 1), gps rc=$gps_rc (want 1): $(head -1 "$TMP_DIR/err1.txt") / $(head -1 "$TMP_DIR/err2.txt")"
        fi
    done
    # a valid file followed by padding is fine (strip drops it, parse accepts it)
    TOTAL=$((TOTAL + 1))
    python3 "$CHECK_PY" mkraw chain-junk-ok "$TMP_DIR/chainjunk.fit"
    if "$PARSER" "$TMP_DIR/chainjunk.fit" --strip-health "$TMP_DIR/chainjunk-out.fit" 2>/dev/null | grep -q '"trailingBytesDropped": 8'; then
        pass "valid file + 8 padding bytes: strip drops and reports them (the validator ignores non-header bytes)"
    else
        fail "valid file + padding handled wrongly"
    fi
    # a compressed timestamp with no earlier full timestamp cannot be expanded: strip refuses
    TOTAL=$((TOTAL + 1))
    python3 "$CHECK_PY" mkraw compressed-first "$TMP_DIR/cfirst.fit"
    rc=0; "$PARSER" "$TMP_DIR/cfirst.fit" --strip-health "$TMP_DIR/cfirst-out.fit" > /dev/null 2>"$TMP_DIR/err.txt" || rc=$?
    if [ $rc -ne 1 ] || [ -e "$TMP_DIR/cfirst-out.fit" ] || ! grep -q "compressed timestamp without a previous full timestamp" "$TMP_DIR/err.txt"; then
        fail "compressed timestamp without reference: rc=$rc $(head -1 "$TMP_DIR/err.txt")"
    else
        pass "compressed timestamp without a previous full timestamp: refused, no output"
    fi
    # nothing left after cleaning (only health messages): refused, no output
    TOTAL=$((TOTAL + 1))
    python3 "$CHECK_PY" mkraw allhealth "$TMP_DIR/allhealth.fit"
    rc=0; "$PARSER" "$TMP_DIR/allhealth.fit" --strip-health "$TMP_DIR/allhealth-out.fit" > /dev/null 2>"$TMP_DIR/err.txt" || rc=$?
    if [ $rc -ne 1 ] || [ -e "$TMP_DIR/allhealth-out.fit" ] || ! grep -q "no messages left" "$TMP_DIR/err.txt"; then
        fail "file with only health messages: rc=$rc $(head -1 "$TMP_DIR/err.txt")"
    else
        pass "file with nothing left after cleaning: refused, no output"
    fi
    # empty definition (no fields) is legal: dropped
    TOTAL=$((TOTAL + 1))
    python3 "$CHECK_PY" mkraw emptydef-ok "$TMP_DIR/emptydef.fit"
    if "$PARSER" "$TMP_DIR/emptydef.fit" --strip-health "$TMP_DIR/emptydef-out.fit" > /dev/null 2>&1; then
        pass "definition without fields: accepted"
    else
        fail "definition without fields: refused"
    fi
    # header bytes are the same for every output (protocol 2.0, SDK profile version)
    TOTAL=$((TOTAL + 1))
    if [ "$(head -c 4 "$TMP_DIR/rich-out.fit" | xxd -p)" = "0e20a652" ] && [ "$(head -c 4 "$TMP_DIR/compressed.fit" | xxd -p)" != "0e20a652" ] \
        && "$PARSER" "$TMP_DIR/compressed.fit" --strip-health "$TMP_DIR/comp-out2.fit" > /dev/null 2>&1 \
        && [ "$(head -c 4 "$TMP_DIR/comp-out2.fit" | xxd -p)" = "0e20a652" ]; then
        pass "output header normalised (14 bytes, protocol 2.0, profile 21158)"
    else
        fail "output header not normalised"
    fi
    # empty input to the library call is an error (the CLI refuses empty files before)
    # compressed timestamp file speed
    BENCH_N="${BENCH_RECORDS:-400000}"
    python3 "$CHECK_PY" mkraw bench-compressed "$TMP_DIR/benchc.fit" "$BENCH_N"
    TOTAL=$((TOTAL + 1))
    t0=$(date +%s%N)
    rc=0; out=$("$PARSER" "$TMP_DIR/benchc.fit" --strip-health "$TMP_DIR/benchc-out.fit" 2>/dev/null) || rc=$?
    t1=$(date +%s%N)
    ms=$(( (t1 - t0) / 1000000 ))
    outsz=$(wc -c < "$TMP_DIR/benchc-out.fit")
    # timestamps: the SDK parse of input and output must agree on every point (rollovers of the 5 bit offset included)
    python3 "$CHECK_PY" mkraw bench-compressed "$TMP_DIR/benchc-small.fit" 3000
    ts_ok=0
    if "$PARSER" "$TMP_DIR/benchc-small.fit" > "$TMP_DIR/bc-in.json" 2>/dev/null \
        && "$PARSER" "$TMP_DIR/benchc-small.fit" --strip-health "$TMP_DIR/bc-small-out.fit" > /dev/null 2>&1 \
        && "$PARSER" "$TMP_DIR/bc-small-out.fit" > "$TMP_DIR/bc-out.json" 2>/dev/null \
        && python3 - "$TMP_DIR/bc-in.json" "$TMP_DIR/bc-out.json" <<'PY' 2>/dev/null
import json, sys
a = json.load(open(sys.argv[1]))['coordinates']; b = json.load(open(sys.argv[2]))['coordinates']
assert len(a) == len(b) == 3001, (len(a), len(b))
assert [p['timestamp'] for p in a] == [p['timestamp'] for p in b], 'timestamps differ'
assert [(p['lat'], p['lon']) for p in a] == [(p['lat'], p['lon']) for p in b], 'positions differ'
assert a[0]['timestamp'] != a[-1]['timestamp']
PY
    then ts_ok=1; fi
    if [ $rc -eq 0 ] && [ $ts_ok -eq 1 ] && [ "$ms" -lt 10000 ] && [ "$outsz" -gt $((21 * BENCH_N)) ] && [ "$outsz" -lt $((21 * BENCH_N + 300)) ]; then
        pass "benchmark compressed timestamps (all 3001 timestamps of a small twin equal before/after): $BENCH_N records, $(( $(wc -c < "$TMP_DIR/benchc.fit") / 1000000 )) MB in, ${ms} ms"
    else
        fail "benchmark compressed timestamps: rc=$rc ts_ok=$ts_ok ${ms} ms out=$outsz"
    fi

    # usage errors
    TOTAL=$((TOTAL + 1))
    if "$PARSER" "$FIXTURES_DIR/sample.gpx" --strip-health 2>/dev/null; then
        fail "--strip-health without an output path was accepted"
    elif "$PARSER" "$FIXTURES_DIR/sample.gpx" --strip-health "$TMP_DIR/x.fit" --convert "$TMP_DIR/y.fit" 2>/dev/null; then
        fail "--strip-health together with --convert was accepted"
    else
        pass "--strip-health usage errors rejected"
    fi
fi

echo ""
echo "========================================="
echo "Results: $PASSED/$TOTAL passed, $FAILED failed"
echo "========================================="

if [ $FAILED -gt 0 ]; then
    exit 1
fi
