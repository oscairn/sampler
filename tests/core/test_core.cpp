// SPDX-License-Identifier: AGPL-3.0-or-later
// The core's own tests, linked directly (no plugin): the sound format, the coverage report, the laws
// against the fork's documented values, and voices rendered against independent computations of the
// fork's segment shapes, loops, sample-accurate starts and the control grid.
#include <cfenv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../../core/laws.h"
#include "../../core/measured.h"
#include "../../core/pcore.h"

using namespace pcm;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { ++failures; std::printf("  %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c); } } while (0)
#define NEAR(a, b, tol) do { const double x_ = (a), y_ = (b); if (!(std::fabs(x_ - y_) <= (tol))) { ++failures; std::printf("  %s:%d: %s = %.9g, want %.9g (+-%g)\n", __FILE__, __LINE__, #a, x_, y_, double(tol)); } } while (0)

static const char* kPad =
    "oscairn-pcm-sound/0\n"
    "sample a/one.flac\n"
    "sample b/two two.flac\n"
    "element ampeg_attack=30 ampeg_decay2=127 ampeg_release=127 fil_type=0 fil_cutoff=244 fil_resonance=6 fileg_decay1=0 "
    "fileg_decay2=59 fileg_release=86 fileg_attack_level=128 fileg_release_level=126 fileg_depth=52 fileg_veldepth=39\n"
    "region key=0-33 vel=1-127 root=30 tune=9.38 pan=-100 element=0 sample=0 loop=544-93904\n"
    "region key=101-127 vel=1-127 root=108 tune=1.56 level=-7.12 element=0 sample=1 loop=58-6112\n";

// ---- the sound format ----
static void soundFormat() {
    std::unique_ptr<SoundData> a(new SoundData), b(new SoundData), c(new SoundData);
    char err[256] = {};
    CHECK(readText(kPad, *a, err, sizeof err));
    CHECK(a->samples == 2 && a->elements == 1 && a->regions == 2);
    CHECK(a->region[0].tune == 938 && a->region[0].pan == -10000 && a->region[1].level == -712 && a->region[0].loopEnd == 93904);
    CHECK(!std::strcmp(a->ref[1], "b/two two.flac"));
    // text -> blob -> parsed -> text: the same, byte for byte
    std::vector<uint8_t> blob(writeBlob(*a, nullptr, 0));
    CHECK(writeBlob(*a, blob.data(), blob.size()) == blob.size());
    CHECK(readBlob(blob.data(), blob.size(), *b, err, sizeof err));
    CHECK(*a == *b);
    std::vector<char> buf(writeText(*a, nullptr, 0) + 1);
    writeText(*a, buf.data(), buf.size());
    CHECK(readText(buf.data(), *c, err, sizeof err) && *c == *a);
    std::vector<char> buf2(buf.size());
    writeText(*c, buf2.data(), buf2.size());
    CHECK(buf == buf2);
    // every single-byte corruption and every truncation of the blob is refused (CRC, sizes, ranges)
    int accepted = 0;
    for (size_t i = 0; i < blob.size(); ++i) {
        std::vector<uint8_t> x = blob; x[i] ^= 0x41;
        accepted += readBlob(x.data(), x.size(), *c, err, sizeof err);
    }
    for (size_t n = 0; n < blob.size(); ++n) accepted += readBlob(blob.data(), n, *c, err, sizeof err);
    CHECK(accepted == 0);
    // a later version is refused with a reason
    std::vector<uint8_t> v = blob; v[4] = 1;
    CHECK(!readBlob(v.data(), v.size(), *c, err, sizeof err) && std::strstr(err, "version"));
    // text errors name their line
    const char* bad[][2] = {
        {"oscairn-pcm-sound/0\nsample x\nelement nope=1\nregion element=0 sample=0\n", "line 3"},
        {"oscairn-pcm-sound/0\nsample x\nelement fil_cutoff=1024\nregion element=0 sample=0\n", "line 3"},
        {"oscairn-pcm-sound/0\nsample x\nelement\nregion element=1 sample=0\n", "bad element"},
        {"oscairn-pcm-sound/0\nsample ../x\nelement\nregion element=0 sample=0\n", "line 2"},
        {"oscairn-pcm-sound/0\nsample x\nelement\nregion element=0 sample=0 key=5-4\n", "line 4"},
        {"oscairn-pcm-sound/0\nsample x\nelement\nregion element=0 sample=0 tune=1.234\n", "line 4"},
        {"oscairn-pcm-sound/0\nsample x\nelement\nregion element=0 sample=0 loop=10-10\n", "line 4"},
        {"oscairn-pcm-sound/1\n", "line 1"},
        {"oscairn-pcm-sound/0\nsample x\nelement\n", "region"},
    };
    for (auto& b2 : bad) { CHECK(!readText(b2[0], *c, err, sizeof err)); if (!std::strstr(err, b2[1])) { ++failures; std::printf("  error '%s' lacks '%s'\n", err, b2[1]); } }
}

// ---- coverage ----
static void coverage() {
    std::unique_ptr<SoundData> d(new SoundData);
    char err[256];
    CHECK(readText(kPad, *d, err, sizeof err));
    Gap g[16];
    CHECK(notCovered(*d, g, 16) == 0);  // the pads use only what the core covers
    d->element[0].v[FilType] = 6;  // two high-pass sections: not covered
    d->element[0].v[ElfoMode] = 1;
    d->region[1].loop = 0; d->region[1].loopStart = d->region[1].loopEnd = 0;  // a one-shot region: covered
    d->element[0].v[TriggerMode] = 2;   // key off: covered
    d->element[0].v[EqType] = 2;      // Boost6: a flat +6.02 dB
    d->element[0].v[HalfDamper] = 1;
    uint32_t n = notCovered(*d, g, 16);
    CHECK(n == 2 && g[0].param == FilType && g[1].param == ElfoMode);
    d->element[0].v[TriggerMode] = 1;   // legato: not covered
    d->element[0].v[EqType] = 0; d->element[0].v[EqGain2] = 70;  // a 2-band EQ with gain: not covered
    n = notCovered(*d, g, 16);
    CHECK(n == 4 && g[2].param == TriggerMode && g[3].param == EqGain2);
}

// ---- laws (the fork's documented values) ----
static void lawValues() {
    NEAR(20 * std::log10(laws::filterGain(230)), -5.45, 1e-4);   // K21: the default gain
    NEAR(laws::levelGain(127), 1.0, 1e-7);
    NEAR(20 * std::log10(laws::levelGain(64)), -0.375 * std::round(40 * std::log10(127.0 / 64) / 0.375), 1e-5);
    NEAR(laws::attackSeconds(30), 0.00435516, 1e-7);
    NEAR(laws::releaseSecondsPer60dB(127), 105.719, 1e-3);  // the fork 0.8.4: equal to 126, as measured
    NEAR(laws::filterDamping(16), 1.0, 1e-6);
    NEAR(laws::dampingLimit(440.0 * std::exp2(4812.0 / 1200)), 1.0016, 1e-3);
    NEAR(laws::cutoffCents(244), -2909.5, 0.5);  // between the measured knots 240 and 256
    NEAR(laws::cutoffCents(8), -5772.0, 0.5);     // the bottom of the range, measured (no extrapolation)
    NEAR(laws::cutoffCents(300), -2416.0, 0.5);   // the fork's knots from 264 on (K20)
    NEAR(laws::filterReleaseFactor(86), 0.9525, 1e-6);  // the filter envelope's release, measured
    NEAR(laws::filterReleaseFactor(88), 1.0, 1e-6);
    NEAR(laws::filterReleaseFactor(92), 0.973, 1e-6);
    const laws::Velocity flat;
    NEAR(laws::velocityGain(flat, 1), 1.0, 1e-7);  // sensitivity 0: every velocity at full level
    NEAR(laws::filterDepthVelocityFactor(39, 2, 100), 1.0 + 39.0 / 64 * (0.854 + 0.131 * 4.0 / 16 - 1.0), 1e-6);
}

// ---- voices ----
struct Rig {
    std::vector<int16_t> pcm;
    std::unique_ptr<SoundData> data{new SoundData};
    std::unique_ptr<Compiled> sound{new Compiled};
    std::unique_ptr<Core> core{new Core};
    SampleView view;
    // a sound over every key on a generated sample (interleaved frames at `rate`), root 60
    Rig(const std::string& element, const std::vector<int16_t>& frames, int channels, float rate, const char* loop, double sr = 48000, const std::string& region = "") {
        pcm.assign(size_t((kGuardPre + kGuardPost) * channels), 0);
        pcm.insert(pcm.begin() + kGuardPre * channels, frames.begin(), frames.end());
        view = {pcm.data() + kGuardPre * channels, uint32_t(frames.size() / size_t(channels)), uint8_t(channels), rate};
        const std::string text = "oscairn-pcm-sound/0\nsample s\nelement " + element + "\nregion key=0-127 root=60 element=0 sample=0 " + region + (loop ? std::string(" loop=") + loop : "") + "\n";
        char err[256];
        if (!readText(text.c_str(), *data, err, sizeof err)) { std::printf("  sound: %s\n", err); ++failures; }
        if (!compile(*data, &view, *sound, err, sizeof err)) { std::printf("  compile: %s\n", err); ++failures; }
        core->prepare(sr);
        core->install(0, sound.get());
        core->setGain(0, 1, 0);
    }
    // a whole sound text over the one generated mono sample
    Rig(const char* text, const std::vector<int16_t>& frames, float rate, double sr) {
        pcm.assign(size_t(kGuardPre + kGuardPost), 0);
        pcm.insert(pcm.begin() + kGuardPre, frames.begin(), frames.end());
        view = {pcm.data() + kGuardPre, uint32_t(frames.size()), 1, rate};
        char err[256];
        if (!readText(text, *data, err, sizeof err)) { std::printf("  sound: %s\n", err); ++failures; }
        if (!compile(*data, &view, *sound, err, sizeof err)) { std::printf("  compile: %s\n", err); ++failures; }
        core->prepare(sr);
        core->install(0, sound.get());
    }
    // renders n frames in blocks of `block`, each split at the given frame offsets
    std::vector<float> run(long n, int block = 128, std::vector<int> splits = {}) {
        std::vector<float> out;
        std::vector<float> L(static_cast<size_t>(block)), R(static_cast<size_t>(block));
        for (long done = 0; done < n; done += block) {
            std::fill(L.begin(), L.end(), 0.f); std::fill(R.begin(), R.end(), 0.f);
            int at = 0;
            for (int s : splits) if (s > at && s < block) { core->render(L.data() + at, R.data() + at, uint32_t(s - at), uint32_t(at)); at = s; }
            core->render(L.data() + at, R.data() + at, uint32_t(block - at), uint32_t(at));
            for (int i = 0; i < block; ++i) { out.push_back(L[size_t(i)]); out.push_back(R[size_t(i)]); }
        }
        return out;
    }
};

static std::vector<int16_t> constant(int n, int16_t v) { return std::vector<int16_t>(size_t(n), v); }
// full-scale level of a gain-1 voice at centre pan, per channel: the reference's staging (-9.43 dB)
static const double kUnit = std::pow(10.0, -9.43 / 20) / 32768;

// The attack: a straight line in amplitude from the initial level, reaching the attack level after
// attackSeconds(index) x attackDurationFactor seconds; the first sounding frame already one step up.
static void attackShape() {
    Rig r("ampeg_attack=60 ampeg_decay1=127 ampeg_decay2=127", constant(48000, 16384), 1, 48000.f, "100-40000");
    r.core->setInterp(Interp::Hermite);
    r.core->noteOn(0, 0, 0, 60, 1.0f);
    const auto out = r.run(9600);
    const double unit = 16384 * kUnit;
    const double samples = laws::attackSeconds(60) * laws::attackDurationFactor(0, 127) * 48000.0;
    double worst = 0;
    for (int i = 0; i < 9600; ++i) {
        const double want = std::min(1.0, (i + 1) / samples) * unit;
        worst = std::max(worst, std::fabs(out[size_t(2 * i)] - want));
    }
    NEAR(worst, 0.0, 2e-6);
    NEAR(out[2 * 9000], unit, 1e-6);
}

// A release: a straight line in dB at the table's time per 60 dB, falling to -96.45 dB below full
// scale (the reference's end, measured), whatever its start; the voice ends there.
static void releaseShape() {
    Rig r("ampeg_attack=0 ampeg_decay1=127 ampeg_decay2=127 ampeg_release=64", constant(48000, 16384), 1, 48000.f, "100-40000");
    r.core->noteOn(0, 0, 0, 60, 1.0f);
    auto out = r.run(1280);
    r.core->noteOff(0);
    auto rel = r.run(96000);
    const double perSample = -60.0 / (laws::releaseSecondsPer60dB(64) * 48000.0);
    const double g0 = out[2 * 1279];
    for (int i : {0, 100, 1000, 10000, 20000, 35000}) NEAR(20 * std::log10(rel[size_t(2 * i)] / g0), perSample * (i + 1), 2e-3);
    const int end = int(-96.45 / perSample);  // the frame the release reaches -96.45 dB
    NEAR(20 * std::log10(rel[size_t(2 * (end - 200))] / g0), -96.45 + 200 * -perSample, 0.05);  // still sounding below -90 dB
    CHECK(rel[size_t(2 * (end + 64))] == 0.0f);
    CHECK(!r.core->voice(0).sounding);
}

// A note starts on its exact frame, wherever the render calls are split; splitting a block does not
// change the sound (control steps stay on the block's grid).
static void startsAndSplits() {
    std::vector<int16_t> noise(40000);
    uint32_t s = 1;
    for (auto& x : noise) { s = s * 1664525u + 1013904223u; x = int16_t(int32_t(s) >> 18); }
    const std::string el = "ampeg_attack=0 fil_type=0 fil_cutoff=600 fileg_depth=30 fileg_decay1=40";
    Rig a(el, noise, 1, 44100.f, "1000-39000"), b(el, noise, 1, 44100.f, "1000-39000");
    std::vector<float> la(128), ra(128), lb(128), rb(128);
    a.core->render(la.data(), ra.data(), 37, 0);
    a.core->noteOn(3, 0, 0, 64, 0.7f);
    a.core->render(la.data() + 37, ra.data() + 37, 91, 37);
    b.core->render(lb.data(), rb.data(), 20, 0);
    b.core->render(lb.data() + 20, rb.data() + 20, 17, 20);
    b.core->noteOn(3, 0, 0, 64, 0.7f);
    b.core->render(lb.data() + 37, rb.data() + 37, 50, 37);
    b.core->render(lb.data() + 87, rb.data() + 87, 41, 87);
    int first = -1;
    for (int i = 0; i < 128; ++i) if (first < 0 && la[size_t(i)] != 0.f) first = i;
    CHECK(first == 37);
    CHECK(la == lb && ra == rb);
    const auto x = a.run(48000), y = b.run(48000, 128, {5, 16, 33, 100});
    CHECK(x == y);
}

// Looped playback: at the root, at the sample's own rate, with Hermite, every output frame is a sample
// frame (integer positions), across loop ends; a loop shorter than a step is stitched several times.
static void loops() {
    std::vector<int16_t> ramp(200);
    for (int i = 0; i < 200; ++i) ramp[size_t(i)] = int16_t(i * 100);
    Rig r("ampeg_attack=0 ampeg_decay1=127 ampeg_decay2=127", ramp, 1, 48000.f, "150-157");
    r.core->setInterp(Interp::Hermite);
    r.core->noteOn(0, 0, 0, 60, 1.0f);
    const auto out = r.run(512);
    int bad = 0;
    for (int i = 0; i < 512; ++i) {
        const int frame = i < 157 ? i : 150 + (i - 150) % 7;
        if (std::fabs(out[size_t(2 * i)] - frame * 100 * kUnit) > 1e-6) ++bad;
    }
    CHECK(bad == 0);
}

// Pan: the key bank's pan and the element's add in raw steps (-100..100 as 1..127), then the balance
// law: the near side keeps the centre level, the far side falls as sin^2 on the 0.375 dB grid.
static void panLaws() {
    struct { const char* region; int elementPan; double left, right; } cases[] = {
        {"pan=-100", 64, 1.0, 0.0},    // a hard-left key bank: centre level on the left, nothing right
        {"pan=-100", 1, 1.0, 0.0},     // pushed further left: clamped
        {"pan=100", 1, 1.0, 1.0},      // hard right plus hard left: centre, both sides at the centre level
        {"pan=100", 32, std::pow(10.0, -6.0 / 20), 1.0},  // raw 95: the left side sin^2(pi/4), on the grid
        {"", 96, std::pow(10.0, -6.375 / 20), 1.0},  // raw 96, 31 steps from the edge: 17 grid steps
    };
    for (auto& c : cases) {
        Rig r("ampeg_attack=0 ampeg_decay1=127 ampeg_decay2=127 amp_pan=" + std::to_string(c.elementPan), constant(30000, 16384), 1, 44100.f, "100-29000", 44100, c.region);
        r.core->noteOn(0, 0, 0, 60, 1.0f);
        const auto out = r.run(4800);
        NEAR(out[2 * 4000], 16384 * kUnit * c.left, 1e-7);
        NEAR(out[2 * 4000 + 1], 16384 * kUnit * c.right, 1e-7);
    }
}

// Filter types 4 and 5: a low-pass (a resonant section, or a one-pole) and a fixed high-pass section
// with damping 1 at the HPF cutoff (the fork's ElementFilter, K23; checked against the reference in
// docs/acceptance.md (d)). DC is blocked; a tone far above the high-pass and below the low-pass passes
// at the filter gain.
static std::vector<int16_t> sine(int n, double hz, double rate, double amp) {
    std::vector<int16_t> s(size_t(n), 0);
    for (int i = 0; i < n; ++i) s[size_t(i)] = int16_t(std::lround(amp * std::sin(2 * 3.14159265358979 * hz * i / rate)));
    return s;
}
static double rmsOf(const std::vector<float>& out, int from, int to) {
    double a = 0;
    for (int i = from; i < to; ++i) a += double(out[size_t(2 * i)]) * out[size_t(2 * i)];
    return std::sqrt(a / (to - from));
}
static void filterTypes45() {
    const double gain = std::pow(10.0, -5.45 / 20);  // filter gain 230
    for (const char* type : {"fil_type=4", "fil_type=5"}) {
        const std::string el = std::string("ampeg_attack=0 ampeg_decay1=127 ampeg_decay2=127 fil_cutoff=1023 ") + type;
        Rig dc(el, constant(44100, 16384), 1, 44100.f, "100-44000", 44100);
        dc.core->setInterp(Interp::Hermite);
        dc.core->noteOn(0, 0, 0, 60, 1.0f);
        const auto o = dc.run(44100);
        CHECK(std::fabs(o[2 * 44000]) < 1e-3 * 16384 * kUnit);  // the high-pass section removes DC
        // 1 kHz (an integer number of cycles in the loop) passes at the filter gain
        Rig tone(el, sine(44100, 1000, 44100, 16384), 1, 44100.f, "0-44100", 44100);
        tone.core->setInterp(Interp::Hermite);
        tone.core->noteOn(0, 0, 0, 60, 1.0f);
        const auto x = tone.run(22050);
        NEAR(20 * std::log10(rmsOf(x, 11025, 22050) / (16384 * kUnit * gain / std::sqrt(2.0))), 0.0, 0.3);
    }
    // element EQ Boost12 with a filter: +12.04 dB; with the filter Thru: bypassed
    for (const char* f : {"fil_type=4 fil_cutoff=1023", "fil_type=18"}) {
        double lv[2];
        for (int b = 0; b < 2; ++b) {
            Rig x(std::string("ampeg_attack=0 ampeg_decay1=127 ampeg_decay2=127 ") + f + (b ? " eq_type=3" : ""), sine(44100, 1000, 44100, 16384), 1, 44100.f, "0-44100", 44100);
            x.core->setInterp(Interp::Hermite);
            x.core->noteOn(0, 0, 0, 60, 1.0f);
            const auto o = x.run(22050);
            lv[b] = 20 * std::log10(rmsOf(o, 11025, 22050));
        }
        NEAR(lv[1] - lv[0], f[9] == '4' ? 20 * std::log10(4.0) : 0.0, 1e-3);
    }
    // the high-pass at HPF cutoff 512 (about 440 Hz): 110 Hz falls about 24 dB (two octaves at 12 dB)
    Rig lo("ampeg_attack=0 ampeg_decay1=127 ampeg_decay2=127 fil_type=4 fil_cutoff=1023 fil_hpf_cutoff=512", sine(44100, 110, 44100, 16384), 1, 44100.f, "0-44100", 44100);
    lo.core->setInterp(Interp::Hermite);
    lo.core->noteOn(0, 0, 0, 60, 1.0f);
    const auto y = lo.run(22050);
    const double down = 20 * std::log10(rmsOf(y, 11025, 22050) / (16384 * kUnit * gain / std::sqrt(2.0)));
    CHECK(down < -18 && down > -30);
}

// Interpolation: at the root (a 44.1 kHz source at 44.1 kHz) the 20-tap kernel passes the source
// unchanged, as the reference does; above the root its cutoff follows the step (laws::kernelCutoff).
static void kernelByTransposition() {
    NEAR(laws::kernelCutoff(1.0), 1.0, 1e-6);
    NEAR(laws::kernelCutoff(0.97), 1.0, 1e-6);
    NEAR(laws::kernelCutoff(0.8), 0.9, 1e-6);
    NEAR(laws::kernelCutoff(1.498), 0.455, 1e-6);
    NEAR(laws::kernelCutoff(1.888 * 2), 0.2025, 1e-6);
    std::vector<int16_t> noise(30000);
    uint32_t s = 7;
    for (auto& x : noise) { s = s * 1664525u + 1013904223u; x = int16_t(int32_t(s) >> 17); }
    Rig r("ampeg_attack=0 ampeg_decay1=127 ampeg_decay2=127", noise, 1, 44100.f, "100-29000", 44100);
    r.core->noteOn(0, 0, 0, 60, 1.0f);
    const auto out = r.run(4096);
    double worst = 0;
    for (int i = 64; i < 4096; ++i) worst = std::max(worst, std::fabs(out[size_t(2 * i)] - noise[size_t(i)] * kUnit));
    NEAR(worst, 0.0, 1e-7);
}

// Key-off elements (element trigger mode 2) start at the key's release, at the note's key and velocity, and
// release 2.0 s after they start (the fork's K42).
static void keyOffElements() {
    Rig r("oscairn-pcm-sound/0\nsample s\n"
          "element ampeg_attack=0 ampeg_release=0\n"
          "element ampeg_attack=0 ampeg_release=40 xa_control=2\n"
          "region key=0-127 root=60 element=0 sample=0 loop=100-29000\n"
          "region key=0-127 root=60 element=1 sample=0 loop=100-29000\n", constant(30000, 16384), 44100.f, 44100);
    r.core->noteOn(0, 0, 0, 60, 1.0f);
    r.run(4410);
    CHECK(r.core->elementVoices() == 1);  // the key-off element waits for the release
    r.core->noteOff(0);
    auto a = r.run(44100 * 2 - 640);      // to just before 2.0 s after the key-off element's start
    CHECK(r.core->elementVoices() == 1);  // the first element has gone, the key-off one sounds
    NEAR(a[a.size() - 2], 16384 * kUnit, 1e-6);  // still at its level
    auto b = r.run(44100);
    CHECK(b[2 * 2000] < 0.5 * 16384 * kUnit);  // released 2.0 s after its start (release 40 is fast)
    CHECK(r.core->voiceCount() == 0);
}

// Half damper: under the sustain pedal a released half damper element releases at its half damper
// time; the pedal's value moves the release index between that and its own release index; with the
// pedal up it takes its own (measured: docs/acceptance.md (g)).
static void halfDamper() {
    Rig r("oscairn-pcm-sound/0\nsample s\nelement ampeg_attack=0 ampeg_release=40 half_damper=1 half_damper_time=103\n"
          "region key=0-127 root=60 element=0 sample=0 loop=100-29000\n", constant(30000, 16384), 44100.f, 44100);
    auto slope = [&](long n) {  // dB/s over n frames
        const auto x = r.run(n);
        return 20 * std::log10(x[x.size() - 2] / x[2 * 128]) / ((n - 129) / 44100.0);
    };
    struct { float pedal; bool up; int index; double tol; } cases[] = {{1.0f, true, 103, 0.05}, {64.0f / 127.0f, true, 72, 0.3}, {0.0f, false, 40, 15.0}};
    for (auto& c : cases) {  // each from the full level (a re-entered release takes the full-scale time)
        r.core->setSustain(0, c.pedal);
        r.core->noteOn(0, 0, 0, 60, 1.0f);
        r.run(4096);
        if (c.up) r.core->keyUp(0); else r.core->noteOff(0);
        NEAR(slope(c.index == 40 ? 512 : 22050), -60.0 / laws::releaseSecondsPer60dB(c.index), c.tol);
        r.core->kill(0);
    }
    // an envelope still in its decays plays on under the pedal; the half damper waits for the sustain
    {
        Rig d("oscairn-pcm-sound/0\nsample s\nelement ampeg_attack=0 ampeg_decay1=100 ampeg_decay1_level=64 ampeg_release=40 half_damper=1 half_damper_time=103\n"
              "region key=0-127 root=60 element=0 sample=0 loop=100-29000\n", constant(30000, 16384), 44100.f, 44100);
        Rig h("oscairn-pcm-sound/0\nsample s\nelement ampeg_attack=0 ampeg_decay1=100 ampeg_decay1_level=64 ampeg_release=40 half_damper=1 half_damper_time=103\n"
              "region key=0-127 root=60 element=0 sample=0 loop=100-29000\n", constant(30000, 16384), 44100.f, 44100);
        for (Rig* x : {&d, &h}) { x->core->setSustain(0, 1.0f); x->core->noteOn(0, 0, 0, 60, 1.0f); x->run(4096); }
        d.core->keyUp(0);  // h: the key stays down
        const auto a = d.run(44100), b = h.run(44100);
        CHECK(a == b);
    }
    // the pedal let go: the release goes on at the element's own index
    r.core->setSustain(0, 1.0f);
    r.core->noteOn(0, 0, 0, 60, 1.0f);
    r.run(4096);
    r.core->keyUp(0);
    r.run(4410);
    r.core->setSustain(0, 0.0f);
    r.core->noteOff(0);
    r.run(44100);
    CHECK(r.core->voiceCount() == 0);
}

// ---- what milestone 1 never exercised ----
// cents of a sine's period over [from, to) frames (zero crossings), against the rate's period
static double sineCents(const std::vector<float>& o, int from, int to, double hz, double sr) {
    int first = -1, last = -1, n = 0;
    for (int i = from + 1; i < to; ++i)
        if (o[size_t(2 * (i - 1))] < 0 && o[size_t(2 * i)] >= 0) { if (first < 0) first = i; last = i; ++n; }
    if (n < 2) return 0;
    return 1200 * std::log2((sr * (n - 1) / double(last - first)) / hz);
}
// The element LFO in reference mode through the mod wheel: a 4.848 Hz triangle, pitch depth
// lfoPitchDepthCents(ratio, cc) and amplitude depth lfoAmpDepthDb peak to peak.
static void elementLfo() {
    Rig a("ampeg_attack=0 ampeg_decay1=127 ampeg_decay2=127 elfo_amp_ratio=32", constant(44100, 16384), 1, 44100.f, "100-44000", 44100);
    a.core->setModWheel(0, 1.0f);
    a.core->noteOn(0, 0, 0, 60, 1.0f);
    const auto o = a.run(44100);
    double lo = 1e9, hi = 0;
    for (int i = 4410; i < 44100; ++i) { lo = std::min(lo, double(o[size_t(2 * i)])); hi = std::max(hi, double(o[size_t(2 * i)])); }
    NEAR(20 * std::log10(hi / lo), laws::lfoAmpDepthDb(32), 0.2);
    Rig p("ampeg_attack=0 ampeg_decay1=127 ampeg_decay2=127 elfo_pitch_ratio=40", sine(44100, 441, 44100, 16384), 1, 44100.f, "0-44100", 44100);
    p.core->setModWheel(0, 1.0f);
    p.core->noteOn(0, 0, 0, 60, 1.0f);
    const auto q = p.run(44100);
    double clo = 1e9, chi = -1e9;
    for (int w = 0; w < 40; ++w) { const double c = sineCents(q, 4410 + w * 882, 4410 + w * 882 + 441, 441, 44100); clo = std::min(clo, c); chi = std::max(chi, c); }
    NEAR(chi - clo, laws::lfoPitchDepthCents(40, 127), 0.15 * laws::lfoPitchDepthCents(40, 127));  // 10 ms windows sample the triangle's peaks
}
// A moving pitch envelope: the hold level's pitch at the start, the attack level's after the attack.
static void movingPitchEnvelope() {
    Rig r("ampeg_attack=0 ampeg_decay1=127 ampeg_decay2=127 pitcheg_depth=32 pitcheg_hold=16 pitcheg_hold_level=160 pitcheg_attack=20",
          sine(44100, 441, 44100, 16384), 1, 44100.f, "0-44100", 44100);
    r.core->noteOn(0, 0, 0, 60, 1.0f);
    const auto o = r.run(44100);
    NEAR(sineCents(o, 50, 1000, 441, 44100), laws::pitchLevelCents(160, 32), 3.0);  // +600 cents held
    NEAR(sineCents(o, 30000, 44000, 441, 44100), 0.0, 1.0);                          // back at the attack level
}
// Level scaling by key and a non-default velocity curve with time shifts reach the voice.
static void levelScalingAndVelocity() {
    const char* el = "ampeg_attack=0 ampeg_decay1=127 ampeg_decay2=127 amp_scaling_key1=48 amp_scaling_offset2=-20 amp_scaling_offset4=-40 amp_velsens=32 amp_velcurve=0";
    for (int key : {48, 66, 90}) for (int vel : {40, 100}) {
        Rig r(el, constant(30000, 16384), 1, 44100.f, "100-29000", 44100, "root=" + std::to_string(key));
        r.core->noteOn(0, 0, 0, key, vel / 127.0f);
        const auto o = r.run(4410);
        laws::LevelScaling ls; ls.keys[0] = 48; ls.offsets[1] = -20; ls.offsets[3] = -40;
        laws::Velocity v{32, 0, 0};
        NEAR(o[2 * 4000], 16384 * kUnit * laws::levelScalingGain(ls, key) * laws::velocityGain(v, vel), 1e-6);
    }
    // the attack's time velocity shift: sensitivity +32 at velocity 100 moves the index by 25
    Rig a("ampeg_attack=70 ampeg_decay1=127 ampeg_decay2=127 ampeg_veltime=32", constant(44100, 16384), 1, 44100.f, "100-44000", 44100);
    a.core->noteOn(0, 0, 0, 60, 100 / 127.0f);
    const auto o = a.run(44100);
    const laws::TimeShifts sh = laws::timeShifts({4, 32, 0, 24, 64}, 60, 100);
    const double frames = laws::attackSeconds(70 - sh.attack) * laws::attackDurationFactor(0, 127) * 44100.0;
    NEAR(o[size_t(2 * int(frames / 2))] / (16384 * kUnit), 0.5, 0.01);
}
// A release in the middle of a block starts on its frame, whatever the block size.
static void releaseMidBlock() {
    std::vector<float> outs[2];
    for (int b = 0; b < 2; ++b) {
        Rig r("ampeg_attack=0 ampeg_decay1=127 ampeg_decay2=127 ampeg_release=40 fil_type=0 fil_cutoff=700 fileg_depth=20",
              constant(30000, 16384), 1, 44100.f, "100-29000", 44100);
        r.core->noteOn(0, 0, 0, 60, 1.0f);
        const int block = b ? 96 : 128;
        std::vector<float> L(static_cast<size_t>(block)), R(static_cast<size_t>(block));
        for (long done = 0; done < 8192; done += block) {
            std::fill(L.begin(), L.end(), 0.f); std::fill(R.begin(), R.end(), 0.f);
            const long off = 4133 - done;  // the note-off at frame 4133
            if (off > 0 && off < block) {
                r.core->render(L.data(), R.data(), uint32_t(off), 0);
                r.core->noteOff(0);
                r.core->render(L.data() + off, R.data() + off, uint32_t(block - off), uint32_t(off));
            } else r.core->render(L.data(), R.data(), uint32_t(block), 0);
            for (int i = 0; i < block; ++i) outs[b].push_back(L[size_t(i)]);
        }
    }
    CHECK(outs[0].size() >= 8192 && outs[1].size() >= 8192);
    double worst = 0;
    for (int i = 0; i < 8192; ++i) worst = std::max(worst, double(std::fabs(outs[0][size_t(i)] - outs[1][size_t(i)])));
    NEAR(worst, 0.0, 1e-6);
    NEAR(outs[0][4132], outs[0][4000], 1e-5);                                   // held to the frame before
    CHECK(20 * std::log10(outs[0][4133 + 441] / outs[0][4132]) < -10);       // released from frame 4133
}

// Stereo samples keep their channels at the centre level per channel; the filter's gain applies; the
// covered rates give the same passband.
static void stereoAndRates() {
    std::vector<int16_t> st(2 * 30000);
    for (int i = 0; i < 30000; ++i) { st[size_t(2 * i)] = 8000; st[size_t(2 * i + 1)] = -4000; }
    for (double sr : {44100.0, 48000.0, 96000.0}) {
        Rig r("ampeg_attack=0 ampeg_decay1=127 ampeg_decay2=127 fil_type=0 fil_cutoff=1023", st, 2, 40000.f, "100-29000", sr);
        r.core->noteOn(0, 0, 0, 72, 1.0f);
        const auto out = r.run(9600);
        const double l = out[2 * 9000], rr = out[2 * 9000 + 1];
        CHECK(std::isfinite(l) && std::isfinite(rr));
        const double g = std::pow(10.0, -9.43 / 20) * std::pow(10.0, -5.45 / 20) / 32768;
        NEAR(l, 8000 * g, 2e-4);
        NEAR(rr, -4000 * g, 2e-4);
    }
}

// No invalid operation (0/0, a NaN into an integer) on the control path: a decay to the level it starts
// at (the defaults: attack and decay levels 127) ends after one sample, as the fork's, and holds there.
static void equalLevelSegments() {
    Rig r("ampeg_attack=0 ampeg_decay1=40 ampeg_decay2=40 ampeg_release=40", constant(48000, 16384), 1, 48000.f, "100-40000");
    std::feclearexcept(FE_ALL_EXCEPT);
    r.core->noteOn(0, 0, 0, 60, 1.0f);
    const auto out = r.run(4800);
    CHECK(!std::fetestexcept(FE_INVALID));
    NEAR(20 * std::log10(out[2 * 4000] / (16384 * kUnit)), 0.0, 0.01);  // sustained at full level
}

int main() {
    struct { const char* name; void (*fn)(); } tests[] = {
        {"sound_format", soundFormat}, {"coverage", coverage}, {"law_values", lawValues}, {"attack_shape", attackShape},
        {"release_shape", releaseShape}, {"starts_and_splits", startsAndSplits}, {"loops", loops}, {"stereo_and_rates", stereoAndRates},
        {"pan_laws", panLaws}, {"filter_types_4_5", filterTypes45},
        {"kernel_by_transposition", kernelByTransposition}, {"key_off_elements", keyOffElements}, {"half_damper", halfDamper},
        {"element_lfo", elementLfo}, {"moving_pitch_envelope", movingPitchEnvelope}, {"level_scaling_and_velocity", levelScalingAndVelocity}, {"release_mid_block", releaseMidBlock},
        {"equal_level_segments", equalLevelSegments},
    };
    for (auto& t : tests) {
        const int before = failures;
        t.fn();
        std::printf("%s %s\n", failures == before ? "ok  " : "FAIL", t.name);
    }
    std::printf("%d failed checks\n", failures);
    return failures ? 1 : 0;
}
