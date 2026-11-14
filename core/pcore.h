// SPDX-License-Identifier: AGPL-3.0-or-later
// The DSP core: a plain C++ library with no dependency on a plugin API, the OS, threads, files or a
// heap. It never allocates; the caller owns every object's memory (the Core, the parsed SoundData,
// the Compiled sound) and the decoded samples, which it hands in as SampleViews.
//
// A sound (sound format v0, sound.cpp): a region table (key and velocity ranges, root, tuning,
// level, pan, sample reference, loop) over element parameter sets in the reference's raw units
// (params.inc). The core applies the reference's measured laws to them (laws.cpp, carried over
// from the sample-engine fork); what it does not cover is reported by notCovered(), never
// approximated in silence (docs/coverage.md).
//
// A voice's plan is data (PlanDesc: channels, interpolator, filter stages); the stage kernels are
// plain functions over structure-of-arrays voice state, compiled once per plan (core.cpp).
#pragma once
#include <cstddef>
#include <cstdint>

namespace pcm {

// ---- parameters --------------------------------------------------------------------------------
enum Param : uint16_t {
#define X(e, id, lo, hi, def) e,
#include "params.inc"
#undef X
    kParamCount
};
enum PartParam : uint16_t {
#define X(e, id, lo, hi, def) e,
#include "partparams.inc"
#undef X
    kPartParamCount
};
struct ParamDef { const char* id; int16_t lo, hi, def; };
extern const ParamDef kParamDefs[kParamCount];
extern const ParamDef kPartParamDefs[kPartParamCount];
int findParam(const char* id, size_t length);      // -1 if none
int findPartParam(const char* id, size_t length);  // -1 if none

// ---- the sound (format v0) ---------------------------------------------------------------------
constexpr uint16_t kFormatVersion = 0;
constexpr uint32_t kMaxSamples = 1024, kMaxElements = 32, kMaxRegions = 1024, kMaxRefBytes = 255;

struct Element {
    int16_t v[kParamCount];
    Element();  // every parameter at its default
};
struct Region {
    uint8_t loKey = 0, hiKey = 127, loVel = 1, hiVel = 127, root = 60;
    int8_t transpose = 0;   // semitones
    uint16_t element = 0, sample = 0;
    int32_t tune = 0;       // 1/100 cent
    int32_t level = 0;      // 1/100 dB (the region's own: a key bank's level)
    int16_t pan = 0;        // -10000..10000: the region's own pan, 1/100 of -100..100
    uint8_t loop = 0;       // 0: plays to the sample's end; 1: forward loop
    uint32_t loopStart = 0, loopEnd = 0;  // frames, the end exclusive
};
// A sound as parsed: the caller's memory (about 330 KB).
struct SoundData {
    int16_t part[kPartParamCount];
    uint32_t samples = 0, elements = 0, regions = 0;
    Element element[kMaxElements];
    Region region[kMaxRegions];
    uint8_t refLength[kMaxSamples];
    char ref[kMaxSamples][kMaxRefBytes + 1];  // abstract sample references, NUL-terminated
    SoundData();
};
// The binary form: "OPCM", version, sizes, a CRC-32, then the tables (sound.cpp). writeBlob and
// writeText return the bytes needed (without a NUL) and write at most cap bytes; readBlob and
// readText validate everything and leave `out` unspecified on failure (error says why).
size_t writeBlob(const SoundData&, uint8_t* out, size_t cap);
bool readBlob(const uint8_t* blob, size_t size, SoundData& out, char* error, size_t errorCap);
size_t writeText(const SoundData&, char* out, size_t cap);
bool readText(const char* text, SoundData& out, char* error, size_t errorCap);
bool operator==(const SoundData&, const SoundData&);
// Every value the core does not reproduce by a measured law (all of them; fills at most cap).
struct Gap { uint16_t element; int16_t param; const char* why; bool part = false; };  // part: param is a PartParam; param -1: about the region table
uint32_t notCovered(const SoundData&, Gap* out, uint32_t cap);

// ---- samples -----------------------------------------------------------------------------------
// Decoded 16-bit PCM, interleaved when stereo, owned by the caller. Frames [-kGuardPre, frames +
// kGuardPost) must be readable and zero outside [0, frames).
constexpr uint32_t kGuardPre = 32, kGuardPost = 1024;
struct SampleView { const int16_t* data = nullptr; uint32_t frames = 0; uint8_t channels = 0; float rate = 0; };

// ---- the plan ----------------------------------------------------------------------------------
enum class Interp : uint8_t { Hermite, Sinc };  // Sinc: 20 taps (docs/design.md)
// LowPass2: type 0, two low-pass sections; LowHigh: type 4, a low-pass section and a fixed high-pass
// section (damping 1, at the HPF cutoff); PoleHigh: type 5, a one-pole low-pass and that high-pass
enum class Filter : uint8_t { Thru, LowPass2, LowHigh, PoleHigh };
constexpr int kFilters = 4;
struct PlanDesc { uint8_t channels; Interp interp; Filter filter; };
// Estimated instructions (aarch64, -O3, measured; docs/cost.md): one element voice for one 128-frame
// block, and a block with no voice sounding.
struct Cost { double voiceBlock, rest; };
Cost estimateCost(const PlanDesc&);

// ---- the compiled sound ------------------------------------------------------------------------
// A sound bound to its decoded samples, ready for the audio thread (caller memory). The laws that
// depend on the note (key, velocity) are applied at note-on. It points into nothing but the
// SampleViews it was given, which must outlive the voices using them.
struct Compiled {
    bool loaded = false;
    uint32_t regions = 0;
    Region region[kMaxRegions];
    SampleView sample[kMaxRegions];   // each region's sample
    PlanDesc plan[kMaxRegions];       // each region's (the interpolator is the core's choice)
    Element element[kMaxElements];
    float bendUp = 2, bendDown = -2;  // semitones at full bend
    int16_t velDepth = 64, velOffset = 64;  // the part's velocity sensitivity (raw)
    bool splitOutputs = false;        // elements to their output buses (else all on bus 0)
    double noteCost = 0;              // estimated instructions a block of the costliest note (all its layers)
    // the controller boxes the core moves (laws::boxDest), in the part's order: number (0-15), source, destination,
    // curve; ratio (raw +127 as +128) and offset (1024 unipolar, 512 bipolar)
    struct Box { uint8_t number, src, dest; int8_t curve; float ratio, off; };
    uint8_t boxes = 0;
    Box box[16];
    bool elfoBoxes = false;           // a box drives the element LFO's depth
};
// Binds a parsed sound to its samples (views[i] for the sound's sample reference i). False when a
// region's sample is missing or empty, or a loop lies outside its sample (error says which).
bool compile(const SoundData&, const SampleView* views, Compiled& out, char* error, size_t errorCap);
// Whether a key and velocity (1..127, as played: the part's velocity sensitivity applies) start any region.
bool keySounds(const Compiled&, int key, int velocity);

// ---- the engine --------------------------------------------------------------------------------
constexpr uint32_t kMaxVoices = 64;   // the caller's voice slots (notes); a note plays every region it hits
constexpr uint32_t kMaxSounds = 8;    // sound slots
constexpr uint32_t kMaxLayers = 8;    // regions a note may start
constexpr uint32_t kPoolVoices = 128; // element voices of one plan
constexpr int kSub = 16;              // frames between control steps, from each block's start

struct VoiceState { bool sounding, releasing; float level; uint64_t age; };

}  // namespace pcm
#include "voices.h"  // the voice pools: plain structure-of-arrays data
namespace pcm {

class Core {
public:
    Core();
    Core(const Core&) = delete;
    Core& operator=(const Core&) = delete;
    void prepare(double sampleRate);  // drops every voice and sound
    // Puts a compiled sound in a slot (by pointer: it must stay unchanged while installed) and ends
    // the voices still playing the slot's previous sound. Audio thread.
    void install(uint32_t sound, const Compiled* s);
    void setGain(uint32_t sound, float gain, float pan);  // the caller's level (linear) and pan (-1..1)
    void setInterp(Interp i) { interp_ = i; }              // default Sinc; Hermite for comparisons

    // Events, between render calls; `at` is the frame of the coming render calls' block (see render).
    void noteOn(uint32_t voice, uint32_t sound, int channel, int key, float velocity);  // velocity 0..1
    void noteOff(uint32_t voice);                     // the key is up and no pedal holds the note
    void keyUp(uint32_t voice);                       // the key is up while the sustain pedal holds the note
    void setSustain(int channel, float value);        // the sustain pedal, 0..1 (half damper elements follow it)
    void choke(uint32_t voice, uint32_t fadeFrames);  // a straight fade, then the slot is free
    void kill(uint32_t voice);                        // free now
    void setVolume(uint32_t voice, float gain);       // the note's volume expression, linear
    void setPitchBend(int channel, float value);      // -1..1 of the sound's bend range
    void setModWheel(int channel, float value);       // 0..1
    // A controller box source of the channel, 0..1: 2 channel pressure, 3 foot controller 1 (CC 11), 7 breath (CC 2)
    void setSource(int channel, int source, float value);
    // A box source of the sound's own (the shell's parameters), 0..1: 6 the ribbon, 8-15 knobs 1-8, 16 and 17 switches 1 and 2
    void setControl(uint32_t sound, int source, float value);
    // A box destination moved from outside the boxes (the shell's parameter plus its modulation) in the destination's raw
    // steps (cents for the tunings): added to its boxes' steps toward zero, the sum within +-span (laws::BoxDest order)
    void setOffset(uint32_t sound, int dest, float steps);
    void reset();                                     // every voice off, controllers to rest
    // Adds every voice into left/right for `frames` frames that start `from` frames into the host's
    // block. Control steps (envelopes, filter coefficients) fall every kSub frames from the block's
    // start, whatever the segments, so a block split by events renders as the unsplit one.
    void render(float* left, float* right, uint32_t frames, uint32_t from);
    // The same with three stereo output buses: 0 thru, 1 to insertion A, 2 to insertion B (an element
    // whose sound splits its outputs plays on its connection's bus; the rest on bus 0). Buses given the
    // same buffers as bus 0 sum into it, exactly as render(left, right, ...).
    void render(float* const* left, float* const* right, uint32_t frames, uint32_t from);
    // Sheds n voices now: the quietest element voices fade out over one control step's frames.
    void shed(uint32_t n);

    uint32_t voiceCount() const;  // slots sounding
    uint32_t elementVoices() const;
    VoiceState voice(uint32_t v) const;

private:
    struct Slot { bool on = false, keyUp = false, keyOff = false; uint8_t sound = 0; uint8_t layers = 0; uint8_t pool[kMaxLayers]; uint16_t id[kMaxLayers]; uint64_t age = 0; float volume = 1;
                  uint8_t key = 0, vel = 0, channel = 0; int16_t vel2048 = 0; };
    Pool& poolOf(const PlanDesc&);
    void free_(Pool&, uint32_t pos);
    void startLayer(uint32_t slot, const Compiled&, uint32_t region, int key, int velocity, int32_t velocity2048, int channel);
    void control(Pool&, uint32_t i, bool grid);
    void releaseLayer(Pool&, uint32_t i, int32_t releaseIndex);
    int32_t noteOnSteps(const Compiled&, const int16_t* element, uint32_t sound, int channel, int dest) const;
    void startKeyOff(uint32_t slot);
    void reap_();
    double sr_ = 48000;
    int64_t clock_ = 0;  // frames rendered since construction
    Interp interp_ = Interp::Sinc;
    uint16_t nextId_ = 1;
    float bend_[16] = {}, wheel_[16] = {}, pedal_[16] = {}, pressure_[16] = {}, foot_[16] = {}, breath_[16] = {};
    float boxSourceOf(uint32_t sound, int channel, int src) const;
    static constexpr int kControls = 18, kOffsets = 11;
    float control_[kMaxSounds][kControls];   // the sounds' own box sources (setControl), 0..1
    int32_t offset_[kMaxSounds][kOffsets];   // the destinations' outside steps (setOffset), toward zero
    bool offsetOn_[kMaxSounds] = {};
    float gain_[kMaxSounds], panL_[kMaxSounds], panR_[kMaxSounds];
    const Compiled* sound_[kMaxSounds] = {};
    Slot slot_[kMaxVoices];
    FilterRate rate_;
    Tables tables_;
    Pool pool_[2 * kFilters];  // channels 1, 2 x the filters
    Scratch scratch_;
};

}  // namespace pcm
