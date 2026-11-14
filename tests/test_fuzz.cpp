// SPDX-License-Identifier: AGPL-3.0-or-later
// Fuzz through the ABI: random sounds over the full raw range of every element parameter (covered or
// not) and of the part's (bend range, velocity sensitivity), random regions, notes, controllers (mod
// wheel, sustain pedal, pitch bend), note volume and chokes, random block sizes, thousands of blocks. The output must stay finite and bounded, and
// process() must not allocate. (Run on each build: NEON, SSE2, scalar; see docs/cost.md.)
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include "host.h"

namespace {
struct Rng {
    uint64_t s;
    uint32_t next() { s = s * 6364136223846793005ull + 1442695040888963407ull; return uint32_t(s >> 33); }
    int in(int lo, int hi) { return lo + int(next() % uint32_t(hi - lo + 1)); }
    double unit() { return next() / 4294967296.0; }
};
struct P { const char* id; int lo, hi; };
const P kParams[] = {
#define X(e, id, lo, hi, def) {id, lo, hi},
#include "../core/params.inc"
#undef X
};

std::string randomSound(Rng& r) {
    std::string s = "oscairn-pcm-sound/0\npart bend_up=" + std::to_string(r.in(0, 24)) + " bend_down=" + std::to_string(-r.in(0, 48))
                  + " vel_depth=" + std::to_string(r.in(0, 127)) + " vel_offset=" + std::to_string(r.in(0, 127)) + "\nsample sine.wav\n";
    const int elements = r.in(1, 3);
    for (int e = 0; e < elements; ++e) {
        s += "element";
        for (const P& p : kParams) if (r.in(0, 2)) s += std::string(" ") + p.id + "=" + std::to_string(r.in(p.lo, p.hi));
        s += "\n";
    }
    const int regions = r.in(1, 6);
    for (int i = 0; i < regions; ++i) {
        const int lo = r.in(0, 127), hi = r.in(lo, 127), vlo = r.in(1, 127), vhi = r.in(vlo, 127);
        s += "region key=" + std::to_string(lo) + "-" + std::to_string(hi) + " vel=" + std::to_string(vlo) + "-" + std::to_string(vhi)
           + " root=" + std::to_string(r.in(0, 127)) + " transpose=" + std::to_string(r.in(-48, 48)) + " tune=" + std::to_string(r.in(-1200, 1200))
           + " level=" + std::to_string(r.in(-40, 6)) + " pan=" + std::to_string(r.in(-100, 100)) + " element=" + std::to_string(r.in(0, elements - 1)) + " sample=0";
        if (r.in(0, 3)) { const int a = r.in(0, 44000); s += " loop=" + std::to_string(a) + "-" + std::to_string(r.in(a + 1, 44100)); }
        s += "\n";
    }
    return s;
}
}  // namespace

TEST(fuzz_random_sounds_and_events) {
    Rng r{12345};
    long blocks = 0, notesStarted = 0, worstAlloc = 0;
    float worst = 0;
    bool finite = true;
    for (int round = 0; round < 24; ++round) {
        Inst a;
        char msg[256] = {};
        uint32_t ctx[3];
        for (int c = 0; c < 3; ++c) {
            const std::string snd = randomSound(r);
            ctx[c] = a.contexts->prepare(a.p, ("s" + std::to_string(c)).c_str(), nullptr, 0, patchWith("level=1 pan=0", snd.c_str()).c_str(), testBase(), msg, sizeof msg);
            CHECK(ctx[c] != 0);
            if (!ctx[c]) std::printf("  prepare failed: %s\n%s", msg, snd.c_str());
        }
        a.block(128);
        a.guard = allocGuardAvailable();
        for (int b = 0; b < 200; ++b, ++blocks) {
            const uint32_t frames = uint32_t(r.in(1, 128));
            std::vector<Bytes> ev;
            const int n = r.in(0, 6);
            for (int k = 0; k < n; ++k) {
                const uint32_t t = uint32_t(r.in(0, int(frames) - 1));
                const int16_t ch = int16_t(r.in(0, 1)), key = int16_t(r.in(0, 127));
                switch (r.in(0, 8)) {
                case 0: case 1: case 2: ev.push_back(on(t, r.in(1, 1000), ch, key, r.unit())); ++notesStarted; break;
                case 3: case 4: ev.push_back(off(t, -1, ch, key)); break;
                case 5: ev.push_back(midi(t, uint8_t(0xb0 | ch), r.in(0, 1) ? 1 : 64, uint8_t(r.in(0, 127)))); break;
                case 6: ev.push_back(midi(t, uint8_t(0xe0 | ch), uint8_t(r.in(0, 127)), uint8_t(r.in(0, 127)))); break;
                case 7: ev.push_back(volume(t, -1, ch, key, 4 * r.unit())); break;
                default: ev.push_back(choke(t, -1, ch, key)); break;
                }
            }
            // events must be in time order
            std::stable_sort(ev.begin(), ev.end(), [](const Bytes& x, const Bytes& y) {
                return reinterpret_cast<const clap_event_header_t*>(x.data())->time < reinterpret_cast<const clap_event_header_t*>(y.data())->time; });
            a.block(frames, ev);
            worstAlloc = std::max(worstAlloc, a.allocations);
        }
        for (float x : a.L) { if (!std::isfinite(x)) finite = false; worst = std::max(worst, std::fabs(x)); }
        for (float x : a.R) { if (!std::isfinite(x)) finite = false; worst = std::max(worst, std::fabs(x)); }
    }
    std::printf("  %ld blocks, %ld notes, peak %.3g, allocations in process %ld%s\n", blocks, notesStarted, double(worst), worstAlloc,
                allocGuardAvailable() ? "" : " (guard unavailable)");
    CHECK(finite);
    CHECK(worst < 64.0f);  // 64 voices of 8 layers at the largest gains stay far below
    CHECK(worstAlloc == 0);
}
