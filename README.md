# BikeCodersLife FIT Parser

High-performance C++ FIT file parser using the official Garmin FIT SDK. Extracts GPS coordinates from FIT files and outputs them as JSON.

## Features

- ✅ **Fast**: Parses 300KB FIT files in <1 second
- ✅ **Secure**: Resource limits (256MB memory, 30s CPU time)
- ✅ **Official SDK**: Uses Garmin FIT SDK for accurate parsing
- ✅ **JSON Output**: Easy integration with any language
- ✅ **Cross-platform**: Linux and macOS binaries

## Performance

| File Size | Points | Parse Time |
|-----------|--------|------------|
| 300KB     | ~8,500 | <1 second  |
| 1MB       | ~28,000| <3 seconds |

**40-80x faster than PHP-based parsers**

## Installation

### Download Pre-compiled Binary

Download the latest release for your platform:

```bash
# Linux (amd64)
wget https://github.com/BikeCodersLife/BikeCodersLife-FitParser/releases/latest/download/fit-parser-linux-amd64.tar.gz
tar -xzf fit-parser-linux-amd64.tar.gz
chmod +x fit-parser
sudo mv fit-parser /usr/local/bin/

# macOS (amd64)
wget https://github.com/BikeCodersLife/BikeCodersLife-FitParser/releases/latest/download/fit-parser-darwin-amd64.tar.gz
tar -xzf fit-parser-darwin-amd64.tar.gz
chmod +x fit-parser
sudo mv fit-parser /usr/local/bin/
```

### Build from Source

**Prerequisites:**
- CMake 3.15+
- C++17 compiler (GCC 7+, Clang 5+)
- Garmin FIT SDK

**Build steps:**

```bash
# Clone repository
git clone https://github.com/BikeCodersLife/BikeCodersLife-FitParser.git
cd BikeCodersLife-FitParser

# Download Garmin FIT SDK
wget https://developer.garmin.com/downloads/fit/FitSDKRelease_21.141.00.zip
unzip FitSDKRelease_21.141.00.zip -d third_party/fit-sdk

# Build
mkdir build && cd build
cmake ..
make

# Test
ctest --output-on-failure

# Install
sudo make install
```

## Usage

```bash
# Parse FIT file
fit-parser /path/to/ride.fit

# Output (JSON to stdout)
{
  "coordinates": [
    {
      "lat": 52.3702,
      "lon": 4.8952,
      "elevation": 2.5,
      "timestamp": "2024-01-15T10:30:00Z"
    },
    {
      "lat": 52.3703,
      "lon": 4.8953,
      "elevation": 2.6,
      "timestamp": "2024-01-15T10:30:05Z"
    }
  ],
  "summary": {
    "points": 1250,
    "duration": 3600,
    "distance": 25000
  }
}
```

### Error Handling

```bash
# Invalid file
fit-parser invalid.fit
# Exit code: 1
# stderr: "Error: Failed to decode FIT file"

# File not found
fit-parser missing.fit
# Exit code: 1
# stderr: "Error: Cannot open file: missing.fit"
```

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
coordinates = data['coordinates']
```

### Node.js

```javascript
const { execSync } = require('child_process');

const output = execSync('/usr/local/bin/fit-parser ride.fit', {
  encoding: 'utf-8',
  timeout: 30000
});

const data = JSON.parse(output);
const coordinates = data.coordinates;
```

## Security

The parser implements multiple security measures:

- **Memory limit**: 256MB maximum
- **CPU time limit**: 30 seconds maximum
- **File size validation**: Configurable max size
- **Read-only access**: No file writes
- **No network access**: Fully offline
- **Input validation**: Magic byte verification

## Development

### Running Tests

```bash
cd build
ctest --output-on-failure
```

### Benchmarking

```bash
time ./build/fit-parser tests/fixtures/valid.fit
```

### Code Style

- C++17 standard
- Compiler warnings as errors (`-Werror`)
- Optimization level 3 (`-O3`)

## License

MIT License - see [LICENSE](LICENSE) file for details.

## Credits

- Built with [Garmin FIT SDK](https://developer.garmin.com/fit/overview/)
- Developed by [BikeCodersLife](https://github.com/BikeCodersLife)

## Support

- **Issues**: [GitHub Issues](https://github.com/BikeCodersLife/BikeCodersLife-FitParser/issues)
- **Releases**: [GitHub Releases](https://github.com/BikeCodersLife/BikeCodersLife-FitParser/releases)
