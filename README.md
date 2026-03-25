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
make -j$(nproc) fit-parser gps-stripper

# Verify
./fit-parser --version
# BikeCodersLife FIT Parser v2.0.0

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
