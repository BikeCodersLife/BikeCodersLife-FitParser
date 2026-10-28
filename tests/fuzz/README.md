# Fuzzing `--strip-health` and the parse mode

Not run by `tests/run_tests.sh` (minutes, needs sanitizers). Run before a release or after touching
`src/health_stripper.cpp` or `src/fit_validator.cpp`:

```bash
make -C build health-fit-tool        # seed generator
tests/fuzz/run.sh 7000 3             # 3 processes x 7000 mutated inputs, ASAN + UBSAN
```

`fuzz.cpp` mutates real fixtures and synthetic files (developer fields, compressed timestamps), fixes
the header size and both CRCs for 90% of the inputs so the code behind the CRC is reached, and checks:

1. `HealthStripper::stripBytes` throws or returns a FIT that an independent checker accepts (only table
   messages/fields/events, no developer flag, no compressed header, valid CRCs, SDK integrity check ok);
2. the plain parse mode: `validateFitFile` refuses, or the vendor SDK parse runs without a sanitizer error.

A crash saves the last input as `/tmp/fuzz-crash-<n>.fit`. (The CLI itself cannot run under ASAN because
`main.cpp` sets RLIMIT_AS, hence the in-process harness.)
