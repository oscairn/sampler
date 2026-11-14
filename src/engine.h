// SPDX-License-Identifier: AGPL-3.0-or-later
// The interface between the plugin shell (plugin.cpp: CLAP, sound contexts, voice budget, note
// lifetime, state, sample assets) and the DSP core. The core implements Engine; the shell never
// looks inside it.
//
// Voices are slots 0..kVoices-1, one note each (a note plays every region of its sound it hits).
// The shell owns the slot assignment: it picks a free slot (or frees one) and starts the note
// there, so a slot is one note's voice and the shell's record of the note has the same index. The
// core ends a voice by itself when its release finishes (voice(i).sounding turns false); the shell
// then ends the note.
//
// Threads: audio thread = process(); "main" = any other. Nothing the audio thread calls may
// allocate, lock or block.
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <oscairn-clap/assets.h>

#include "../core/pcore.h"
#include "samples.h"

namespace pcmshell {

constexpr uint32_t kVoices = pcm::kMaxVoices;  // the pool: the most notes the core can sound at once
constexpr uint32_t kContexts = 8;              // sound contexts (slot 0 is the default sound)

// The parameters, per sound. level: linear, 1 = the sound's own level; polyphony: the most voices
// this sound may keep (1..kVoices).
enum Param : uint32_t { kLevel, kPan, kPolyphony, kParamCount };
struct ParamInfo { const char* name; double min, max, def; bool stepped; };
inline constexpr ParamInfo kParams[kParamCount] = {
    {"Level", 0, 1, 1, false}, {"Pan", -1, 1, 0, false}, {"Polyphony Limit", 1, kVoices, kVoices, true}};

// A float that can be copied (a Patch is copied) and read by one thread while another writes it.
struct Cell {
    std::atomic<float> v{0};
    Cell() = default;
    Cell(const Cell& o) : v(o.v.load(std::memory_order_relaxed)) {}
    Cell& operator=(const Cell& o) { set(o.get()); return *this; }
    float get() const { return v.load(std::memory_order_relaxed); }
    void set(float x) { v.store(x, std::memory_order_relaxed); }
};

// A sound as the shell holds it: its blob (sound format v0), where its samples resolve from (a base
// asset reference), the parsed form, the decoded samples and the compiled sound. Immutable once
// built; shared by the patches and the engine's staged copies. The main thread builds and frees it.
struct Sound {
    std::vector<uint8_t> blob;
    std::string library, path;                         // the base: samples are <library>/<path>/<reference>
    std::unique_ptr<pcm::SoundData> data;
    std::vector<std::shared_ptr<const SampleBuffer>> samples;
    std::vector<oscairn_asset_ref_t> assets;           // as resolved, with their SHA-256 when the host gave it
    std::unique_ptr<pcm::Compiled> compiled;
};

// One sound's values, as the shell holds, saves and loads them: the shell's parameters and the sound.
struct Patch {
    Cell p[kParamCount];
    std::shared_ptr<const Sound> sound;  // none: plays nothing
    Patch() { for (uint32_t i = 0; i < kParamCount; ++i) p[i].set(float(kParams[i].def)); }
};

struct VoiceInfo {
    bool sounding = false;   // false: the slot is free (never started, ended, killed or faded out)
    bool releasing = false;  // in its release, or fading out after choke()
    float level = 0;         // linear, 1 at full: envelope and volume; not the sound's level
    uint64_t age = 0;        // frames since noteOn
};

struct EngineMemory { uint64_t shared, perContext; };  // bytes, for oscairn.memory
// A sound's estimated cost before any note sounds (for oscairn.voice-budget): one voice for a block of
// maxBlock frames, and the engine with no voice sounding, in microseconds on the device.
struct SoundCost { double voiceMicros, restMicros; };

class Engine {
public:
    virtual ~Engine() = default;
    // Main thread, before processing. May allocate; drops every voice.
    virtual void prepare(double sampleRate, uint32_t maxBlock) = 0;
    virtual EngineMemory memory(double sampleRate, uint32_t maxBlock) const = 0;
    // Main thread, also while processing: the sound for `context` (kContexts slots), which the
    // audio thread will not use before the shell publishes it. May allocate.
    virtual void loadPatch(uint32_t context, const Patch&) = 0;

    // Audio thread from here on.
    virtual bool keySounds(uint32_t context, int16_t key, double velocity) = 0;  // velocity 0..1
    virtual void noteOn(uint32_t voice, uint32_t context, int16_t channel, int16_t key, double velocity) = 0;
    virtual void noteOff(uint32_t voice) = 0;                       // enter the release
    virtual void keyUp(uint32_t voice) = 0;                         // the key is up, the sustain pedal holds the note
    virtual void setSustain(int16_t channel, double value) = 0;     // the sustain pedal, 0..1
    virtual void choke(uint32_t voice, uint32_t fadeFrames) = 0;    // fade out over fadeFrames (the give-up), then free
    virtual void kill(uint32_t voice) = 0;                          // free the slot now; no-op if free
    virtual void setVolume(uint32_t voice, double gain) = 0;        // the note's volume expression, linear 0..4
    virtual void setParam(uint32_t context, Param, double value) = 0;
    virtual void setPitchBend(int16_t channel, double value) = 0;   // -1..1 of the sound's range
    virtual void setModWheel(int16_t channel, double value) = 0;    // 0..1
    // Adds the sound of every voice of every context into left/right; `from` is the frame of the
    // host's block the segment starts at (control steps keep to the block's grid).
    virtual void render(float* left, float* right, uint32_t frames, uint32_t from) = 0;
    virtual uint32_t voiceCount() const = 0;                        // voices sounding, fading ones included
    virtual VoiceInfo voice(uint32_t voice) const = 0;
    virtual SoundCost cost(uint32_t context) const = 0;              // any thread
    virtual void reset() = 0;                                       // every voice off now, controllers to rest
    // Measurement only (main thread, before processing): Hermite instead of the 20-tap kernel.
    virtual void setHermite(bool on) = 0;
};

std::unique_ptr<Engine> makeEngine();  // the core's factory

}  // namespace pcmshell
