#!/bin/bash
# Build the fuzzer with ASAN+UBSAN in a scratch dir and run it. Usage: tests/fuzz/run.sh [runs-per-process] [processes]
# Not part of tests/run_tests.sh. Needs g++ with sanitizers; takes ~10 minutes (the vendor SDK compiles slowly with ASAN).
# -fno-sanitize=shift-base: the vendor SDK left-shifts negative values (harmless, reported on every valid file).
set -e
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$HERE/../.."
RUNS="${1:-7000}"
PROCS="${2:-3}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
SDK="$ROOT/third_party/fit-sdk/cpp"
g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize=shift-base -fno-omit-frame-pointer \
    -I"$SDK" -I"$ROOT/src" \
    "$HERE/fuzz.cpp" "$ROOT/src/health_stripper.cpp" "$ROOT/src/fit_validator.cpp" "$ROOT/src/fit_parser.cpp" "$SDK"/*.cpp \
    -o "$WORK/fuzz"
# seeds: the real fixtures plus synthetic files (developer fields, compressed timestamps, bench track)
BIN="${HEALTH_TOOL:-$ROOT/build/health-fit-tool}"
"$BIN" rich "$WORK/rich.fit"
python3 "$ROOT/tests/check_health_strip.py" mkraw compressed "$WORK/compressed.fit"
python3 "$ROOT/tests/check_health_strip.py" mkraw bench "$WORK/bench.fit" 300
python3 "$ROOT/tests/check_health_strip.py" mkraw unknown "$WORK/unknown.fit"
for i in $(seq 1 "$PROCS"); do
    mkdir "$WORK/p$i"
    (rc=0; ASAN_OPTIONS=halt_on_error=1:detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
        "$WORK/fuzz" "$RUNS" $((i * 101)) "$WORK/p$i" "$WORK"/*.fit "$ROOT"/tests/fixtures/*.fit > "$WORK/out$i.txt" 2> "$WORK/err$i.txt" || rc=$?; echo $rc > "$WORK/rc$i.txt") &
done
wait
status=0
for i in $(seq 1 "$PROCS"); do
    echo "--- process $i (exit $(cat "$WORK/rc$i.txt"))"; cat "$WORK/out$i.txt"; head -20 "$WORK/err$i.txt"
    [ "$(cat "$WORK/rc$i.txt")" = "0" ] || { status=1; cp "$WORK/p$i/cur.fit" "/tmp/fuzz-crash-$i.fit" 2>/dev/null && echo "last input saved as /tmp/fuzz-crash-$i.fit"; }
done
exit $status
