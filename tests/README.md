# Test Framework

This directory contains the test framework for the FIT parser.

## Structure

```
tests/
├── fixtures/          # FIT test files (29 files, gitignored)
├── expected/          # Expected JSON outputs for validation
├── run_tests.sh       # Main test runner
└── generate_expected.sh  # Generate baseline expected outputs
```

## Running Tests

### Generate Expected Outputs (First Time)

```bash
./tests/generate_expected.sh
```

This will:
1. Parse all FIT files in `fixtures/`
2. Save the JSON output to `expected/`
3. These become the "known good" baseline for future tests

### Run Tests

```bash
# Run all tests
./tests/run_tests.sh

# Run with verbose output
./tests/run_tests.sh -v
```

The test runner will:
1. Parse each FIT file
2. Compare output with expected JSON
3. Report PASS/FAIL for each file

## Test Files

We have 29 real-world FIT files:
- **Wahoo ELEMNT** rides (outdoor cycling)
- **Zwift** virtual rides (indoor training)
- File sizes: 132KB - 631KB
- GPS points: ~3,000 - 15,000 per file

## Performance Benchmarks

Example from `2022-08-27-094047-ELEMNT BFF6-124-0.fit`:
- File size: 551KB
- GPS points: 13,160
- Parse time: **237ms**
- **55x faster than 1 second target!**

## Validation

Each expected output file contains:
- Array of coordinates (lat, lon, elevation, timestamp)
- Summary with point count
- Valid JSON format

## Adding New Tests

1. Add FIT file to `fixtures/`
2. Run `./tests/generate_expected.sh` to create expected output
3. Run `./tests/run_tests.sh` to verify

## CI Integration

The test suite can be integrated into GitHub Actions:

```yaml
- name: Run Tests
  run: |
    ./tests/generate_expected.sh
    ./tests/run_tests.sh
```
