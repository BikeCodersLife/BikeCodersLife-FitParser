# Third-Party Licenses

This project uses third-party software components that are subject to their own licenses.

## Garmin FIT SDK

**Version**: 21.158.00  
**Website**: https://developer.garmin.com/fit/overview/  
**License**: FIT Protocol License

### Description

The BikeCodersLife FIT Parser uses the Garmin FIT Software Development Kit (SDK) to parse FIT (Flexible and Interoperable Data Transfer) files. The FIT SDK is the official implementation provided by Garmin for reading and writing FIT files.

### Usage

The FIT SDK is used as a library dependency during compilation. The SDK source code is:
- Downloaded during the build process (via GitHub Actions or manually)
- Not included in this repository (excluded via `.gitignore`)
- Linked statically into the compiled binary

### License Terms

The Garmin FIT SDK is provided under the FIT Protocol License, which allows:
- ✅ Commercial use
- ✅ Distribution of compiled binaries
- ✅ Modification for internal use
- ✅ Royalty-free usage

### Acknowledgment

This software includes the Garmin FIT SDK:
```
Copyright © 2024 Garmin Ltd. or its subsidiaries.
```

The FIT protocol and SDK are developed and maintained by Garmin. For the complete FIT SDK license terms, please visit:
https://developer.garmin.com/fit/download/

### Disclaimer

The BikeCodersLife FIT Parser is an independent project and is not affiliated with, endorsed by, or sponsored by Garmin Ltd. or its subsidiaries.

---

## Project License

The BikeCodersLife FIT Parser source code (excluding the Garmin FIT SDK) is licensed under the MIT License. See [LICENSE](LICENSE) for details.

**Copyright © 2025 BikeCodersLife**

The MIT License applies to:
- All source code in `src/` directory
- Build configuration files (`CMakeLists.txt`)
- GitHub Actions workflows (`.github/workflows/`)
- Documentation files
- Test scripts and fixtures

The MIT License does **not** apply to:
- Garmin FIT SDK (subject to FIT Protocol License)
- Third-party dependencies (subject to their respective licenses)
