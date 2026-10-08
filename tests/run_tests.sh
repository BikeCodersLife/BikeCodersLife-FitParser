#!/bin/bash
# Test script for FIT parser v2.0
# Tests: FIT parsing, GPX/TCX parsing, GPX/TCX→FIT conversion, round-trip

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PARSER="${SCRIPT_DIR}/../build/fit-parser"
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

# The converted-FIT parse runs full stats including the GPS de-spike (v2.1.6),
# which removes isolated glitch fixes; the lighter GPX-direct path does not.
# So the FIT may have a FEW fewer points — never more, and never a wholesale
# loss. Allow the small de-spike delta rather than demanding exact equality.
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
echo "========================================="
echo "Results: $PASSED/$TOTAL passed, $FAILED failed"
echo "========================================="

if [ $FAILED -gt 0 ]; then
    exit 1
fi
