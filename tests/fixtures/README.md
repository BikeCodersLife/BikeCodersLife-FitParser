# Test Fixtures

This directory contains FIT files for testing the parser.

## Files

- `valid.fit` - A valid FIT file with GPS coordinates (to be added)
- `invalid.fit` - An invalid FIT file for error handling tests (to be added)
- `malformed.fit` - A malformed FIT file for robustness tests (to be added)

## Adding Test Files

You can add your own FIT files here for testing. Common sources:

1. **Garmin Connect**: Export activities as FIT files
2. **Strava**: Download original FIT files from activities
3. **Garmin Devices**: Copy FIT files directly from device
4. **Sample Files**: Use FIT SDK sample files

## Usage

```bash
# Test with a fixture
./build/fit-parser tests/fixtures/valid.fit
```

## Note

Test fixtures are gitignored to avoid committing potentially large binary files.
You can add your own test files locally for development.
