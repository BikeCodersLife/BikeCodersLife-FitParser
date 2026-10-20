# Test Fixtures

Ride files that `tests/run_tests.sh` parses. They are committed: the suite
asserts on them (golden JSON in `tests/expected/`, or checks inside the script).

## Kinds

- **FIT** (`*.fit`): real device and Zwift files; each is compared with its
  `tests/expected/<name>.json`.
- **GPX / TCX** (`*.gpx`, `*.tcx`): every GPX is parsed, converted to FIT and
  read back (round trip); several have their own section in `run_tests.sh`:
  sensor fields (`with-power-and-hr.gpx`, `no-sensors-gps-only.gpx`,
  `tcx-with-sensors.tcx`, `tcx-no-sensors.tcx`), GPS spikes
  (`make_gps_spike_fixtures.py` builds those), the GlobalSat device max speed
  (`globalsat-catchup.gpx`), and indoor rides without position
  (`indoor-no-position.gpx` — GlobalSat writes every point at lat="0"
  lon="0"; `indoor-no-position.tcx` — no `<Position>` at all).

## Adding a fixture

Add the file here and, if the generic checks are not enough, a section in
`run_tests.sh`. A file the parser must reject does not belong here (the
generic GPX loop expects every `*.gpx` to parse): build it inside the test
instead, as the "no position and no time" case does.

## Running

```bash
./tests/run_tests.sh                       # against build/fit-parser
PARSER=/path/to/fit-parser ./tests/run_tests.sh   # against another build
```
