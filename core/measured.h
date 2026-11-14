// SPDX-License-Identifier: AGPL-3.0-or-later
// Laws this engine measured on the reference itself (docs/acceptance.md), beside the fork's in laws.h.
#pragma once
#include <cstdint>

namespace pcm {
namespace laws {

// A release's end: a straight line in dB at the release's rate down to here, where the voice ends
// (measured on the reference at release 96, 104 and 112, element levels 127 and 64: -96.45 dB re the
// envelope's full scale; docs/acceptance.md (e))
constexpr double kReleaseEndDb = -96.45;

// The filter envelope's release time at an index as a share of the pitch envelope's (1.5 x the
// decay table's time per 60 dB): measured, periodic in the index's place within its octave of 8.
float filterReleaseFactor(int32_t index) noexcept;

// Elements 2-8 play 15 steps of 0.375 dB below element 1 when their filter is thru (measured on
// sines: element 2, 2 with element 1 off, and 8 all -5.63 dB, whatever the filter gain; with a filter
// elements 1 and 2 are alike; docs/acceptance.md (i))
constexpr double kThruElementDb = -5.625;

// ---- the part's velocity ----
// The part's velocity sensitivity depth and offset (raw 0-127, 64 neutral) move a note's velocity, on the
// 0-2047 scale of scale2048(): scale2048(v) x scale2048(depth) / 1024 + 2 (scale2048(offset) - 1024),
// truncated, clamped to 0-2047 (measured: docs/acceptance.md (h)). Every use of the note's velocity sees
// it: element velocity limits, level, filter and envelope velocity laws.
int32_t partVelocity2048(int velocity, int32_t depth, int32_t offset) noexcept;
// A velocity on that scale back to 1-127: the highest step at or below it (for the velocity limits and
// the laws that take steps)
int velocityFrom2048(int32_t velocity2048) noexcept;

// ---- interpolation ----
// The interpolation kernel's cutoff, as a share of the source's Nyquist, for a transposition (source
// frames per output frame at 44.1 kHz): measured on the reference (docs/acceptance.md (f)). At and
// just below the root the reference passes the source unchanged (1.0); further down 0.95 (measured);
// above the root the cutoff follows the step.
float kernelCutoff(double transposition) noexcept;

}  // namespace laws
}  // namespace pcm
