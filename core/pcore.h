// SPDX-License-Identifier: AGPL-3.0-or-later
// The DSP core: a plain C++ library with no dependency on a plugin API, the OS, threads, files or a
// heap. It never allocates; the caller owns every object's memory (the Core, the parsed SoundData,
// the Compiled sound) and the decoded samples, which it hands in as SampleViews. Audio that does not
// stay in memory streams: the caller's I/O thread fills the Core's fixed pool of Streams, handed over
// with atomics only (the Stream rules below).
//
// A sound (sound format v1, sound.cpp): a region table (key and velocity ranges, root, tuning,
// level, pan, sample reference, loop) over element parameter sets in the reference's raw units
// (params.inc). The core applies the reference's measured laws to them (laws.cpp, carried over
// from the sample-engine fork); what it does not cover is reported by notCovered(), never
// approximated in silence (docs/coverage.md).
//
// A voice's plan is data (PlanDesc: channels, interpolator, filter stages); the stage kernels are
// plain functions over structure-of-arrays voice state, compiled once per plan (core.cpp).
#pragma once
#include <atomic>
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

// ---- the sound (format v1; v0 reads unchanged) ---------------------------------------------------
constexpr uint16_t kFormatVersion = 1;
constexpr uint32_t kMaxSamples = 1024, kMaxElements = 32, kMaxRegions = 1024, kMaxRefBytes = 255, kMaxWaveforms = 32;

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
    uint32_t loopStart = 0, loopEnd = 0;  // frames from the region's start, the end exclusive
    uint32_t start = 0, end = 0;          // the sample's frames it plays, the end exclusive (0: the sample's end); not in
                                          // the blob: a waveform's key bank gives them (expand())
};
// A sound as parsed: the caller's memory (about 330 KB).
struct SoundData {
    int16_t part[kPartParamCount];
    uint32_t samples = 0, elements = 0, regions = 0;
    Element element[kMaxElements];
    Region region[kMaxRegions];
    uint8_t refLength[kMaxSamples];
    char ref[kMaxSamples][kMaxRefBytes + 1];  // abstract sample references, NUL-terminated
    uint32_t waveforms = 0;
    uint8_t waveform[kMaxWaveforms][32];      // the waveform ids its elements name (v1)
    SoundData();
};
// The binary form: "OSMP", version, sizes, a CRC-32, then the tables (sound.cpp). writeBlob and
// writeText return the bytes needed (without a NUL) and write at most cap bytes; readBlob and
// readText validate everything and leave `out` unspecified on failure (error says why).
size_t writeBlob(const SoundData&, uint8_t* out, size_t cap);
bool readBlob(const uint8_t* blob, size_t size, SoundData& out, char* error, size_t errorCap);
size_t writeText(const SoundData&, char* out, size_t cap);
bool readText(const char* text, SoundData& out, char* error, size_t errorCap);
bool operator==(const SoundData&, const SoundData&);
// A waveform as the caller read it (its file: the samplepack library): its key banks in canonical order and its
// samples' references ("sample:<64 hex digits>"), which a bank's `sample` indexes.
struct KeyBank {
    uint8_t keyLo, keyHi, velLo, velHi, root; int8_t transpose; uint8_t loop; uint16_t sample;
    int32_t tune, level; int16_t pan; uint32_t start, end, loopStart, loopEnd;
};
struct WaveformData { const KeyBank* banks; uint32_t count; const char* const* samples; uint32_t sampleCount; };
// The region table the sound plays (docs/sound-format.md, "Expansion"): `in` with a region for every key bank of each
// element's waveform (waveforms[i] for the sound's waveform id i) appended, and the samples they use. False when it
// exceeds a limit (error says which).
bool expand(const SoundData& in, const WaveformData* waveforms, SoundData& out, char* error, size_t errorCap);
// Every value the core does not reproduce by a measured law (all of them; fills at most cap).
struct Gap { uint16_t element; int16_t param; const char* why; bool part = false; };  // part: param is a PartParam; param -1: about the region table
uint32_t notCovered(const SoundData&, Gap* out, uint32_t cap);

// ---- samples -----------------------------------------------------------------------------------
// A region's audio, owned by the caller: 16-bit PCM, interleaved when stereo, frames counted from the
// region's own start. `data` holds frames [0, head) with kGuardPre zero frames before them and, when
// head is every frame, kGuardPost zero frames after. A forward loop's body [loopStart, loopEnd) is in
// data when loopEnd <= head, else in `loop` (its frames only) when the caller keeps it whole, else in
// the stream. Every other frame from head on streams: `stream` is the caller's description of it for
// its I/O thread (Stream::source; the core never reads it). Every buffer is readable kOverread values past
// its end.
constexpr uint32_t kGuardPre = 32, kGuardPost = 1024, kOverread = 8;
struct SampleView {
    const int16_t* data = nullptr; uint32_t frames = 0; uint8_t channels = 0; float rate = 0;
    uint32_t head = 0;               // frames in data; 0: all of them
    const int16_t* loop = nullptr;   // a forward loop's body, kept whole
    const void* stream = nullptr;    // the frames from head on (null: nothing streams)
    // compile() sets these from the region
    bool looped = false, loopStreams = false;  // a forward loop; its body streams (too long to keep)
    uint32_t loopStart = 0, loopEnd = 0;
};

// One streaming voice's ring of decoded frames, from a fixed pool in the Core (Core::streams()). The
// audio thread and the caller's I/O thread hand it over with atomics only; the audio thread never
// waits, locks or reads a frame not yet handed over:
//   state: Free -> Active by the audio thread when a voice starts (source and channels set before);
//          Active -> Released when the voice ends; Released -> Free by the I/O thread, which then no
//          longer touches the ring (it zeroes written and consumed first).
//   written: stream frames handed over (stream frame 0 is the region's frame `head`), stored by the
//          I/O thread with release after their samples; frame f sits at ring[(f % kStreamFrames) x channels].
//   consumed: stream frames the voice will not read again, stored by the audio thread with release; the
//          I/O thread writes frame f only while f < consumed + kStreamFrames.
//   speed: source frames per output frame now, for the I/O thread's order of urgency.
// io*: the I/O thread's own fields.
constexpr uint32_t kStreams = 128;        // streaming element voices at once: two a voice slot
constexpr uint32_t kStreamFrames = 4096;  // a ring: four 1,024-frame blocks
struct Stream {
    enum : uint8_t { Free, Active, Released };
    std::atomic<uint8_t> state{Free};
    std::atomic<int64_t> written{0}, consumed{0};
    std::atomic<float> speed{1};
    const void* source = nullptr;
    uint8_t channels = 0;
    bool ioFresh = true, ioDone = false;
    int64_t ioAt = 0, ioZeros = 0;
    alignas(16) int16_t ring[kStreamFrames * 2 + kOverread] = {};
};
// A streaming voice is shed (a 16-frame fade) once fewer than this many frames past its reads are handed
// over and more are due: enough to play the fade at the largest step
constexpr int64_t kStreamMargin = 1152;

// ---- the plan ----------------------------------------------------------------------------------
enum class Interp : uint8_t { Hermite, Sinc };  // Sinc: 20 taps (docs/design.md)
// LowPass2: type 0, two low-pass sections; LowHigh: type 4, a low-pass section and a fixed high-pass
// section (damping 1, at the HPF cutoff); PoleHigh: type 5, a one-pole low-pass and that high-pass;
// LowLow: type 1, a low-pass section and one with damping 1; LowPole: types 2 and 3, a low-pass section
// and a one-pole low-pass at the cutoff (type 3: the section higher, laws::filterOffsetCents); HighHigh:
// type 6, two high-pass sections; High: type 7, one; Band: type 10, a low-pass and a high-pass section
// with damping 1, spread about the cutoff by the width
enum class Filter : uint8_t { Thru, LowPass2, LowHigh, PoleHigh, LowLow, LowPole, HighHigh, High, Band };
constexpr int kFilters = 9;
Filter filterOf(int type) noexcept;  // the plan of a filter type (Thru for the types not built)
struct PlanDesc { uint8_t channels; Interp interp; Filter filter; };
// Estimated instructions (aarch64, -O3, measured; docs/cost.md): one element voice for one 128-frame
// block, and a block with no voice sounding.
struct Cost { double voiceBlock, rest; };
Cost estimateCost(const PlanDesc&);

// ---- the compiled sound ------------------------------------------------------------------------
// A sound bound to its regions' audio, ready for the audio thread (caller memory). The laws that
// depend on the note (key, velocity) are applied at note-on. It points into nothing but the audio
// its SampleViews name, which must outlive the voices using them.
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
    // the part LFO (laws::PartLfo*): one oscillator for the sound, its slots on the destinations the core moves
    struct Lfo {
        uint8_t slots = 0;            // slots with a depth on a built destination (0: no part LFO)
        uint8_t slot[3], dest[3];     // the slot (0-2) and its destination (laws::LfoDest)
        float depth[3];               // the slot's amplitude in its destination's unit
        bool box1 = false;            // a box moves slot 1's depth (destination 57): its amplitude at every control step
        uint8_t depth1 = 0;           // slot 1's raw depth
        uint8_t wave = 0, reset = 2, cycle = 16, slope = 0;
        float hz = 0, phase = 0;      // cycles a second; the start phase in cycles
        float delay = 0, fadeIn = 0, hold = -1, fadeOut = 0;  // the envelope's seconds (hold < 0: for ever)
        int8_t step[16] = {};         // the user wave's steps (as displayed)
    } lfo;
    bool select = false;              // an element plays by cycle, at random or by the switches (Core::noteOn)
};
// Binds a parsed sound to its regions' audio (views[i] for region i). False when a region's audio is
// missing or empty, a loop lies outside it, or frames it plays are neither in memory nor streamed
// (error says which).
bool compile(const SoundData&, const SampleView* views, Compiled& out, char* error, size_t errorCap);
// Whether a key and velocity (1..127, as played: the part's velocity sensitivity applies) start any region.
bool keySounds(const Compiled&, int key, int velocity);

// ---- the engine --------------------------------------------------------------------------------
constexpr uint32_t kMaxVoices = 64;   // the caller's voice slots (notes); a note plays every region it hits
constexpr uint32_t kMaxSounds = 8;    // sound slots
constexpr uint32_t kMaxLayers = 16;   // regions a note may start
constexpr uint32_t kPoolVoices = 128; // element voices of one plan
constexpr int kSub = 16;              // frames between control steps, from each block's start
constexpr uint32_t kOutputs = 11;     // render's output buses: 0-8 the caller's ports, 9 and 10 two more (see route)

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
    // Where a sound plays: output bus `bus` (0..kOutputs-1), and with `elements` an element of a sound that splits its
    // outputs plays on its connection's bus 1 or 2 instead (connection 0 on `bus`). Its voices move at once. Default:
    // bus 0, elements on. Audio thread.
    void route(uint32_t sound, uint32_t bus, bool elements);

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
    // The same with kOutputs stereo output buses, each voice on its sound's bus (route): buses given the same buffers
    // as bus 0 sum into it, exactly as render(left, right, ...).
    void render(float* const* left, float* const* right, uint32_t frames, uint32_t from);
    // Sheds n voices now: the quietest element voices fade out over one control step's frames.
    void shed(uint32_t n);
    // The streams the caller's I/O thread fills (kStreams of them; the Stream rules above).
    Stream* streams() { return streams_; }
    // Element voices given up for want of streamed audio: shed because their stream was late, or not
    // started because every stream was taken. Any thread.
    uint32_t streamSheds() const { return streamSheds_.load(std::memory_order_relaxed); }

    uint32_t voiceCount() const;  // slots sounding
    // Nothing sounds or waits to start: render(frames) then does no more than advance(frames).
    bool idle() const { return !pendingSlots_ && !live_ && !voiceCount(); }
    // The clock moves on without rendering (frames the host skipped while the instrument slept): part LFOs keep their phase.
    void advance(int64_t frames) { clock_ += frames; }
    uint32_t elementVoices() const;
    VoiceState voice(uint32_t v) const;

private:
    struct Slot { bool on = false, keyUp = false, keyOff = false; uint8_t sound = 0; uint8_t layers = 0; uint8_t pool[kMaxLayers]; uint16_t id[kMaxLayers]; uint64_t age = 0; float volume = 1;
                  uint8_t key = 0, vel = 0, channel = 0; int16_t vel2048 = 0;
                  uint8_t pending = 0; uint16_t pendRegion[kMaxLayers]; int64_t pendAt[kMaxLayers]; };  // layers a key-on delay holds back
    Pool& poolOf(const PlanDesc&);
    void free_(Pool&, uint32_t pos);
    void startLayer(uint32_t slot, const Compiled&, uint32_t region, int key, int velocity, int32_t velocity2048, int channel);
    void control(Pool&, uint32_t i, bool grid);
    float partLfo(Pool&, uint32_t i);
    void releaseLayer(Pool&, uint32_t i, int32_t releaseIndex);
    float quietLevel(const Pool&, uint32_t i) const;
    int32_t noteOnSteps(const Compiled&, const int16_t* element, uint32_t sound, int channel, int dest) const;
    uint32_t selectElements(const Compiled&, uint32_t sound, int key, int velocity);
    void startKeyOff(uint32_t slot);
    void dropPending(Slot&);
    int64_t startPending_();  // starts the held-back layers due now; the clock the next one is due at
    void reap_();
    void fadeFast(Pool&, uint32_t i);   // a 16-frame fade, then free (a shed)
    Stream* takeStream();
    double sr_ = 48000;
    int64_t clock_ = 0;  // frames rendered since construction
    Interp interp_ = Interp::Sinc;
    uint16_t nextId_ = 1;
    float bend_[16] = {}, wheel_[16] = {}, pedal_[16] = {}, pressure_[16] = {}, foot_[16] = {}, breath_[16] = {};
    float boxSourceOf(uint32_t sound, int channel, int src) const;
    static constexpr int kControls = 18, kOffsets = 12;
    float control_[kMaxSounds][kControls];   // the sounds' own box sources (setControl), 0..1
    int32_t offset_[kMaxSounds][kOffsets];   // the destinations' outside steps (setOffset), toward zero
    bool offsetOn_[kMaxSounds] = {};
    int64_t lfoStart_[kMaxSounds] = {};      // the engine clock the sound's part LFO last started (reset) at
    uint8_t cycle_[kMaxSounds][8] = {};      // each element group's next cycle position (an element index)
    uint32_t draw_[kMaxSounds] = {};         // the random elements' generator
    uint32_t pendingSlots_ = 0;
    uint32_t live_ = 0;  // element voices in all pools (the silent test: the pools are many)
    uint32_t rand_ = 0x9e3779b9u;            // random pitch draws              // slots with layers held back by a key-on delay
    float gain_[kMaxSounds], panL_[kMaxSounds], panR_[kMaxSounds];
    uint8_t bus_[kMaxSounds] = {};           // route(): each sound's bus
    bool elements_[kMaxSounds];              // and whether its elements may split off
    uint8_t busOf(const VoiceScalar&) const;
    const Compiled* sound_[kMaxSounds] = {};
    Slot slot_[kMaxVoices];
    FilterRate rate_;
    Pool pool_[2 * kFilters];  // channels 1, 2 x the filters
    Scratch scratch_;
    Stream streams_[kStreams];
    uint32_t nextStream_ = 0;
    std::atomic<uint32_t> streamSheds_{0};
};

}  // namespace pcm
