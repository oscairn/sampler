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
    // the element EQ's gains take their whole range, -12 to +12 dB (raw 32-96)
    CHECK(readText("oscairn-pcm-sound/0\nsample s\nelement eq_gain1=32 eq_gain2=96\nregion element=0 sample=0\n", *c, err, sizeof err));
    // a blob from before the last part parameters were added (two fewer): read, those at their defaults
    {
        std::vector<uint8_t> old(blob.size() - 4);
        std::memcpy(old.data(), blob.data(), 16);  // magic, version, flags, total, crc, then the counts
        size_t at = 16;
        for (int k = 0; k < 3; ++k, at += 2) std::memcpy(&old[at], &blob[at], 2);
        const uint16_t parts = uint16_t(kPartParamCount - 2);
        std::memcpy(&old[at], &parts, 2);
        std::memcpy(&old[at + 2], &blob[at + 2], size_t(parts) * 2);
        std::memcpy(&old[at + 2 + size_t(parts) * 2], &blob[at + 2 + size_t(kPartParamCount) * 2], blob.size() - (at + 2 + size_t(kPartParamCount) * 2));
        const uint32_t total = uint32_t(old.size());
        std::memcpy(&old[8], &total, 4);
        uint32_t crc = 0xffffffffu;
        for (size_t i = 16; i < old.size(); ++i) { crc ^= old[i]; for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1))); }
        crc = ~crc;
        std::memcpy(&old[12], &crc, 4);
        CHECK(readBlob(old.data(), old.size(), *c, err, sizeof err));
        if (!readBlob(old.data(), old.size(), *c, err, sizeof err)) std::printf("  %s\n", err);
        CHECK(*c == *a);
    }
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
    d->element[0].v[ElementConnection] = 2;  // to insertion B: the thru level (measured), covered
    uint32_t n = notCovered(*d, g, 16);
    CHECK(n == 2 && g[0].param == FilType && g[1].param == ElfoMode);
    d->element[0].v[TriggerMode] = 1;   // legato: not covered
    d->element[0].v[EqType] = 0; d->element[0].v[EqGain2] = 70;  // a 2-band EQ with gain: covered
    n = notCovered(*d, g, 16);
    CHECK(n == 3 && g[2].param == TriggerMode);
}

// docs/coverage.md agrees with notCovered(): every parameter id is named in a row of the element or
// part table; the ids of a row whose "implemented" column starts with "yes" are never reported, at any
// value, with the filter thru, type 0 or type 4; those of a row starting with "no" are reported at
// every value but the default in one of those settings. (Rows starting otherwise are partial.)
static void coverageTable() {
    FILE* f = std::fopen(PCM_SOURCE_DIR "/docs/coverage.md", "rb");
    CHECK(f != nullptr);
    if (!f) return;
    std::string doc;
    char buf[4096];
    for (size_t k; (k = std::fread(buf, 1, sizeof buf, f)) > 0;) doc.append(buf, k);
    std::fclose(f);
    struct Row { std::string table; char status; std::vector<std::string> ids; };  // status: y, n, p
    std::vector<Row> rows;
    std::string table;
    size_t at = 0;
    while (at < doc.size()) {
        size_t end = doc.find('\n', at);
        if (end == std::string::npos) end = doc.size();
        std::string line = doc.substr(at, end - at);
        at = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();  // a CRLF checkout (Windows)
        if (line.rfind("## ", 0) == 0) { table = line.substr(3); continue; }
        if ((table != "Element" && table != "Part") || line.rfind("| ", 0) != 0 || line.rfind("| parameter", 0) == 0) continue;
        // the implemented column: the third cell of the element table, the second of the part table (c: the bar before it)
        size_t c = 0;
        for (int k = 0; k < (table == "Element" ? 2 : 1) && c != std::string::npos; ++k) c = line.find('|', c + 1);
        if (c == std::string::npos) continue;
        const size_t ce = line.find('|', c + 1);
        std::string cell = line.substr(c + 1, ce - c - 1);
        while (!cell.empty() && cell[0] == ' ') cell.erase(0, 1);
        Row r{table, cell.rfind("yes", 0) == 0 ? 'y' : cell.rfind("no", 0) == 0 ? 'n' : 'p', {}};
        for (size_t q = cell.find('`'); q != std::string::npos; q = cell.find('`', q + 1)) {
            const size_t qe = cell.find('`', q + 1);
            if (qe == std::string::npos) break;
            std::string id = cell.substr(q + 1, qe - q - 1);
            id = id.substr(0, id.find('='));
            r.ids.push_back(id);
            q = qe;
        }
        rows.push_back(r);
    }
    auto names = [](const std::string& pat, const char* id) {
        return pat.back() == '*' ? std::strncmp(id, pat.c_str(), pat.size() - 1) == 0 : pat == id;
    };
    std::unique_ptr<SoundData> d(new SoundData);
    char err[256];
    CHECK(readText("oscairn-pcm-sound/0\nsample s\nelement\nregion element=0 sample=0\n", *d, err, sizeof err));
    for (int part = 0; part < 2; ++part) {
        const int count = part ? int(kPartParamCount) : int(kParamCount);
        for (int i = 0; i < count; ++i) {
            const ParamDef& def = part ? kPartParamDefs[i] : kParamDefs[i];
            char status = 0;
            for (const Row& r : rows)
                if ((r.table == "Part") == bool(part))
                    for (const std::string& id : r.ids)
                        if (names(id, def.id)) status = status == 0 || status == r.status ? r.status : 'p';
            if (!status) { ++failures; std::printf("  docs/coverage.md names no row for `%s`\n", def.id); continue; }
            if (status == 'p') continue;
            for (int v = def.lo; v <= def.hi; ++v) {
                if (v == def.def) continue;
                bool reported = false;
                for (int type : {18, 0, 4}) {
                    std::unique_ptr<SoundData> x(new SoundData(*d));
                    x->element[0].v[FilType] = int16_t(type);
                    if (part) x->part[i] = int16_t(v); else x->element[0].v[i] = int16_t(v);
                    reported |= notCovered(*x, nullptr, 0) > 0;
                }
                if (reported != (status == 'n')) {
                    ++failures;
                    std::printf("  docs/coverage.md says %s for `%s`, notCovered() %s value %d\n", status == 'y' ? "yes" : "no", def.id,
                                reported ? "reports" : "accepts", v);
                    break;
                }
            }
        }
    }
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
    // the high-pass key follow (measured: the cutoff's law about key 60): at key 84 (the sample's root there too, so the
    // tone stays 110 Hz) key follow 0 moves the high-pass 4800 cents down: the tone passes; at 64 it does not move
    for (int kf : {0, 64}) {
        const std::string text = "oscairn-pcm-sound/0\nsample s\nelement ampeg_attack=0 ampeg_decay1=127 ampeg_decay2=127 fil_type=4 fil_cutoff=1023 fil_hpf_cutoff=512 fil_hpf_keyfollow="
                                 + std::to_string(kf) + "\nregion key=0-127 root=84 element=0 sample=0 loop=0-44100\n";
        Rig k(text.c_str(), sine(44100, 110, 44100, 16384), 44100.f, 44100);
        k.core->setGain(0, 1, 0);
        k.core->setInterp(Interp::Hermite);
        k.core->noteOn(0, 0, 0, 84, 1.0f);
        const auto z = k.run(22050);
        const double at = 20 * std::log10(rmsOf(z, 11025, 22050) / (16384 * kUnit * gain / std::sqrt(2.0)));
        if (kf == 0) NEAR(at, 0.0, 1.0); else CHECK(at < -18 && at > -30);
    }
}

// The element EQ against the reference's response (white noise through type 0 wide open, each take fitted
// within 0.04 dB rms; the targets are those fits at the given frequencies): a sine through the EQ against
// the same with the EQ thru, at 44.1 kHz.
static void elementEq() {
    struct Case { const char* eq; double hz, db; } cases[] = {
        {"eq_type=1 eq_freq1=157 eq_gain1=96 eq_q=8", 1000, 11.96}, {"eq_type=1 eq_freq1=157 eq_gain1=96 eq_q=8", 250, 1.74},
        {"eq_type=1 eq_freq1=157 eq_gain1=96 eq_q=8", 4000, 1.98}, {"eq_type=1 eq_freq1=157 eq_gain1=48 eq_q=8", 500, -1.74},
        {"eq_type=1 eq_freq1=157 eq_gain1=48 eq_q=8", 1000, -5.95}, {"eq_type=1 eq_freq1=220 eq_gain1=96 eq_q=8", 4000, 7.84},
        {"eq_type=1 eq_freq1=220 eq_gain1=96 eq_q=8", 12000, 6.91}, {"eq_type=0 eq_freq1=80 eq_gain1=96", 250, 9.33},
        {"eq_type=0 eq_freq1=80 eq_gain1=96", 1000, 2.79}, {"eq_type=0 eq_freq2=200 eq_gain2=32", 2000, -3.62},
        {"eq_type=0 eq_freq2=200 eq_gain2=32", 8000, -10.31}, {"eq_type=0 eq_freq1=80 eq_gain1=96 eq_freq2=200 eq_gain2=32", 2000, 0.91 - 3.62},
        {"eq_type=1 eq_freq1=243 eq_gain1=96 eq_q=8", 8665, 10.36},  // past the top threshold: the Q's own frequency
        {"eq_type=1 eq_freq1=157 eq_gain1=96 eq_q=31", 1000, 11.25},
    };
    for (const Case& c : cases) {
        double lv[2];
        for (int b = 0; b < 2; ++b) {
            Rig x(std::string("ampeg_attack=0 ampeg_decay1=127 ampeg_decay2=127 fil_type=0 fil_cutoff=1023 fileg_depth=0 ") + (b ? c.eq : "eq_type=5"),
                  sine(44100, c.hz, 44100, 8192), 1, 44100.f, "0-44100", 44100);
            x.core->setInterp(Interp::Hermite);
            x.core->noteOn(0, 0, 0, 60, 1.0f);
            const auto o = x.run(22050);
            lv[b] = 20 * std::log10(rmsOf(o, 11025, 22050));
        }
        if (std::fabs(lv[1] - lv[0] - c.db) > 0.1) { ++failures; std::printf("  %s at %g Hz: %+.2f dB, the reference %+.2f\n", c.eq, c.hz, lv[1] - lv[0], c.db); }
    }
}

// Interpolation: at the root (a 44.1 kHz source at 44.1 kHz) the 20-tap kernel passes the source
// unchanged, as the reference does; above the root its cutoff follows the step, below it 0.95 (both
// measured: laws::kernelCutoff).
static void kernelByTransposition() {
    NEAR(laws::kernelCutoff(1.0), 1.0, 1e-6);
    NEAR(laws::kernelCutoff(0.97), 1.0, 1e-6);
    NEAR(laws::kernelCutoff(0.8), 0.95, 1e-6);
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

// Key-off elements (element trigger mode 2) start when the note ends, at the note's key and velocity, and
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
    // under the sustain pedal the key's release starts nothing: the key-off element sounds when the
    // pedal releases the note (measured: docs/acceptance.md (g))
    r.core->setSustain(0, 1.0f);
    r.core->noteOn(0, 0, 0, 60, 1.0f);
    r.run(4410);
    r.core->keyUp(0);
    r.run(4410);
    CHECK(r.core->elementVoices() == 1);
    r.core->setSustain(0, 0.0f);
    r.core->noteOff(0);
    r.run(128);
    CHECK(r.core->elementVoices() == 1 && r.core->voice(0).sounding);  // the first element released (fast), the key-off one sounds
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
    for (auto& c : cases) {  // each from the full level
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
    // the pedal pressed only after the key went up: the release slows to the half damper time (measured)
    r.core->setSustain(0, 0.0f);
    r.core->noteOn(0, 0, 0, 60, 1.0f);
    r.run(4096);
    r.core->noteOff(0);
    r.run(256);
    r.core->setSustain(0, 1.0f);
    NEAR(slope(22050), -60.0 / laws::releaseSecondsPer60dB(103), 0.05);
    r.core->kill(0);
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
// elfoPitchDepthCents and amplitude depth elfoAmpDepthDb peak to peak at the box output c x ratio / 8.
static void elementLfo() {
    Rig a("ampeg_attack=0 ampeg_decay1=127 ampeg_decay2=127 elfo_amp_ratio=32", constant(44100, 16384), 1, 44100.f, "100-44000", 44100);
    a.core->setModWheel(0, 1.0f);
    a.core->noteOn(0, 0, 0, 60, 1.0f);
    const auto o = a.run(44100);
    double lo = 1e9, hi = 0;
    for (int i = 4410; i < 44100; ++i) { lo = std::min(lo, double(o[size_t(2 * i)])); hi = std::max(hi, double(o[size_t(2 * i)])); }
    NEAR(20 * std::log10(hi / lo), laws::elfoAmpDepthDb(4096), 0.2);
    Rig p("ampeg_attack=0 ampeg_decay1=127 ampeg_decay2=127 elfo_pitch_ratio=40", sine(44100, 441, 44100, 16384), 1, 44100.f, "0-44100", 44100);
    p.core->setModWheel(0, 1.0f);
    p.core->noteOn(0, 0, 0, 60, 1.0f);
    const auto q = p.run(44100);
    double clo = 1e9, chi = -1e9;
    for (int w = 0; w < 40; ++w) { const double c = sineCents(q, 4410 + w * 882, 4410 + w * 882 + 441, 441, 44100); clo = std::min(clo, c); chi = std::max(chi, c); }
    NEAR(chi - clo, laws::elfoPitchDepthCents(5120), 0.15 * laws::elfoPitchDepthCents(5120));  // 10 ms windows sample the triangle's peaks
    // the same law from a knob box on E.LFO PMD (knob 768, ratio +32: o 3072, 35.97 cents), and nothing at a negative ratio
    for (int ratio : {32, -32}) {
        const std::string text = "oscairn-pcm-sound/0\npart knob1=768 box1_src=8 box1_dst=64 box1_ratio=" + std::to_string(ratio) +
                                 "\nsample s\nelement ampeg_attack=0 ampeg_decay1=127 ampeg_decay2=127\nregion key=0-127 root=60 element=0 sample=0 loop=0-44100\n";
        Rig k(text.c_str(), sine(44100, 441, 44100, 16384), 44100.f, 44100);
        k.core->setGain(0, 1, 0);
        k.core->setControl(0, 8, 768 / 1023.f);  // the shell starts knob 1 at the sound's value
        k.core->noteOn(0, 0, 0, 60, 1.0f);
        const auto z = k.run(44100);
        double lo2 = 1e9, hi2 = -1e9;
        for (int w = 0; w < 40; ++w) { const double c = sineCents(z, 4410 + w * 882, 4410 + w * 882 + 441, 441, 44100); lo2 = std::min(lo2, c); hi2 = std::max(hi2, c); }
        if (ratio > 0) NEAR(hi2 - lo2, 35.97, 0.15 * 35.97); else NEAR(hi2 - lo2, 0.0, 0.05);
    }
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

// The part's velocity sensitivity (measured: docs/acceptance.md (h)): the note's velocity on the 0-2047
// scale, scale2048(v) x scale2048(depth) / 1024 + 2 (scale2048(offset) - 1024), for the level and for the
// element velocity limits.
static void partVelocity() {
    NEAR(laws::partVelocity2048(64, 64, 64), 1024, 0);    // neutral: as played
    NEAR(laws::partVelocity2048(80, 32, 64), 642, 0);     // depth 32 halves: 1284 / 2
    NEAR(laws::partVelocity2048(1, 64, 96), 1056, 0);     // offset 96 adds 1039
    NEAR(laws::partVelocity2048(127, 0, 127), 2046, 0);   // depth 0: the offset alone
    NEAR(laws::partVelocity2048(100, 64, 0), 0, 0);       // clamped
    NEAR(laws::velocityFrom2048(1023), 63, 0);            // the step at or below: 1023 is velocity 63 (measured)
    NEAR(laws::velocityFrom2048(1024), 64, 0);
    NEAR(laws::velocityFrom2048(2047), 127, 0);
    const char* text = "oscairn-pcm-sound/0\npart vel_offset=96\nsample s\n"
                       "element amp_velsens=32 amp_pan=1\nelement amp_velsens=32 amp_pan=127\n"
                       "region key=0-127 vel=1-63 root=60 element=0 sample=0 loop=100-29000\n"
                       "region key=0-127 vel=64-127 root=60 element=1 sample=0 loop=100-29000\n";
    Rig r(text, constant(30000, 16384), 44100.f, 44100);
    r.core->noteOn(0, 0, 0, 60, 1.0f / 127.0f);
    const auto o = r.run(4410);
    // velocity 1 plays as 1056 (velocity 66): the upper region, on the right, at 40 log10(1056 / 2047) dB
    CHECK(std::fabs(o[2 * 4000]) < 1e-9);
    float pl, pr;
    laws::balanceGains(127, pl, pr);
    const double want = 16384 * kUnit * laws::velocityGain2048({32, 0, 3}, 1056) * pr;
    NEAR(o[2 * 4000 + 1], want, 1e-6);
    CHECK(keySounds(*r.sound, 60, 1));
}

// Elements 2-8 with the filter thru play 5.625 dB below element 1 (measured: docs/acceptance.md (i));
// with a filter, the element's number changes nothing.
static void elementNumber() {
    for (int type : {18, 0}) {
        double lv[2];
        for (int number : {1, 2}) {
            const std::string el = "fil_type=" + std::to_string(type) + " fil_cutoff=1023 fil_keyfollow=64 fileg_depth=0 element_number=" + std::to_string(number);
            Rig r(el, constant(48000, 16384), 1, 48000.f, "100-40000");
            r.core->noteOn(0, 0, 0, 60, 1.0f);
            const auto o = r.run(9600);
            lv[number - 1] = o[2 * 9000];
        }
        NEAR(20 * std::log10(lv[1] / lv[0]), type == 18 ? -5.625 : 0.0, 1e-4);
    }
}

// Controller boxes (measured: docs/acceptance.md (l)): the fine tune at the wheel's values, each box truncated
// toward zero before the sum, the sum within the span, the element's switch, the bend as a source; a box on the
// cutoff or the level plays as the static value it lands on.
static void controllerBoxes() {
    const std::string tone = "region key=0-127 root=60 element=0 sample=0 loop=0-44100\n";
    auto rig = [&](const std::string& part, const std::string& el) {
        return std::unique_ptr<Rig>(new Rig(("oscairn-pcm-sound/0\npart " + part + "\nsample s\nelement ampeg_attack=0 ampeg_decay1=127 ampeg_decay2=127 " + el + "\n" + tone).c_str(),
                                            sine(44100, 441, 44100, 16384), 44100.f, 44100));
    };
    struct { const char* part; const char* el; float wheel; double cents; } cases[] = {
        {"box1_dst=68 box1_ratio=63", "", 1.0f, 98}, {"box1_dst=68 box1_ratio=63", "", 64 / 127.f, 49},
        {"box1_dst=68 box1_ratio=63", "", 56 / 127.f, 43}, {"box1_dst=68 box1_ratio=64 box2_dst=68 box2_ratio=64", "", 16 / 127.f, 24},
        {"box1_dst=68 box1_ratio=64 box2_dst=68 box2_ratio=64", "", 1.0f, 100}, {"box1_dst=68 box1_ratio=32 box1_pol=1", "", 0.0f, -25},
        {"box1_dst=67 box1_ratio=-32", "", 64 / 127.f, -600}, {"box1_dst=68 box1_ratio=63", "box_sw1=0", 1.0f, 0},
        {"box1_dst=68 box1_ratio=63 box1_p1=8", "", 64 / 127.f, 16},
    };
    for (auto& c : cases) {
        auto r = rig(c.part, c.el);
        r->core->setModWheel(0, c.wheel);
        r->core->noteOn(0, 0, 0, 60, 1.0f);
        const auto o = r->run(22050);
        const double got = sineCents(o, 4410, 22050, 441, 44100);
        if (std::fabs(got - c.cents) > 0.6) { ++failures; std::printf("  %s %s wheel %.3f: %.2f cents, the reference %.0f\n", c.part, c.el, c.wheel, got, c.cents); }
    }
    {  // the bend as a source (14-bit / 16) with its own bend range of 2 semitones: 12288 -> +100 + 73 (the reference 173.0)
        auto r = rig("box1_src=0 box1_dst=68 box1_ratio=63", "");
        r->core->setPitchBend(0, 0.5f);
        r->core->noteOn(0, 0, 0, 60, 1.0f);
        NEAR(sineCents(r->run(22050), 4410, 22050, 441, 44100), 173, 0.6);
    }
    // a cutoff box lands on the static index: base 300, ratio +32 at wheel 64 -> 300 + 2048 x 1024 / 8192
    {
        auto a = rig("box1_dst=85 box1_ratio=32", "fil_type=0 fil_cutoff=300 fileg_depth=0"), b = rig("", "fil_type=0 fil_cutoff=556 fileg_depth=0");
        a->core->setModWheel(0, 64 / 127.f);
        for (auto* x : {a.get(), b.get()}) x->core->noteOn(0, 0, 0, 60, 1.0f);
        CHECK(a->run(8192) == b->run(8192));
    }
    {   // a level box: 100 - trunc(2048 x 128 / 8192) = 68
        auto a = rig("box1_dst=60 box1_ratio=-16", "amp_level=100"), b = rig("", "amp_level=68");
        a->core->setModWheel(0, 1.0f);
        for (auto* x : {a.get(), b.get()}) x->core->noteOn(0, 0, 0, 60, 1.0f);
        const auto x = a->run(8192), y = b->run(8192);
        NEAR(20 * std::log10(rmsOf(x, 4096, 8192) / rmsOf(y, 4096, 8192)), 0.0, 1e-4);
    }
    {   // the filter envelope's depth, read at note-on: 64 + trunc(4096 x 64 / 8192) = 96 (raw +32) at wheel 127, ratio +32; a
        // wheel moved while the note holds changes nothing
        const char* env = "fil_type=0 fil_cutoff=300 fil_keyfollow=64 fileg_hold_level=255 fileg_attack_level=255 fileg_decay1_level=255 fileg_decay2_level=255 ";
        auto a = rig("box1_dst=79 box1_ratio=32", std::string(env) + "fileg_depth=0"), b = rig("", std::string(env) + "fileg_depth=32");
        a->core->setModWheel(0, 1.0f);
        for (auto* x : {a.get(), b.get()}) x->core->noteOn(0, 0, 0, 60, 1.0f);
        CHECK(a->run(8192) == b->run(8192));
        a->core->setModWheel(0, 0.0f);
        CHECK(a->run(8192) == b->run(8192));
    }
    {   // filter cutoff scaling: offsets of +64 at every break point move cutoff 512 by 3.125 x 64 = 200 index steps; -64 to
        // +64 between break points 36 and 48 is 0 at key 42
        auto a = rig("", "fil_type=0 fil_cutoff=512 fil_keyfollow=64 fil_scaling_offset1=64 fil_scaling_offset2=64 fil_scaling_offset3=64 fil_scaling_offset4=64");
        auto b = rig("", "fil_type=0 fil_cutoff=712 fil_keyfollow=64");
        auto c = rig("", "fil_type=0 fil_cutoff=512 fil_keyfollow=64 fil_scaling_offset1=-64 fil_scaling_offset2=64");
        auto d = rig("", "fil_type=0 fil_cutoff=512 fil_keyfollow=64");
        for (auto* x : {a.get(), b.get()}) x->core->noteOn(0, 0, 0, 60, 1.0f);
        for (auto* x : {c.get(), d.get()}) x->core->noteOn(0, 0, 0, 42, 1.0f);
        CHECK(a->run(8192) == b->run(8192));
        CHECK(c->run(8192) == d->run(8192));
    }
    {   // the amplitude envelope's attack (read at note-on) and release (read at the note-off) boxes, span 128: the wheel at 127
        // through ratio -16 takes attack 80 to 48, through +16 release 40 to 72
        auto a = rig("box1_dst=75 box1_ratio=-16 box2_dst=78 box2_ratio=16", "ampeg_attack=80 ampeg_release=40"), b = rig("", "ampeg_attack=48 ampeg_release=72");
        a->core->setModWheel(0, 1.0f);
        for (auto* x : {a.get(), b.get()}) x->core->noteOn(0, 0, 0, 60, 1.0f);
        CHECK(a->run(22050) == b->run(22050));
        for (auto* x : {a.get(), b.get()}) x->core->noteOff(0);
        CHECK(a->run(22050) == b->run(22050));
    }
}

// A one-shot region (no loop) whose sample (2000 frames) ends before the note and before its release: the voice ends at
// the sample's end, as a normal element and as a key-off element, in blocks of 128, 96, 64 and 32 frames.
static void oneShotEnds() {
    for (int block : {128, 96, 64, 32})
        for (const char* el : {"ampeg_decay1=127 ampeg_decay2=127 ampeg_release=127", "ampeg_decay1=127 ampeg_decay2=127 ampeg_release=127 xa_control=2"}) {
            Rig r(el, sine(2000, 441, 44100, 16384), 1, 44100.f, nullptr, 44100);
            r.core->noteOn(0, 0, 0, 60, 1.0f);
            r.run(4410, block);
            r.core->noteOff(0);
            r.run(44100, block);
            if (r.core->voiceCount() != 0 || r.core->elementVoices() != 0) { ++failures; std::printf("  %s, blocks of %d: %u voices\n", el, block, r.core->voiceCount()); }
        }
}

int main() {
    struct { const char* name; void (*fn)(); } tests[] = {
        {"sound_format", soundFormat}, {"coverage", coverage}, {"coverage_table", coverageTable}, {"part_velocity", partVelocity}, {"element_number", elementNumber}, {"law_values", lawValues}, {"attack_shape", attackShape},
        {"release_shape", releaseShape}, {"starts_and_splits", startsAndSplits}, {"loops", loops}, {"stereo_and_rates", stereoAndRates},
        {"pan_laws", panLaws}, {"filter_types_4_5", filterTypes45}, {"element_eq", elementEq},
        {"kernel_by_transposition", kernelByTransposition}, {"key_off_elements", keyOffElements}, {"half_damper", halfDamper},
        {"element_lfo", elementLfo}, {"moving_pitch_envelope", movingPitchEnvelope}, {"level_scaling_and_velocity", levelScalingAndVelocity}, {"release_mid_block", releaseMidBlock},
        {"equal_level_segments", equalLevelSegments}, {"controller_boxes", controllerBoxes}, {"one_shot_ends", oneShotEnds},
    };
    for (auto& t : tests) {
        const int before = failures;
        t.fn();
        std::printf("%s %s\n", failures == before ? "ok  " : "FAIL", t.name);
    }
    std::printf("%d failed checks\n", failures);
    return failures ? 1 : 0;
}
