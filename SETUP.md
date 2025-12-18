# Setup Instructions

## 1. Create GitHub Repository

1. Go to https://github.com/BikeCodersLife
2. Click "New repository"
3. Repository name: `BikeCodersLife-FitParser`
4. Description: "High-performance C++ FIT file parser using Garmin FIT SDK"
5. Public repository
6. **Do NOT** initialize with README, .gitignore, or license (we already have these)
7. Click "Create repository"

## 2. Push to GitHub

```bash
cd /home/xkoevoet/Development/BikeCodersLife/FitParser

# Add remote
git remote add origin git@github.com:BikeCodersLife/BikeCodersLife-FitParser.git

# Push to GitHub
git push -u origin main
```

## 3. Download Garmin FIT SDK

The FIT SDK is required to build the parser. **Note**: The Garmin website requires manual download (automated wget/curl doesn't work).

### Manual Download:

1. Visit: https://developer.garmin.com/fit/download/
2. Click "Download" for FIT SDK Release 21.158.00 (or latest)
3. Save the ZIP file to `/home/xkoevoet/Development/BikeCodersLife/FitParser/`

### Extract SDK:

```bash
cd /home/xkoevoet/Development/BikeCodersLife/FitParser

# Extract to third_party
unzip FitSDKRelease_21.158.00.zip -d third_party/fit-sdk

# Verify extraction
ls -la third_party/fit-sdk/cpp/
# Should show: fit.cpp, fit.hpp, fit_decode.cpp, fit_decode.hpp, etc.
```

## 4. Build Locally

```bash
# Create build directory
mkdir build
cd build

# Configure with CMake
cmake ..

# Build
make

# Test
./fit-parser --version
./fit-parser --help
```

## 5. Test with a Real FIT File

```bash
# Download a sample FIT file or use one from your Garmin device
# Example: Copy from Garmin device
cp /path/to/your/ride.fit tests/fixtures/valid.fit

# Parse it
./build/fit-parser tests/fixtures/valid.fit
```

## 6. Create First Release

Once the build works locally and you've pushed to GitHub:

```bash
# Tag version
git tag -a v1.0.0 -m "Release v1.0.0: Initial BikeCodersLife FIT Parser"

# Push tag
git push origin v1.0.0
```

This will trigger the GitHub Actions release workflow, which will:
- Build binaries for Linux (amd64) and macOS (amd64)
- Create a GitHub release
- Upload the binaries as downloadable assets

## Next Steps

After the release is created:
1. Verify binaries are available in GitHub Releases
2. Test downloading and running the Linux binary
3. Move to Phase 4: PHP Integration in Velogrid
