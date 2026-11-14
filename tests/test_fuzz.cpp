// SPDX-License-Identifier: AGPL-3.0-or-later
// Fuzz through the ABI: random sounds over the full raw range of every element parameter (covered or
// not) and of the part's (bend range, velocity sensitivity, controller boxes, knob values), random regions, notes, controllers (mod
// wheel, breath, foot, sustain pedal, channel pressure, pitch bend), note volume and chokes, every shell parameter's value
// (knobs, ribbon, switches, box destinations) and the destinations' modulation, random block sizes, thousands of blocks. The output must stay finite and bounded, and
// process() must not allocate. (Run on each build: NEON, SSE2, scalar; see docs/cost.md.)
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
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
                  + " vel_depth=" + std::to_string(r.in(0, 127)) + " vel_offset=" + std::to_string(r.in(0, 127))
                  + " split_outputs=" + std::to_string(r.in(0, 1)) + " part_cutoff=" + std::to_string(r.in(-64, 63)) + " part_resonance=" + std::to_string(r.in(-64, 63));
    for (int k = 1; k <= 8; ++k) s += " knob" + std::to_string(k) + "=" + std::to_string(r.in(0, 1023));
    // controller boxes: four at random over every part parameter's range, built destinations and sources likelier
    static const int dsts[11] = {85, 86, 60, 67, 68, 87, 79, 64, 65, 75, 78}, srcs[16] = {0, 1, 3, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17};
    for (int k = 1; k <= 4; ++k) {
        const std::string b = " box" + std::to_string(k) + "_";
        s += b + "src=" + std::to_string(r.in(0, 1) ? srcs[r.in(0, 15)] : r.in(0, 127)) + b + "dst=" + std::to_string(r.in(0, 2) ? dsts[r.in(0, 10)] : r.in(0, 413))
           + b + "ratio=" + std::to_string(r.in(-128, 127)) + b + "pol=" + std::to_string(r.in(0, 1)) + b + "bank=" + std::to_string(r.in(0, 1) ? 0 : r.in(0, 25))
           + b + "curve=" + std::to_string(r.in(0, 1) ? 0 : r.in(0, 31)) + b + "p1=" + std::to_string(r.in(0, 127)) + b + "p2=" + std::to_string(r.in(0, 127));
    }
    // the part LFO: every setting at random, its built destinations likelier
    s += " plfo_wave=" + std::to_string(r.in(0, 12)) + " plfo_speed=" + std::to_string(r.in(0, 63)) + " plfo_phase=" + std::to_string(r.in(0, 5))
       + " plfo_keyon_reset=" + std::to_string(r.in(0, 2)) + " plfo_delay=" + std::to_string(r.in(0, 1) ? 0 : r.in(0, 127)) + " plfo_fade_in=" + std::to_string(r.in(0, 1) ? 0 : r.in(0, 127))
       + " plfo_hold=" + std::to_string(r.in(0, 1) ? 127 : r.in(0, 127)) + " plfo_fade_out=" + std::to_string(r.in(0, 127)) + " plfo_user_cycle=" + std::to_string(r.in(0, 6)) + " plfo_user_slope=" + std::to_string(r.in(0, 3));
    for (int k = 1; k <= 3; ++k)
        s += " plfo_dst" + std::to_string(k) + "=" + std::to_string(r.in(0, 2) ? r.in(64, 68) : r.in(0, 127)) + " plfo_depth" + std::to_string(k) + "=" + std::to_string(r.in(0, 1) ? r.in(0, 127) : 0);
    for (int k = 1; k <= 16; ++k) s += " plfo_user_step" + std::to_string(k) + "=" + std::to_string(r.in(-64, 63));
    s += "\nsample sine.wav\n";
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
        a.outputs = round % 2 ? 3 : 1;  // the host gives the three output ports, or the main one only
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
                switch (r.in(0, 10)) {
                case 0: case 1: case 2: ev.push_back(on(t, r.in(1, 1000), ch, key, r.unit())); ++notesStarted; break;
                case 3: case 4: ev.push_back(off(t, -1, ch, key)); break;
                case 5: { static const uint8_t ccs[5] = {1, 64, 2, 11, 4};
                          if (r.in(0, 5)) ev.push_back(midi(t, uint8_t(0xb0 | ch), ccs[r.in(0, 4)], uint8_t(r.in(0, 127))));
                          else ev.push_back(midi(t, uint8_t(0xd0 | ch), uint8_t(r.in(0, 127)), 0)); } break;
                case 6: ev.push_back(midi(t, uint8_t(0xe0 | ch), uint8_t(r.in(0, 127)), uint8_t(r.in(0, 127)))); break;
                case 7: ev.push_back(volume(t, -1, ch, key, 4 * r.unit())); break;
                case 9: { clap_param_info_t info{}; const clap_id id = clap_id(r.in(3, int(a.params->count(a.p)) - 1));  // a shell value over its range, a little beyond
                          a.params->get_info(a.p, id, &info);
                          ev.push_back(param(t, id, info.min_value + (info.max_value - info.min_value) * (1.2 * r.unit() - 0.1), r.in(0, 2) ? ch : -1)); } break;
                case 10: { clap_param_info_t info{}; const clap_id id = clap_id(r.in(14, int(a.params->count(a.p)) - 1));  // a destination's modulation, to twice its span
                           a.params->get_info(a.p, id, &info);
                           ev.push_back(mod(t, id, info.max_value * (4 * r.unit() - 2), r.in(0, 2) ? ch : -1)); } break;
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
        for (int k = 0; k < 2; ++k)
            for (const auto* port : {&a.PL[k], &a.PR[k]})
                for (float x : *port) { if (!std::isfinite(x)) finite = false; worst = std::max(worst, std::fabs(x)); }
    }
    std::printf("  %ld blocks, %ld notes, peak %.3g, allocations in process %ld%s\n", blocks, notesStarted, double(worst), worstAlloc,
                allocGuardAvailable() ? "" : " (guard unavailable)");
    CHECK(finite);
    CHECK(worst < 64.0f);  // 64 voices of 8 layers at the largest gains stay far below
    CHECK(worstAlloc == 0);
}

// Every note ends: random sounds (their releases and half damper times at most index 90, no time shifts, so the longest
// release falls to the voice end within about 7.2 s; the sustain pedal moves at random among the events), random notes, controllers and chokes; then every key up, the pedal up and
// 12 s: no voice remains and each note-on got exactly one NOTE_END (refused ones included).
TEST(fuzz_every_note_ends) {
    Rng r{777};
    auto bounded = [](std::string s) {
        struct { const char* id; int most; } caps[] = {{" ampeg_release=", 90}, {" half_damper_time=", 90}, {" ampeg_veltime=", 0}, {" ampeg_keytime=", 0}};
        for (const auto& cap : caps) {
            const char* id = cap.id;
            for (size_t at = s.find(id); at != std::string::npos; at = s.find(id, at + 1)) {
                const size_t v = at + std::strlen(id), e = s.find_first_of(" \n", v);
                const int x = std::atoi(s.substr(v, e - v).c_str());
                const int y = cap.most ? std::min(x, cap.most) : 0;
                s.replace(v, e - v, std::to_string(y));
            }
        }
        return s;
    };
    int total = 0; uint32_t sounding = 0, left = 0;
    for (int round = 0; round < 8; ++round) {
        Inst a;
        char msg[256] = {};
        uint32_t ctx[2];
        for (int c = 0; c < 2; ++c) {
            ctx[c] = a.contexts->prepare(a.p, ("s" + std::to_string(c)).c_str(), nullptr, 0, patchWith("level=1", bounded(randomSound(r)).c_str()).c_str(), testBase(), msg, sizeof msg);
            CHECK(ctx[c] != 0);
            a.contexts->select(a.p, ctx[c], c);
        }
        int32_t id = 1;
        uint32_t most = 0;
        for (int b = 0; b < 150; ++b) {
            const uint32_t frames = uint32_t(r.in(1, 128));
            std::vector<Bytes> ev;
            for (int k = r.in(0, 4); k > 0; --k) {
                const uint32_t t = uint32_t(r.in(0, int(frames) - 1));
                const int16_t ch = int16_t(r.in(0, 1)), key = int16_t(r.in(0, 127));
                switch (r.in(0, 5)) {
                case 0: case 1: ev.push_back(on(t, id++, ch, key, r.unit())); break;
                case 2: case 3: ev.push_back(off(t, -1, ch, key)); break;
                case 4: { static const uint8_t ccs[4] = {1, 64, 2, 11}; ev.push_back(midi(t, uint8_t(0xb0 | ch), ccs[r.in(0, 3)], uint8_t(r.in(0, 127)))); } break;
                default: ev.push_back(choke(t, -1, ch, key)); break;
                }
            }
            std::stable_sort(ev.begin(), ev.end(), [](const Bytes& x, const Bytes& y) {
                return reinterpret_cast<const clap_event_header_t*>(x.data())->time < reinterpret_cast<const clap_event_header_t*>(y.data())->time; });
            a.block(frames, ev);
            most = std::max(most, a.voices());
        }
        const uint32_t held = a.voices();
        std::vector<Bytes> up = {midi(0, 0xb0, 64, 0), midi(0, 0xb1, 64, 0)};
        for (int16_t ch = 0; ch < 2; ++ch) for (int16_t k = 0; k < 128; ++k) up.push_back(off(0, -1, ch, k));
        a.block(128, up);
        a.run(48000 * 12);
        const int notes = id - 1;
        if (!(a.voices() == 0 && a.endsTotal() == notes)) { ++g_failures; std::printf("  round %d: %u voices left, %d NOTE_ENDs for %d notes\n", round, a.voices(), a.endsTotal(), notes); }
        total += notes; sounding = std::max(sounding, most); left += held;
    }
    std::printf("  %d notes, up to %u voices at once, %u sounding at the keys-up, all ended\n", total, sounding, left);
}
