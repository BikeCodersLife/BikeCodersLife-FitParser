# BikeCodersLife FIT Parser

High-performance C++ FIT file parser using the official Garmin FIT SDK. Extracts GPS coordinates from FIT files and outputs them as JSON.

## Features

- ✅ **Fast**: Parses 300KB files in ~200ms (55,000 points/second)
- ✅ **Secure**: Multiple security layers with resource limits
- ✅ **Reliable**: 100% success rate on 29 real-world test files
- ✅ **Cross-platform**: Linux and macOS binaries via GitHub Actions

## Quick Start

### Download Pre-compiled Binary

```bash
# Linux (amd64)
wget https://github.com/BikeCodersLife/BikeCodersLife-FitParser/releases/latest/download/fit-parser-linux-amd64.tar.gz
tar -xzf fit-parser-linux-amd64.tar.gz
chmod +x fit-parser
sudo mv fit-parser /usr/local/bin/
```

### Usage

```bash
# Parse FIT file
fit-parser ride.fit

# Output (JSON to stdout)
{
  "coordinates": [
    {"lat": 52.3702, "lon": 4.8952, "elevation": 2.5, "timestamp": "2024-01-15T10:30:00Z"},
    ...
  ],
  "summary": {
    "points": 13160
  }
}
```

## Distance Calculation (Haversine)

The `FitParser` calculates total ride distance using the **Haversine formula**. This mathematical equation accounts for the Earth's curvature by calculating the "great-circle distance" between points on a sphere. 

Unlike simple grid-based geometry, the Haversine formula ensures high-precision distance metrics for cycling activities, using a mean Earth radius of 6,371.003 km.

## Performance

| File Size | Points | Parse Time |
|-----------|--------|------------|
| 551KB     | 13,160 | **237ms**  |
| 300KB     | 8,500  | ~200ms     |
| 132KB     | 3,625  | ~60ms      |

**210x faster than PHP-based parsers** 🚀

See [docs/BENCHMARKS.md](docs/BENCHMARKS.md) for detailed performance analysis.

## Security

Multiple security layers protect against malicious files:

- ✅ Resource limits (256MB memory, 30s CPU)
- ✅ File validation (size, type, path sanitization)
- ✅ Process isolation
- ✅ Read-only operations

See [docs/SECURITY.md](docs/SECURITY.md) for comprehensive security documentation.

## Build from Source

**Prerequisites:**
- CMake 3.15+
- C++17 compiler (GCC 7+, Clang 5+)
- Garmin FIT SDK 21.158.00

**Build:**

```bash
# Clone repository
git clone https://github.com/BikeCodersLife/BikeCodersLife-FitParser.git
cd BikeCodersLife-FitParser

# Download FIT SDK manually from https://developer.garmin.com/fit/download/
# Extract to third_party/fit-sdk/

# Build
mkdir build && cd build
cmake ..
make

# Test
./fit-parser --version
```

See [SETUP.md](SETUP.md) for detailed build instructions.

## Integration Examples

### PHP (Symfony)

```php
use Symfony\Component\Process\Process;

$process = new Process(['/usr/local/bin/fit-parser', $fitFilePath]);
$process->setTimeout(30);
$process->mustRun();

$data = json_decode($process->getOutput(), true);
$coordinates = $data['coordinates'];
```

### Python

```python
import subprocess
import json

result = subprocess.run(
    ['/usr/local/bin/fit-parser', 'ride.fit'],
    capture_output=True,
    text=True,
    timeout=30
)

data = json.loads(result.stdout)
```

### Node.js

```javascript
const { execSync } = require('child_process');

const output = execSync('/usr/local/bin/fit-parser ride.fit', {
  encoding: 'utf-8',
  timeout: 30000
});

const data = JSON.parse(output);
```

## Testing

```bash
# Run test suite
./tests/run_tests.sh

# Results: 29/29 tests passed ✅
```

See [tests/README.md](tests/README.md) for testing documentation.

## Documentation

- **[SETUP.md](SETUP.md)** - Build and installation instructions
- **[docs/SECURITY.md](docs/SECURITY.md)** - Security measures and threat model
- **[docs/BENCHMARKS.md](docs/BENCHMARKS.md)** - Performance analysis and benchmarks
- **[tests/README.md](tests/README.md)** - Testing framework documentation

## Static Analysis

This project uses `cppcheck` and `clang-tidy` to maintain high code quality. These are automatically run in GitHub Actions on every push.

**Run locally:**

```bash
# Cppcheck
cppcheck --enable=all --suppress=missingIncludeSystem src/

# Clang-Tidy
# First, generate compile commands (in build directory)
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..
# Then run
clang-tidy -p build/ src/*.cpp
```

## GitHub Actions

Automated CI/CD workflows:

- **Build**: Compiles and tests on Ubuntu + macOS
- **Release**: Creates binaries for each tagged version

## License

MIT License - see [LICENSE](LICENSE) for details.

Third-party licenses - see [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) for Garmin FIT SDK license.

## Credits

- Built with [Garmin FIT SDK](https://developer.garmin.com/fit/overview/)
- Developed by **Xander** <handlebar@bikecoders.life> ([BikeCodersLife](https://github.com/BikeCodersLife))

## Support

- **Issues**: [GitHub Issues](https://github.com/BikeCodersLife/BikeCodersLife-FitParser/issues)
- **Releases**: [GitHub Releases](https://github.com/BikeCodersLife/BikeCodersLife-FitParser/releases)
