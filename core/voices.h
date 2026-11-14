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
    OFS,                   // cents the first section sits above the cutoff (type 3: its section; type 10: its low-pass, the high-pass as far below), else 0
    TOPC,                  // the cents (from 440 Hz) the first section rises to at most (type 3: above kTopHz) (laws::sectionTopCents)
    CINV, CCI, CKI, CKF, CK, CKFG, CC1, CC2, CC1M, CC2M,  // the (first) section's coefficients (types 0, 6: both alike)
    HINV, HCI, HKI, HKF, HK, HKFG, HC1, HC2, HC1M, HC2M,  // the second section's (types 4, 5: the fixed high-pass; 1, 10: designed
                                                          // with the first; 2, 3: the one-pole)
    ST0,                   // filter states: [section][channel][low1, low2, band1, band2]
    DAMP = ST0 + 16,       // damping of the resonance (before the limit)
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

struct VoiceScalar {  // 512 bytes (static_assert below): the pools index it by shifts
    uint16_t id = 0;           // the slot's layer id
    uint8_t slot = 0, sound = 0, channel = 0, key = 60, bus = 0;
    bool releasing = false, fresh = true, filterMoves = false, eq = false;  // eq: the element EQ shapes the sound
    bool steady = false, dirty = true;  // steady: control steps change nothing until an event makes it dirty
    uint8_t lfoRatio[3] = {127, 127, 127};  // the element's part LFO depth ratio for each slot (it scales a slot on the cutoff)
    // the region's audio (virtual, unwrapped positions; see feed()): its view, the span of it the
    // positions in [segLo, segHi) read (frame x at seg + x * channels), the frames rebasing took off,
    // the stream while it streams; starve: 1 its stream is late (the next control step sheds it), 2 shed
    const SampleView* src = nullptr;
    const int16_t* seg = nullptr;
    int64_t segLo = 0, segHi = 0, shift = 0;
    Stream* stream = nullptr;
    bool loop = false, ended = false;
    uint8_t starve = 0;
    uint8_t plfo = 0, lfoSlots = 0;  // the part LFO: bit d, a slot on destination d (laws::LfoDest) reaches it; bit k, slot k does
    int16_t panRaw = 64;       // the pan before the part LFO (raw)
    AmpEnv amp;
    CentsEnv peg, feg;
    float pegScale = 1;        // pitch EG level scale (depth velocity sensitivity)
    double baseStep = 1;       // source frames per output frame at the region's pitch, no modulation
    float level = 0;           // constant gain: velocity, element and region level, filter gain, staging
    float panL = 1, panR = 1;  // pan laws
    float volume = 1, soundGain = 1, soundPanL = 1, soundPanR = 1;
    float lfoPitchRatio = 0, lfoAmpRatio = 0;  // controller-set ratios (0: none)
    bool halfDamper = false, pedalRelease = false;  // a half damper element; releasing under the pedal
    bool mods = false;         // boxes, element LFO ratios or the sound's offsets may move this voice (else control skips them)
    int32_t releaseBase = 0, halfDamperTime = 127;  // the release index (time shifts in) and the half damper's
    int64_t releaseAt = 0;     // a key-off element: the engine clock its release starts at (0: none)
    int64_t start = 0;         // engine clock of the first sample
    float curCents = 0;        // the filter envelope's cents the coefficients were designed for
    float curPitch = 0;        // the pitch cents the step was set for
    // the controller boxes: the moved values' bases (raw) and the steps they move by now (laws::BoxDest order)
    float levelRest = 0, keyCents = 0, hpfCents = 0;  // the constant gain but the element level's; the cutoff's key and velocity cents
    float quiet = 0;           // released: the envelope level under which its output stays below -96 dBFS (Core::quietLevel)
    int16_t base[6] = {}, moved[6] = {};
    const int16_t* element = nullptr;  // the element's parameters (in the Compiled sound)
};
static_assert(sizeof(VoiceScalar) == 512, "the pools' indexing cost grows past 512 bytes (docs/cost.md)");

struct Pool {
    PlanDesc plan{};
    uint32_t n = 0;  // voices 0..n-1 are live
    alignas(64) float f[NFIELDS][kPoolVoices];
    alignas(64) int64_t pos[kPoolVoices], step[kPoolVoices], dstep[kPoolVoices];
    alignas(16) int32_t kernel[kPoolVoices + 4];  // each voice's kernel: its offset into kSinc
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

// The interpolation kernels (docs/design.md): 8-tap Kaiser-windowed sincs, kPhases + 1 rows of the taps
// each (a voice interpolates linearly between a row and the next), one kernel per cutoff 1.0, 0.95, ...,
// 0.05 of the source's Nyquist (kernelCut()); a voice takes the one its transposition asks for
// (laws::kernelCutoff), as an offset into kSinc. The bank is constant data (core/kernels.cpp, written by
// `pcbench kernels` from kernelTap()), one copy for every instance and process that maps the binary.
constexpr int kTaps = 8, kPhaseBits = 6, kPhases = 1 << kPhaseBits;
constexpr double kKernelBeta = 9.5;
constexpr int kKernelBank = 20, kKernelFloats = ((kPhases + 1) * kTaps + 15) / 16 * 16;  // each kernel on its own cache lines
inline double kernelCut(int k) { return 1.0 - 0.05 * k; }
double kernelTap(int kernel, int phase, int tap);  // the bank's formula (each row's taps sum to 1)
extern const float kSinc[kKernelBank * kKernelFloats];

// Per-render scratch: each lane voice's tap window for the piece, as float (at most kSub steps of
// the largest step plus the taps).
constexpr int kWindowFrames = kSub * 33 + 64;
struct Scratch {
    alignas(16) float win[4][kWindowFrames * 2 + 8];
    float* const* outL = nullptr;  // routed pieces: the output buses (Core::render), at the piece's first frame
    float* const* outR = nullptr;
};

}  // namespace pcm
