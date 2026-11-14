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

// ---- interpolation ----
// The interpolation kernel's cutoff, as a share of the source's Nyquist, for a transposition (source
// frames per output frame at 44.1 kHz): measured on the reference (docs/acceptance.md (f)). At and
// just below the root the reference passes the source unchanged (1.0); further down this engine's
// 0.9 (the reference's droop there is not measured); above the root the cutoff follows the step.
float kernelCutoff(double transposition) noexcept;

}  // namespace laws
}  // namespace pcm
