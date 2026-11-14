// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

// The reference's measured element laws, carried over from the sample-engine fork
// (src/sfizz/oscairn: ElementEnvelope, ElementLevel, ElementPitch, ElementFilter, ElementLfo),
// where each is documented with its measurement and fit (the fork's doc/oscairn-plan.md,
// K1-K65). The functions are the fork's, with the types made plain; the per-sample machinery
// that uses them is this engine's own (core.cpp).

#pragma once
#include <cstdint>

namespace pcm {
namespace laws {

// ---- envelope (ElementEnvelope) ----
// Level 0 as a decay or release target: segments are straight lines in dB that end here.
constexpr double kFloorDb = -90.0;
// Amplitude of a level index, (level / 127)^2 on the 0.375 dB grid.
float levelGain(int32_t level) noexcept;
// Seconds a full-scale attack (silence to level 127) takes at an attack index.
float attackSeconds(int32_t index) noexcept;
// An attack's time between two levels, as a multiple of attackSeconds().
float attackDurationFactor(int32_t from, int32_t to) noexcept;
// Seconds a decay at an index takes per 60 dB (a full-scale segment, 0 dB to the floor, takes 1.5 times).
float decaySecondsPer60dB(int32_t index) noexcept;
// Seconds the release at an index (32 and up) takes per 60 dB.
float releaseSecondsPer60dB(int32_t index) noexcept;
// Below release index 32: seconds the release's straight line in amplitude takes from level 127
// to silence; 0 above.
float releaseRampSeconds(int32_t index) noexcept;

// Time-index shifts for a note (velocity 1-127): subtracted from each segment's index.
struct TimeScaling {
    int32_t segments = 4;      // 0 attack, 1 attack+decay, 2 decay, 3 attack+release, 4 all
    int32_t velocity = 0;      // -64..+63
    int32_t keyFollow = 0;     // -64..+63
    int32_t center = 24;       // note
    int32_t releaseAdjust = 64;  // 0..127
};
struct TimeShifts { int32_t attack, decay, release; };
TimeShifts timeShifts(const TimeScaling& s, int key, int velocity) noexcept;

// ---- level (ElementLevel) ----
struct Velocity { int32_t sensitivity = 0, offset = 0, curve = 3; };
int32_t velocityScale(const Velocity& v, int velocity) noexcept;  // 0..2047
float velocityGain(const Velocity& v, int velocity) noexcept;
int32_t scale2048(int32_t velocity) noexcept;                       // 1-127 on the 0-2047 scale
float velocityGain2048(const Velocity& v, int32_t velocity2048) noexcept;  // a velocity on that scale
struct LevelScaling {
    int32_t keys[4] = {36, 48, 60, 72};
    int32_t offsets[4] = {0, 0, 0, 0};
    int32_t keyFollow = 0;
    int32_t level = 127;
};
float levelScalingGain(const LevelScaling& s, int key) noexcept;
void balanceGains(int32_t pan, float& left, float& right) noexcept;  // pan 0-127, 64 centre

// ---- pitch and filter envelopes (ElementPitch) ----
float pitchLevelCents(int32_t level, int32_t depth) noexcept;  // raw level 0-255 at a depth
float pitchHoldSeconds(int32_t index) noexcept;
float filterDepthVelocityFactor(int32_t sensitivity, int32_t curve, int velocity) noexcept;

// ---- filter (ElementFilter) ----
constexpr double kReferenceRate = 44100.0;
constexpr double kTopHz = 7290.0;  // the highest cutoff the reference's sections reach
float cutoffCents(float index) noexcept;  // cents from 440 Hz of a cutoff index 0-1023
float filterCutoffHz(int32_t index, float cents) noexcept;
float cutoffVelocityCents(int32_t cutoff, int32_t sensitivity, int velocity) noexcept;
float filterDamping(int32_t resonance) noexcept;
// Cents the first section of a filter type sits above the cutoff at a cutoff index and resonance (width):
// type 3 its section (above kTopHz the design holds it there and damps it, measured), type 10 its low-pass
// section (the high-pass as far below); 0 for the other types
float filterOffsetCents(int type, float index, int32_t resonance) noexcept;
constexpr float kTypeThreeSteps = 340.0f;  // type 3's section above the cutoff, cutoff indices (measured)
constexpr float kTypeThreeSlope = 9.0f;    // and its cents an index past the table's top (measured)
// Type 3's section past kTopHz rises only while its damping (the resonance's, at most 1) stays within 0.9 of the
// section's stability bound (f^2 + 2 f d < 4): the cents from 440 Hz it stops at (measured: docs/acceptance.md (t));
// the other types' sections stop at kTopHz
float sectionTopCents(int type, int32_t resonance) noexcept;
float filterGain(int32_t gain) noexcept;
float dampingLimit(double hz) noexcept;
// The damping limit as a function of cents from 440 Hz: 2 below kDampingKnotCents[0], then
// linear in cents between consecutive knots (kDampingKnotLimit), 1 from the last knot on.
constexpr int kDampingKnots = 13;
extern const float kDampingKnotCents[kDampingKnots];
extern const float kDampingKnotLimit[kDampingKnots];

// ---- element selection (Elements) ----
// An element's key-on delay in seconds at a time length 0-127: 28 knots measured on the reference, linear between (K52)
double keyOnDelaySeconds(int32_t timeLength) noexcept;

// ---- element LFO, reference mode (ElementLfo) ----
constexpr double kLfoHz = 4.848;

}  // namespace laws
}  // namespace pcm
