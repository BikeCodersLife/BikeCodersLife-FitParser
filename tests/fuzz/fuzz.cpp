// Mutation fuzzer for --strip-health and the plain parse mode (NOT run by default; see README.md).
//
//   fuzz <runs> <seed> <workdir> <seed1.fit> [seed2.fit ...]
//
// Each seed is cut at record boundaries, then mutated (bit flips, length fields,
// definition counts, architecture byte, header-type bits incl. compressed and
// developer flags, insert/delete, truncation, chained second file, trailing junk).
// 90% of the inputs get the header size and both CRCs fixed up, so the structure
// code behind the CRC check is reached. For every input:
//   1. HealthStripper::stripBytes must either throw or return bytes that an
//      independent checker accepts (only table messages/fields/events, no developer
//      flag, no compressed header, CRCs valid, SDK CheckIntegrity ok);
//   2. validateFitFile must either refuse or the vendor SDK parse (FitParser) must
//      run without a crash (it is built with ASAN/UBSAN, so memory errors abort).
// Exit code 1 when any output violates the table.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <fit_decode.hpp>

#include "fit_parser.h"
#include "fit_validator.h"
#include "health_stripper.h"

struct HealthKeepRule { uint16_t mesg; const uint8_t* fields; std::size_t count; };
#include "health_table.inc"

typedef std::string S;

static uint16_t crc16(const unsigned char* d, size_t n) {
    static const uint16_t t[16] = {0x0000, 0xCC01, 0xD801, 0x1400, 0xF001, 0x3C00, 0x2800, 0xE401, 0xA001, 0x6C00, 0x7800, 0xB401, 0x5000, 0x9C01, 0x8801, 0x4400};
    uint16_t crc = 0;
    for (size_t i = 0; i < n; i++) {
        uint16_t tmp = t[crc & 0xF]; crc = (crc >> 4) & 0x0FFF; crc = crc ^ tmp ^ t[d[i] & 0xF];
        tmp = t[crc & 0xF]; crc = (crc >> 4) & 0x0FFF; crc = crc ^ tmp ^ t[(d[i] >> 4) & 0xF];
    }
    return crc;
}

static std::map<int, std::set<int>> keep;
static std::set<int> keepEvents;

static S checkOne(const unsigned char* d, size_t n, size_t& used) {
    if (n < 14) return "short";
    size_t hdr = d[0];
    if (hdr != 14) return "header size";
    uint32_t ds = d[4] | d[5] << 8 | d[6] << 16 | (uint32_t)d[7] << 24;
    if (hdr + (size_t)ds + 2 > n) return "size";
    used = hdr + ds + 2;
    if (crc16(d, used - 2) != (uint16_t)(d[used - 2] | d[used - 1] << 8)) return "crc";
    if (crc16(d, 12) != (uint16_t)(d[12] | d[13] << 8)) return "header crc";
    struct D { bool v = false; int g = 0; std::vector<std::pair<int, int>> f; size_t sz = 0; } defs[16];
    size_t p = hdr, e = hdr + ds;
    while (p < e) {
        unsigned h = d[p++];
        if (h & 0x80) return "compressed header in output";
        int lt = h & 15;
        if (h & 0x40) {
            if (h & 0x20) return "developer flag in output";
            if (e - p < 5) return "truncated definition";
            int arch = d[p + 1];
            int g = arch ? (d[p + 2] << 8 | d[p + 3]) : (d[p + 2] | d[p + 3] << 8);
            int nf = d[p + 4]; p += 5;
            if (e - p < (size_t)nf * 3) return "truncated definition";
            auto it = keep.find(g);
            if (it == keep.end()) return "disallowed message " + std::to_string(g);
            D& x = defs[lt]; x = D(); x.v = true; x.g = g;
            for (int i = 0; i < nf; i++) {
                int fn = d[p + i * 3], sz = d[p + i * 3 + 1];
                if (!it->second.count(fn)) return "disallowed field " + std::to_string(g) + "." + std::to_string(fn);
                x.f.push_back({fn, sz}); x.sz += sz;
            }
            p += nf * 3;
        } else {
            D& x = defs[lt];
            if (!x.v) return "data without definition";
            if (e - p < x.sz) return "truncated data";
            if (x.g == 21) { size_t o = 0; for (auto& f : x.f) { if (f.first == 0 && (f.second != 1 || !keepEvents.count(d[p + o]))) return "disallowed event"; o += f.second; } }
            p += x.sz;
        }
    }
    if (p != e) return "record stream end";
    return "";
}

static S checkAll(const S& o) {
    if (o.empty()) return "empty output";
    size_t pos = 0;
    while (pos < o.size()) {
        size_t used = 0;
        S r = checkOne((const unsigned char*)o.data() + pos, o.size() - pos, used);
        if (!r.empty()) return r;
        pos += used;
    }
    std::istringstream is(o, std::ios::in | std::ios::binary);
    fit::Decode dec;
    if (!dec.CheckIntegrity(is)) return "SDK integrity check failed";
    return "";
}

static void fixup(S& b, std::mt19937& r) {
    if (b.size() < 14) return;
    size_t hdr = (unsigned char)b[0];
    if (hdr != 12 && hdr != 14) hdr = (r() & 1) ? 12 : 14;
    if (b.size() < hdr + 2) return;
    uint32_t ds = b.size() - hdr - 2;
    for (int i = 0; i < 4; i++) b[4 + i] = (ds >> (8 * i)) & 255;
    if (hdr == 14) { uint16_t c = crc16((unsigned char*)b.data(), 12); b[12] = c & 255; b[13] = c >> 8; }
    uint16_t c = crc16((unsigned char*)b.data(), b.size() - 2);
    b[b.size() - 2] = c & 255; b[b.size() - 1] = c >> 8;
}

static S readFile(const S& p) { std::ifstream f(p, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }

static S cutRecords(const S& b, int k) {
    size_t hdr = (unsigned char)b[0];
    uint32_t ds = (unsigned char)b[4] | (unsigned char)b[5] << 8 | (unsigned char)b[6] << 16 | (uint32_t)(unsigned char)b[7] << 24;
    size_t e = hdr + ds, p = hdr; struct D { size_t sz = 0; } defs[16]; int c = 0;
    while (p < e && c < k) {
        unsigned h = (unsigned char)b[p++];
        if (h & 0x80) { p += defs[(h >> 5) & 3].sz; c++; continue; }
        int lt = h & 15;
        if (h & 0x40) {
            int nf = (unsigned char)b[p + 4]; size_t sz = 0;
            for (int i = 0; i < nf; i++) sz += (unsigned char)b[p + 5 + i * 3 + 1];
            size_t q = p + 5 + nf * 3;
            if (h & 0x20) { int nd = (unsigned char)b[q++]; for (int i = 0; i < nd; i++) sz += (unsigned char)b[q + i * 3 + 1]; q += nd * 3; }
            defs[lt].sz = sz; p = q;
        } else p += defs[lt].sz;
        c++;
    }
    S o = b.substr(0, p); std::mt19937 z; fixup(o, z); return o;
}

int main(int argc, char** argv) {
    if (argc < 5) { std::cerr << "usage: fuzz <runs> <seed> <workdir> <seed.fit>...\n"; return 2; }
    for (auto& k : kKeepRules) for (size_t i = 0; i < k.count; i++) keep[k.mesg].insert(k.fields[i]);
    for (auto e : kKeepEvents) keepEvents.insert(e);
    int runs = atoi(argv[1]); unsigned seed = atoi(argv[2]); S work = argv[3];
    std::vector<S> seeds;
    for (int i = 4; i < argc; i++) {
        S b = readFile(argv[i]);
        if (b.size() < 14) continue;
        seeds.push_back(b);
        for (int k : {3, 8, 15, 40, 120, 400}) { S c = cutRecords(b, k); if (!c.empty()) seeds.push_back(c); }
    }
    std::mt19937 r(seed);
    long stripOk = 0, stripRefused = 0, bad = 0, slow = 0, parsed = 0, parseRefused = 0;
    std::map<std::string, int> reasons;
    for (int it = 0; it < runs; it++) {
        S b = seeds[r() % seeds.size()];
        int nm = 1 + r() % 2;
        for (int m = 0; m < nm && !b.empty(); m++) {
            size_t off = (r() % 3 == 0) ? r() % b.size() : r() % std::min<size_t>(b.size(), 700);
            switch (r() % 14) {
                case 0: b[off] ^= 1 << (r() % 8); break;
                case 1: b[off] = 0; break;
                case 2: b[off] = (char)255; break;
                case 3: b[off] = r(); break;
                case 4: if (off + 1 < b.size()) { b[off] = (char)255; b[off + 1] = (char)255; } break;
                case 5: if (off + 1 < b.size()) { b[off] = 0; b[off + 1] = 0; } break;
                case 6: b.insert(off, 1, (char)r()); break;
                case 7: b.erase(off, 1 + r() % 4); break;
                case 8: b.resize(off); break;
                case 9: b[off] = (char)(r() % 32); break;
                case 10: b[off] = (char)(0x40 | (r() & 0x3f)); break;
                case 11: b[off] = (char)(0x20 | 0x40 | (r() % 16)); break;
                case 12: b[off] = (char)(0x80 | (r() & 0x7f)); break;
                case 13: b[off] = (char)(r() % 2 ? 1 : r() % 40); break;
            }
        }
        int mode = r() % 10;
        if (mode < 9) fixup(b, r);
        if (r() % 10 == 0) b += S(r() % 20, (char)(r() % 2 ? 0 : r()));
        { std::ofstream(work + "/cur.fit", std::ios::binary) << b; } // last input, kept for a crash repro

        auto t0 = std::chrono::steady_clock::now();
        HealthStripStats st; S out; bool good = false;
        try { out = HealthStripper::stripBytes(b, st); good = true; }
        catch (const std::exception& e) { stripRefused++; reasons[e.what()]++; }
        double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (dt > 2) { slow++; std::cerr << "SLOW " << it << "\n"; }
        if (good) {
            stripOk++;
            S rr = checkAll(out);
            if (!rr.empty()) { bad++; std::cerr << "BAD " << it << " " << rr << "\n"; std::ofstream(work + "/bad" + std::to_string(it) + ".fit", std::ios::binary) << b; }
        }

        // plain parse mode: structural pre-check, then the vendor SDK (only reached when the check passes)
        std::string reason = validateFitFile(work + "/cur.fit");
        if (!reason.empty()) { parseRefused++; }
        else {
            try { FitParser p(work + "/cur.fit"); p.extractCoordinates(); } catch (const std::exception&) {}
            parsed++;
        }
    }
    std::cout << "runs=" << runs << " strip_ok=" << stripOk << " strip_refused=" << stripRefused << " violations=" << bad
              << " slow=" << slow << " parse_sdk_reached=" << parsed << " parse_refused_by_validator=" << parseRefused << "\n";
    for (auto& k : reasons) std::cout << "  " << k.second << " " << k.first << "\n";
    return bad ? 1 : 0;
}
