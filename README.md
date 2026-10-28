# BikeCodersLife FIT Parser

High-performance C++ multi-format cycling file parser and converter. Parses FIT, GPX, and TCX files using the official Garmin FIT SDK. Converts GPX/TCX to FIT for standardized processing.

## Features

- **Multi-format**: Parses FIT, GPX 1.0/1.1, and TCX natively
- **GPX/TCX to FIT conversion**: Convert any cycling file to compact FIT binary (up to 10x smaller)
- **Full sensor data**: Heart rate, power, cadence, temperature, speed, elevation
- **Privacy by design**: Converted FIT files contain no device serial, user name, or author info
- **Fast**: Parses 300KB files in ~200ms (55,000 points/second)
- **Secure**: Resource limits (256MB memory, 30s CPU), file validation, path sanitization
- **GPS Stripper**: Companion tool to trim GPS data from ride start/end for privacy
- **Health strip** (`--strip-health`, v2.4.0): removes heart rate, power, cadence and every other physiological or biomechanical value, plus all developer data, and writes a clean FIT

## Quick Start

### Download Pre-compiled Binary

```bash
# Linux (amd64)
curl -sL https://github.com/BikeCodersLife/BikeCodersLife-FitParser/releases/latest/download/fit-parser-linux-amd64.tar.gz | tar xz
chmod +x fit-parser gps-stripper
sudo mv fit-parser gps-stripper /usr/local/bin/

# macOS (amd64)
curl -sL https://github.com/BikeCodersLife/BikeCodersLife-FitParser/releases/latest/download/fit-parser-darwin-amd64.tar.gz | tar xz

# Alpine Linux (for Docker)
curl -sL https://github.com/BikeCodersLife/BikeCodersLife-FitParser/releases/latest/download/fit-parser-alpine-amd64.tar.gz | tar xz
```

### Parse any format to JSON

```bash
# FIT file
fit-parser ride.fit

# GPX file
fit-parser ride.gpx

# TCX file
fit-parser activity.tcx
```

All formats produce the same JSON output:

```json
{
  "coordinates": [
    {
      "lat": 52.3702,
      "lon": 4.8952,
      "elevation": 2.5,
      "heartRate": 142,
      "power": 245,
      "cadence": 90,
      "timestamp": "2025-06-15T06:30:00Z"
    }
  ],
  "summary": {
    "points": 802,
    "distanceKm": 55.28,
    "durationMin": 134.57,
    "avgHeartRate": 138.0,
    "maxHeartRate": 172.0,
    "avgPower": 210.0,
    "maxPower": 450.0,
    "avgCadence": 88.0,
    "avgSpeed": 6.85,
    "maxSpeed": 12.4,
    "startTime": "2025-06-15T06:30:00Z",
    "endTime": "2025-06-15T08:44:34Z"
  }
}
```

### Convert GPX/TCX to FIT

```bash
# Convert GPX to FIT
fit-parser ride.gpx --convert ride.fit

# Convert TCX to FIT
fit-parser activity.tcx --convert activity.fit
```

**File size comparison:**

| Source | GPX Size | FIT Size | Ratio |
|--------|----------|----------|-------|
| Short ride (3 points) | 688 B | 338 B | 2x smaller |
| Real ride (802 points) | 166 KB | 17 KB | **10x smaller** |
| Long ride (2000+ points) | 400 KB | 40 KB | **10x smaller** |

### GPS Privacy Stripping

```bash
# Strip first 2000m and last 2000m of GPS data from a FIT file
gps-stripper ride.fit stripped_ride.fit 2000 2000
```

### Health data stripping

```bash
# FIT, GPX or TCX in, a FIT without health data out
fit-parser ride.fit --strip-health clean.fit
# stdout: {"droppedMessages": 497, "strippedFields": 107718, "developerFieldsRemoved": 25295, "trailingBytesDropped": 0}
```

For riders who turned "health data" off: the file is decoded with the FIT SDK and
re-encoded message by message (same order, valid header and CRC), keeping time,
position, altitude, distance, speed, ambient temperature, grade, GPS accuracy,
events, laps / sessions and device info (including sensor battery). Removed:
heart rate, HRV, power and everything derived from it, cadence and cycle counts,
respiration, core body temperature, running and pedalling dynamics, muscle
oxygen, calories, training load, zones, user profile, sleep and monitoring
data, and **all developer data** (CORE, Stryd, Moxy and similar carry their
values there).

It is an allow-list: a message or field that is not in the table is removed.
The table is decided field by field from the FIT SDK profile and lives in
[`docs/health-strip.md`](docs/health-strip.md) (generated from
`docs/health-strip-table.tsv` by `tools/gen_health_table.py`).

Fail closed: a file that cannot be decoded or cleaned (truncated, bad CRC, not
FIT) gives a non-zero exit and **no output file**; the output is written under a
temporary name and renamed only on success. GPX/TCX input is converted to FIT
first and then goes through the same filter, so every source ends up behind one
filter. A FIT holding several chained files has each of them cleaned; bytes after
the last valid file (junk, padding, a damaged chained file) are not copied, are
counted in `trailingBytesDropped` and reported on stderr.

Speed: a linear byte-level rewrite driven by the same table (a 34 MB FIT takes
about 0.35 s, peak memory under 3x the file size). The strip never calls the vendor
SDK decoder. Compressed-timestamp record headers are expanded to full timestamps
(computed from the last full timestamp, rolling 5 bit offset) at the same speed.
The output header is always the same 14 bytes (protocol 2.0, profile 21158).

Structure checks (strip mode, and since v2.4.0 also the plain parse mode, `--convert`
and `gps-stripper` before the SDK decoder sees a byte): a file with a zero sized
field, a size that is not a multiple of its base type, a base type number outside
the FIT table, an architecture byte above 1, a data message without a definition,
duplicate field numbers in one definition, or a record stream that does not end at
the header's data size exits non-zero with `invalid FIT structure: <reason>`.
Every chained FIT file in the input is checked (the vendor decoder reads them all); a damaged
or truncated chained file is refused in parse mode and gps-stripper (the strip drops it and
reports `trailingBytesDropped`). Bytes after a file that do not start like a FIT header are
ignored. The parse mode reads the file once and decodes the very bytes it validated.

Callers: pass a fresh output path. An existing file is replaced only on success
and is never deleted on failure. Handled failures remove the temporary files
(`<out>.tmp.<pid>`, `<out>.conv.<pid>`), but a SIGKILL (for instance the 30 s CPU
limit) cannot be handled: delete the output directory contents in a `finally`.

## Supported Sensor Data

| Sensor | FIT | GPX | TCX | Notes |
|--------|-----|-----|-----|-------|
| GPS (lat/lon) | Yes | Yes | Yes | |
| Elevation | Yes | Yes | Yes | |
| Heart Rate | Yes | Yes (TrackPointExtension) | Yes (HeartRateBpm) | |
| Power | Yes | Yes (TrackPointExtension) | Yes (Watts extension) | |
| Cadence | Yes | Yes (TrackPointExtension) | Yes | |
| Temperature | Yes | Yes (TrackPointExtension) | No | |
| Speed | Yes | Yes (TrackPointExtension) | Yes (Speed extension) | GPX: from speed sensor |
| Timestamps | Yes | Yes | Yes | Converted to FIT epoch |

GPX extensions supported: Garmin `gpxtpx:TrackPointExtension` (v1 and v2), `ns3:` namespace variant, and unprefixed variants.

## Max speed and the GPS spike filter

`maxSpeed` is the raw single-sample peak of the speed derived from GPS
positions. One bad fix sets it, so treat it as diagnostic only.
`smoothedMaxSpeedKmh` is the value to show and store. The rules, the same for
FIT, GPX and TCX:

1. **De-spike isolated fixes.** A fix that is only reachable at more than
   120 km/h from both neighbours (a teleport out and back), or a first / last
   fix with an impossible segment (the pre-lock (0,0) fix), is dropped from
   the track. FIT has done this since v2.1.6, GPX/TCX since v2.3.2. When a
   GPX/TCX fix is dropped, the distance and ascent / descent are summed again
   over the kept fixes. A TCX device distance (`<DistanceMeters>`) is kept.
   A ride where nothing is dropped reports exactly the reader totals.
2. **Hard cap.** A segment implying more than 150 km/h never counts towards
   the max. The fastest road descents in racing reach about 130 km/h.
3. **Sustained window.** The max is the peak of a rolling median over a window
   of at least 7 s that holds at least 4 speed samples. A 1 Hz log holds 8
   samples in 7 s. On a sparse log (one fix every 5-20 s) the window extends
   to 4 samples, so one catch-up segment after a GPS freeze cannot set the
   max on its own.
4. **Device bound.** The result never exceeds the device's own recorded max
   speed: FIT `session.max_speed`, the GlobalSat GPX track summary
   `<maxspeed>`, or the highest TCX `<Lap><MaximumSpeed>`. The device measures
   speed by Doppler or a wheel sensor, which position jumps do not affect.
   GPX/TCX emit that value as `sessionMaxSpeedKmh`. It is ignored when it is
   not above the session average (a "max = average" filler, not a
   measurement).

Rule 4 exists because some logs lag and then catch up over several fixes. A
GlobalSat ride logged 40, 60, 72, 84, 84, 96, 171 km/h and then 24 km/h on a
flat road. No per-sample rule separates that from real acceleration, but the
device recorded 32.5 km/h as its max. A real descent keeps its speed: a 10 s
plateau at 95 km/h reports 95.3 km/h (`tests/fixtures/fast-descent.gpx`).
The per-point `speed` values in `coordinates` stay the raw GPS-derived values.

## Build from Source

**Prerequisites:**
- CMake 3.15+
- C++17 compiler (GCC 7+, Clang 5+)
- Garmin FIT SDK (included in `third_party/fit-sdk/`)
- pugixml (auto-fetched via CMake FetchContent)

```bash
git clone https://github.com/BikeCodersLife/BikeCodersLife-FitParser.git
cd BikeCodersLife-FitParser

mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc) fit-parser gps-stripper health-fit-tool   # health-fit-tool: test helper only

# Verify
./fit-parser --version
# BikeCodersLife FIT Parser v2.4.0

# Run tests
cd .. && chmod +x tests/run_tests.sh && tests/run_tests.sh
# Results: 62/62 passed ✓
```

## Docker Integration

```dockerfile
# Download pre-built binaries in a multi-stage Docker build
FROM alpine:3.21 AS fit-downloader
ARG FIT_PARSER_VERSION=v2.0.4
RUN apk add --no-cache curl tar \
    && mkdir -p /binaries \
    && curl -sL "https://github.com/BikeCodersLife/BikeCodersLife-FitParser/releases/download/${FIT_PARSER_VERSION}/fit-parser-alpine-amd64.tar.gz" \
       | tar xz -C /binaries

# Copy into your application image
FROM php:8.4-cli-alpine
COPY --from=fit-downloader /binaries/fit-parser /usr/local/bin/fit-parser
COPY --from=fit-downloader /binaries/gps-stripper /usr/local/bin/gps-stripper
RUN chmod +x /usr/local/bin/fit-parser /usr/local/bin/gps-stripper
```

## Integration Examples

### PHP (Symfony)

```php
use Symfony\Component\Process\Process;

// Parse any format — FIT, GPX, or TCX
$process = new Process(['/usr/local/bin/fit-parser', $filePath]);
$process->setTimeout(30);
$process->mustRun();
$data = json_decode($process->getOutput(), true);

// Convert GPX to FIT
$process = new Process(['/usr/local/bin/fit-parser', $gpxPath, '--convert', $fitPath]);
$process->setTimeout(60);
$process->mustRun();
```

### Python

```python
import subprocess, json

# Parse any format
result = subprocess.run(
    ['/usr/local/bin/fit-parser', 'ride.gpx'],
    capture_output=True, text=True, timeout=30
)
data = json.loads(result.stdout)

# Convert GPX to FIT
subprocess.run(
    ['/usr/local/bin/fit-parser', 'ride.gpx', '--convert', 'ride.fit'],
    check=True, timeout=60
)
```

### Node.js

```javascript
const { execSync } = require('child_process');
const data = JSON.parse(
  execSync('/usr/local/bin/fit-parser ride.gpx', { encoding: 'utf-8', timeout: 30000 })
);
```

## Testing

```bash
./tests/run_tests.sh

# 62/62 tests passed ✓
# - 29 FIT file parsing tests
# - 10 GPX JSON output tests
# - 10 GPX→FIT conversion tests
# - 10 GPX→FIT→JSON round-trip tests
# - 3 edge case tests
```

**Test fixtures include:** full sensor data, GPS-only, heart rate only, speed sensor, evening-into-night (midnight crossing), GPS signal drops, below-sea-level elevation, negative temperatures.

## Performance

| File Size | Points | Parse Time |
|-----------|--------|------------|
| 551KB FIT | 13,160 | **237ms** |
| 166KB GPX | 802 | **45ms** |
| 300KB FIT | 8,500 | ~200ms |

**210x faster than PHP-based parsers.**

## Security

- Resource limits: 256MB memory, 30s CPU, 10 file descriptors
- File validation: size (max 100MB), type, path sanitization (no directory traversal)
- Process isolation: no network, no core dumps
- Privacy: converted FIT files strip all device/user identifiers

## CI/CD

Automated GitHub Actions workflows:
- **Build**: Compiles and tests on Ubuntu + macOS on every push
- **Release**: Builds binaries for Linux (amd64), Alpine (amd64), and macOS (amd64) on tagged versions
- **Static Analysis**: cppcheck + clang-tidy

## License

MIT License - see [LICENSE](LICENSE).

Third-party licenses - see [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) for Garmin FIT SDK license.

## Credits

- Built with [Garmin FIT SDK](https://developer.garmin.com/fit/overview/) and [pugixml](https://pugixml.org/)
- Developed by [BikeCodersLife](https://github.com/BikeCodersLife)
