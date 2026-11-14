// SPDX-License-Identifier: AGPL-3.0-or-later
#include "measured.h"
#include "laws.h"
#include <algorithm>
#include <cmath>

namespace pcm {
namespace laws {

float filterReleaseFactor(int32_t index) noexcept
{
    // Measured (docs/acceptance.md (d)): releases at 48-104 from +4800 cents, the reference's time
    // against 1.5 x the decay table's: 1.0 at indices 8k (48 0.98, 64 0.995, 72, 80, 88, 96, 104 within
    // 1 %), 0.9875 at 8k+2 (82, 90), 0.973 at 8k+4 (76, 84, 92, 100 within 0.3 %), 0.9525 at 8k+6 (86).
    // shortcut: the odd places are interpolated, not measured
    static constexpr float place[8] = { 1.0f, 0.994f, 0.9875f, 0.98f, 0.973f, 0.963f, 0.9525f, 0.976f };
    return place[std::max<int32_t>(0, index) % 8];
}

int32_t partVelocity2048(int velocity, int32_t depth, int32_t offset) noexcept
{
    // Fitted to 18 depth / offset pairs at 11 velocities each through the element law at sensitivity +32
    // (factor 1): rms 0.057 dB, at most 0.16 dB over 150 notes; the gate cases at sensitivity +63 (14
    // pairs) agree at every velocity
    const int64_t moved = int64_t(scale2048(velocity)) * scale2048(depth) / 1024 + 2 * (int64_t(scale2048(offset)) - 1024);
    return int32_t(std::max<int64_t>(0, std::min<int64_t>(2047, moved)));
}

int velocityFrom2048(int32_t s) noexcept
{
    // the highest step at or below (measured: 1023 plays an element limited to velocities 1-63)
    int v = s <= 1024 ? s / 16 : 64 + int((int64_t(s) - 1024) * 63 / 1023);
    while (v < 127 && scale2048(v + 1) <= s) ++v;
    while (v > 0 && scale2048(v) > s) --v;
    return std::max(1, std::min(127, v));
}

float kernelCutoff(double r) noexcept
{
    // Above the root: the cutoff of this engine's 20-tap kernel whose response best fits the
    // reference's at each measured transposition (saw source loops, harmonics to the source's
    // Nyquist): rms 2.2, 2.2, 3.5, 0.2, 0.05 dB at 1.059, 1.122, 1.335, 1.498, 1.888 (the
    // reference's slope is gentler than a sinc's); linear between, 1 / r beyond.
    static constexpr double at[6] = { 1.0, 1.059, 1.122, 1.335, 1.498, 1.888 };
    static constexpr double cut[6] = { 1.0, 0.905, 0.81, 0.625, 0.455, 0.405 };
    // Below the root (measured through the pitch bend, 1-12 semitones down, so the sample is not picked
    // again): the bank's 0.95 fits the reference's gentler slope best, rms 1.15 dB, at most 2.27 (0.9:
    // 2.67 / 7.03; 1.0: 1.61 / 3.01)
    if (r < 0.95)
        return 0.95f;
    if (r <= 1.0)
        return 1.0f;
    if (r >= at[5])
        return static_cast<float>(std::max(0.2, cut[5] * at[5] / r));
    int i = 1;
    while (r > at[i])
        ++i;
    return static_cast<float>(cut[i - 1] + (cut[i] - cut[i - 1]) * (r - at[i - 1]) / (at[i] - at[i - 1]));
}

double eqHz(int raw) noexcept
{
    // Fitted per take (batches X, Z and S, rounds 3-4; every take within 0.05 dB rms of its model): the peak's
    // frequency and both shelves' read one table (the peak and the high shelf agree within 0.02 % at every
    // raw value both were measured at); the raw value's low 8 bits index it (300 plays as 44, 511 as 255)
    static constexpr double pt[][2] = {
        { 0, 14.1 }, { 30, 31.3 }, { 44, 47.6 }, { 46, 49.9 }, { 54, 62.2 }, { 62, 79.4 }, { 70, 96.4 }, { 78, 117.7 },
        { 83, 139.2 }, { 86, 151.9 }, { 91, 173.3 }, { 94, 186.1 }, { 99, 207.4 }, { 100, 211.8 }, { 102, 221.9 },
        { 107, 264.7 }, { 110, 290.2 }, { 115, 332.9 }, { 118, 358.4 }, { 123, 401.3 }, { 126, 426.7 }, { 131, 502.0 },
        { 134, 553.1 }, { 139, 638.7 }, { 142, 689.7 }, { 147, 775.4 }, { 150, 826.3 }, { 155, 949.7 }, { 157, 1018.6 }, { 158, 1051.8 },
        { 163, 1223.7 }, { 166, 1325.6 }, { 171, 1498.1 }, { 174, 1599.7 }, { 179, 1793.5 }, { 182, 1998.1 },
        { 187, 2345.5 }, { 195, 2901.1 }, { 200, 3245.0 }, { 203, 3461.1 }, { 211, 4527.3 }, { 219, 5695.1 },
        { 220, 5849.9 }, { 227, 6906.2 }, { 231, 7765.5 }, { 235, 9078.8 }, { 239, 10521.0 }, { 241, 11258.0 }, { 243, 12106.0 },
        { 247, 13940.0 }, { 251, 16268.0 }, { 255, 19794.6 } };
    constexpr int n = int(sizeof pt / sizeof pt[0]);
    const double x = raw & 255;
    int i = 1;
    while (i < n - 1 && x > pt[i][0])
        ++i;
    const double t = (x - pt[i - 1][0]) / (pt[i][0] - pt[i - 1][0]);
    return std::exp(std::log(pt[i - 1][1]) + t * (std::log(pt[i][1]) - std::log(pt[i - 1][1])));
}

double eqPeakQ(int raw) noexcept
{
    // raw 0, 8, 20, 31 at 212 Hz, 1 kHz and 5.8 kHz: 0.701, 1.400, 3.945, 10.22 (the law 0.7, 1.4, 3.96, 10.27)
    return 0.7 * std::exp2(raw / 8.0);
}

bool elementEq(int type, int q, int f1, int g1, int f2, int g2, double sr, Biquad& out) noexcept
{
    out = Biquad{};
    if ((type != 0 && type != 1) || sr <= 0)
        return false;
    constexpr double pi = 3.14159265358979323846;
    const double G1 = std::pow(10.0, (g1 - 64) * 0.375 / 20.0), G2 = std::pow(10.0, (g2 - 64) * 0.375 / 20.0);
    // a band's coefficient: 2 sin(pi F / rate) (at the host's rate the same Hz; kept below 2)
    auto coef = [&](double hz) { return 2.0 * std::sin(pi * std::min(hz, 0.49 * sr) / sr); };
    double b[3], a[3];
    if (type == 1) {
        if (g1 == 64)
            return false;
        // the peak (measured: every take of 36 within 0.05 dB rms): the poles of a state-variable section at
        // f = 2 sin(pi F / rate), damping d = 1 / Q, so a1 = f^2 + f d - 2, a2 = 1 - f d; the zeros make the
        // response 1 at DC and at the Nyquist frequency with the gain G at the centre: a boost has
        // b = (1 + f d (G - 1) / 2, a1, 1 - f d (G + 1) / 2) over a = (1, a1, 1 - f d); a cut is the boost of
        // 1 / G inverted (the same in dB)
        // near the top of the range the reference replaces the frequency by a lower one that depends on Q
        // (measured at Q raw 0, 4, 8, 12, 16, 20, 25, 31, frequency raw 187-255 every 4: past a threshold the
        // peak sits at that Q's own frequency, every raw value alike); between the measured Q values both
        // log-linear in Q (inferred)
        static constexpr double qs[8] = { 0, 4, 8, 12, 16, 20, 25, 31 };
        static constexpr double above[8] = { 3955, 7318, 9773, 11281, 12990, 15061, 17948, 17941 };  // the brackets' geometric middle
        static constexpr double to[8] = { 3735.6, 6516.1, 8665.2, 10342.0, 11649.7, 12663.3, 13615.0, 14380.5 };
        int k = 1;
        while (k < 7 && q > qs[k])
            ++k;
        const double u = (std::max(0, std::min(31, q)) - qs[k - 1]) / (qs[k] - qs[k - 1]);
        const double limit = std::exp(std::log(above[k - 1]) + u * (std::log(above[k]) - std::log(above[k - 1])));
        const double cap = std::exp(std::log(to[k - 1]) + u * (std::log(to[k]) - std::log(to[k - 1])));
        double hz = eqHz(f1);
        if (hz > limit)
            hz = cap;
        double f = coef(hz);
        const double d = 1.0 / eqPeakQ(q);
        // stability at any rate (the section needs f^2 + 2 f d < 4); the measured settings reach 3.96
        if (f * f + 2 * f * d > 3.98)
            f = std::sqrt(d * d + 3.98) - d;
        const double a1 = f * f + f * d - 2.0;
        const double G = G1 >= 1 ? G1 : 1.0 / G1;
        const double n0 = 1.0 + f * d * (G - 1.0) / 2.0, n2 = 1.0 - f * d * (G + 1.0) / 2.0, d2 = 1.0 - f * d;
        if (G1 >= 1) { b[0] = n0; b[1] = a1; b[2] = n2; a[0] = 1; a[1] = a1; a[2] = d2; }
        else { b[0] = 1; b[1] = a1; b[2] = d2; a[0] = n0; a[1] = a1; a[2] = n2; }
    } else {
        if (g1 == 64 && g2 == 64)
            return false;
        // first-order shelves through the bilinear transform at K = 1.006 x 2 sin(pi F / rate) (measured: the
        // shelves' corners read the peak's table through that factor at every raw value, 0.6 %); the boost's
        // pole and the cut's zero sit at the corner; the high shelf's frequency stays within raw 131-241
        auto shelf = [](double K, double G, bool low, double* s) {
            if (low) { if (G >= 1) { s[0] = 1 + K * G; s[1] = K * G - 1; s[2] = 1 + K; s[3] = K - 1; }
                       else { s[0] = 1 + K; s[1] = K - 1; s[2] = 1 + K / G; s[3] = K / G - 1; } }
            else { if (G >= 1) { s[0] = G + K; s[1] = K - G; s[2] = 1 + K; s[3] = K - 1; }
                   else { s[0] = 1 + K; s[1] = K - 1; s[2] = 1 / G + K; s[3] = K - 1 / G; } }
        };
        double lo[4], hi[4];
        shelf(1.006 * coef(eqHz(f1)), G1, true, lo);
        shelf(1.006 * coef(eqHz(std::max(131, std::min(241, f2)))), G2, false, hi);
        b[0] = lo[0] * hi[0]; b[1] = lo[0] * hi[1] + lo[1] * hi[0]; b[2] = lo[1] * hi[1];
        a[0] = lo[2] * hi[2]; a[1] = lo[2] * hi[3] + lo[3] * hi[2]; a[2] = lo[3] * hi[3];
    }
    out.b0 = b[0] / a[0]; out.b1 = b[1] / a[0]; out.b2 = b[2] / a[0]; out.a1 = a[1] / a[0]; out.a2 = a[2] / a[0];
    return true;
}

// Measured on sines (round 5, batch B): a box from knob 1 on the element LFO's pitch or amplitude depth at ratios 8, 16, 32, 63 and stored values
// 16-1023; the depth is one function of o = c x ratio / 8 whatever the ratio (and the source: the mod wheel at 32, 64,
// 96, 127 through ratios 16, 32, 63 plays the knob's depth at the same o within 0.3 %); peak to peak by zero crossings
// (pitch) and the demodulated envelope (amplitude). Between the knots log-linear (pitch), linear (amplitude). The
// reference's depths come in steps (0.92 cent, about 0.3 dB at small depths); the knots keep its values.
namespace {
float knots(const float* o, const float* y, int n, float x, bool logy) noexcept
{
    if (x <= o[0]) return y[0];
    for (int k = 1; k < n; ++k)
        if (x <= o[k]) {
            const float u = (x - o[k - 1]) / (o[k] - o[k - 1]);
            if (logy && y[k - 1] > 0) return y[k - 1] * std::pow(y[k] / y[k - 1], u);
            return y[k - 1] + (y[k] - y[k - 1]) * u;
        }
    return y[n - 1];
}
}  // namespace

float elfoPitchDepthCents(float x) noexcept
{
    static const float o[] = { 32, 64, 128, 256, 512, 1008, 1024, 1536, 2016, 2048, 3072, 4092, 6048, 8056, 8192 };
    static const float y[] = { 0, 4.61f, 4.61f, 5.53f, 6.45f, 8.30f, 9.22f, 12.91f, 17.52f, 18.44f, 35.97f, 71.95f, 263.86f, 1047.3f, 1146 };
    return x <= 32 ? 0.0f : knots(o, y, int(sizeof o / sizeof o[0]), x, true);  // 8192: one more step of the last doubling (inferred)
}

float elfoAmpDepthDb(float x) noexcept
{
    static const float o[] = { 0, 256, 512, 768, 1024, 1536, 2048, 3072, 4092, 6048, 8056, 8192 };
    static const float y[] = { 0, 0.8f, 1.5f, 2.2f, 2.56f, 4.04f, 5.14f, 8.45f, 12.13f, 23.17f, 82.46f, 83 };
    return knots(o, y, int(sizeof o / sizeof o[0]), std::max(0.0f, x), false);
}

// Measured on white noise through type 0 at cutoff 512 (round 5, batch A: three settings of break points and offsets,
// twelve keys each, every note matched against static cutoffs in 2-step increments): 3.125 index steps an offset unit,
// linear between the break points; rms 8.4 index steps over 28 notes (worst 22), the notes past index 900 (the table's
// top) and the keys 12, 24 and 108 (the transposed noise too narrow to match) left out.
float cutoffScalingSteps(const int16_t* k, const int16_t* o, int key) noexcept
{
    float off = o[0];
    if (key >= k[3]) off = o[3];
    else
        for (int i = 0; i < 3; ++i)
            if (key >= k[i] && key < k[i + 1]) off = o[i] + float(o[i + 1] - o[i]) * float(key - k[i]) / float(k[i + 1] - k[i]);
    return 3.125f * off;
}

// Measured at every raw value (round 5, batches I and J: a sine an octave below and above centre 60, cents a key from the
// pitches): whole cents within 0.013, up and down alike within 0.022; the centre note keeps its pitch (also checked at
// centres 48 and 72, keys 36-84, within 1 cent at 3,840 cents).
float pitchKeyFollowCents(int raw) noexcept
{
    static const int16_t t[128] = {
        -200, -199, -198, -197, -196, -195, -194, -193, -192, -191, -190, -185, -180, -175, -170, -165,
        -160, -155, -150, -145, -140, -135, -130, -125, -120, -115, -110, -105, -104, -103, -102, -101,
        -100, -99, -98, -97, -96, -95, -90, -85, -80, -75, -70, -65, -60, -55, -50, -45,
        -40, -35, -30, -25, -20, -15, -10, -9, -8, -7, -6, -5, -4, -3, -2, -1,
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 15, 20, 25, 30, 35,
        40, 45, 50, 55, 60, 65, 70, 75, 80, 85, 90, 95, 96, 97, 98, 99,
        100, 101, 102, 103, 104, 105, 110, 115, 120, 125, 130, 135, 140, 145, 150, 155,
        160, 165, 170, 175, 180, 185, 190, 192, 193, 194, 195, 196, 197, 198, 199, 200,
    };
    return float(t[raw < 0 ? 0 : raw > 127 ? 127 : raw]);
}

int resonanceAtVelocity(int resonance, int s, int v) noexcept
{
    // 12 settings (bases 0-127, sensitivities -64 to +63) at 7 velocities against the reference's own static resonances:
    // rms 0.58 steps, at most 1.7 (round 5 batch A, round 6 batch L)
    if (s == 0) return resonance;
    const int k = s >= 63 ? 64 : s < 0 ? -s : s;
    const float drop = float(k) * float(s > 0 ? 127 - v : v - 1) / 126.0f;
    return std::max(0, std::min(127, resonance - int(std::lround(drop))));
}

float hpfKeyCents(int kf, int key) noexcept
{
    // the cutoff's key follow law, about key 60: measured on white noise (type 4, the high-pass at 400, key follow 0, 32, 96,
    // 127 against 64 at keys 24, 48, 60, 72, 96; round 5, batch A): rms 0.60 dB over 262 half-octave bands within 40 dB
    return float(kf - 64) * 3.125f * float(key - 60);
}

float boxSource(int v) noexcept
{
    // measured through the fine tune at 17 wheel values and the coarse tune at 6 (every one within 0.1 cent;
    // 64 plays as 512, not 516)
    v = std::max(0, std::min(127, v));
    return v <= 64 ? 8.0f * v : 512.0f + (v - 64) * 512.0f / 63.0f;
}

// Measured through the fine tune at ratio +63 (round 5, batch A): knob 1 stored at 0, 64, 256, 511, 512, 768, 1000,
// 1023 and knob 8 at 768 play as c = the raw value, but 1023 plays as 1024 (as a ratio of +127 plays +128: a coarse box
// of +64 at knob 256, 512, 1000, 1022, 1023 gives 600, 1200, 2343, 2395, 2400 cents within 0.05; batch F); the ribbon by CC 16 at
// 17 values as the controllers' map (within 0.1 cent); switches 1 and 2 by CC 86 / 87 as that map too (0 and 127: off
// and on, 0 and 1024)
float controlSource(int src, float x) noexcept
{
    if (src == 6)
        return boxSource(int(std::lround(x * 127.0f)));
    if (src >= 16)
        return x >= 0.5f ? 1024.0f : 0.0f;
    const float k = std::round(x * 1023.0f);
    return k >= 1023.0f ? 1024.0f : k;
}

namespace {
struct Curve { uint8_t bank, type, p1, p2; float s[17], y[17]; };
// c at the measured wheel values 0, 8, ..., 120, 127 (this engine's map) and at the analog engine's (VA) knots
#define PCM_W17 { 0, 64, 128, 192, 256, 320, 384, 448, 512, 577.0f, 642.0f, 707.0f, 772.1f, 837.1f, 902.1f, 967.1f, 1024 }
#define PCM_VA17 { 0, 64, 129, 193, 258, 322, 387, 451, 516, 580, 644, 709, 773, 838, 902, 966, 1023 }
// Type 0 with parameter 0, 2 and 8: measured here through the fine tune (ratio +63, whole cents: +-5 of 1024);
// 6, 9, 10 and two of type 2: the analog engine's measurements of the same boxes (its docs/coverage.md)
const Curve kCurveTab[] = {
    { 0, 0, 0, 0, PCM_W17, { 0, 754, 879, 931, 952, 973, 984, 993, 1004, 1004, 1004, 1015, 1015, 1015, 1024, 1024, 1024 } },
    { 0, 0, 2, 0, PCM_W17, { 0, 255, 432, 545, 640, 713, 765, 816, 858, 889, 910, 942, 962, 984, 993, 1015, 1024 } },
    { 0, 0, 8, 0, PCM_W17, { 0, 16, 26, 47, 68, 88, 109, 140, 172, 213, 255, 317, 390, 484, 609, 796, 1024 } },
    { 0, 0, 6, 0, PCM_VA17, { 0, 33, 71, 118, 156, 204, 261, 308, 365, 431, 497, 573, 649, 734, 829, 924, 1024 } },
    { 0, 0, 9, 0, PCM_VA17, { 0, 0, 14, 24, 33, 43, 52, 71, 81, 109, 137, 175, 223, 298, 412, 630, 1024 } },
    { 0, 0, 10, 0, PCM_VA17, { 0, 0, 0, 0, 0, 14, 14, 14, 24, 33, 43, 52, 71, 99, 156, 298, 1024 } },
    { 0, 2, 10, 4, PCM_VA17, { 0, 0, 0, 0, 0, 0, 0, 0, 33, 156, 280, 412, 526, 659, 791, 914, 1024 } },
    { 0, 2, 0, 4, PCM_VA17, { 0, 128, 251, 374, 507, 621, 753, 877, 1009, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024 } },
};
#undef PCM_W17
#undef PCM_VA17
}  // namespace

int boxCurve(int bank, int type, int p1, int p2) noexcept
{
    if (bank == 0 && type == 0 && p1 == 5)
        return 0;  // linear (measured)
    for (int i = 0; i < int(sizeof kCurveTab / sizeof kCurveTab[0]); ++i) {
        const Curve& c = kCurveTab[i];
        if (c.bank == bank && c.type == type && c.p1 == p1 && (type == 0 || c.p2 == p2))
            return i + 1;
    }
    return -1;
}

float boxCurveAt(int curve, float c) noexcept
{
    if (curve <= 0)
        return c;
    const Curve& t = kCurveTab[curve - 1];
    int i = 1;
    while (i < 16 && c > t.s[i])
        ++i;
    const float u = std::max(0.0f, std::min(1.0f, (c - t.s[i - 1]) / (t.s[i] - t.s[i - 1])));
    return t.y[i - 1] + (t.y[i] - t.y[i - 1]) * u;
}

// Spans, measured with the mod wheel against static settings (docs/acceptance.md (l), (m)): cutoff 1024 index steps (the
// wheel at 127 through ratio +8 and at 64 through one and three +1 boxes land on the static index 448, not 447; round 5,
// batch F), resonance and element level 128 (level: 16 boxes at
// four element levels, rms 0.32 dB), coarse 2400 cents and fine 100 cents (whole cents, within 0.1 cent),
// the high-pass cutoff 1024 (one take within 2 steps; inferred from the cutoff's); the filter envelope's depth 64 (raw
// depth steps: the wheel at 64 and 127 through a ratio +32 box, a note each, sound as the static depths +16 and +32
// within 0.01 dB in every octave band; a held note does not follow the box: read at note-on; round 5, batch A)
// the amplitude envelope's attack and release 128 index steps (a box from knob 1, one note a take: release slopes and
// attack times equal to the static index's at 12 of 14 settings, the other two analysis failures on the fastest releases;
// the release read at the note-off (the wheel moved during the hold), the attack at note-on (inferred))
const int16_t kBoxSpan[kBoxDests] = { 1024, 128, 128, 2400, 100, 1024, 64, 8192, 8192, 128, 128 };

int boxDest(int ref) noexcept
{
    switch (ref) {
    case 85: return BoxCutoff;
    case 86: return BoxResonance;
    case 60: return BoxLevel;
    case 67: return BoxCoarse;
    case 68: return BoxFine;
    case 87: return BoxHpf;
    case 79: return BoxFegDepth;
    case 64: return BoxElfoPitch;
    case 65: return BoxElfoAmp;
    case 75: return BoxAegAttack;
    case 78: return BoxAegRelease;
    default: return -1;
    }
}

// ---- the part LFO (docs/acceptance.md (p)) ----
int lfoDest(int ref) noexcept
{
    // element level, pitch, cutoff, pan (LfoDest order); the resonance not yet (its takes clipped)
    return ref >= 64 && ref <= 68 && ref != 67 ? ref - 64 : -1;
}

float partLfoHz(int speed) noexcept
{
    // Measured (round 6, batch K): speeds 2-31 by a saw's resets over 6-70 s takes, 32-63 by a triangle's zero crossings
    // (round 5, batch D): one exponential, doubling every 8.026 steps, within 0.7 % at 19 speeds; speed 1 a 565 s cycle (a
    // saw rising 0.199 cents a second over 68 s), speed 0 no motion
    if (speed <= 0) return 0.0f;
    if (speed == 1) return 0.00177f;
    return std::exp2(-4.26258f + 0.124591f * float(speed));
}

float partLfoDepth(int dest, int depth) noexcept
{
    depth = std::max(0, std::min(127, depth));
    if (dest == LfoPitch) {
        // cents of the wave's full swing (a bipolar wave +-, a unipolar one 0..): whole cents measured every 4 steps on a
        // square's plateaus (round 6, batch K: 33 depths, each plateau within 0.1 cent of whole), linear between
        static const int16_t knot[33] = {0, 6, 7, 9, 10, 13, 16, 19, 23, 29, 35, 42, 51, 62, 76, 92, 112,
                                         136, 166, 201, 244, 297, 360, 438, 531, 646, 785, 953, 1158, 1406, 1708, 2074, 2400};
        if (depth >= 124) return 2074.0f + (2400.0f - 2074.0f) * float(depth - 124) / 3.0f;
        const int k = depth / 4;
        return float(knot[k]) + float(knot[k + 1] - knot[k]) * float(depth % 4) / 4.0f;
    }
    switch (dest) {
    case LfoLevel: return float(depth);         // level steps (a square at 17 depths: within 0.8 step, the level's grid)
    case LfoCutoff: return 8.0f * float(depth);  // index steps (a square on white noise at 8 depths: 128, 256, 380 / 364)
    case LfoPan: {
        // raw pan steps, saturating: a square's far side at 8 depths on the balance law's grid (within 0.23 dB): 34, 50,
        // 57, 61, 62 at 16-80, the far side silent from 96; linear between
        static const int8_t knot[8] = {0, 34, 50, 57, 61, 62, 63, 63};
        const int k = depth / 16;
        return k >= 7 ? 63.0f : float(knot[k]) + float(knot[k + 1] - knot[k]) * float(depth % 16) / 16.0f;
    }
    default: return 0.0f;  // the resonance: not built (lfoDest)
    }
}

float partLfoWave(int wave, double phase, const int8_t* steps, int cycle, int slope, uint32_t seed) noexcept
{
    const double fl = std::floor(phase);
    const float p = float(phase - fl);
    switch (wave) {
    case 0: return p < 0.25f ? 4.0f * p : p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f;  // triangle, from zero rising
    case 1: return p < 0.5f ? 2.0f * p : 2.0f - 2.0f * p;                                  // triangle+: 0..1
    case 2: return p;                                                                      // saw up: 0..1
    case 3: return -p;                                                                     // saw down: 0..-1
    case 4: case 5: case 6: case 7: case 8: {                                              // squares: duty 1/4..3/4
        static const float duty[5] = {0.25f, 1.0f / 3.0f, 0.5f, 2.0f / 3.0f, 0.75f};
        return p < duty[wave - 4] ? 1.0f : -1.0f;
    }
    case 9: {                                                                              // trapezoid: twice the triangle, clipped
        const float t = p < 0.25f ? 4.0f * p : p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f;
        return std::max(-1.0f, std::min(1.0f, 2.0f * t));
    }
    case 10: case 11: {  // sample and hold: a value a cycle, uniform within -1..1; S/H 2 holds 0 on about half the cycles
        uint32_t h = uint32_t(int64_t(fl)) * 0x9e3779b1u ^ seed;
        h ^= h >> 15; h *= 0x2c1b3c6du; h ^= h >> 12; h *= 0x297a2d39u; h ^= h >> 15;
        if (wave == 11 && (h & 1)) return 0.0f;
        return float(h >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }
    default: {  // user: its steps, each to the next by a straight line where the slope says (up, down or both), else held
        auto at = [&](int k) { return steps[k] >= 63 ? 1.0f : float(steps[k]) / 64.0f; };  // +63 plays as +64
        const int n = std::max(1, cycle);
        const float x = p * float(n);
        const int k = std::min(n - 1, int(x));
        const float a = at(k), b = at((k + 1) % n);
        const bool ramp = slope == 3 || (slope == 1 && b > a) || (slope == 2 && b < a);
        return ramp ? a + (b - a) * (x - float(k)) : a;
    }
    }
}


}  // namespace laws
}  // namespace pcm
