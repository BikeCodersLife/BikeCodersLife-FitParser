# Garmin FIT SDK

This directory should contain the Garmin FIT SDK, which will be downloaded automatically during the build process.

## Manual Download

If you need to download the SDK manually:

1. Visit: https://developer.garmin.com/fit/download/
2. Download: FitSDKRelease_21.141.00.zip
3. Extract to this directory:
   ```bash
   unzip FitSDKRelease_21.141.00.zip -d third_party/fit-sdk
   ```

## Directory Structure

After extraction, you should have:
```
third_party/fit-sdk/
├── cpp/
│   ├── fit.cpp
│   ├── fit.hpp
│   ├── fit_decode.cpp
│   ├── fit_decode.hpp
│   ├── fit_mesg.cpp
│   ├── fit_mesg.hpp
│   ├── fit_mesg_broadcaster.cpp
│   ├── fit_mesg_broadcaster.hpp
│   └── ... (other FIT SDK files)
└── ... (other SDK components)
```

## License

The Garmin FIT SDK has its own license. Please review the license terms at:
https://developer.garmin.com/fit/overview/

This directory is gitignored and will not be committed to the repository.
