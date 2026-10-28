#!/usr/bin/env python3
"""Checks used by tests/run_tests.sh for fit-parser --strip-health.

  check_health_strip.py dump <dump.txt>              the dump of a stripped file obeys docs/health-strip-table.tsv
  check_health_strip.py track <in.json> <out.json> [--expect-health] [--identical]
        the JSON of the stripped file has the same track (and non-health summary) as the input
        and no heart rate / power / cadence anywhere
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TSV = os.path.join(ROOT, "docs", "health-strip-table.tsv")

HEALTH_COORD_KEYS = {"heartRate", "power", "cadence"}
# summary keys derived from heart rate, power or cadence
HEALTH_SUMMARY_MARKERS = ("heartrate", "power", "cadence", "coasting")


def load_table():
    keep_fields = {}
    keep_events = set()
    for line in open(TSV, encoding="utf-8"):
        if line.startswith("#") or not line.strip():
            continue
        kind, mnum, _mname, fnum, _fname, _units, dec, _reason = line.rstrip("\n").split("\t")
        if kind == "M" and dec == "K":
            keep_fields.setdefault(int(mnum), set())
        elif kind == "F" and dec == "K":
            keep_fields.setdefault(int(mnum), set()).add(int(fnum))
        elif kind == "E" and dec == "K":
            keep_events.add(int(fnum))
    return keep_fields, keep_events


def check_dump(path):
    keep_fields, keep_events = load_table()
    seen = set()
    n = 0
    for line in open(path, encoding="utf-8"):
        parts = line.split()
        if not parts or parts[0] != "mesg":
            continue
        n += 1
        num = int(parts[1])
        seen.add(num)
        if num not in keep_fields:
            sys.exit("message %d (%s) survived" % (num, parts[2]))
        dev_at = parts.index("dev")
        fields = {int(x) for x in parts[4:dev_at]}
        extra = fields - keep_fields[num]
        if extra:
            sys.exit("message %d kept removed fields %s" % (num, sorted(extra)))
        if int(parts[dev_at + 1]) != 0:
            sys.exit("message %d still has developer fields" % num)
        if num == 21:
            tail = parts[dev_at + 2:]
            ev = int(tail[tail.index("event") + 1]) if "event" in tail else None
            if ev is None or ev not in keep_events:
                sys.exit("event %s survived" % ev)
    if n == 0:
        sys.exit("empty dump")
    print("%d messages, %d kinds, all inside the table" % (n, len(seen)))


def strip_coord(c):
    return {k: v for k, v in c.items() if k not in HEALTH_COORD_KEYS}


def check_track(in_json, out_json, expect_health, identical):
    a = json.load(open(in_json))
    b = json.load(open(out_json))
    if len(a["coordinates"]) != len(b["coordinates"]):
        sys.exit("point count %d != %d" % (len(a["coordinates"]), len(b["coordinates"])))
    had_health = any(set(c) & HEALTH_COORD_KEYS for c in a["coordinates"])
    if expect_health and not had_health:
        sys.exit("input had no health values: the test would prove nothing")
    for i, (ca, cb) in enumerate(zip(a["coordinates"], b["coordinates"])):
        if set(cb) & HEALTH_COORD_KEYS:
            sys.exit("point %d still has %s" % (i, sorted(set(cb) & HEALTH_COORD_KEYS)))
        if strip_coord(ca) != cb:
            sys.exit("point %d differs: %s vs %s" % (i, strip_coord(ca), cb))
        if identical and ca != cb:
            sys.exit("point %d differs" % i)
    sa = a.get("summary", {})
    sb = b.get("summary", {})
    for k in sb:
        if any(m in k.lower() for m in HEALTH_SUMMARY_MARKERS):
            sys.exit("summary key %s survived" % k)
    for k, v in sa.items():
        if any(m in k.lower() for m in HEALTH_SUMMARY_MARKERS):
            continue
        if k in ("startTime", "endTime") and k not in sb:
            sys.exit("summary key %s lost" % k)
        if sb.get(k) != v:
            sys.exit("summary %s differs: %r vs %r" % (k, v, sb.get(k)))
    # everything else at the top level (device list, indoor flag, ...) is not health data
    for k in set(a) | set(b):
        if k in ("coordinates", "summary"):
            continue
        if a.get(k) != b.get(k):
            sys.exit("top-level %s differs" % k)
    print("%d points, track and summary equal" % len(a["coordinates"]))


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == "dump":
        check_dump(sys.argv[2])
    elif len(sys.argv) >= 4 and sys.argv[1] == "track":
        check_track(sys.argv[2], sys.argv[3], "--expect-health" in sys.argv, "--identical" in sys.argv)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
