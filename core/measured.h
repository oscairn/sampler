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

// ---- element EQ ----
// The element EQ for one voice as one biquad at the host's rate (a0 = 1), from the raw type, Q, frequencies
// and gains (measured on white noise through type 0 wide open: docs/acceptance.md (k)). Type 0 (2-band):
// a first-order low shelf (frequency 1, gain 1) times a first-order high shelf (frequency 2, gain 2); type 1
// (P.EQ): a peak at frequency 1, gain 1, Q. Gains (raw - 64) x 0.375 dB. False when the EQ leaves the sound
// unchanged (types 2-5: Boost6/12/18 are flat gains the voice's level carries; 5 thru; 0 dB).
struct Biquad { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
bool elementEq(int type, int q, int freq1, int gain1, int freq2, int gain2, double sampleRate, Biquad& out) noexcept;
// The frequency table every band reads (Hz at a raw value, its low 8 bits) and the peak's Q at a raw Q.
double eqHz(int raw) noexcept;
double eqPeakQ(int raw) noexcept;

// ---- high-pass key follow ----
float hpfKeyCents(int keyFollow, int key) noexcept;  // cents the high-pass cutoff moves at a key

// ---- controller boxes ----
// A box's output (measured: docs/acceptance.md (l)): (curve(c) + o - 1024) x ratio / 8 within +-8192, c the source on
// 0..1024, o 1024 unipolar or 512 bipolar, ratio raw +127 counting as +128. A destination moves by the sum over its
// boxes of trunc(output x span / 8192) (each box truncated toward zero), within +-span.
// The source maps: a controller value v (0-127) is 8 v up to 64, then 512 + (v - 64) x 512 / 63 (1024 at 127); the
// pitch bend its 14-bit value / 16.
float boxSource(int v127) noexcept;
// The part's own sources at x (0..1, the shell's parameters): the ribbon (6) as its controller, CC 16 at round(127 x);
// knobs 1-8 (8-15) at the raw knob value round(1023 x); switches 1-2 (16, 17) off or on.
float controlSource(int source, float x) noexcept;
// The measured curves (bank 0, type 0, parameter p1): their value at c, 0..1024; -1 for one not measured.
int boxCurve(int bank, int type, int p1, int p2) noexcept;
float boxCurveAt(int curve, float c) noexcept;
// The destinations the core moves: the reference's number, the span (raw steps of a full box) in the
// destination's unit (index, or cents for the tunings). The first kLiveDests move a sounding voice at every control
// step; the others are read when the note starts (the filter envelope's depth: measured so).
enum BoxDest : uint8_t { BoxCutoff, BoxResonance, BoxLevel, BoxCoarse, BoxFine, BoxHpf, BoxFegDepth, kBoxDests };
constexpr int kLiveDests = 6;
int boxDest(int reference) noexcept;  // -1: not built
extern const int16_t kBoxSpan[kBoxDests];

}  // namespace laws
}  // namespace pcm
