// SPDX-License-Identifier: AGPL-3.0-or-later
// Sound format v1 (v0 reads unchanged): the binary blob, its text form, validation, the expansion of
// waveforms into regions and the coverage report (docs/sound-format.md is the normative text).
//
// Blob (little-endian):
//   0  "OSMP"   4  u16 version (0, or 1 with waveforms)   6  u16 flags (0)   8  u32 size of the whole blob
//   12 u32 CRC-32 (IEEE) of bytes 16..size
//   16 u16 samples, u16 elements, u16 regions, u16 part parameters (kPartParamCount)
//   24 part parameters: i16 each, in partparams.inc order
//      samples: u8 length, then the reference's bytes (a relative path, no NUL)
//      elements: u16 count, then count x (u16 parameter index in params.inc order, i16 value),
//                indices increasing, only values that differ from the default
//      regions, 32 bytes each: u8 lokey hikey lovel hivel root, i8 transpose, u8 loop, u8 0,
//                u16 element, u16 sample, i32 tune (1/100 cent), i32 level (1/100 dB),
//                i16 pan (1/100), u16 0, u32 loop start, u32 loop end (exclusive)
//      version 1: u16 W (0-32), then W waveform ids of 32 bytes
// Parameter indices are stable: params.inc and partparams.inc only ever grow at their ends.
//
// Text form (tests and hand-written sounds), one item per line, "name=value" tokens:
//   oscairn-sampler-sound/0                               (or /1)
//   part bend_up=12                                   (values that differ from the default)
//   sample <reference>                                (numbered in order from 0)
//   waveform <64 hex digits>                          (version 1; numbered in order from 0)
//   element ampeg_attack=30 fil_type=0 ...            (numbered in order from 0)
//   region key=0-33 vel=1-127 root=30 tune=9.38 level=-7.12 pan=-100 element=0 sample=0 loop=544-93904
#include "pcore.h"
#include "measured.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace pcm {

const ParamDef kParamDefs[kParamCount] = {
#define X(e, id, lo, hi, def) {id, lo, hi, def},
#include "params.inc"
#undef X
};
const ParamDef kPartParamDefs[kPartParamCount] = {
#define X(e, id, lo, hi, def) {id, lo, hi, def},
#include "partparams.inc"
#undef X
};

namespace {
int find(const ParamDef* d, int n, const char* id, size_t len) {
    for (int i = 0; i < n; ++i) if (std::strlen(d[i].id) == len && !std::memcmp(d[i].id, id, len)) return i;
    return -1;
}
uint32_t crc32(const uint8_t* p, size_t n) {
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; ++i) { c ^= p[i]; for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1))); }
    return ~c;
}
void err(char* e, size_t cap, const char* fmt, long a = 0, long b = 0) { if (e && cap) std::snprintf(e, cap, fmt, a, b); }
// 64 lowercase hex digits: the 32 bytes of an id into out (when not null).
bool hexId(const char* s, uint8_t* out) {
    for (int k = 0; k < 64; ++k) {
        const char c = s[k];
        const int x = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (x < 0) return false;
        if (out) out[k / 2] = uint8_t(k % 2 ? out[k / 2] | x : x << 4);
    }
    return true;
}
// A sample reference: a relative path of printable characters, no "..", no leading '/'; in version 1 "sample:<id>".
bool goodRef(const char* s, size_t n, bool v1 = false) {
    if (v1) return n == 71 && !std::memcmp(s, "sample:", 7) && hexId(s + 7, nullptr);
    if (!n || n > kMaxRefBytes || s[0] == '/') return false;
    for (size_t i = 0; i < n; ++i) if ((unsigned char)s[i] < 0x20 || s[i] == 0x7f || s[i] == '\\') return false;
    for (size_t i = 0; i + 1 < n; ++i) if (s[i] == '.' && s[i + 1] == '.' && (i == 0 || s[i - 1] == '/') && (i + 2 == n || s[i + 2] == '/')) return false;
    return true;
}
const char* checkRegion(const Region& r, uint32_t elements, uint32_t samples) {
    if (r.loKey > r.hiKey || r.hiKey > 127) return "key range";
    if (r.loVel < 1 || r.loVel > r.hiVel || r.hiVel > 127) return "velocity range";
    if (r.root > 127) return "root";
    if (r.element >= elements) return "element";
    if (r.sample >= samples) return "sample";
    if (r.tune < -1200000 || r.tune > 1200000) return "tune";
    if (r.level < -14400 || r.level > 4800) return "level";
    if (r.pan < -10000 || r.pan > 10000) return "pan";
    if (r.loop > 1) return "loop mode";
    if (r.loop && r.loopStart >= r.loopEnd) return "loop";
    if (!r.loop && (r.loopStart || r.loopEnd)) return "loop points without a loop";
    return nullptr;
}
// What both readers refuse beyond each value's range: an element naming a waveform the sound lacks, or naming one and
// having regions too, or with inverted limits; a sound with nothing to play.
const char* checkSound(const SoundData& d) {
    if (!d.elements) return "a sound needs an element";
    bool plays = d.regions > 0;
    for (uint32_t e = 0; e < d.elements; ++e) {
        const int16_t* v = d.element[e].v;
        if (!v[Waveform]) continue;
        if (uint32_t(v[Waveform]) > d.waveforms) return "an element names a waveform the sound does not list";
        for (uint32_t i = 0; i < d.regions; ++i) if (d.region[i].element == e) return "an element has a waveform and regions";
        if (v[KeyLo] > v[KeyHi] || v[VelLo] > v[VelHi]) return "an element with a waveform has inverted key or velocity limits";
        plays = true;
    }
    return plays ? nullptr : "a sound needs a region or an element with a waveform";
}
// little-endian writer that only counts past the end
struct W8 {
    uint8_t* p; size_t cap, n = 0;
    void u8(uint32_t v) { if (n < cap) p[n] = uint8_t(v); ++n; }
    void u16(uint32_t v) { u8(v); u8(v >> 8); }
    void u32(uint32_t v) { u16(v); u16(v >> 16); }
};
struct R8 {
    const uint8_t* p; size_t n, at = 0; bool bad = false;
    uint32_t u8() { if (at >= n) { bad = true; return 0; } return p[at++]; }
    uint32_t u16() { uint32_t a = u8(); return a | u8() << 8; }
    uint32_t u32() { uint32_t a = u16(); return a | u16() << 16; }
};
}  // namespace

int findParam(const char* id, size_t len) { return find(kParamDefs, kParamCount, id, len); }
int findPartParam(const char* id, size_t len) { return find(kPartParamDefs, kPartParamCount, id, len); }

Element::Element() { for (int i = 0; i < kParamCount; ++i) v[i] = kParamDefs[i].def; }
SoundData::SoundData() {
    for (int i = 0; i < kPartParamCount; ++i) part[i] = kPartParamDefs[i].def;
    std::memset(refLength, 0, sizeof refLength);
    std::memset(ref, 0, sizeof ref);
    std::memset(waveform, 0, sizeof waveform);
}

bool operator==(const SoundData& a, const SoundData& b) {
    if (a.samples != b.samples || a.elements != b.elements || a.regions != b.regions || a.waveforms != b.waveforms) return false;
    if (std::memcmp(a.waveform, b.waveform, sizeof a.waveform[0] * a.waveforms)) return false;
    if (std::memcmp(a.part, b.part, sizeof a.part)) return false;
    for (uint32_t i = 0; i < a.samples; ++i) if (a.refLength[i] != b.refLength[i] || std::memcmp(a.ref[i], b.ref[i], a.refLength[i])) return false;
    for (uint32_t i = 0; i < a.elements; ++i) if (std::memcmp(a.element[i].v, b.element[i].v, sizeof a.element[i].v)) return false;
    for (uint32_t i = 0; i < a.regions; ++i) {
        const Region &x = a.region[i], &y = b.region[i];
        if (x.loKey != y.loKey || x.hiKey != y.hiKey || x.loVel != y.loVel || x.hiVel != y.hiVel || x.root != y.root || x.transpose != y.transpose
            || x.element != y.element || x.sample != y.sample || x.tune != y.tune || x.level != y.level || x.pan != y.pan || x.loop != y.loop
            || x.loopStart != y.loopStart || x.loopEnd != y.loopEnd || x.start != y.start || x.end != y.end) return false;
    }
    return true;
}

// ---- blob ----

size_t writeBlob(const SoundData& d, uint8_t* out, size_t cap) {
    W8 w{out, cap};
    w.u8('O'); w.u8('S'); w.u8('M'); w.u8('P');
    w.u16(d.waveforms ? 1 : 0); w.u16(0); w.u32(0); w.u32(0);  // version 0 when it has no waveform: v0 sounds keep their bytes
    w.u16(d.samples); w.u16(d.elements); w.u16(d.regions); w.u16(kPartParamCount);
    for (int i = 0; i < kPartParamCount; ++i) w.u16(uint16_t(d.part[i]));
    for (uint32_t i = 0; i < d.samples; ++i) { w.u8(d.refLength[i]); for (int k = 0; k < d.refLength[i]; ++k) w.u8(uint8_t(d.ref[i][k])); }
    for (uint32_t e = 0; e < d.elements; ++e) {
        uint32_t count = 0;
        for (int i = 0; i < kParamCount; ++i) count += d.element[e].v[i] != kParamDefs[i].def;
        w.u16(count);
        for (int i = 0; i < kParamCount; ++i) if (d.element[e].v[i] != kParamDefs[i].def) { w.u16(uint32_t(i)); w.u16(uint16_t(d.element[e].v[i])); }
    }
    for (uint32_t i = 0; i < d.regions; ++i) {
        const Region& r = d.region[i];
        w.u8(r.loKey); w.u8(r.hiKey); w.u8(r.loVel); w.u8(r.hiVel); w.u8(r.root); w.u8(uint8_t(r.transpose)); w.u8(r.loop); w.u8(0);
        w.u16(r.element); w.u16(r.sample); w.u32(uint32_t(r.tune)); w.u32(uint32_t(r.level)); w.u16(uint16_t(r.pan)); w.u16(0);
        w.u32(r.loopStart); w.u32(r.loopEnd);
    }
    if (d.waveforms) { w.u16(d.waveforms); for (uint32_t i = 0; i < d.waveforms; ++i) for (int k = 0; k < 32; ++k) w.u8(d.waveform[i][k]); }
    const size_t n = w.n;
    if (n <= cap) {
        W8 h{out + 8, 8}; h.u32(uint32_t(n)); h.u32(crc32(out + 16, n - 16));
    }
    return n;
}

bool readBlob(const uint8_t* blob, size_t size, SoundData& d, char* e, size_t cap) {
    d = SoundData{};
    if (!blob || size < 24 || std::memcmp(blob, "OSMP", 4)) return err(e, cap, "not a sound blob"), false;
    R8 r{blob, size, 4};
    const uint32_t version = r.u16(), flags = r.u16(), total = r.u32(), crc = r.u32();
    if (version > kFormatVersion) return err(e, cap, "sound format version %ld, this engine reads %ld", long(version), long(kFormatVersion)), false;
    if (flags) return err(e, cap, "unknown flags %ld", long(flags)), false;
    if (total != size) return err(e, cap, "blob size %ld, header says %ld", long(size), long(total)), false;
    if (crc32(blob + 16, size - 16) != crc) return err(e, cap, "checksum mismatch"), false;
    d.samples = r.u16(); d.elements = r.u16(); d.regions = r.u16();
    const uint32_t parts = r.u16();
    if (d.samples > kMaxSamples || d.elements > kMaxElements || d.regions > kMaxRegions) return err(e, cap, "too many samples, elements or regions"), false;
    if (version == 0 && (!d.elements || !d.regions || !d.samples)) return err(e, cap, "a sound needs a sample, an element and a region"), false;
    // a blob written before later part parameters were added has fewer: those keep their defaults
    if (parts > kPartParamCount) return err(e, cap, "%ld part parameters, this engine knows %ld", long(parts), long(kPartParamCount)), false;
    for (int i = 0; i < kPartParamCount; ++i) {
        if (uint32_t(i) >= parts) { d.part[i] = kPartParamDefs[i].def; continue; }
        const int v = int16_t(r.u16());
        if (v < kPartParamDefs[i].lo || v > kPartParamDefs[i].hi) return err(e, cap, "part parameter %ld out of range", i), false;
        d.part[i] = int16_t(v);
    }
    for (uint32_t i = 0; i < d.samples; ++i) {
        const uint32_t n = r.u8();
        for (uint32_t k = 0; k < n; ++k) d.ref[i][k] = char(r.u8());
        d.ref[i][n] = 0; d.refLength[i] = uint8_t(n);
        if (r.bad || !goodRef(d.ref[i], n, version == 1)) return err(e, cap, "sample %ld: bad reference", long(i)), false;
    }
    for (uint32_t el = 0; el < d.elements; ++el) {
        const uint32_t n = r.u16();
        int prev = -1;
        for (uint32_t k = 0; k < n; ++k) {
            const int id = int(r.u16()), v = int16_t(r.u16());
            if (r.bad || id <= prev || id >= kParamCount) return err(e, cap, "element %ld: bad parameter index", long(el)), false;
            if (v < kParamDefs[id].lo || v > kParamDefs[id].hi) return err(e, cap, "element %ld: parameter %ld out of range", long(el), id), false;
            d.element[el].v[id] = int16_t(v); prev = id;
        }
    }
    for (uint32_t i = 0; i < d.regions; ++i) {
        Region& g = d.region[i];
        g.loKey = uint8_t(r.u8()); g.hiKey = uint8_t(r.u8()); g.loVel = uint8_t(r.u8()); g.hiVel = uint8_t(r.u8()); g.root = uint8_t(r.u8());
        g.transpose = int8_t(r.u8()); g.loop = uint8_t(r.u8());
        const uint32_t z1 = r.u8();
        g.element = uint16_t(r.u16()); g.sample = uint16_t(r.u16()); g.tune = int32_t(r.u32()); g.level = int32_t(r.u32()); g.pan = int16_t(r.u16());
        const uint32_t z2 = r.u16();
        g.loopStart = r.u32(); g.loopEnd = r.u32();
        if (r.bad) return err(e, cap, "truncated"), false;
        const char* why = checkRegion(g, d.elements, d.samples);
        if (z1 || z2) why = "reserved bytes";
        if (why) { if (e && cap) std::snprintf(e, cap, "region %u: bad %s", i, why); return false; }
    }
    if (version == 1) {
        d.waveforms = r.u16();
        if (d.waveforms > kMaxWaveforms) return err(e, cap, "too many waveforms"), false;
        for (uint32_t i = 0; i < d.waveforms; ++i) for (int k = 0; k < 32; ++k) d.waveform[i][k] = uint8_t(r.u8());
        if (r.bad) return err(e, cap, "truncated"), false;
    }
    if (r.at != size) return err(e, cap, "%ld trailing bytes", long(size - r.at)), false;
    if (const char* why = checkSound(d)) { if (e && cap) std::snprintf(e, cap, "%s", why); return false; }
    return true;
}

// ---- text (no heap: tokens are bounded, the writer counts past the end) ----

namespace {
struct Tok { const char* p; size_t n; bool is(const char* w) const { return std::strlen(w) == n && !std::memcmp(p, w, n); } };
// "-12.34" -> -1234: at most two decimals
bool fixed2(Tok t, int64_t& out) {
    size_t i = 0; bool neg = false;
    if (i < t.n && (t.p[i] == '-' || t.p[i] == '+')) neg = t.p[i++] == '-';
    int64_t v = 0; int digits = 0, frac = -1;
    for (; i < t.n; ++i) {
        if (t.p[i] == '.' && frac < 0) { frac = 0; continue; }
        if (t.p[i] < '0' || t.p[i] > '9') return false;
        if (frac >= 0 && ++frac > 2) return false;
        v = v * 10 + (t.p[i] - '0'); ++digits;
        if (v > (int64_t(1) << 40)) return false;
    }
    if (!digits) return false;
    for (int k = frac < 0 ? 0 : frac; k < 2; ++k) v *= 10;
    out = neg ? -v : v;
    return true;
}
bool integer(Tok t, long& out) {
    int64_t f;
    for (size_t i = 0; i < t.n; ++i) if (t.p[i] == '.') return false;
    if (!fixed2(t, f)) return false;
    out = long(f / 100);
    return true;
}
bool range(Tok t, long& a, long& b) {
    for (size_t i = 1; i < t.n; ++i)
        if (t.p[i] == '-') return integer({t.p, i}, a) && integer({t.p + i + 1, t.n - i - 1}, b);
    if (!integer(t, a)) return false;
    b = a;
    return true;
}
struct Out {
    char* p; size_t cap, n = 0;
    void put(const char* s, size_t k) { for (size_t i = 0; i < k; ++i) { if (n < cap) p[n] = s[i]; ++n; } }
    void put(const char* s) { put(s, std::strlen(s)); }
    void num(long v) { char b[24]; std::snprintf(b, sizeof b, "%ld", v); put(b); }
    void fix(int64_t v) {
        char b[32];
        const long long a = v < 0 ? -v : v;
        if (a % 100 == 0) std::snprintf(b, sizeof b, "%s%lld", v < 0 ? "-" : "", a / 100);
        else if (a % 10 == 0) std::snprintf(b, sizeof b, "%s%lld.%lld", v < 0 ? "-" : "", a / 100, a % 100 / 10);
        else std::snprintf(b, sizeof b, "%s%lld.%02lld", v < 0 ? "-" : "", a / 100, a % 100);
        put(b);
    }
};
}  // namespace

bool readText(const char* text, SoundData& d, char* e, size_t cap) {
    d = SoundData{};
    if (!text) return err(e, cap, "line 1: no sound"), false;
    const char* p = text;
    int line = 0;
    bool header = false, v1 = false;
    constexpr int kMaxTokens = 2 * kParamCount + 8;
    Tok tok[kMaxTokens];
    while (*p) {
        const char* eol = p;
        while (*eol && *eol != '\n') ++eol;
        const char* end = eol;
        const char* next = *eol ? eol + 1 : eol;
        ++line;
        if (end > p && end[-1] == '\r') --end;
        int nt = 0;
        for (const char* q = p; q < end;) {
            while (q < end && (*q == ' ' || *q == '\t')) ++q;
            if (q == end) break;
            const char* t = q;
            while (q < end && *q != ' ' && *q != '\t') ++q;
            if (nt == kMaxTokens) { if (e && cap) std::snprintf(e, cap, "line %d: too many tokens", line); return false; }
            tok[nt++] = {t, size_t(q - t)};
        }
        const char* lineStart = p;
        p = next;
        if (!nt || tok[0].p[0] == '#') continue;
        auto bad = [&](const char* what, Tok t) { if (e && cap) std::snprintf(e, cap, "line %d: %s '%.*s'", line, what, int(std::min<size_t>(t.n, 200)), t.p); return false; };
        const Tok kind = tok[0];
        if (!header) {
            if (!(kind.is("oscairn-sampler-sound/0") || kind.is("oscairn-sampler-sound/1")) || nt != 1) return bad("expected 'oscairn-sampler-sound/0' or '/1', not", kind);
            header = true;
            v1 = kind.is("oscairn-sampler-sound/1");
            continue;
        }
        if (kind.is("waveform") && v1) {
            if (d.waveforms == kMaxWaveforms) return bad("too many waveforms at", kind);
            if (nt != 2 || tok[1].n != 64) return bad("expected 64 hex digits after", kind);
            if (!hexId(tok[1].p, d.waveform[d.waveforms])) return bad("bad waveform id", tok[1]);
            ++d.waveforms;
            continue;
        }
        if (kind.is("sample")) {
            if (d.samples == kMaxSamples) return bad("too many samples at", kind);
            if (nt < 2) return bad("no reference after", kind);
            const Tok ref{tok[1].p, size_t(end - tok[1].p)};  // the rest of the line (a path may hold spaces)
            if (!goodRef(ref.p, ref.n, v1)) return bad("bad sample reference", ref);
            std::memcpy(d.ref[d.samples], ref.p, ref.n); d.ref[d.samples][ref.n] = 0;
            d.refLength[d.samples++] = uint8_t(ref.n);
            (void)lineStart;
            continue;
        }
        if (kind.is("part") || kind.is("element")) {
            const bool part = kind.is("part");
            if (!part && d.elements == kMaxElements) return bad("too many elements at", kind);
            Element& el = d.element[part ? 0 : d.elements];
            if (!part) el = Element{};
            for (int k = 1; k < nt; ++k) {
                const char* eq = static_cast<const char*>(std::memchr(tok[k].p, '=', tok[k].n));
                if (!eq) return bad("expected name=value, not", tok[k]);
                const size_t ln = size_t(eq - tok[k].p);
                const int id = part ? findPartParam(tok[k].p, ln) : findParam(tok[k].p, ln);
                if (id < 0) return bad("unknown parameter", tok[k]);
                const ParamDef& def = part ? kPartParamDefs[id] : kParamDefs[id];
                long v;
                if (!integer({eq + 1, tok[k].n - ln - 1}, v) || v < def.lo || v > def.hi) return bad("bad value in", tok[k]);
                (part ? d.part[id] : el.v[id]) = int16_t(v);
            }
            if (!part) ++d.elements;
            continue;
        }
        if (kind.is("region")) {
            if (d.regions == kMaxRegions) return bad("too many regions at", kind);
            Region g;
            bool haveSample = false, haveElement = false;
            for (int k = 1; k < nt; ++k) {
                const char* eq = static_cast<const char*>(std::memchr(tok[k].p, '=', tok[k].n));
                if (!eq) return bad("expected name=value, not", tok[k]);
                const Tok n{tok[k].p, size_t(eq - tok[k].p)}, v{eq + 1, tok[k].n - n.n - 1};
                long a, b; int64_t f;
                if (n.is("key") && range(v, a, b) && a >= 0 && b <= 127 && a <= b) { g.loKey = uint8_t(a); g.hiKey = uint8_t(b); }
                else if (n.is("vel") && range(v, a, b) && a >= 1 && b <= 127 && a <= b) { g.loVel = uint8_t(a); g.hiVel = uint8_t(b); }
                else if (n.is("root") && integer(v, a) && a >= 0 && a <= 127) g.root = uint8_t(a);
                else if (n.is("transpose") && integer(v, a) && a >= -127 && a <= 127) g.transpose = int8_t(a);
                else if (n.is("tune") && fixed2(v, f) && f >= -1200000 && f <= 1200000) g.tune = int32_t(f);
                else if (n.is("level") && fixed2(v, f) && f >= -14400 && f <= 4800) g.level = int32_t(f);
                else if (n.is("pan") && fixed2(v, f) && f >= -10000 && f <= 10000) g.pan = int16_t(f);
                else if (n.is("element") && integer(v, a) && a >= 0 && a < long(kMaxElements)) { g.element = uint16_t(a); haveElement = true; }
                else if (n.is("sample") && integer(v, a) && a >= 0 && a < long(kMaxSamples)) { g.sample = uint16_t(a); haveSample = true; }
                else if (n.is("loop") && range(v, a, b) && a >= 0 && a < b) { g.loop = 1; g.loopStart = uint32_t(a); g.loopEnd = uint32_t(b); }
                else return bad("bad region token", tok[k]);
            }
            if (!haveSample || !haveElement) return bad("a region needs element= and sample=", kind);
            d.region[d.regions++] = g;
            continue;
        }
        return bad("unknown line", kind);
    }
    if (!header) return err(e, cap, "line 1: expected 'oscairn-sampler-sound/0'"), false;
    if (!v1 && (!d.samples || !d.elements || !d.regions)) return err(e, cap, "line %ld: a sound needs a sample, an element and a region", line), false;
    if (const char* why = checkSound(d)) { if (e && cap) std::snprintf(e, cap, "line %d: %s", line, why); return false; }
    for (uint32_t i = 0; i < d.regions; ++i)
        if (const char* why = checkRegion(d.region[i], d.elements, d.samples)) {
            if (e && cap) std::snprintf(e, cap, "line %d: region %u: bad %s", line, i, why);
            return false;
        }
    return true;
}

size_t writeText(const SoundData& d, char* out, size_t cap) {
    Out o{out, cap};
    o.put(d.waveforms ? "oscairn-sampler-sound/1\n" : "oscairn-sampler-sound/0\n");
    bool part = false;
    for (int i = 0; i < kPartParamCount; ++i)
        if (d.part[i] != kPartParamDefs[i].def) { o.put(part ? " " : "part "); o.put(kPartParamDefs[i].id); o.put("="); o.num(d.part[i]); part = true; }
    if (part) o.put("\n");
    for (uint32_t i = 0; i < d.samples; ++i) { o.put("sample "); o.put(d.ref[i], d.refLength[i]); o.put("\n"); }
    for (uint32_t i = 0; i < d.waveforms; ++i) {
        char h[66];
        for (int k = 0; k < 32; ++k) std::snprintf(h + 2 * k, 3, "%02x", d.waveform[i][k]);
        o.put("waveform "); o.put(h); o.put("\n");
    }
    for (uint32_t e = 0; e < d.elements; ++e) {
        o.put("element");
        for (int i = 0; i < kParamCount; ++i)
            if (d.element[e].v[i] != kParamDefs[i].def) { o.put(" "); o.put(kParamDefs[i].id); o.put("="); o.num(d.element[e].v[i]); }
        o.put("\n");
    }
    for (uint32_t i = 0; i < d.regions; ++i) {
        const Region& r = d.region[i];
        o.put("region key="); o.num(r.loKey); o.put("-"); o.num(r.hiKey); o.put(" vel="); o.num(r.loVel); o.put("-"); o.num(r.hiVel);
        o.put(" root="); o.num(r.root);
        if (r.transpose) { o.put(" transpose="); o.num(r.transpose); }
        if (r.tune) { o.put(" tune="); o.fix(r.tune); }
        if (r.level) { o.put(" level="); o.fix(r.level); }
        if (r.pan) { o.put(" pan="); o.fix(r.pan); }
        o.put(" element="); o.num(r.element); o.put(" sample="); o.num(r.sample);
        if (r.loop) { o.put(" loop="); o.num(long(r.loopStart)); o.put("-"); o.num(long(r.loopEnd)); }
        o.put("\n");
    }
    if (o.n < cap) out[o.n] = 0;
    return o.n;
}

// ---- expansion: each element's waveform into regions ----

bool expand(const SoundData& in, const WaveformData* w, SoundData& d, char* e, size_t cap) {
    d = in;
    for (uint32_t el = 0; el < d.elements; ++el) {
        const int16_t* v = d.element[el].v;
        if (!v[Waveform]) continue;
        const WaveformData& f = w[v[Waveform] - 1];
        const int c = v[Coarse];
        const int32_t fine = int32_t(v[Fine] * 100 / 64) * 100;  // s x 100 / 64 cents toward zero, in 1/100 cent
        int lowest = 127, highest = 0;  // the waveform's ends: every bank there reaches key 0 (127)
        for (uint32_t b = 0; b < f.count; ++b) { lowest = std::min<int>(lowest, f.banks[b].keyLo); highest = std::max<int>(highest, f.banks[b].keyHi); }
        for (uint32_t b = 0; b < f.count; ++b) {
            const KeyBank& k = f.banks[b];
            // the reference picks the sample by key plus coarse tune: the bank serves keys lo - c..hi - c
            const int lo = std::max({k.keyLo == lowest ? 0 : k.keyLo - c, 0, int(v[KeyLo])}), hi = std::min({k.keyHi == highest ? 127 : k.keyHi - c, 127, int(v[KeyHi])});
            const int vlo = std::max<int>(k.velLo, v[VelLo]), vhi = std::min<int>(k.velHi, v[VelHi]);
            if (lo > hi || vlo > vhi) continue;
            if (d.regions == kMaxRegions) return err(e, cap, "the waveforms expand to more than %ld regions", long(kMaxRegions)), false;
            if (k.sample >= f.sampleCount) return err(e, cap, "a key bank names a sample its waveform lacks"), false;
            // its sample among the sound's references
            const char* ref = f.samples[k.sample];
            const size_t n = std::strlen(ref);
            uint32_t s = 0;
            while (s < d.samples && !(d.refLength[s] == n && !std::memcmp(d.ref[s], ref, n))) ++s;
            if (s == d.samples) {
                if (d.samples == kMaxSamples || !goodRef(ref, n)) return err(e, cap, "the waveforms use more than %ld samples", long(kMaxSamples)), false;
                std::memcpy(d.ref[s], ref, n + 1); d.refLength[s] = uint8_t(n); ++d.samples;
            }
            Region g;
            g.loKey = uint8_t(lo); g.hiKey = uint8_t(hi); g.loVel = uint8_t(vlo); g.hiVel = uint8_t(vhi);
            g.root = k.root;  // the coarse tune joins the transpose: the same pitch as root - c, which may lie outside 0-127
            const int t = k.transpose + c;
            g.element = uint16_t(el); g.sample = uint16_t(s);
            g.tune = k.tune + fine; g.level = k.level; g.pan = k.pan;
            // loop points from the region's start; a file passed samplepack's checks has start <= loop start < loop end <= end
            if ((k.end && k.end <= k.start) || (k.loop && (k.loopStart < k.start || (k.end && k.loopEnd > k.end))))
                return err(e, cap, "element %ld: a key bank's frames are out of order", long(el)), false;
            g.loop = k.loop; g.loopStart = k.loop ? k.loopStart - k.start : 0; g.loopEnd = k.loop ? k.loopEnd - k.start : 0;
            g.start = k.start; g.end = k.end;
            if (t < -127 || t > 127 || checkRegion(g, d.elements, d.samples)) return err(e, cap, "element %ld: a key bank's tuning is out of range", long(el)), false;
            g.transpose = int8_t(t);
            d.region[d.regions++] = g;
        }
    }
    return true;
}

// ---- coverage ----

uint32_t notCovered(const SoundData& d, Gap* out, uint32_t cap) {
    uint32_t n = 0;
    auto add = [&](uint16_t el, int16_t param, const char* why, bool part = false) { if (n < cap && out) out[n] = {el, param, why, part}; ++n; };
    if (d.part[PartMono]) add(0, PartMono, "mono part: element selection and legato are not built", true);
    // a note starts at most kMaxLayers regions (overlapping key banks of a waveform each start one)
    uint32_t most = 0;
    for (int key = 0; key < 128 && most <= kMaxLayers; ++key)
        for (int vel = 1; vel < 128; ++vel) {
            uint32_t n = 0;
            for (uint32_t i = 0; i < d.regions; ++i) { const Region& r = d.region[i]; n += key >= r.loKey && key <= r.hiKey && vel >= r.loVel && vel <= r.hiVel; }
            most = std::max(most, n);
        }
    if (most > kMaxLayers) add(0, -1, "a note starts more regions than a note may (the rest do not sound)");
    if (d.part[PartPortamento]) add(0, PartPortamento, "portamento is not built", true);
    // controller boxes: the destinations, sources and curves measured (laws::boxDest, boxCurve, controlSource); the foot
    // switch, sequenced and envelope-follower sources are not built, channel pressure's map is not measured
    for (int k = 0; k < 16; ++k) {
        const int16_t* b = d.part + PartBox1Src + 8 * k;
        if (!b[1] || !b[2]) continue;
        const int base = PartBox1Src + 8 * k;
        if (laws::boxDest(b[1]) < 0) add(0, int16_t(base + 1), "a controller box destination this engine does not move yet", true);
        else if (!((b[0] >= 0 && b[0] <= 4) || (b[0] >= 6 && b[0] <= 17))) add(0, int16_t(base), "a controller box source not built (foot switch, sequenced, envelope followers)", true);
        else if (b[0] == 2) add(0, int16_t(base), "channel pressure as a box source: its map is not measured", true);
        else if (laws::boxCurve(b[4], b[5], b[6], b[7]) < 0) add(0, int16_t(base + 5), "a controller box curve not measured", true);
        else if (b[1] == 65 && b[2] < 0) add(0, int16_t(base + 2), "a negative ratio on the element LFO's amplitude depth is not measured", true);
    }
    // the part LFO: built for the level, pitch, cutoff and pan destinations (laws::lfoDest), every wave, the speeds, start
    // phases, key-on resets, delay, fade in, hold, fade out and the slots' element switches; not its tempo sync or random speed
    bool lfo = false, box57 = false;  // (a box on slot 1's depth, destination 57, makes the slot move)
    for (int k = 0; k < 16; ++k) box57 |= d.part[PartBox1Dst + 8 * k] == 57 && d.part[PartBox1Ratio + 8 * k] != 0;
    for (int k = 0; k < 3; ++k) {
        const int dst = d.part[PartLfoDst1 + 2 * k];
        if (!d.part[PartLfoDepth1 + 2 * k] && !(k == 0 && box57)) continue;
        lfo = true;
        if (laws::lfoDest(dst) < 0) add(0, int16_t(PartLfoDst1 + 2 * k), "a part LFO destination this engine does not move yet", true);
        for (uint32_t e = 0; e < d.elements; ++e)
            if (d.element[e].v[PlfoRatio1 + k] != 127 && dst != 64 && dst != 65 && dst != 66)
                add(uint16_t(e), int16_t(PlfoRatio1 + k), "a part LFO depth ratio on the pan or resonance is not measured");
    }
    if (lfo) {
        if (d.part[PartLfoTempoSync]) add(0, PartLfoTempoSync, "a part LFO synced to the tempo is not built", true);
        if (d.part[PartLfoRandomSpeed]) add(0, PartLfoRandomSpeed, "the part LFO's random speed is not built", true);
        if (d.elements > 1 && d.part[PartLfoKeyOnReset] == 0)  // (no effect measured with each-on and first-on resets)
            for (uint32_t e = 0; e < d.elements; ++e)
                if (d.element[e].v[PlfoPhaseOffset]) add(uint16_t(e), PlfoPhaseOffset, "the part LFO's phase offset between elements with reset off is not measured");
    }
    for (uint32_t e = 0; e < d.elements; ++e) {
        const int16_t* v = d.element[e].v;
        const int type = v[FilType];
        if (type != 18 && filterOf(type) == Filter::Thru)
            add(uint16_t(e), FilType, "filter types other than 0-7, 10 and 18 (thru)");
        if (v[ElfoMode] == 1) add(uint16_t(e), ElfoMode, "the element LFO's own settings (settings mode) are not built");
        if (v[ElfoMode] == 0 && v[ElfoAmpRatio] < 0) add(uint16_t(e), ElfoAmpRatio, "a negative element LFO amplitude ratio is not measured");
        if (v[TriggerMode] == 1 && d.part[PartMono]) add(uint16_t(e), TriggerMode, "legato elements in a mono part are not built");
        if (v[KeyOnDelayNote]) add(uint16_t(e), KeyOnDelayNote, "a key-on delay of a note length at the tempo is not built");
    }
    return n;
}

}  // namespace pcm
