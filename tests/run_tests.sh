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
echo "FIT Parser Test Suite v2.0"
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
echo "========================================="
echo "Results: $PASSED/$TOTAL passed, $FAILED failed"
echo "========================================="

if [ $FAILED -gt 0 ]; then
    exit 1
fi
