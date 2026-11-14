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
    if (r < 0.95)
        return 0.9f;
    if (r <= 1.0)
        return 1.0f;
    if (r >= at[5])
        return static_cast<float>(std::max(0.2, cut[5] * at[5] / r));
    int i = 1;
    while (r > at[i])
        ++i;
    return static_cast<float>(cut[i - 1] + (cut[i] - cut[i - 1]) * (r - at[i - 1]) / (at[i] - at[i - 1]));
}

}  // namespace laws
}  // namespace pcm
