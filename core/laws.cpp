// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

// The fork's element laws (laws.h), carried over with their measurement notes.

#include "laws.h"
#include "tables.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>

namespace pcm {
namespace laws {

namespace {
int32_t clampIndex(int32_t i) { return std::max<int32_t>(0, std::min<int32_t>(127, i)); }
}  // namespace

// ---- envelope ----

float levelGain(int32_t level) noexcept
{
    // (level / 127)^2 on a grid of 0.375 dB steps, rounded to the nearest step, as the
    // reference's levels measure (element levels and envelope levels 8-127)
    const int32_t l = clampIndex(level);
    if (l == 0)
        return 0.0f;
    const double steps = std::round(40.0 * std::log10(127.0 / l) / 0.375);
    return static_cast<float>(std::pow(10.0, -0.375 * steps / 20.0));
}

float attackSeconds(int32_t index) noexcept { return tables::attackSeconds[clampIndex(index)]; }

float attackDurationFactor(int32_t from, int32_t to) noexcept
{
    // Fitted to attacks at one index from levels 0-112 to 127 (eight renders) and 32 to 96; the factor
    // depends only on the ratio of the smaller level to the larger one, the same up and down.
    // shortcut: measured knots, linear in between; ratios between 0 and 1/16 are not measured.
    static constexpr float q[9] = { 0.0f, 0.063f, 0.126f, 0.252f, 0.333f, 0.504f, 0.756f, 0.882f, 1.0f };
    static constexpr float g[9] = { 1.0f, 2.02f, 2.60f, 3.82f, 4.28f, 6.11f, 8.31f, 8.97f, 9.3f };
    const int32_t lo = std::min(clampIndex(from), clampIndex(to)), hi = std::max(clampIndex(from), clampIndex(to));
    if (hi == 0)
        return 0.0f;
    const float r = static_cast<float>(lo) / static_cast<float>(hi);
    int i = 1;
    while (i < 8 && r > q[i])
        ++i;
    const float f = (r - q[i - 1]) / (q[i] - q[i - 1]);
    float factor = g[i - 1] + std::max(0.0f, std::min(1.0f, f)) * (g[i] - g[i - 1]);
    if (lo == 0) {
        // From (or to) level 0 the time grows a little as the other level falls: measured
        // 1.29, 1.166, 1.08 and 1 times at 32, 64, 96 and 127 (indices 80 and 96)
        static constexpr float levels[4] = { 32.0f, 64.0f, 96.0f, 127.0f };
        static constexpr float slow[4] = { 1.29f, 1.166f, 1.08f, 1.0f };
        const float h = static_cast<float>(hi);
        int k = 1;
        while (k < 3 && h > levels[k])
            ++k;
        const float u = std::max(0.0f, std::min(1.0f, (h - levels[k - 1]) / (levels[k] - levels[k - 1])));
        factor = h < levels[0] ? slow[0] : slow[k - 1] + u * (slow[k] - slow[k - 1]);
    }
    return levelGain(hi) * factor;
}
float decaySecondsPer60dB(int32_t index) noexcept { return tables::decaySecondsPer60dB[clampIndex(index)]; }
float releaseSecondsPer60dB(int32_t index) noexcept { return tables::releaseSecondsPer60dB[clampIndex(index)]; }
float releaseRampSeconds(int32_t index) noexcept
{
    const int32_t i = clampIndex(index);
    return i < 32 ? tables::releaseRampSeconds[i] : 0.0f;
}

TimeShifts timeShifts(const TimeScaling& p, int key, int velocity) noexcept
{
    // Fitted to renders: sensitivity * velocity / 128 index steps, and key-follow sensitivity
    // per 64 steps per semitone from the center, both truncated toward zero, with +63 counting
    // as 64 (velocity 60 at +63 shifts by 30). The release gets the key shift scaled by
    // 2^((adjustment - 127) / 32): all of it at 127, a quarter at 64.
    auto full = [](int32_t s) { return s >= 63 ? 64 : s; };
    const int32_t vel = full(p.velocity) * velocity / 128;
    const int32_t keyShift = (key - p.center) * full(p.keyFollow) / 64;
    // truncated: adjustment 0 gave a shift of 1 where 24 * 2^(-127/32) = 1.53
    const int32_t releaseKey = static_cast<int32_t>(keyShift * std::exp2((p.releaseAdjust - 127) / 32.0));
    const int s = p.segments;
    const bool velAttack = s == 0 || s == 1 || s == 3 || s == 4;
    const bool velDecay = s == 1 || s == 2 || s == 4;
    const bool velRelease = s == 3 || s == 4;
    return { keyShift + (velAttack ? vel : 0), keyShift + (velDecay ? vel : 0), releaseKey + (velRelease ? vel : 0) };
}

// ---- level ----

namespace {
// Velocity curves 0, 1, 2 and 4 in dB at measured velocities (sensitivity +32, offset 0, where
// the scale is the velocity's): velocity 1 and every fourth velocity (renders fl3 vk_*);
// interpolated linearly in dB between them. Curve 3 is (scale / 2047)^2.
constexpr int kKnots = 33;
constexpr int knotVelocities[kKnots] = { 1, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60, 64, 68, 72, 76, 80, 84, 88, 92, 96, 100, 104, 108, 112, 116, 120, 124, 127 };
constexpr float curveDb[5][kKnots] = {
    {
        -84.29f, -71.49f, -64.34f, -59.83f, -56.07f, -53.06f, -50.42f, -47.79f, -45.53f, -43.27f, -41.39f, -39.51f,
        -37.63f, -35.37f, -33.49f, -31.61f, -30.10f, -28.22f, -26.34f, -24.46f, -22.58f, -21.07f, -19.19f, -17.31f,
        -15.43f, -13.17f, -11.29f, -9.41f, -7.53f, -5.64f, -3.76f, -1.88f, 0.00f },
    {
        -95.20f, -92.94f, -89.93f, -86.92f, -83.91f, -80.90f, -77.89f, -74.88f, -71.87f, -68.86f, -65.85f, -62.84f,
        -59.83f, -56.82f, -53.81f, -50.80f, -47.79f, -44.78f, -41.77f, -38.76f, -35.75f, -32.74f, -29.73f, -26.72f,
        -23.71f, -20.32f, -17.31f, -14.30f, -11.29f, -8.28f, -5.27f, -2.26f, 0.00f },
    {
        -94.45f, -90.69f, -85.42f, -80.90f, -75.63f, -71.12f, -66.98f, -62.46f, -58.32f, -54.19f, -50.42f, -46.66f,
        -42.90f, -39.51f, -36.12f, -32.74f, -29.73f, -26.72f, -24.08f, -21.45f, -18.81f, -16.56f, -14.30f, -12.04f,
        -10.16f, -8.28f, -6.40f, -5.27f, -3.76f, -2.26f, -1.51f, -0.38f, 0.00f },
    {},
    {
        -95.20f, -92.94f, -89.93f, -86.92f, -83.54f, -80.52f, -77.51f, -74.50f, -71.12f, -67.73f, -64.72f, -61.33f,
        -57.57f, -53.81f, -49.67f, -44.03f, -30.10f, -16.93f, -13.55f, -11.29f, -9.78f, -8.28f, -7.15f, -6.02f,
        -5.27f, -4.14f, -3.39f, -3.01f, -2.26f, -1.51f, -1.13f, -0.38f, 0.00f },
};

}  // namespace

// A note velocity, or a velocity offset, 1-127 on the 0-2047 scale the response works on:
// 16 per step up to 64, then 1023 / 63 per step (renders lv1 and lv2; checked on sv1)
int32_t scale2048(int32_t v) noexcept
{
    v = std::max<int32_t>(0, std::min<int32_t>(127, v));
    return v <= 64 ? 16 * v : static_cast<int32_t>(std::lround(1024.0 + (v - 64) * 1023.0 / 63.0));
}

namespace {

// The sensitivity's factor, in 2048ths: |s| / 31.75 up to 31, then 31.75 / (63.75 - |s|),
// and 127 at 64 (so that only the end of the range sounds)
int64_t factor2048(int32_t s)
{
    const int32_t a = std::abs(s);
    if (a >= 64)
        return 127 * 2048;
    return std::lround(a < 32 ? 2048.0 * a / 31.75 : 2048.0 * 31.75 / (63.75 - a));
}

double curveDbAt(int curve, int32_t r)
{
    int i = 1;
    while (i < kKnots - 1 && r > scale2048(knotVelocities[i]))
        ++i;
    const double a = scale2048(knotVelocities[i - 1]), b = scale2048(knotVelocities[i]);
    const double f = std::max(0.0, std::min(1.0, (r - a) / (b - a)));
    return curveDb[curve][i - 1] + f * (curveDb[curve][i] - curveDb[curve][i - 1]);
}
}  // namespace

static int32_t velocityScaleAt(const Velocity& v, int32_t vel) noexcept
{
    const int32_t s = v.sensitivity >= 63 ? 64 : std::max<int32_t>(-64, v.sensitivity); // +63 counts as 64
    const int32_t offset = v.offset > 0 ? scale2048(v.offset) : 0;
    const int64_t k = factor2048(s);
    const int64_t moved = s >= 0 ? (2047 - std::min(vel + offset, 2047)) * k
                                 : std::max(0, std::min(2047, vel - offset)) * k;
    return 2047 - static_cast<int32_t>(std::min<int64_t>(2047, moved / 2048));
}

int32_t velocityScale(const Velocity& v, int velocity) noexcept { return velocityScaleAt(v, scale2048(velocity)); }

float velocityGain(const Velocity& v, int velocity) noexcept { return velocityGain2048(v, scale2048(velocity)); }

float velocityGain2048(const Velocity& v, int32_t velocity2048) noexcept
{
    const int32_t r = velocityScaleAt(v, std::max<int32_t>(0, std::min<int32_t>(2047, velocity2048)));
    if (r <= 0)
        return 0.0f;
    const double steps = (v.curve == 3 || v.curve < 0 || v.curve > 4)
        ? std::round(40.0 * std::log10(2047.0 / r) / 0.375)
        : std::round(-curveDbAt(v.curve, r) / 0.375);
    if (steps >= 255.0)
        return 0.0f;
    return static_cast<float>(std::pow(10.0, -0.375 * std::max(0.0, steps) / 20.0));
}

float levelScalingGain(const LevelScaling& s, int key) noexcept
{
    double offset = s.offsets[0];
    if (key >= s.keys[3])
        offset = s.offsets[3];
    else {
        for (int i = 0; i < 3; ++i) {
            if (key >= s.keys[i] && key < s.keys[i + 1]) {
                const double f = static_cast<double>(key - s.keys[i]) / (s.keys[i + 1] - s.keys[i]);
                offset = s.offsets[i] + f * (s.offsets[i + 1] - s.offsets[i]);
            }
        }
    }
    const int32_t follow = s.keyFollow >= 63 ? 64 : s.keyFollow;
    offset = std::round(offset) + follow * (key - 60) / 32.0;
    if (s.level <= 0)
        return 1.0f;
    auto gridDb = [](double level) { return -0.375 * std::round(40.0 * std::log10(127.0 / level) / 0.375); };
    const double scaled = std::min(127.0, s.level * (128.0 + offset) / 128.0);
    if (scaled <= 0.0)
        return 0.0f;
    return static_cast<float>(std::pow(10.0, (gridDb(scaled) - gridDb(s.level)) / 20.0));
}

void balanceGains(int32_t pan, float& left, float& right) noexcept
{
    const int32_t p = std::max<int32_t>(1, std::min<int32_t>(127, pan));
    constexpr float halfPi = 1.5707963267948966f;
    // The far side: sin^2 of the distance from the edge over 64, on the 0.375 dB grid
    auto far = [&](int32_t distance) {
        const double s = std::sin(halfPi * distance / 64.0);
        if (s <= 0.0)
            return 0.0f;
        const double steps = std::round(-40.0 * std::log10(s) / 0.375);
        return static_cast<float>(std::pow(10.0, -0.375 * steps / 20.0));
    };
    left = right = 1.0f;
    if (p < 64)
        right = far(p - 1);
    else if (p > 64)
        left = far(127 - p);
}

// ---- pitch and filter envelopes ----

float pitchLevelCents(int32_t level, int32_t depth) noexcept
{
    const int32_t steps = level >= 255 ? 128 : std::max<int32_t>(0, level) - 128;
    const int32_t d = depth >= 63 ? 64 : std::max<int32_t>(-64, depth);
    return static_cast<float>(steps) * 37.5f * static_cast<float>(d) / 64.0f;
}

float pitchHoldSeconds(int32_t index) noexcept
{
    // Measured at 16, 32, 64 and 80 (0.027, 0.075, 0.421, 0.978 s); interpolated in log time,
    // extended beyond 80 at the 64-80 slope.
    static constexpr float knots[4] = { 16.0f, 32.0f, 64.0f, 80.0f };
    static constexpr float seconds[4] = { 0.027f, 0.075f, 0.421f, 0.978f };
    if (index <= 0)
        return 0.0f;
    if (index <= 16)
        return seconds[0] * static_cast<float>(index) / 16.0f;
    int i = 1;
    while (i < 3 && index > knots[i])
        ++i;
    const float f = (static_cast<float>(index) - knots[i - 1]) / (knots[i] - knots[i - 1]);
    return std::exp(std::log(seconds[i - 1]) + f * (std::log(seconds[i]) - std::log(seconds[i - 1])));
}

float filterDepthVelocityFactor(int32_t sensitivity, int32_t curve, int velocity) noexcept
{
    // Measured at sensitivity +63 (fl3 dv_*, dc_*): the depth at velocity v on each curve, as a
    // share of the depth without sensitivity; other sensitivities scale the distance from 1 by
    // sensitivity / 64 (+63 counts as 64), negative ones mirror the velocity (128 - v).
    static constexpr float v9[9] = { 1.f, 16.f, 32.f, 48.f, 64.f, 80.f, 96.f, 112.f, 127.f };
    static constexpr float curve2[9] = { 0.f, 0.123f, 0.281f, 0.418f, 0.538f, 0.715f, 0.854f, 0.985f, 1.125f };
    static constexpr float v6[6] = { 1.f, 16.f, 48.f, 80.f, 112.f, 127.f };
    static constexpr float others[5][6] = {
        { 0.f, 0.247f, 0.511f, 0.576f, 0.898f, 1.125f },
        { 0.f, 0.012f, 0.152f, 0.435f, 0.883f, 1.125f },
        {},
        { 0.f, 0.263f, 0.687f, 0.973f, 1.094f, 1.125f },
        { 0.f, 0.018f, 0.330f, 0.816f, 1.077f, 1.125f },
    };
    if (sensitivity == 0)
        return 1.0f;
    const int32_t s = sensitivity >= 63 ? 64 : std::max<int32_t>(-64, sensitivity);
    const float v = static_cast<float>(std::max(1, std::min(127, s > 0 ? velocity : 128 - velocity)));
    const int c = std::max<int32_t>(0, std::min<int32_t>(4, curve));
    const float* x = c == 2 ? v9 : v6;
    const float* y = c == 2 ? curve2 : others[c];
    const int n = c == 2 ? 9 : 6;
    float g = y[n - 1];
    for (int k = 1; k < n; ++k) {
        if (v <= x[k]) {
            g = y[k - 1] + (y[k] - y[k - 1]) * (v - x[k - 1]) / (x[k] - x[k - 1]);
            break;
        }
    }
    return 1.0f + static_cast<float>(std::abs(s)) / 64.0f * (g - 1.0f);
}

// ---- filter ----

namespace {
struct Knot {
    int index;
    float cents; // relative to 440 Hz
};
// Cutoff in cents from 440 Hz, linear in cents between knots. From 264 up: the fork's knots, measured
// on reference renders every 8 indices (white noise, resonance 0; K20). From 0 to 256: this engine's
// measurement (docs/acceptance.md (d)): a saw's harmonics at note 36 through the two sections,
// resonance 0, the cutoff fitted every 16 indices (harmonics within 0.26 dB rms).
constexpr Knot kCutoffKnots[] = {
    { 0, -5932.f }, { 16, -5612.f }, { 32, -5382.f }, { 48, -5200.f }, { 64, -5058.f }, { 80, -4934.f },
    { 96, -4814.f }, { 112, -4539.f }, { 128, -4281.f }, { 144, -4082.f }, { 160, -3921.f }, { 176, -3784.f },
    { 192, -3658.f }, { 208, -3465.f }, { 224, -3173.f }, { 240, -2955.f }, { 256, -2773.f }, { 264, -2703.f }, { 272, -2647.f }, { 280, -2529.f },
    { 288, -2502.f }, { 296, -2442.f }, { 304, -2390.f }, { 312, -2215.f }, { 320, -2065.f }, { 328, -1978.f },
    { 336, -1809.f }, { 344, -1724.f }, { 352, -1627.f }, { 360, -1549.f }, { 368, -1508.f }, { 376, -1391.f },
    { 384, -1356.f }, { 392, -1325.f }, { 400, -1247.f }, { 408, -1199.f }, { 416, -1029.f }, { 424, -839.f },
    { 432, -753.f }, { 440, -638.f }, { 448, -519.f }, { 456, -446.f }, { 464, -337.f }, { 472, -282.f },
    { 480, -225.f }, { 488, -162.f }, { 496, -81.f }, { 504, -57.f }, { 512, 23.f }, { 520, 214.f },
    { 528, 347.f }, { 536, 482.f }, { 544, 598.f }, { 552, 696.f }, { 560, 774.f }, { 568, 846.f },
    { 576, 925.f }, { 584, 984.f }, { 592, 1057.f }, { 600, 1116.f }, { 608, 1171.f }, { 616, 1252.f },
    { 624, 1437.f }, { 632, 1570.f }, { 640, 1701.f }, { 648, 1812.f }, { 656, 1895.f }, { 664, 1989.f },
    { 672, 2059.f }, { 680, 2147.f }, { 688, 2209.f }, { 696, 2263.f }, { 704, 2316.f }, { 712, 2370.f },
    { 720, 2502.f }, { 728, 2663.f }, { 736, 2802.f }, { 744, 2918.f }, { 752, 3026.f }, { 760, 3122.f },
    { 768, 3214.f }, { 776, 3291.f }, { 784, 3363.f }, { 792, 3429.f }, { 800, 3490.f }, { 808, 3544.f },
    { 816, 3598.f }, { 824, 3745.f }, { 832, 3906.f }, { 840, 4047.f }, { 848, 4168.f }, { 856, 4279.f },
    { 864, 4375.f }, { 872, 4467.f }, { 880, 4545.f }, { 888, 4621.f }, { 896, 4688.f }, { 904, 4753.f },
    { 912, 4812.f },
};
}  // namespace

const float kDampingKnotCents[kDampingKnots] = { 3598.f, 3745.f, 3906.f, 4047.f, 4168.f, 4279.f, 4375.f,
                                                 4467.f, 4545.f, 4621.f, 4688.f, 4753.f, 4812.f };
// 2 - 0.0104 (index - 816) at indices 816, 824, ..., 912
const float kDampingKnotLimit[kDampingKnots] = { 2.0f, 1.9168f, 1.8336f, 1.7504f, 1.6672f, 1.584f, 1.5008f,
                                                 1.4176f, 1.3344f, 1.2512f, 1.168f, 1.0848f, 1.0016f };

float cutoffCents(float index) noexcept
{
    const float i = std::max(0.0f, std::min(1023.0f, index));
    const Knot* first = std::begin(kCutoffKnots);
    const Knot* last = std::end(kCutoffKnots) - 1;
    if (i <= static_cast<float>(first->index))
        return first->cents;
    if (i >= static_cast<float>(last->index))
        return 1200.0f * std::log2(static_cast<float>(kTopHz) / 440.0f);
    const Knot* hi = std::upper_bound(first, last + 1, i, [](float v, const Knot& k) { return v < static_cast<float>(k.index); });
    const Knot* lo = hi - 1;
    const float f = (i - static_cast<float>(lo->index)) / static_cast<float>(hi->index - lo->index);
    return lo->cents + f * (hi->cents - lo->cents);
}

// Damping limit at resonance 0, measured: 2 up to index 816, then falling by 0.0104 per
// index to 1 (about index 912); expressed through the knots so that cutoff shifts in cents
// move along it the same way.
float dampingLimit(double hz) noexcept
{
    const float cents = 1200.0f * static_cast<float>(std::log2(hz / 440.0));
    const Knot* k = std::begin(kCutoffKnots);
    const Knot* end = std::end(kCutoffKnots);
    while (k != end && k->index < 816)
        ++k;
    if (cents <= k->cents)
        return 2.0f;
    float index = static_cast<float>((end - 1)->index);
    for (; k + 1 != end; ++k) {
        if (cents <= (k + 1)->cents) {
            index = static_cast<float>(k->index) + static_cast<float>((k + 1)->index - k->index) * (cents - k->cents) / ((k + 1)->cents - k->cents);
            break;
        }
    }
    return std::max(1.0f, 2.0f - 0.0104f * (index - 816.0f));
}

float filterCutoffHz(int32_t index, float cents) noexcept
{
    return 440.0f * std::exp2((cutoffCents(static_cast<float>(index)) + cents) / 1200.0f);
}

float cutoffVelocityCents(int32_t cutoff, int32_t sensitivity, int velocity) noexcept
{
    // Measured (fl3 cv_*): the cutoff index moves by 0.115 * sensitivity * (velocity - 115),
    // +63 counting as 64, negative sensitivities mirroring the velocity (128 - v)
    if (sensitivity == 0)
        return 0.0f;
    const int32_t s = sensitivity >= 63 ? 64 : std::max<int32_t>(-64, sensitivity);
    const float v = static_cast<float>(s > 0 ? velocity : 128 - velocity);
    const float shift = 0.115f * static_cast<float>(s > 0 ? s : -s) * (v - 115.0f);
    const float base = static_cast<float>(cutoff);
    return cutoffCents(base + shift) - cutoffCents(base);
}

float filterDamping(int32_t resonance) noexcept
{
    const int32_t r = std::max<int32_t>(0, std::min<int32_t>(127, resonance));
    return 2.0f * std::exp2(-static_cast<float>(r) / 16.0f);
}

float filterGain(int32_t gain) noexcept
{
    // Passband level measured at these values (dB), linear in dB between them; about
    // 0.375 dB per step below 64; 0 is silent
    static constexpr float values[5] = { 64.f, 128.f, 192.f, 230.f, 255.f };
    static constexpr float db[5] = { -68.34f, -44.38f, -20.18f, -5.45f, 3.25f };
    const int32_t g = std::max<int32_t>(0, std::min<int32_t>(255, gain));
    if (g == 0)
        return 0.0f;
    float level = db[0] - 0.375f * (values[0] - static_cast<float>(g));
    for (int k = 0; k < 4; ++k) {
        if (static_cast<float>(g) >= values[k] && static_cast<float>(g) <= values[k + 1])
            level = db[k] + (db[k + 1] - db[k]) * (static_cast<float>(g) - values[k]) / (values[k + 1] - values[k]);
    }
    return std::pow(10.0f, level / 20.0f);
}

// ---- element LFO ----

}  // namespace laws
}  // namespace pcm
