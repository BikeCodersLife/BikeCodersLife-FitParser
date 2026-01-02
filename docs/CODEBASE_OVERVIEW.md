# FitParser (C++) Codebase Overview

**Purpose**: High-performance C++ CLI tool for parsing Garmin FIT files and extracting track points (lat, lon, elevation, timestamp) into JSON format. It is designed to run within the Scaleway serverless container.

---

## Core Components (`src/`)

- **`main.cpp`**: Entry point. Handles command-line arguments (input file path) and orchestration.
- **`fit_parser.cpp` / `fit_parser.h`**: Core logic for reading binary FIT files using the official Garmin SDK structure (or custom implementation).
- **`json_output.cpp` / `json_output.h`**: Handles formatting the extracted data points into a standard JSON structure expected by the `ScalewayJobRunner`.

---

## Usage

```bash
./fit_parser <input_fit_file>
```

**Output**: JSON string to stdout.

---

**Last Updated**: 2026-01-01
