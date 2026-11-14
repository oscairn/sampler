// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Parts of this file are derived from the sample-engine fork of sfizz (its element envelope, pitch
// envelope, filter design and one-pole, and the filter's sample-rate handling, re-implemented here
// sample for sample) and are also under its licence:
//   This code is part of the sfizz library and is licensed under a BSD 2-clause
//   license. You should have receive a LICENSE.md file along with the code.
//   If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz
// (SPDX-License-Identifier: BSD-2-Clause for those parts; third_party/sfizz-laws/LICENSE.)
//
// The engine: element voices in pools of one plan each, four voices to a vector lane. A render
// walks the host's block in pieces that never cross a control step (every kSub frames from the
// block's start). At a control step each voice's envelopes advance in double precision to that
// frame (the fork's segment shapes, sample for sample: a straight line in amplitude for attacks and
// short releases, a straight line in dB for decays and releases); the per-sample loops then carry
// the amplitude as g = min(max(g m + a, lo), hi) from there, so a segment's shape is exact and only
// the step into the next segment waits for the next control step. Events act between pieces at
// their own frame. Positions are 32.32 fixed point, virtual (never wrapped): the scalar feeder maps
// each piece's tap window to a base pointer and stitches loop crossings into scratch, so the
// per-sample loops have no loop or end branches.
#include "pcore.h"
#include "laws.h"
#include "measured.h"
#include "vsimd.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#if defined(__SSE2__) || defined(_M_X64)
#include <xmmintrin.h>  // the MXCSR, scalar builds included
#endif

namespace pcm {
using namespace pv;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr float kBig = 1e30f;
// Output staging: a full-scale 16-bit sample at element level 127, part volume 127, pan centre plays
// at -9.43 dB per channel on the reference (mono and stereo samples alike; docs/acceptance.md (c))
constexpr double kStagingDb = -9.43;
constexpr double kMaxStep = 32.0;     // source frames per output frame: what the guard frames allow

#ifdef PCM_PROFILE
#define PCM_STAGE PV_NOINLINE
#else
#define PCM_STAGE PV_INLINE
#endif

struct FtzScope {  // flush denormals to zero while rendering
#if defined(__aarch64__) && !defined(_MSC_VER)
    uint64_t old;
    FtzScope() { __asm__ volatile("mrs %0, fpcr" : "=r"(old)); __asm__ volatile("msr fpcr, %0" ::"r"(old | (1ull << 24))); }
    ~FtzScope() { __asm__ volatile("msr fpcr, %0" ::"r"(old)); }
#elif defined(__SSE2__) || defined(_M_X64)
    unsigned old;
    FtzScope() { old = _mm_getcsr(); _mm_setcsr(old | 0x8040); }
    ~FtzScope() { _mm_setcsr(old); }
#endif
};

// A kernel row holds the taps in groups of four, each followed by its four differences to the next
// phase's row, so one vector pair load serves a group.
constexpr int coefAt(int tap) { return (tap / 4) * 8 + tap % 4; }

double besselI0(double x) { double s = 1, t = 1; for (int k = 1; k < 40; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; } return s; }

// ---- control-rate vector helpers ----
PV_INLINE vf sinv(vf x) {  // |x| <= 1.6, error < 1e-7
    vf x2 = x * x;
    vf p = fma(x2, splat(-1.f / 39916800.f), splat(1.f / 362880.f));
    p = fma(p, x2, splat(-1.f / 5040.f)); p = fma(p, x2, splat(1.f / 120.f)); p = fma(p, x2, splat(-1.f / 6.f));
    return fma(p * x2, x, x);
}

}  // namespace

// ==== the plan ====================================================================================

Cost estimateCost(const PlanDesc& p) {
    // Measured with tools/cost/cg.sh (docs/cost.md): one element voice a 128-frame block, from 8 to 32
    // voices; the filtered plans while the filter envelope moves the cutoff (the costlier case).
    // [channels - 1][interp][filter]; the Hermite plans without the filter are derived, not measured.
    static const double kVoice[2][2][kFilters] = {{{5739, 8970, 9134, 8256}, {8993, 12224, 12388, 11510}},
                                                  {{6895, 11850, 12304, 10896}, {11441, 16396, 16850, 15442}}};
    return {kVoice[p.channels == 2][p.interp == Interp::Sinc][int(p.filter)], 182};
}

// ==== compile =====================================================================================

bool compile(const SoundData& d, const SampleView* views, Compiled& out, char* error, size_t cap) {
    auto fail = [&](const char* fmt, unsigned a, unsigned b) { if (error && cap) std::snprintf(error, cap, fmt, a, b); out.loaded = false; return false; };
    out.loaded = false;
    out.regions = d.regions;
    for (uint32_t e = 0; e < d.elements; ++e) out.element[e] = d.element[e];
    out.bendUp = float(d.part[PartBendUp]);
    out.bendDown = float(d.part[PartBendDown]);
    out.velDepth = d.part[PartVelDepth]; out.velOffset = d.part[PartVelOffset];
    out.splitOutputs = d.part[PartSplitOutputs] != 0;
    double worst = 0;
    for (uint32_t i = 0; i < d.regions; ++i) {
        const Region& r = d.region[i];
        const SampleView& v = views[r.sample];
        if (!v.data || !v.frames || (v.channels != 1 && v.channels != 2) || !(v.rate > 0))
            return fail("region %u: sample %u is missing or empty", i, r.sample);
        if (r.loop && (r.loopEnd > v.frames || r.loopStart >= r.loopEnd))
            return fail("region %u: the loop lies outside its sample of %u frames", i, v.frames);
        out.region[i] = r;
        out.sample[i] = v;
        const int type = d.element[r.element].v[FilType];
        out.plan[i] = {v.channels, Interp::Sinc, type == 0 ? Filter::LowPass2 : type == 4 ? Filter::LowHigh : type == 5 ? Filter::PoleHigh : Filter::Thru};
    }
    // the costliest note: every region it starts
    for (int key = 0; key < 128; ++key)
        for (int vel = 1; vel < 128; vel += 6) {
            double c = 0;
            for (uint32_t i = 0; i < d.regions; ++i) {
                const Region& r = d.region[i];
                if (key >= r.loKey && key <= r.hiKey && vel >= r.loVel && vel <= r.hiVel) c += estimateCost(out.plan[i]).voiceBlock;
            }
            worst = std::max(worst, c);
        }
    out.noteCost = worst;
    out.loaded = true;
    return true;
}

bool keySounds(const Compiled& s, int key, int velocity) {
    if (!s.loaded) return false;
    velocity = laws::velocityFrom2048(laws::partVelocity2048(velocity, s.velDepth, s.velOffset));  // as noteOn
    for (uint32_t i = 0; i < s.regions; ++i) {
        const Region& r = s.region[i];
        if (key >= r.loKey && key <= r.hiKey && velocity >= r.loVel && velocity <= r.hiVel) return true;
    }
    return false;
}

// ==== envelopes (control side, double precision) =================================================

namespace {

double toDb(double gain) { return gain > 0.0 ? std::max(20.0 * std::log10(gain), laws::kFloorDb) : laws::kFloorDb; }
double levelDb(int32_t level) { return toDb(laws::levelGain(level)); }

// The fork's ElementEnvelope::enter
void ampEnter(AmpEnv& e, AmpEnv::Stage stage, double sr) {
    e.stage = stage;
    e.ramp = false;
    switch (stage) {
    case AmpEnv::Attack: {
        e.ramp = true;
        e.target = laws::levelGain(e.attackLevel);
        const double seconds = double(laws::attackSeconds(e.attackTime)) * laws::attackDurationFactor(e.initialLevel, e.attackLevel);
        const double distance = e.target - e.gain;
        e.step = seconds > 0.0 ? distance / (seconds * sr) : distance;
        if (e.step == 0.0) e.step = 1e-12;  // equal levels: the attack ends at once
        break;
    }
    case AmpEnv::Release:
        if (e.releaseOverride <= 0.0 && e.releaseTime < 32) {
            e.ramp = true;
            e.target = 0.0;
            const double samples = laws::releaseRampSeconds(e.releaseTime) * sr;
            e.step = samples >= 1.0 ? -1.0 / samples : -e.gain;
            if (e.step == 0.0) e.step = -1e-12;
            break;
        }
        [[fallthrough]];
    case AmpEnv::Decay1:
    case AmpEnv::Decay2: {
        const bool release = stage == AmpEnv::Release;
        const int32_t index = release ? e.releaseTime : stage == AmpEnv::Decay1 ? e.decay1Time : e.decay2Time;
        const double per60 = release ? (e.releaseOverride > 0.0 ? e.releaseOverride : laws::releaseSecondsPer60dB(index))
                                     : laws::decaySecondsPer60dB(index);
        const double floor = release ? laws::kReleaseEndDb : laws::kFloorDb;  // the full-scale segment's span
        const double samples = std::max(1.0, per60 * (-floor / 60.0) * sr);
        e.target = release ? floor : levelDb(stage == AmpEnv::Decay1 ? e.decay1Level : e.decay2Level);
        e.db = toDb(e.gain);
        if (e.gain <= 0.0 && e.target > e.db) e.gain = std::pow(10.0, e.db / 20.0);  // from level 0 a rise starts at the floor
        e.step = (e.target - e.db) / samples;
        e.ratio = std::pow(10.0, e.step / 20.0);
        e.ratio16 = std::pow(10.0, e.step * kSub / 20.0);
        break;
    }
    case AmpEnv::Sustain:
        if (e.gain <= 0.0) e.stage = AmpEnv::Done;  // held at level 0: nothing more can sound
        break;
    case AmpEnv::Fade:
        e.ramp = true;
        break;
    case AmpEnv::Done:
        e.gain = 0.0;
        break;
    }
}

// Brings the envelope to include sample `to` (the engine clock), segment ends included.
void ampAdvance(AmpEnv& e, int64_t to, double sr) {
    int64_t steps = to - e.last;
    if (steps <= 0) return;
    e.last = to;
    while (steps > 0) {
        switch (e.stage) {
        case AmpEnv::Attack:
        case AmpEnv::Fade:
        case AmpEnv::Release:
            if (e.ramp || e.stage != AmpEnv::Release) {
                // straight line in amplitude: how many samples to the target
                const double left = (e.target - e.gain) / e.step;  // > 0 while short of it
                if (double(steps) < left) { e.gain += e.step * double(steps); return; }
                const int64_t used = std::max<int64_t>(1, int64_t(std::ceil(left)));
                steps -= std::min(steps, used);
                e.gain = e.target;
                ampEnter(e, e.stage == AmpEnv::Attack ? AmpEnv::Decay1 : AmpEnv::Done, sr);
                if (e.stage == AmpEnv::Done) return;
                continue;
            }
            [[fallthrough]];
        case AmpEnv::Decay1:
        case AmpEnv::Decay2: {
            // a segment at its target already (equal levels: step 0) ends after one sample, as the fork's
            const double left = e.step != 0.0 ? (e.target - e.db) / e.step : 0.0;
            if (double(steps) < left) {
                e.db += e.step * double(steps);
                e.gain *= steps == kSub ? e.ratio16 : std::pow(e.ratio, double(steps));
                return;
            }
            const int64_t used = std::max<int64_t>(1, int64_t(std::ceil(left)));
            steps -= std::min(steps, used);
            e.db = e.target;
            e.gain = e.db <= laws::kFloorDb ? 0.0 : std::pow(10.0, e.db / 20.0);
            ampEnter(e, e.stage == AmpEnv::Release ? AmpEnv::Done : e.stage == AmpEnv::Decay1 ? AmpEnv::Decay2 : AmpEnv::Sustain, sr);
            if (e.stage == AmpEnv::Done || e.stage == AmpEnv::Sustain) return;
            continue;
        }
        default:
            return;
        }
    }
}

// The per-sample chain's parameters from the envelope's state.
void ampFields(const AmpEnv& e, float* f[], int i) {
    float g = float(e.gain), m = 1, a = 0, lo = -kBig, hi = kBig;
    switch (e.stage) {
    case AmpEnv::Attack: case AmpEnv::Fade:
        a = float(e.step);
        if (e.step >= 0) hi = float(e.target); else lo = float(e.target);
        break;
    case AmpEnv::Release:
        if (e.ramp) { a = float(e.step); lo = 0; break; }
        [[fallthrough]];
    case AmpEnv::Decay1: case AmpEnv::Decay2: {
        m = float(e.ratio);
        const float t = float(std::pow(10.0, e.target / 20.0));
        if (e.step >= 0) { hi = t; lo = 0; } else lo = t;
        break;
    }
    case AmpEnv::Done: g = 0; break;
    default: break;
    }
    f[0][i] = g; f[1][i] = m; f[2][i] = a; f[3][i] = lo; f[4][i] = hi;
}

// ---- the pitch and filter envelopes: the fork's ElementPitchEnvelope ----
void centsEnter(CentsEnv& e, CentsEnv::Stage stage, double sr) {
    e.stage = stage;
    if (stage == CentsEnv::Sustain || stage == CentsEnv::Done) { e.step = 0; e.left = 0; return; }
    const int k = int(stage);  // Hold, Attack, Decay1, Decay2 are 0..3; Release 5 -> 4
    const int at = stage == CentsEnv::Release ? 4 : k;
    double seconds = stage == CentsEnv::Hold ? laws::pitchHoldSeconds(e.time[0]) : 1.5 * laws::decaySecondsPer60dB(e.time[at]);
    if (stage == CentsEnv::Release && e.filter) seconds *= laws::filterReleaseFactor(e.time[4]);
    e.left = std::max<int64_t>(1, std::llround(seconds * sr));
    e.target = e.level[at];
    e.step = (e.target - e.cents) / double(e.left);
}
// Brings the envelope to include sample `to`; returns the cents there.
float centsAdvance(CentsEnv& e, int64_t to, double sr) {
    int64_t steps = to - e.last;
    if (steps > 0) {
        e.last = to;
        while (steps > 0 && e.moving()) {
            if (steps < e.left) { e.cents += e.step * double(steps); e.left -= steps; break; }
            steps -= e.left;
            e.cents = e.target;  // a segment's last sample is its target
            const CentsEnv::Stage next = e.stage == CentsEnv::Hold ? CentsEnv::Attack : e.stage == CentsEnv::Attack ? CentsEnv::Decay1
                                       : e.stage == CentsEnv::Decay1 ? CentsEnv::Decay2 : e.stage == CentsEnv::Decay2 ? CentsEnv::Sustain : CentsEnv::Done;
            centsEnter(e, next, sr);
        }
    }
    return float(e.cents);
}
void centsStart(CentsEnv& e, double sr) {
    e.cents = e.level[0];  // the hold level (the hold segment's target is the same level)
    centsEnter(e, e.time[0] > 0 ? CentsEnv::Hold : CentsEnv::Attack, sr);
}

// ==== the kernels ===================================================================================

struct Herm {
    static constexpr int H = 1, A = 2;
    static PV_INLINE vf curve(vf xm1, vf x0, vf x1, vf x2, vf f) {
        vf c1 = splat(0.5f) * (x1 - xm1);
        vf c2 = fma(splat(2.f), x1, fma(splat(-2.5f), x0, xm1)) - splat(0.5f) * x2;
        vf c3 = fma(splat(0.5f), x2 - xm1, splat(1.5f) * (x0 - x1));
        return fma(fma(fma(c3, f, c2), f, c1), f, x0);
    }
    template <int C> static PV_INLINE void at(const Tables&, const float* const* b, vp pos, const int32_t*, vf* o) {
        alignas(16) int32_t ix[W];
        pidx(pos, ix);
        const vf fr = pfrac(pos);
#if !PV_SCALAR
        vf r[C][4];
        for (int j = 0; j < 4; ++j) {
            const float* p = b[j] + (ix[j] - 1) * C;
            if constexpr (C == 1) r[0][j] = ld4(p); else ld4x2(p, r[0][j], r[1][j]);
        }
        for (int c = 0; c < C; ++c) { transpose(r[c][0], r[c][1], r[c][2], r[c][3]); o[c] = curve(r[c][0], r[c][1], r[c][2], r[c][3], fr); }
#else
        const float* p = b[0] + (ix[0] - 1) * C;
        for (int c = 0; c < C; ++c) o[c] = curve(splat(p[c]), splat(p[C + c]), splat(p[2 * C + c]), splat(p[3 * C + c]), fr);
#endif
    }
};

// 20-tap Kaiser-windowed sinc, cutoff at 0.9 of the source's Nyquist, 256 phases with linear
// interpolation between phases (docs/design.md: the length and cutoff that meet the reference's
// measured image rejection at the lowest cost).
struct Sinc {
    static constexpr int N = kTaps, H = N / 2 - 1, A = N / 2;
    template <int C> static PV_INLINE void at(const Tables& t, const float* const* b, vp pos, const int32_t* kernel, vf* o) {
        alignas(16) int32_t ix[W], row[W];
        pidx(pos, ix);
        vf u;
        prow(pos, 2 * N, row, u);
        for (int j = 0; j < W; ++j) row[j] += kernel[j];  // each voice's kernel row (the top 8 fraction bits) and the rest of the fraction
        static_assert(kPhases == 256, "prow takes the top 8 fraction bits");
#if !PV_SCALAR
        alignas(16) float up[4];
        store(up, u);
        vf acc[C][4];
        for (int j = 0; j < 4; ++j) {
            const float* r = t.sinc + row[j];
            const float* p = b[j] + (ix[j] - H) * C;
            const vf uj = splat(up[j]);
            vf a0 = splat(0.f), a1 = splat(0.f);
            for (int k = 0; k < N; k += 4) {
                vf cf = fma(uj, load(r + 2 * k + 4), load(r + 2 * k));
                if constexpr (C == 1) a0 = fma(ld4(p + k), cf, a0);
                else { vf l, rr; ld4x2(p + 2 * k, l, rr); a0 = fma(l, cf, a0); a1 = fma(rr, cf, a1); }
            }
            acc[0][j] = a0; if constexpr (C == 2) acc[1][j] = a1;
        }
        for (int c = 0; c < C; ++c) o[c] = hsum4(acc[c][0], acc[c][1], acc[c][2], acc[c][3]);
#else
        const float* r = t.sinc + row[0];
        const float* p = b[0] + (ix[0] - H) * C;
        for (int c = 0; c < C; ++c) {
            float a = 0;
            for (int k = 0; k < N; ++k) a += p[k * C + c] * (r[coefAt(k)] + u.v * r[coefAt(k) + 4]);
            o[c] = {a};
        }
#endif
    }
};

struct Lane {
    Pool* p; int o;
    PV_INLINE vf operator()(int k) const { return load(&p->f[k][o]); }
    PV_INLINE void set(int k, vf x) const { store(&p->f[k][o], x); }
};

// Designs the sections for the lane's cutoffs at this rate: the fork's ElementFilter::design with
// its pin(), the coefficients of its runDirect loop. The cutoff is C0 + CENTS cents from 440 Hz, at
// most the sections' 7290 Hz.
PV_INLINE void design(const Lane& l, const FilterRate& fr) {
    const vf one = splat(1.f);
    static const float topCents = float(1200.0 * std::log2(laws::kTopHz / 440.0));
    const vf cents = vmin(l(C0) + l(CENTS), splat(topCents));
    const vf h = exp2v(cents * splat(1.f / 1200.f)) * splat(440.f);
    vf th = h * splat(float(2 * kPi / laws::kReferenceRate));
    vf a0 = sinv(th * splat(0.5f));
    vf f = a0 + a0;  // 2 sin(pi h / 44100)
    // the damping limit (laws::dampingLimit): 2, falling by 0.0104 a cutoff index past index 816,
    // the index linear in cents between the measured knots
    vf lim = splat(2.f);
    if (any(splat(laws::kDampingKnotCents[0]) < cents)) {
        vf steps = splat(0.f);
        for (int k = 0; k + 1 < laws::kDampingKnots; ++k) {
            const float c0 = laws::kDampingKnotCents[k], w = 1.0f / (laws::kDampingKnotCents[k + 1] - c0);
            steps = steps + vmin(vmax((cents - splat(c0)) * splat(w), splat(0.f)), one);
        }
        lim = vmax(one, splat(2.f) - steps * splat(8 * 0.0104f));
    }
    vf d = vmin(l(DAMP), lim);
    vf g, c1, c2;
    if (fr.allpass) {
        vf ph = th * splat(float(1.0 / fr.ratio));
        vf a1 = sinv(th * splat(0.5f) + ph), bb = sinv(ph - th * splat(0.5f));
        vf idet = one / (a0 * splat(fr.pin2[1]) - a1 * splat(fr.pin2[0]));
        c1 = (bb * splat(fr.pin2[1]) - a1 * splat(fr.pin2[2])) * idet;
        c2 = (a0 * splat(fr.pin2[2]) - bb * splat(fr.pin2[0])) * idet;
        g = c2;
    } else {
        g = c1 = c2 = splat(0.f);  // a unit delay: the plain 44.1 kHz section
    }
    vf k = one / (one - g);
    vf cs = fma(k, f, fma(k * k * f, g, fma(d * g, k, d)));
    vf inv = one / fma(k * k * f * f, g, fma(d * g * k, f, one));
    l.set(CINV, inv); l.set(CCI, cs * inv); l.set(CKI, k * inv); l.set(CKF, k * f); l.set(CK, k); l.set(CKFG, k * f * g);
    l.set(CC1, c1); l.set(CC2, c2);
    if (fr.allpass) { l.set(CC1M, c1 * (one - c2)); l.set(CC2M, one - c2 * c2); }
    else { l.set(CC1M, one); l.set(CC2M, splat(0.f)); }
}

// The sections' coefficients (both alike for type 0), and one section's step: the fork's directRun
// (high, band and low from the delayed values, then the allpass states), on the caller's locals.
struct Coefs {
    vf inv, ci, ki, kf, k, kfg, c1, c2, c1m, c2m;
    explicit PV_INLINE Coefs(const Lane& l, int b = CINV)
        : inv(l(b)), ci(l(b + 1)), ki(l(b + 2)), kf(l(b + 3)), k(l(b + 4)), kfg(l(b + 5)), c1(l(b + 6)), c2(l(b + 7)), c1m(l(b + 8)), c2m(l(b + 9)) {}
};
PV_INLINE vf section(const Coefs& q, vf x, vf& low1, vf& low2, vf& band1, vf& band2) {
    const vf high = q.inv * x - q.ci * band1 - q.ki * low1;
    const vf band = fma(q.kf, high, q.k * band1);
    const vf low = fma(q.kfg, band, fma(q.kf, band1, q.k * low1));
    const vf n1 = fma(q.c1m, low, low2 - q.c1 * low1);
    low2 = q.c2m * low - q.c2 * low1; low1 = n1;
    const vf m1 = fma(q.c1m, band, band2 - q.c1 * band1);
    band2 = q.c2m * band - q.c2 * band1; band1 = m1;
    return low;
}
// The same section's high-pass output (types 4 and 5: the fixed section, damping 1)
PV_INLINE vf highSection(const Coefs& q, vf x, vf& low1, vf& low2, vf& band1, vf& band2) {
    const vf high = q.inv * x - q.ci * band1 - q.ki * low1;
    const vf band = fma(q.kf, high, q.k * band1);
    const vf low = fma(q.kfg, band, fma(q.kf, band1, q.k * low1));
    const vf n1 = fma(q.c1m, low, low2 - q.c1 * low1);
    low2 = q.c2m * low - q.c2 * low1; low1 = n1;
    const vf m1 = fma(q.c1m, band, band2 - q.c1 * band1);
    band2 = q.c2m * band - q.c2 * band1; band1 = m1;
    return high;
}
// The one-pole low-pass (type 5): y = (f x + (1 - f) y') inv, y' the reference-rate delay of y (the
// fork's designOnePole and run); its coefficients sit in the section's k (1 - f) inv and kf (f inv)
PV_INLINE vf pole(const Coefs& q, vf x, vf& low1, vf& low2) {
    const vf low = fma(q.k, low1, q.kf * x);
    const vf n1 = fma(q.c1m, low, low2 - q.c1 * low1);
    low2 = q.c2m * low - q.c2 * low1; low1 = n1;
    return low;
}

// The one-pole's coefficients for the lane's cutoffs (the fork's designOnePole)
PV_INLINE void designPole(const Lane& l, const FilterRate& fr) {
    const vf one = splat(1.f);
    static const float topCents = float(1200.0 * std::log2(laws::kTopHz / 440.0));
    const vf cents = vmin(l(C0) + l(CENTS), splat(topCents));
    const vf h = exp2v(cents * splat(1.f / 1200.f)) * splat(440.f);
    vf th = h * splat(float(2 * kPi / laws::kReferenceRate));
    vf a0 = sinv(th * splat(0.5f));
    vf f = vmin(one, a0 + a0);
    vf g, c1, c2;
    if (fr.allpass) {
        vf ph = th * splat(float(1.0 / fr.ratio));
        vf a1 = sinv(th * splat(0.5f) + ph), bb = sinv(ph - th * splat(0.5f));
        vf idet = one / (a0 * splat(fr.pin2[1]) - a1 * splat(fr.pin2[0]));
        c1 = (bb * splat(fr.pin2[1]) - a1 * splat(fr.pin2[2])) * idet;
        c2 = (a0 * splat(fr.pin2[2]) - bb * splat(fr.pin2[0])) * idet;
        g = c2;
    } else {
        g = c1 = c2 = splat(0.f);
    }
    vf inv = one / (one - (one - f) * g);
    l.set(CK, (one - f) * inv); l.set(CKF, f * inv);
    l.set(CC1, c1); l.set(CC2, c2);
    if (fr.allpass) { l.set(CC1M, c1 * (one - c2)); l.set(CC2M, one - c2 * c2); }
    else { l.set(CC1M, one); l.set(CC2M, splat(0.f)); }
}

// The fixed high-pass section of types 4 and 5 for one voice (the fork's design at damping 1, in the
// same float arithmetic as design()), into the H fields
void designHigh(Pool& p, uint32_t i, float hz, const FilterRate& fr) {
    const double h = std::min<double>(hz, laws::kTopHz);
    const float th = float(h * 2 * kPi / laws::kReferenceRate);
    const float a0 = std::sin(th * 0.5f), f = a0 + a0, d = std::min(1.0f, laws::dampingLimit(h));
    float g = 0, c1 = 0, c2 = 0;
    if (fr.allpass) {
        const float ph = th * float(1.0 / fr.ratio);
        const float a1 = std::sin(th * 0.5f + ph), bb = std::sin(ph - th * 0.5f);
        const float idet = 1.0f / (a0 * fr.pin2[1] - a1 * fr.pin2[0]);
        c1 = (bb * fr.pin2[1] - a1 * fr.pin2[2]) * idet;
        c2 = (a0 * fr.pin2[2] - bb * fr.pin2[0]) * idet;
        g = c2;
    }
    const float k = 1.0f / (1.0f - g);
    const float cs = k * f + k * k * f * g + d * g * k + d;
    const float inv = 1.0f / (1.0f + k * k * f * f * g + d * g * k * f);
    p.f[HINV][i] = inv; p.f[HCI][i] = cs * inv; p.f[HKI][i] = k * inv; p.f[HKF][i] = k * f; p.f[HK][i] = k; p.f[HKFG][i] = k * f * g;
    p.f[HC1][i] = c1; p.f[HC2][i] = c2;
    p.f[HC1M][i] = fr.allpass ? c1 * (1.0f - c2) : 1.0f; p.f[HC2M][i] = fr.allpass ? 1.0f - c2 * c2 : 0.0f;
}

}  // namespace

// ==== the engine ====================================================================================

// A lane's output into the mix: R (routed) sends each voice to its output bus by its masks, bus k's
// samples at acc + k kSub; otherwise everything to acc (bus 0, as before)
struct Buses { vf m0, m1, m2; };
template <bool R> PV_INLINE Buses busesOf(const Lane& l) {
    if constexpr (R) { const vf m1 = l(B1), m2 = l(B2); return {splat(1.f) - m1 - m2, m1, m2}; }
    else return {splat(1.f), splat(0.f), splat(0.f)};
}
template <bool R> PV_INLINE void put(vf* acc, int s, vf y, vf g, const Buses& b) {
    if constexpr (!R) acc[s] = fma(y, g, acc[s]);
    else {
        const vf v = y * g;
        acc[s] = fma(v, b.m0, acc[s]); acc[kSub + s] = fma(v, b.m1, acc[kSub + s]); acc[2 * kSub + s] = fma(v, b.m2, acc[2 * kSub + s]);
    }
}

struct Kernel {
    // One piece of n frames (n <= kSub) for the voices of a pool, added into accL/accR (R: per bus).
    template <int C, class I, int K, bool R>
    static void run(Pool& p, const Tables& t, Scratch& sc, int n, vf* accL, vf* accR) {
        const int lanes = int(p.n + W - 1) / W;
        for (int ln = 0; ln < lanes; ++ln) {
            const int o = ln * W;
            const float* base[W];
            feedLane<C, I>(p, o, n, sc, base);
            vf buf[C][kSub];
            fetchLane<C, I>(p, t, o, n, base, buf);
            if constexpr (K != 0) {  // and the mix; the element EQ only in lanes that use it
                bool eq = false;
                for (int j = 0; j < W && o + j < int(p.n); ++j) eq |= p.v[o + j].eq;
                if (eq) filterLane<C, K, R, true>(p, o, n, buf, accL, accR);
                else filterLane<C, K, R, false>(p, o, n, buf, accL, accR);
            } else mixLane<C, R>(p, o, n, buf, accL, accR);
        }
    }
    // each voice's base pointer for this piece's taps (idle lanes read zeros)
    template <int C, class I> static PCM_STAGE void feedLane(Pool& p, int o, int n, Scratch& sc, const float** base) {
        static const float zeros[(Sinc::N + 4) * 2] = {};
        for (int j = 0; j < W; ++j) {
            const int i = o + j;
            if (i < int(p.n)) base[j] = feed<C>(p, i, n, I::H, I::A, sc.win[j]);
            else { base[j] = zeros + (Sinc::N / 2 + 1) * C; p.pos[i] = p.step[i] = p.dstep[i] = 0; }
        }
    }
    // the interpolated source, per sample, the pitch ramped per sample
    // the interpolated source times the amplitude envelope, per sample: g = min(max(g m + a, lo), hi);
    // the pitch ramped per sample while it moves
    template <int C, class I> static PCM_STAGE void fetchLane(Pool& p, const Tables& t, int o, int n, const float* const* base, vf (*buf)[kSub]) {
        const Lane l{&p, o};
        vf g = l(G); const vf m = l(M), a = l(A), lo = l(LO), hi = l(HI);
        vp pos = pload(&p.pos[o]), stp = pload(&p.step[o]);
        bool ramp = false;
        for (int j = 0; j < W; ++j) ramp |= p.dstep[o + j] != 0;
        if (ramp) {
            const vp ds = pload(&p.dstep[o]);
            for (int s = 0; s < n; ++s) {
                vf out[C];
                I::template at<C>(t, base, pos, &p.kernel[o], out);
                g = vmin(vmax(fma(g, m, a), lo), hi);
                for (int c = 0; c < C; ++c) buf[c][s] = out[c] * g;
                stp = stp + ds; pos = pos + stp;
            }
            pstore(&p.step[o], stp);
        } else {
            for (int s = 0; s < n; ++s) {
                vf out[C];
                I::template at<C>(t, base, pos, &p.kernel[o], out);
                g = vmin(vmax(fma(g, m, a), lo), hi);
                for (int c = 0; c < C; ++c) buf[c][s] = out[c] * g;
                pos = pos + stp;
            }
        }
        pstore(&p.pos[o], pos);
        l.set(G, g);
    }
    // the two sections in series, each channel's states in locals, then into the mix with its gain
    // ramp (a mono voice's one channel to both sides)
    // K: the filter (1 type 0, 2 type 4, 3 type 5)
    template <int K> static PV_INLINE vf chain(const Coefs& q, const Coefs& h, vf x, vf& a1, vf& a2, vf& a3, vf& a4, vf& b1, vf& b2, vf& b3, vf& b4) {
        if constexpr (K == 1) return section(q, section(q, x, a1, a2, a3, a4), b1, b2, b3, b4);
        else if constexpr (K == 2) return highSection(h, section(q, x, a1, a2, a3, a4), b1, b2, b3, b4);
        else return highSection(h, pole(q, x, a1, a2), b1, b2, b3, b4);
    }
    // the element EQ's biquad (transposed direct form II): y = b0 x + s1; s1 = b1 x - a1 y + s2; s2 = b2 x - a2 y
    struct Eq { vf b0, b1, b2, a1, a2; };
    static PV_INLINE vf eqStep(const Eq& e, vf x, vf& s1, vf& s2) {
        const vf y = fma(e.b0, x, s1);
        s1 = fma(e.b1, x, s2) - e.a1 * y;
        s2 = e.b2 * x - e.a2 * y;
        return y;
    }
    template <int C, int K, bool R, bool E> static PCM_STAGE void filterLane(Pool& p, int o, int n, vf (*buf)[kSub], vf* accL, vf* accR) {
        const Lane l{&p, o};
        const Coefs q(l), h(l, K == 1 ? CINV : HINV);
        const Buses bs = busesOf<R>(l);
        Eq eq{};
        if constexpr (E) eq = {l(EB0), l(EB1), l(EB2), l(EA1), l(EA2)};
        for (int c = 0; c < C; ++c) {
            vf a1 = l(st(0, c, 0)), a2 = l(st(0, c, 1)), a3 = l(st(0, c, 2)), a4 = l(st(0, c, 3));
            vf b1 = l(st(1, c, 0)), b2 = l(st(1, c, 1)), b3 = l(st(1, c, 2)), b4 = l(st(1, c, 3));
            vf e1 = splat(0.f), e2 = splat(0.f);
            if constexpr (E) { e1 = l(ES0 + 2 * c); e2 = l(ES0 + 2 * c + 1); }
            const vf* x = buf[c];
            if constexpr (C == 1) {
                vf gl = l(GL), gr = l(GR); const vf dgl = l(DGL), dgr = l(DGR);
                for (int s = 0; s < n; ++s) {
                    vf y = chain<K>(q, h, x[s], a1, a2, a3, a4, b1, b2, b3, b4);
                    if constexpr (E) y = eqStep(eq, y, e1, e2);
                    gl = gl + dgl; gr = gr + dgr;
                    put<R>(accL, s, y, gl, bs); put<R>(accR, s, y, gr, bs);
                }
                l.set(GL, gl); l.set(GR, gr);
            } else {
                vf g = l(c ? GR : GL); const vf dg = l(c ? DGR : DGL);
                vf* acc = c ? accR : accL;
                for (int s = 0; s < n; ++s) {
                    vf y = chain<K>(q, h, x[s], a1, a2, a3, a4, b1, b2, b3, b4);
                    if constexpr (E) y = eqStep(eq, y, e1, e2);
                    g = g + dg;
                    put<R>(acc, s, y, g, bs);
                }
                l.set(c ? GR : GL, g);
            }
            l.set(st(0, c, 0), a1); l.set(st(0, c, 1), a2); l.set(st(0, c, 2), a3); l.set(st(0, c, 3), a4);
            l.set(st(1, c, 0), b1); l.set(st(1, c, 1), b2); l.set(st(1, c, 2), b3); l.set(st(1, c, 3), b4);
            if constexpr (E) { l.set(ES0 + 2 * c, e1); l.set(ES0 + 2 * c + 1, e2); }
        }
    }
    template <int C, bool R> static PCM_STAGE void mixLane(Pool& p, int o, int n, vf (*buf)[kSub], vf* accL, vf* accR) {
        Lane l{&p, o};
        const Buses bs = busesOf<R>(l);
        vf gl = l(GL), gr = l(GR); const vf dgl = l(DGL), dgr = l(DGR);
        for (int s = 0; s < n; ++s) {
            gl = gl + dgl; gr = gr + dgr;
            put<R>(accL, s, buf[0][s], gl, bs); put<R>(accR, s, buf[C - 1][s], gr, bs);
        }
        l.set(GL, gl); l.set(GR, gr);
    }

    // A voice's tap window for n frames, converted to float once (each frame serves every output
    // frame whose taps reach it): a base pointer b with b[v * C] the source frame of virtual frame v
    // over [first - H, last + A], from the sample or across a loop end, stitched.
    template <int C> static const float* feed(Pool& p, int i, int n, int H, int A, float* win) {
        VoiceScalar& v = p.v[i];
        int64_t p0 = p.pos[i];
        const int64_t s0 = p.step[i], d = p.dstep[i];
        if (v.loop) {
            const int64_t L = v.loopEnd - v.loopStart;
            if ((p0 >> 32) > (int64_t(1) << 30)) {  // keep virtual positions within int32
                const int64_t k = ((p0 >> 32) - v.loopEnd) / L;
                p0 -= (k * L) << 32; p.pos[i] = p0; v.cycEnd -= k * L; v.shift -= k * L;
            }
        }
        const int64_t last = p0 + int64_t(n - 1) * s0 + d * int64_t(n - 1) * n / 2;
        const int64_t lo = (p0 >> 32) - H, hi = (last >> 32) + A;
        const int count = int(hi - lo + 1);
        if (!v.loop) {
            if ((p0 >> 32) >= v.frames) v.ended = true;  // past the end: the reads stay in the zero guard
            convert(v.data + lo * C, win, count * C);
            return win - lo * C;
        }
        const int64_t L = v.loopEnd - v.loopStart;
        while (lo >= v.cycEnd) { v.cycEnd += L; v.shift += L; }
        if (hi < v.cycEnd) { convert(v.data + (lo - v.shift) * C, win, count * C); return win - lo * C; }
        // across the loop end (possibly more than once for a short loop): segment by segment
        int64_t at = lo - v.shift;  // real frame of virtual lo
        for (int64_t x = lo; x <= hi;) {
            const int64_t k = std::min<int64_t>(hi - x + 1, v.loopEnd - at);
            convert(v.data + at * C, win + (x - lo) * C, int(k) * C);
            x += k; at += k;
            if (at >= v.loopEnd) at -= L;
        }
        return win - lo * C;
    }
    // int16 to float, four at a time (may write up to three values past `count`; reads stay in the
    // sample's guard frames)
    static PV_INLINE void convert(const int16_t* src, float* dst, int count) {
#if PV_SCALAR
        for (int k = 0; k < count; ++k) dst[k] = float(src[k]);
#else
        for (int k = 0; k < count; k += 4) storeu(dst + k, ld4(src + k));
#endif
    }
};

namespace {
using RunFn = void (*)(Pool&, const Tables&, Scratch&, int, vf*, vf*);
template <int C, class I, int K, bool R> void runOf(Pool& p, const Tables& t, Scratch& s, int n, vf* l, vf* r) { Kernel::run<C, I, K, R>(p, t, s, n, l, r); }
// [routed][channels - 1][interp][filter]
#define PCM_KERNELS(R) \
    {{{runOf<1, Herm, 0, R>, runOf<1, Herm, 1, R>, runOf<1, Herm, 2, R>, runOf<1, Herm, 3, R>}, {runOf<1, Sinc, 0, R>, runOf<1, Sinc, 1, R>, runOf<1, Sinc, 2, R>, runOf<1, Sinc, 3, R>}}, \
     {{runOf<2, Herm, 0, R>, runOf<2, Herm, 1, R>, runOf<2, Herm, 2, R>, runOf<2, Herm, 3, R>}, {runOf<2, Sinc, 0, R>, runOf<2, Sinc, 1, R>, runOf<2, Sinc, 2, R>, runOf<2, Sinc, 3, R>}}}
const RunFn kKernels[2][2][2][kFilters] = {PCM_KERNELS(false), PCM_KERNELS(true)};
#undef PCM_KERNELS
}  // namespace

Core::Core() {
    // the interpolation kernels: Kaiser beta 7.5, one per cutoff (kernelCut)
    constexpr double beta = PCM_KERNEL_BETA;
    for (int k = 0; k < kKernelBank; ++k) {
        float* tab = tables_.sinc + k * kKernelFloats;
        const double cut = kernelCut(k);
        for (int ph = 0; ph <= kPhases; ++ph)
            for (int i = 0; i < kTaps; ++i) {
                const double x = double(ph) / kPhases + kTaps / 2 - 1 - i;
                const double u = x / (kTaps / 2);
                const double w = std::fabs(u) >= 1 ? 0 : besselI0(beta * std::sqrt(1 - u * u)) / besselI0(beta);
                const double s = x == 0 ? 1 : std::sin(kPi * cut * x) / (kPi * cut * x);
                tab[ph * 2 * kTaps + coefAt(i)] = float(cut * s * w);
            }
        for (int ph = 0; ph <= kPhases; ++ph)
            for (int i = 0; i < kTaps; ++i)
                tab[ph * 2 * kTaps + coefAt(i) + 4] = ph < kPhases ? tab[(ph + 1) * 2 * kTaps + coefAt(i)] - tab[ph * 2 * kTaps + coefAt(i)] : 0.0f;
    }
    for (uint32_t s = 0; s < kMaxSounds; ++s) gain_[s] = panL_[s] = panR_[s] = 1;
    for (int k = 0; k < 2 * kFilters; ++k) {
        Pool& p = pool_[k];
        p.plan = {uint8_t(k / kFilters + 1), Interp::Sinc, Filter(k % kFilters)};
        std::memset(p.f, 0, sizeof p.f);
        for (uint32_t i = 0; i < kPoolVoices; ++i) { p.f[C0][i] = p.f[CENTS][i] = 0; p.f[DAMP][i] = 2; p.f[EB0][i] = 1; p.pos[i] = p.step[i] = p.dstep[i] = 0; }
        for (auto& k : p.kernel) k = 0;
    }
    prepare(48000);
}

void Core::prepare(double sr) {
    sr_ = sr > 0 ? sr : 48000;
    reset();
    for (auto& s : sound_) s = nullptr;
    // the fork's ElementFilter::setSampleRate
    const double r = sr_ / laws::kReferenceRate, whole = std::round(r);
    rate_.ratio = r;
    rate_.allpass = std::fabs(r - whole) > 1e-9 || whole < 1.0;
    const int delay = rate_.allpass ? std::max(0, int(std::floor(r)) - 2) : int(whole);
    rate_.covered = r >= 1.0 && (rate_.allpass ? delay == 0 : whole == 1.0);
    if (!rate_.covered || r < 1.0) rate_.allpass = false;  // shortcut: other rates play the plain 44.1 kHz sections
    const double theta = 2.0 * kPi * 18000.0 / laws::kReferenceRate, phi = theta / r, alpha = (theta - 2.0 * phi) / 2.0;
    rate_.pin2[0] = float(std::sin(phi + alpha)); rate_.pin2[1] = float(std::sin(2.0 * phi + alpha)); rate_.pin2[2] = float(-std::sin(alpha));
}

void Core::install(uint32_t s, const Compiled* c) {
    if (s >= kMaxSounds) return;
    for (uint32_t v = 0; v < kMaxVoices; ++v) if (slot_[v].on && slot_[v].sound == s) kill(v);
    sound_[s] = c && c->loaded ? c : nullptr;
}

void Core::setGain(uint32_t s, float gain, float pan) {
    if (s >= kMaxSounds) return;
    gain_[s] = gain;
    panL_[s] = pan > 0 ? 1 - pan : 1;
    panR_[s] = pan < 0 ? 1 + pan : 1;
    for (auto& p : pool_) for (uint32_t i = 0; i < p.n; ++i) if (p.v[i].sound == s) { p.v[i].soundGain = gain; p.v[i].soundPanL = panL_[s]; p.v[i].soundPanR = panR_[s]; p.v[i].dirty = true; }
}

Pool& Core::poolOf(const PlanDesc& d) { return pool_[(d.channels == 2 ? kFilters : 0) + int(d.filter)]; }

void Core::noteOn(uint32_t voice, uint32_t sound, int channel, int key, float velocity) {
    if (voice >= kMaxVoices || sound >= kMaxSounds) return;
    kill(voice);
    const Compiled* s = sound_[sound];
    if (!s) return;
    const int played = std::max(1, std::min(127, int(std::lround(velocity * 127.0f))));
    // the part's velocity sensitivity: what every law and the velocity limits see (measured)
    const int32_t vel2048 = laws::partVelocity2048(played, s->velDepth, s->velOffset);
    const int vel = laws::velocityFrom2048(vel2048);
    Slot& sl = slot_[voice];
    sl = Slot{};
    sl.sound = uint8_t(sound);
    sl.key = uint8_t(key); sl.vel = uint8_t(vel); sl.vel2048 = int16_t(vel2048); sl.channel = uint8_t(channel & 15);
    for (uint32_t i = 0; i < s->regions && sl.layers < kMaxLayers; ++i) {
        const Region& r = s->region[i];
        if (key < r.loKey || key > r.hiKey || vel < r.loVel || vel > r.hiVel) continue;
        if (s->element[r.element].v[TriggerMode] == 2) { sl.keyOff = true; continue; }  // sounds from the key's release
        startLayer(voice, *s, i, key, vel, vel2048, channel);
    }
    sl.on = sl.layers > 0 || sl.keyOff;
}

void Core::startLayer(uint32_t voice, const Compiled& s, uint32_t ri, int key, int vel, int32_t vel2048, int channel) {
    const Region& r = s.region[ri];
    const SampleView& sv = s.sample[ri];
    Pool& p = poolOf(s.plan[ri]);
    if (p.n >= kPoolVoices) return;  // shortcut: a full pool skips the layer (64 notes of at most two layers fit)
    const int16_t* e = s.element[r.element].v;
    const uint32_t i = p.n++;
    VoiceScalar& v = p.v[i];
    v = VoiceScalar{};
    Slot& sl = slot_[voice];
    v.id = nextId_++; if (!nextId_) nextId_ = 1;
    sl.pool[sl.layers] = uint8_t(&p - pool_); sl.id[sl.layers] = v.id; ++sl.layers;
    v.slot = uint8_t(voice); v.sound = sl.sound; v.channel = uint8_t(channel & 15); v.key = uint8_t(key);
    // the output bus: the element's connection (thru, insertion A, B) when the sound splits its outputs
    v.bus = s.splitOutputs ? uint8_t(e[ElementConnection]) : 0;
    p.f[B1][i] = v.bus == 1 ? 1.f : 0.f; p.f[B2][i] = v.bus == 2 ? 1.f : 0.f;
    v.start = clock_;
    // the sample
    v.data = sv.data; v.frames = sv.frames;
    v.loop = r.loop != 0; v.loopStart = r.loopStart; v.loopEnd = r.loopEnd;
    v.cycEnd = v.loop ? v.loopEnd : (int64_t(1) << 62); v.shift = 0;
    // pitch: the fork's float arithmetic (keytrack 100 cents, the region's tune and transpose)
    const float cents = 100.0f * (float(key) - float(r.root)) + float(r.tune) / 100.0f + 100.0f * float(r.transpose);
    const float ratio = std::pow(2.0f, cents / 1200.0f) * float(double(sv.rate) / sr_);
    v.baseStep = std::min<double>(ratio, kMaxStep);
    p.pos[i] = 0;
    p.step[i] = int64_t(std::llround(v.baseStep * 4294967296.0));
    p.dstep[i] = 0;
    // level: velocity, level scaling, element level, the region's level, staging
    laws::Velocity vl{e[AmpVelSens], e[AmpVelOffset], e[AmpVelCurve]};
    float level = laws::velocityGain2048(vl, vel2048);
    bool scaling = e[AmpKeyFollow] != 0;
    for (int k = 0; k < 4; ++k) scaling |= e[AmpScalingOffset1 + k] != 0;
    if (scaling) {
        laws::LevelScaling ls;
        for (int k = 0; k < 4; ++k) { ls.keys[k] = e[AmpScalingKey1 + k]; ls.offsets[k] = e[AmpScalingOffset1 + k]; }
        ls.keyFollow = e[AmpKeyFollow]; ls.level = e[AmpLevel];
        level *= laws::levelScalingGain(ls, key);
    }
    level *= laws::levelGain(e[AmpLevel]);
    level *= std::pow(10.0f, (float(r.level) / 100.0f) * 0.05f);
    level *= float(std::pow(10.0, kStagingDb / 20.0));
    level *= 1.0f / 32768.0f;  // 16-bit sample values to full scale
    if (s.plan[ri].filter == Filter::Thru && e[ElementNumber] >= 2) level *= float(std::pow(10.0, laws::kThruElementDb / 20.0));
    if (s.plan[ri].filter != Filter::Thru) {
        level *= laws::filterGain(e[FilGain]);
        // element EQ Boost6/12/18: a flat +6.02/+12.04/+18.06 dB; bypassed with the filter Thru (measured)
        if (e[EqType] >= 2 && e[EqType] <= 4) level *= float(1 << (e[EqType] - 1));
    }
    v.level = level;
    // pan: the key bank's pan (the region's, -100..100 as raw 1..127) and the element's added in raw
    // steps, then the element's balance law, mono and stereo alike (measured: docs/acceptance.md (c))
    const int bankPan = int(std::lround(float(r.pan) / 10000.0f * 63.0f));
    laws::balanceGains(std::max(1, std::min(127, e[AmpPan] + bankPan)), v.panL, v.panR);
    v.soundGain = gain_[sl.sound]; v.soundPanL = panL_[sl.sound]; v.soundPanR = panR_[sl.sound];
    // the amplitude envelope with its time shifts
    AmpEnv& a = v.amp;
    laws::TimeScaling ts{e[AmpEgVelTimeSegments], e[AmpEgVelTime], e[AmpEgKeyTime], e[AmpEgKeyTimeCenter], e[AmpEgKeyTimeRelease]};
    const laws::TimeShifts sh = laws::timeShifts(ts, key, vel);
    auto clampIndex = [](int x) { return std::max(0, std::min(127, x)); };
    a.attackTime = clampIndex(e[AmpEgAttack] - sh.attack);
    a.decay1Time = clampIndex(e[AmpEgDecay1] - sh.decay);
    a.decay2Time = clampIndex(e[AmpEgDecay2] - sh.decay);
    a.releaseTime = clampIndex(e[AmpEgRelease] - sh.release);
    v.releaseBase = a.releaseTime;
    v.halfDamper = e[HalfDamper] != 0; v.halfDamperTime = e[HalfDamperTime];
    a.initialLevel = e[AmpEgInitialLevel]; a.attackLevel = e[AmpEgAttackLevel];
    a.decay1Level = e[AmpEgDecay1Level]; a.decay2Level = e[AmpEgDecay2Level];
    a.gain = laws::levelGain(a.initialLevel);
    a.db = toDb(a.gain);
    a.last = clock_ - 1;
    ampEnter(a, AmpEnv::Attack, sr_);
    // the pitch envelope (37.5 cents a step) and the filter envelope (75)
    auto cenv = [&](CentsEnv& c, int base, int depth, int velDepth, int curve, int segs, int vt, int kt, int ktc, float perStep, float& scale) {
        scale = laws::filterDepthVelocityFactor(e[velDepth], e[curve], vel);
        int32_t t[5] = {e[base], e[base + 1], e[base + 2], e[base + 3], e[base + 4]};
        if (e[vt] != 0 || e[kt] != 0) {
            laws::TimeScaling s2{e[segs], e[vt], e[kt], e[ktc], 127};  // shortcut (the fork's): the release gets the whole key shift
            const laws::TimeShifts h = laws::timeShifts(s2, key, vel);
            t[1] = clampIndex(t[1] - h.attack); t[2] = clampIndex(t[2] - h.decay); t[3] = clampIndex(t[3] - h.decay); t[4] = clampIndex(t[4] - h.release);
        }
        for (int k = 0; k < 5; ++k) {
            c.time[k] = t[k];
            c.level[k] = float(double(laws::pitchLevelCents(e[base + 5 + k], e[depth])) * (perStep / 37.5f) * scale);
        }
        c.last = clock_ - 1;
        centsStart(c, sr_);
    };
    float fs = 1, ps = 1;
    v.feg.filter = true;
    cenv(v.feg, FilEgHold, FilEgDepth, FilEgVelDepth, FilEgVelDepthCurve, FilEgVelTimeSegments, FilEgVelTime, FilEgKeyTime, FilEgKeyTimeCenter, 75.0f, fs);
    cenv(v.peg, PitchEgHold, PitchEgDepth, PitchEgVelDepth, PitchEgVelDepthCurve, PitchEgVelTimeSegments, PitchEgVelTime, PitchEgKeyTime, PitchEgKeyTimeCenter, 37.5f, ps);
    v.pegScale = ps;
    // the filter: cutoff with key follow and velocity
    const float keyCents = float(e[FilKeyFollow] - 64) * 3.125f * float(key - 60) + laws::cutoffVelocityCents(e[FilCutoff], e[FilVelSens], vel);
    p.f[C0][i] = laws::cutoffCents(float(e[FilCutoff])) + keyCents;
    p.f[DAMP][i] = laws::filterDamping(e[FilResonance]);
    for (int k = ST0; k < ST0 + 16; ++k) p.f[k][i] = 0;
    if (s.plan[ri].filter == Filter::LowHigh || s.plan[ri].filter == Filter::PoleHigh)
        designHigh(p, i, laws::filterCutoffHz(e[FilHpfCutoff], 0.0f), rate_);
    // the element EQ (with a filter only: thru bypasses it, measured)
    laws::Biquad bq;
    v.eq = s.plan[ri].filter != Filter::Thru && laws::elementEq(e[EqType], e[EqQ], e[EqFreq1], e[EqGain1], e[EqFreq2], e[EqGain2], sr_, bq);
    p.f[EB0][i] = float(bq.b0); p.f[EB1][i] = float(bq.b1); p.f[EB2][i] = float(bq.b2); p.f[EA1][i] = float(bq.a1); p.f[EA2][i] = float(bq.a2);
    for (int k = ES0; k < ES0 + 4; ++k) p.f[k][i] = 0;
    // the element LFO in reference mode, through the mod wheel's controller sets
    if (e[ElfoMode] == 0) { v.lfoPitchRatio = e[ElfoPitchRatio]; v.lfoAmpRatio = e[ElfoAmpRatio]; }
    v.fresh = true;
    v.curCents = 1e30f; v.curPitch = 0;
    float* f[5] = {p.f[G], p.f[M], p.f[A], p.f[LO], p.f[HI]};
    ampFields(a, f, int(i));
    p.f[GL][i] = p.f[GR][i] = 0; p.f[DGL][i] = p.f[DGR][i] = 0;
}

// One element voice into its release at a release index (from where its envelope stands); the cents
// envelopes enter theirs the first time.
void Core::releaseLayer(Pool& p, uint32_t i, int32_t index) {
    VoiceScalar& v = p.v[i];
    ampAdvance(v.amp, clock_ - 1, sr_);
    if (v.amp.stage != AmpEnv::Done && v.amp.stage != AmpEnv::Fade) {
        v.amp.releaseTime = index;
        ampEnter(v.amp, AmpEnv::Release, sr_);
    }
    float* f[5] = {p.f[G], p.f[M], p.f[A], p.f[LO], p.f[HI]};
    ampFields(v.amp, f, int(i));
    if (!v.releasing)
        for (CentsEnv* c : {&v.feg, &v.peg}) {
            centsAdvance(*c, clock_ - 1, sr_);
            if (c->stage != CentsEnv::Release && c->stage != CentsEnv::Done) centsEnter(*c, CentsEnv::Release, sr_);
        }
    v.releasing = true;
    v.steady = false;
}

// A half damper element's release under the pedal: its release index moves from its own (pedal up)
// to the half damper time (pedal down), in proportion to the pedal (measured: docs/acceptance.md (g))
static int32_t pedalIndex(const VoiceScalar& v, float pedal) {
    return int32_t(std::lround(v.releaseBase + (v.halfDamperTime - v.releaseBase) * pedal / 127.0f));
}

// Key-off elements: the note's regions in element trigger mode "key off" start when the note ends (the
// key's release, or the sustain pedal's when it holds the note: measured, docs/acceptance.md (g)), at
// the note's key and velocity, and release 2.0 s after they start (the fork's K42, measured there).
void Core::startKeyOff(uint32_t voice) {
    Slot& sl = slot_[voice];
    if (!sl.keyOff) return;
    sl.keyOff = false;
    const Compiled* s = sound_[sl.sound];
    if (!s) return;
    for (uint32_t ri = 0; ri < s->regions && sl.layers < kMaxLayers; ++ri) {
        const Region& r = s->region[ri];
        if (sl.key < r.loKey || sl.key > r.hiKey || sl.vel < r.loVel || sl.vel > r.hiVel || s->element[r.element].v[TriggerMode] != 2) continue;
        const uint8_t before = sl.layers;
        startLayer(voice, *s, ri, sl.key, sl.vel, sl.vel2048, sl.channel);
        if (sl.layers > before) {
            Pool& p = pool_[sl.pool[sl.layers - 1]];
            p.v[p.n - 1].releaseAt = clock_ + std::llround(2.0 * sr_);
        }
    }
}

void Core::keyUp(uint32_t voice) {
    if (voice >= kMaxVoices || !slot_[voice].on || slot_[voice].keyUp) return;
    Slot& sl = slot_[voice];
    sl.keyUp = true;
    const uint8_t layers = sl.layers;
    for (int k = 0; k < layers; ++k) {
        Pool& p = pool_[sl.pool[k]];
        for (uint32_t i = 0; i < p.n; ++i) {
            VoiceScalar& v = p.v[i];
            if (v.id != sl.id[k] || !v.halfDamper || v.releasing) continue;
            v.pedalRelease = true;
            // the envelope's decays go on under the pedal; the half damper's release starts at the
            // sustain (measured: a piano, in its decay, plays on as held; a sustaining sine releases)
            ampAdvance(v.amp, clock_ - 1, sr_);
            if (v.amp.stage == AmpEnv::Sustain) releaseLayer(p, i, pedalIndex(v, pedal_[sl.channel]));
        }
    }
}

void Core::noteOff(uint32_t voice) {
    if (voice >= kMaxVoices || !slot_[voice].on) return;
    Slot& sl = slot_[voice];
    const uint8_t layers = sl.layers;
    startKeyOff(voice);  // key-off elements sound when the note ends, under the pedal at its release (measured)
    sl.keyUp = true;
    for (int k = 0; k < layers; ++k) {
        Pool& p = pool_[sl.pool[k]];
        for (uint32_t i = 0; i < p.n; ++i) {
            VoiceScalar& v = p.v[i];
            if (v.id != sl.id[k] || v.releaseAt) continue;
            if (v.releasing && !v.pedalRelease) continue;
            v.pedalRelease = false;
            releaseLayer(p, i, v.releaseBase);  // everything up to the frame before this one; the release starts here
        }
    }
}

void Core::setSustain(int ch, float x) {
    if (ch < 0 || ch >= 16) return;
    pedal_[ch] = 127.0f * std::max(0.0f, std::min(1.0f, x));
    for (auto& p : pool_)
        for (uint32_t i = 0; i < p.n; ++i)
            if (p.v[i].channel == ch && p.v[i].pedalRelease && p.v[i].releasing) releaseLayer(p, i, pedalIndex(p.v[i], pedal_[ch]));
}

void Core::choke(uint32_t voice, uint32_t frames) {
    if (voice >= kMaxVoices || !slot_[voice].on) return;
    Slot& sl = slot_[voice];
    for (int k = 0; k < sl.layers; ++k) {
        Pool& p = pool_[sl.pool[k]];
        for (uint32_t i = 0; i < p.n; ++i) {
            VoiceScalar& v = p.v[i];
            if (v.id != sl.id[k]) continue;
            ampAdvance(v.amp, clock_ - 1, sr_);
            if (v.amp.stage == AmpEnv::Done) continue;
            v.releasing = true;
            v.steady = false;
            v.amp.stage = AmpEnv::Fade; v.amp.ramp = true; v.amp.target = 0;
            v.amp.step = -v.amp.gain / double(std::max<uint32_t>(1, frames));
            if (v.amp.step == 0) v.amp.step = -1e-12;
            float* f[5] = {p.f[G], p.f[M], p.f[A], p.f[LO], p.f[HI]};
            ampFields(v.amp, f, int(i));
            p.f[LO][i] = 0;
        }
    }
}

void Core::free_(Pool& p, uint32_t i) {
    const uint32_t last = --p.n;
    if (i != last) {
        for (int k = 0; k < NFIELDS; ++k) p.f[k][i] = p.f[k][last];
        p.pos[i] = p.pos[last]; p.step[i] = p.step[last]; p.dstep[i] = p.dstep[last]; p.kernel[i] = p.kernel[last];
        p.v[i] = p.v[last];
    }
    // the vacated slot renders silence through finite coefficients
    for (int k = ST0; k < ST0 + 16; ++k) p.f[k][last] = 0;
    p.f[G][last] = p.f[GL][last] = p.f[GR][last] = p.f[DGL][last] = p.f[DGR][last] = 0;
    p.f[C0][last] = p.f[CENTS][last] = 0; p.f[DAMP][last] = 2; p.f[B1][last] = p.f[B2][last] = 0;
    p.f[EB0][last] = 1; for (int k = EB1; k < NFIELDS; ++k) p.f[k][last] = 0;  // the EQ: identity, silent states
}

void Core::kill(uint32_t voice) {
    if (voice >= kMaxVoices) return;
    Slot& sl = slot_[voice];
    if (!sl.on) { sl = Slot{}; return; }
    for (int k = 0; k < sl.layers; ++k) {
        Pool& p = pool_[sl.pool[k]];
        for (uint32_t i = 0; i < p.n; ++i) if (p.v[i].id == sl.id[k]) { free_(p, i); break; }
    }
    sl = Slot{};
}

void Core::setVolume(uint32_t voice, float g) {
    if (voice >= kMaxVoices || !slot_[voice].on) return;
    Slot& sl = slot_[voice];
    sl.volume = g;
    for (int k = 0; k < sl.layers; ++k) {
        Pool& p = pool_[sl.pool[k]];
        for (uint32_t i = 0; i < p.n; ++i) if (p.v[i].id == sl.id[k]) { p.v[i].volume = g; p.v[i].dirty = true; }
    }
}

void Core::setPitchBend(int ch, float x) {
    if (ch < 0 || ch >= 16) return;
    bend_[ch] = std::max(-1.0f, std::min(1.0f, x));
    for (auto& p : pool_) for (uint32_t i = 0; i < p.n; ++i) if (p.v[i].channel == ch) p.v[i].dirty = true;
}
void Core::setModWheel(int ch, float x) {
    if (ch < 0 || ch >= 16) return;
    wheel_[ch] = std::max(0.0f, std::min(1.0f, x));
    for (auto& p : pool_) for (uint32_t i = 0; i < p.n; ++i) if (p.v[i].channel == ch) p.v[i].dirty = true;
}

void Core::reset() {
    for (auto& p : pool_) p.n = 0;
    for (auto& s : slot_) s = Slot{};
    for (auto& b : bend_) b = 0;
    for (auto& w : wheel_) w = 0;
    for (auto& d : pedal_) d = 0;
}

void Core::shed(uint32_t n) {
    for (uint32_t k = 0; k < n; ++k) {
        Pool* best = nullptr; uint32_t bi = 0; float bg = kBig;
        for (auto& p : pool_)
            for (uint32_t i = 0; i < p.n; ++i) {
                if (p.v[i].amp.stage == AmpEnv::Fade) continue;
                const float g = p.f[G][i] * p.v[i].level * p.v[i].volume;
                if (g < bg) { bg = g; best = &p; bi = i; }
            }
        if (!best) return;
        VoiceScalar& v = best->v[bi];
        ampAdvance(v.amp, clock_ - 1, sr_);
        v.releasing = true;
        v.steady = false;
        v.amp.stage = AmpEnv::Fade; v.amp.ramp = true; v.amp.target = 0;
        v.amp.step = -std::max(v.amp.gain, 1e-9) / kSub;
        float* f[5] = {best->f[G], best->f[M], best->f[A], best->f[LO], best->f[HI]};
        ampFields(v.amp, f, int(bi));
        best->f[LO][bi] = 0;
    }
}

// One voice's control step (`grid`: at a control frame; else the first piece of a voice started
// between two): the envelopes to this frame, the filter's cutoff, the pitch and the gains.
void Core::control(Pool& p, uint32_t i, bool grid) {
    VoiceScalar& v = p.v[i];
    if (v.steady && !v.dirty && !v.fresh) return;  // sustaining, nothing moving: nothing to do
    v.dirty = false;
    const bool fresh = v.fresh;
    v.fresh = false;
    if (grid || fresh) {
        if (grid) {
            ampAdvance(v.amp, clock_ - 1, sr_);
            if (v.pedalRelease && !v.releasing && v.amp.stage == AmpEnv::Sustain) releaseLayer(p, i, pedalIndex(v, pedal_[v.channel]));
            float* f[5] = {p.f[G], p.f[M], p.f[A], p.f[LO], p.f[HI]};
            ampFields(v.amp, f, int(i));
        }
        // the cents envelopes at this frame (a voice started between control frames reads its hold level)
        const float fc = grid ? centsAdvance(v.feg, clock_, sr_) : float(v.feg.cents);
        const float pc = grid ? centsAdvance(v.peg, clock_, sr_) : float(v.peg.cents);
        // the element LFO (reference mode): a triangle at 4.848 Hz from note-on, falling from zero
        float lfoCents = 0, lfoGain = 1;
        const float wheel = wheel_[v.channel] * 127.0f;
        if ((v.lfoPitchRatio != 0 || v.lfoAmpRatio != 0) && wheel > 0) {
            const double cycles = double(clock_ - v.start) * laws::kLfoHz / sr_;
            const float ph = float(cycles - std::floor(cycles));
            const float w = ph < 0.25f ? -4.0f * ph : (ph < 0.75f ? 4.0f * ph - 2.0f : 4.0f - 4.0f * ph);
            lfoCents = 0.5f * laws::lfoPitchDepthCents(int32_t(v.lfoPitchRatio), wheel) * w;
            const float amp = laws::lfoAmpDepthDb(std::fabs(v.lfoAmpRatio) * wheel / 127.0f) * (v.lfoAmpRatio < 0 ? -1.0f : 1.0f);
            lfoGain = std::pow(10.0f, -amp * 2.0f * std::min(ph, 1.0f - ph) / 20.0f);
        }
        if (p.plan.filter != Filter::Thru && fc != v.curCents) {
            v.curCents = fc;
            p.f[CENTS][i] = fc;
            v.filterMoves = true;
        }
        // pitch: the envelope, the LFO and the bend
        const float bend = bend_[v.channel];
        const Compiled* s = sound_[v.sound];
        const float bendCents = s ? (bend >= 0 ? bend * s->bendUp : -bend * s->bendDown) * 100.0f : 0.0f;
        const float pitch = pc + lfoCents + bendCents;
        if (pitch != v.curPitch || fresh) {
            v.curPitch = pitch;
            const double target = std::min(v.baseStep * std::exp2(double(pitch) / 1200.0), kMaxStep);
            const int64_t t = int64_t(std::llround(target * 4294967296.0));
            // the kernel for this transposition (at 44.1 kHz), the nearest of the bank's cutoffs
            const int k = int(std::lround((1.0 - laws::kernelCutoff(target * sr_ / laws::kReferenceRate)) / 0.05));
            p.kernel[i] = std::max(0, std::min(kKernelBank - 1, k)) * kKernelFloats;
            if (fresh) { p.step[i] = t; p.dstep[i] = 0; }
            else p.dstep[i] = (t - p.step[i]) / kSub;
        } else p.dstep[i] = 0;
        // gains
        const float common = v.level * v.volume * v.soundGain * lfoGain;
        const float tl = common * v.panL * v.soundPanL, tr = common * v.panR * v.soundPanR;
        if (fresh) { p.f[GL][i] = tl; p.f[GR][i] = tr; p.f[DGL][i] = p.f[DGR][i] = 0; }
        else {
            p.f[DGL][i] = (tl - p.f[GL][i]) * (1.0f / kSub);
            p.f[DGR][i] = (tr - p.f[GR][i]) * (1.0f / kSub);
        }
        // steady: the next control steps would change nothing until an event marks the voice dirty
        v.steady = grid && v.amp.stage == AmpEnv::Sustain && !v.feg.moving() && !v.peg.moving() && lfoCents == 0 && lfoGain == 1
                && p.f[DGL][i] == 0 && p.f[DGR][i] == 0 && p.dstep[i] == 0;
    }
}

void Core::render(float* L, float* R, uint32_t frames, uint32_t from) {
    bool any = false;
    for (auto& p : pool_) any |= p.n > 0;
    if (!any) { clock_ += frames; return; }  // the rest cost: nothing more than this when silent
    float* const l3[3] = {L, L, L};
    float* const r3[3] = {R, R, R};
    render(l3, r3, frames, from);
}

void Core::render(float* const* L, float* const* R, uint32_t frames, uint32_t from) {
    uint32_t done = 0;
    bool any = false;
    for (auto& p : pool_) any |= p.n > 0;
    if (!any) { clock_ += frames; return; }
    // routed: the outputs differ, and a pool has voices on bus 1 or 2 (voices start only between calls)
    const bool split = L[1] != L[0] || L[2] != L[0] || R[1] != R[0] || R[2] != R[0];
    bool routed[2 * kFilters] = {}, anyRouted = false;
    if (split)
        for (int k = 0; k < 2 * kFilters; ++k)
            for (uint32_t i = 0; i < pool_[k].n; ++i) if (pool_[k].v[i].bus) { routed[k] = anyRouted = true; break; }
    FtzScope ftz;
    while (done < frames) {
        const uint32_t phase = (from + done) % kSub;
        const bool grid = phase == 0;
        const uint32_t n = std::min<uint32_t>(kSub - phase, frames - done);
        vf accL[3 * kSub], accR[3 * kSub];
        for (int b = 0; b < (anyRouted ? 3 : 1); ++b)
            for (uint32_t s = 0; s < n; ++s) accL[b * kSub + s] = accR[b * kSub + s] = splat(0.f);
        for (auto& p : pool_) {
            if (!p.n) continue;
            // control: snap the gain ramps of the last step, then each voice
            for (uint32_t i = 0; i < p.n; ++i) {
                VoiceScalar& v = p.v[i];
                if (grid || v.fresh) control(p, i, grid);
            }
            // the filter's coefficients for the lanes whose cutoff moved
            if (p.plan.filter != Filter::Thru)
                for (uint32_t o = 0; o < p.n; o += W) {
                    bool moved = false;
                    for (uint32_t j = o; j < o + W && j < p.n; ++j) { moved |= p.v[j].filterMoves; p.v[j].filterMoves = false; }
                    if (moved) { if (p.plan.filter == Filter::PoleHigh) designPole(Lane{&p, int(o)}, rate_); else design(Lane{&p, int(o)}, rate_); }
                }
            const int c = p.plan.channels == 2 ? 1 : 0;
            kKernels[routed[&p - pool_]][c][interp_ == Interp::Sinc ? 1 : 0][int(p.plan.filter)](p, tables_, scratch_, int(n), accL, accR);
        }
        for (int b = 0; b < (anyRouted ? 3 : 1); ++b)
            for (uint32_t s = 0; s < n; ++s) { L[b][done + s] += hsum(accL[b * kSub + s]); R[b][done + s] += hsum(accR[b * kSub + s]); }
        clock_ += n;
        done += n;
        reap_();
    }
}

void Core::reap_() {
    for (int k = 0; k < 2 * kFilters; ++k) {
        Pool& p = pool_[k];
        for (uint32_t i = p.n; i-- > 0;) {
            VoiceScalar& v = p.v[i];
            if (v.releaseAt && clock_ >= v.releaseAt && !v.releasing) { v.releaseAt = 0; releaseLayer(p, i, v.releaseBase); }
            bool gone = v.ended;
            if (!gone && (v.amp.stage == AmpEnv::Done || v.amp.stage == AmpEnv::Fade)) {
                ampAdvance(v.amp, clock_ - 1, sr_);
                gone = v.amp.stage == AmpEnv::Done;
            }
            if (!gone) continue;
            Slot& sl = slot_[v.slot];
            for (int q = 0; q < sl.layers; ++q)
                if (sl.pool[q] == k && sl.id[q] == v.id) {
                    sl.pool[q] = sl.pool[sl.layers - 1]; sl.id[q] = sl.id[sl.layers - 1]; --sl.layers;
                    break;
                }
            if (!sl.layers) sl.on = false;
            free_(p, i);
        }
    }
}

uint32_t Core::voiceCount() const { uint32_t n = 0; for (const auto& s : slot_) n += s.on; return n; }
uint32_t Core::elementVoices() const { uint32_t n = 0; for (const auto& p : pool_) n += p.n; return n; }

VoiceState Core::voice(uint32_t v) const {
    if (v >= kMaxVoices || !slot_[v].on) return {false, false, 0, 0};
    const Slot& sl = slot_[v];
    bool releasing = true; float level = 0; uint64_t age = 0;
    for (int k = 0; k < sl.layers; ++k) {
        const Pool& p = pool_[sl.pool[k]];
        for (uint32_t i = 0; i < p.n; ++i)
            if (p.v[i].id == sl.id[k]) {
                releasing &= p.v[i].releasing;
                level = std::max(level, std::fabs(p.f[G][i]) * p.v[i].volume);
                age = uint64_t(clock_ - p.v[i].start);
            }
    }
    return {true, releasing, level, age};
}

}  // namespace pcm
