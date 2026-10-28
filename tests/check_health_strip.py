#!/usr/bin/env python3
"""Checks used by tests/run_tests.sh for fit-parser --strip-health.

  check_health_strip.py dump <dump.txt>              the dump of a stripped file obeys docs/health-strip-table.tsv
  check_health_strip.py mkraw compressed|unknown|bench <out.fit> [records]
        hand-built FIT files (compressed timestamp headers, unknown message 65300 + unknown record field 200, big track)
  check_health_strip.py track <in.json> <out.json> [--expect-health] [--identical]
        the JSON of the stripped file has the same track (and non-health summary) as the input
        and no heart rate / power / cadence anywhere
"""
import json
import os
import struct
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


def crc16(data, crc=0):
    table = [0x0000, 0xCC01, 0xD801, 0x1400, 0xF001, 0x3C00, 0x2800, 0xE401,
             0xA001, 0x6C00, 0x7800, 0xB401, 0x5000, 0x9C01, 0x8801, 0x4400]
    for b in data:
        tmp = table[crc & 0xF]
        crc = (crc >> 4) & 0x0FFF
        crc = crc ^ tmp ^ table[b & 0xF]
        tmp = table[crc & 0xF]
        crc = (crc >> 4) & 0x0FFF
        crc = crc ^ tmp ^ table[(b >> 4) & 0xF]
    return crc


def frame(data):
    head = struct.pack("<BBHI4s", 14, 0x20, 2158, len(data), b".FIT")
    head += struct.pack("<H", crc16(head))
    body = head + data
    return body + struct.pack("<H", crc16(body))


def definition(local, glob, fields):
    d = bytes([0x40 | local, 0, 0]) + struct.pack("<H", glob) + bytes([len(fields)])
    for num, size, base in fields:
        d += bytes([num, size, base])
    return d


U8, U16, U32, S32 = 0x02, 0x84, 0x86, 0x85
FILE_ID = definition(0, 0, [(0, 1, 0x00), (1, 2, U16)]) + bytes([0, 4]) + struct.pack("<H", 255)
T0 = 1100000000


def make_raw(kind, out, n):
    if kind == "compressed":
        d = FILE_ID
        d += definition(1, 20, [(253, 4, U32), (0, 4, S32), (1, 4, S32), (3, 1, U8), (7, 2, U16)])
        d += bytes([1]) + struct.pack("<IiiBH", T0, 626000000, 60000000, 140, 200)
        d += definition(2, 20, [(0, 4, S32), (1, 4, S32), (3, 1, U8), (7, 2, U16)])
        # an hr message (dropped) carrying a timestamp between the records
        d += definition(3, 132, [(253, 4, U32), (0, 1, U8)]) + bytes([3]) + struct.pack("<IB", T0 + 3, 150)
        for i in range(1, 6):
            d += bytes([0x80 | (2 << 5) | (i + 3)]) + struct.pack("<iiBH", 626000000 + i * 1000, 60000000 + i * 1500, 140 + i, 200 + i)
    elif kind == "unknown":
        d = FILE_ID
        d += definition(1, 20, [(253, 4, U32), (0, 4, S32), (1, 4, S32), (3, 1, U8), (200, 1, U8)])
        d += definition(2, 65300, [(0, 1, U8), (1, 2, U16)])
        for i in range(4):
            d += bytes([1]) + struct.pack("<IiiBB", T0 + i, 626000000 + i * 1000, 60000000 + i * 1500, 140, 99)
            d += bytes([2]) + struct.pack("<BH", 7, 4242)
    elif kind == "bench":
        d = FILE_ID
        d += definition(1, 20, [(253, 4, U32), (0, 4, S32), (1, 4, S32), (2, 2, U16), (3, 1, U8), (7, 2, U16), (4, 1, U8), (5, 4, U32), (6, 2, U16)])
        rec = struct.Struct("<BIiiHBHBIH")
        d += b"".join(rec.pack(1, T0 + i, 626000000 + i * 100, 60000000 + i * 150, 1000 + i % 50, 120 + i % 60, 150 + i % 200, 90, i * 5, 7000) for i in range(n))
    else:
        sys.exit("unknown kind")
    with open(out, "wb") as f:
        f.write(frame(d))


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == "dump":
        check_dump(sys.argv[2])
    elif len(sys.argv) >= 4 and sys.argv[1] == "mkraw":
        make_raw(sys.argv[2], sys.argv[3], int(sys.argv[4]) if len(sys.argv) > 4 else 0)
    elif len(sys.argv) >= 4 and sys.argv[1] == "track":
        check_track(sys.argv[2], sys.argv[3], "--expect-health" in sys.argv, "--identical" in sys.argv)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
