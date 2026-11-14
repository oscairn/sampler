// SPDX-License-Identifier: AGPL-3.0-or-later
// Internal plain data of the core (included by pcore.h): the element voice pools as structure of
// arrays (the per-sample state the kernels read in vector lanes) plus each voice's scalar side (the
// control step's envelopes, in double, and the sample feeder).
#pragma once
#include <cstdint>

namespace pcm {

// Per-voice vector fields: f[field][voice].
enum Field {
    G, M, A, LO, HI,       // the amplitude envelope, per sample: g = min(max(g m + a, lo), hi)
    GL, DGL, GR, DGR,      // output gains (level, pan, volume, LFO, filter gain) and their ramps
    C0, CENTS,             // the filter cutoff: cents from 440 Hz without the envelope (key follow, velocity in), the envelope's
    CINV, CCI, CKI, CKF, CK, CKFG, CC1, CC2, CC1M, CC2M,  // the (first) section's coefficients (type 0: both alike)
    HINV, HCI, HKI, HKF, HK, HKFG, HC1, HC2, HC1M, HC2M,  // types 4, 5: the fixed high-pass section's
    ST0,                   // filter states: [section][channel][low1, low2, band1, band2]
    DAMP = ST0 + 16,       // damping of the resonance (before the limit)
    B1, B2,                // 1 on output bus 1 (to insertion A) / bus 2 (to insertion B), else 0
    EB0, EB1, EB2, EA1, EA2,  // the element EQ's biquad (identity when it is flat)
    ES0,                   // its states: [channel][s1, s2] (transposed direct form II)
    NFIELDS = ES0 + 4
};
constexpr int st(int section, int channel, int k) { return ST0 + (section * 2 + channel) * 4 + k; }

// The amplitude envelope's control side (the fork's ElementEnvelope, segment by segment).
struct AmpEnv {
    enum Stage : uint8_t { Attack, Decay1, Decay2, Sustain, Release, Fade, Done };
    Stage stage = Done;
    bool ramp = false;        // a straight line in amplitude (attack, short release, fade); else in dB
    double gain = 0, db = 0;  // where the per-sample chain stands after sample `last`
    double step = 0;          // per sample: amplitude (ramp) or dB
    double ratio = 1, ratio16 = 1;  // dB segments: gain factor per sample and per kSub samples
    double target = 0;        // amplitude (ramp) or dB
    double releaseOverride = 0;  // seconds per 60 dB, 0 for the index's time
    bool again = false;       // a release entered again (a new index while releasing): it keeps the index's rate
    int64_t last = -1;        // the engine clock of the last sample `gain` includes
    int32_t attackTime = 0, decay1Time = 0, decay2Time = 0, releaseTime = 0;  // shifted indices
    int32_t initialLevel = 0, attackLevel = 127, decay1Level = 127, decay2Level = 127;
};

// The pitch and filter envelopes (the fork's ElementPitchEnvelope): straight lines in cents taking
// their segment's time. Read once a control step, at the step's first sample.
struct CentsEnv {
    enum Stage : uint8_t { Hold, Attack, Decay1, Decay2, Sustain, Release, Done };
    Stage stage = Done;
    double cents = 0, step = 0, target = 0;
    int64_t left = 0;          // samples left in the segment
    int64_t last = -1;         // the engine clock of the last sample `cents` includes
    int32_t time[5] = {};      // hold, attack, decay 1, decay 2, release (indices, shifted)
    float level[5] = {};       // their target cents
    bool filter = false;       // the filter envelope (its release law differs: laws::filterReleaseFactor)
    bool moving() const { return stage != Sustain && stage != Done; }
};

struct VoiceScalar {
    uint16_t id = 0;           // the slot's layer id
    uint8_t slot = 0, sound = 0, channel = 0, key = 60, bus = 0;
    bool releasing = false, fresh = true, filterMoves = false, eq = false;  // eq: the element EQ shapes the sound
    bool steady = false, dirty = true;  // steady: control steps change nothing until an event makes it dirty
    // the sample and its loop (virtual, unwrapped positions; see feed())
    const int16_t* data = nullptr;
    int64_t frames = 0, loopStart = 0, loopEnd = 0, cycEnd = 0, shift = 0;
    bool loop = false, ended = false;
    AmpEnv amp;
    CentsEnv peg, feg;
    float pegScale = 1;        // pitch EG level scale (depth velocity sensitivity)
    double baseStep = 1;       // source frames per output frame at the region's pitch, no modulation
    float level = 0;           // constant gain: velocity, element and region level, filter gain, staging
    float panL = 1, panR = 1;  // pan laws
    float volume = 1, soundGain = 1, soundPanL = 1, soundPanR = 1;
    float lfoPitchRatio = 0, lfoAmpRatio = 0;  // controller-set ratios (0: none)
    bool halfDamper = false, pedalRelease = false;  // a half damper element; releasing under the pedal
    int32_t releaseBase = 0, halfDamperTime = 127;  // the release index (time shifts in) and the half damper's
    int64_t releaseAt = 0;     // a key-off element: the engine clock its release starts at (0: none)
    int64_t start = 0;         // engine clock of the first sample
    float curCents = 0;        // the filter envelope's cents the coefficients were designed for
    float curPitch = 0;        // the pitch cents the step was set for
};

struct Pool {
    PlanDesc plan{};
    uint32_t n = 0;  // voices 0..n-1 are live
    alignas(64) float f[NFIELDS][kPoolVoices];
    alignas(64) int64_t pos[kPoolVoices], step[kPoolVoices], dstep[kPoolVoices];
    alignas(16) int32_t kernel[kPoolVoices + 4];  // each voice's kernel: its offset into Tables::sinc
    VoiceScalar v[kPoolVoices];
};

// The filter's reference-rate delay at this rate (the fork's ElementFilter::setSampleRate): the
// allpass alone (delay 0: rates between 44.1 and 132.3 kHz that are no whole multiple) or a unit
// delay (44.1 kHz, and below it the plain 44.1 kHz sections). Others are not covered.
struct FilterRate {
    bool allpass = true;
    float pin2[3] = {};
    double ratio = 48000.0 / 44100.0;
    bool covered = true;
};

// The 20-tap interpolation kernel: kPhases + 1 rows, each the taps in groups of four, every group
// followed by its differences to the next row.
// (PCM_KERNEL_TAPS and _BETA build the comparison kernels of docs/design.md; the default is the
// engine's.)
#ifndef PCM_KERNEL_TAPS
#define PCM_KERNEL_TAPS 20
#define PCM_KERNEL_BETA 7.5
#endif
constexpr int kTaps = PCM_KERNEL_TAPS, kPhases = 256;
// The kernels at cutoffs 1.0, 0.95, ..., 0.2 of the source's Nyquist (kernelCut()): a voice takes the
// one its transposition asks for (laws::kernelCutoff), as an offset into `sinc`
constexpr int kKernelBank = 17, kKernelFloats = (kPhases + 1) * 2 * kTaps;
inline double kernelCut(int k) { return 1.0 - 0.05 * k; }
struct Tables {
    alignas(16) float sinc[kKernelBank * kKernelFloats];
};

// Per-render scratch: each lane voice's tap window for the piece, as float (at most kSub steps of
// the largest step plus the taps).
constexpr int kWindowFrames = kSub * 33 + 64;
struct Scratch {
    alignas(16) float win[4][kWindowFrames * 2 + 8];
};

}  // namespace pcm
