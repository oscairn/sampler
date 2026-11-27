// SPDX-License-Identifier: AGPL-3.0-or-later
// A sound's audio from packs: each sample reference resolved to a record (through the caller's resolver, or taken from a
// sound of the process that holds it), the records resolved hinted in one read-ahead batch and their headers read in
// ascending offset order, and for each region what the core keeps in memory and what streams (docs/design.md,
// "Streaming"). Main thread.
#pragma once
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../core/pcore.h"
#include "packs.h"
#include "streamer.h"

namespace pcmshell {

// A sample reference to its record, found but not yet read (findRecord, onlyRecord); false with a reason.
using SampleResolver = std::function<bool(const char* ref, Record&, std::string& error)>;

// A region keeps this many frames from its start in memory before it streams (the record's window when it has at
// least that many), and a forward loop's body whole up to kMaxLoopBody frames.
constexpr uint32_t kHeadFrames = 4096, kMaxLoopBody = 16384;

// OSCAIRN_SAMPLER_TRACE=1: a line on stderr for each sound prepared (printTrace); nothing is timed without it.
inline bool tracing() { static const bool on = [] { const char* e = std::getenv("OSCAIRN_SAMPLER_TRACE"); return e && !std::strcmp(e, "1"); }(); return on; }
// Wall time since the last call (or construction), in ms; 0 and no clock read when not tracing.
struct Lap {
    std::chrono::steady_clock::time_point t = tracing() ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    double operator()() {
        if (!tracing()) return 0;
        const auto n = std::chrono::steady_clock::now();
        const double d = std::chrono::duration<double, std::milli>(n - t).count();
        t = n;
        return d;
    }
};
// What loadSamples did, in ms (see printTrace).
struct LoadTimes {
    uint32_t samples = 0, resolves = 0;
    double resolve = 0, held = 0, checks = 0, regions = 0, alloc = 0, reads = 0, keep = 0, total = 0;
};
// A prepare's parts, in ms: `hand` is giving the sound to the audio side (negative: not part of this one).
struct PrepareTimes {
    double parse = 0, expand = 0, compile = 0, hand = -1, total = 0;
    LoadTimes load;
};
void printTrace(const PrepareTimes&);

// What a sound's regions play; the core's views point into it, so it lives while the sound does. Freed under
// holdStreams(): the I/O thread may still be finishing a unit of a voice that just ended.
struct Samples {
    std::vector<Record> records;               // one a sample reference
    // what stays in memory: heads (with their guards) and loop bodies, shared with every sound of the process that
    // plays the same frames of the same record
    std::vector<std::shared_ptr<const std::vector<int16_t>>> pcm;
    std::vector<pcm::SampleView> views;        // one a region
    std::vector<StreamSource> sources;         // one a region (used by those that stream)
    Samples() = default;
    Samples(const Samples&) = delete;
    ~Samples();
};
// A waveform reference "waveform:<64 hex digits>" to the path of its file (the host's resolve); false with a reason.
using WaveformResolver = std::function<bool(const char* ref, std::string& path, std::string& error)>;
// The region table `in` plays: each element's waveform read from its file (checked by samplepack, its id compared with
// the reference) and expanded into regions (pcm::expand). False with a reason.
bool expandWaveforms(const pcm::SoundData& in, const WaveformResolver&, pcm::SoundData& out, std::string& error);
// whole: every region decoded whole into memory, nothing streams (for comparisons: OSCAIRN_SAMPLER_DECODE_WHOLE).
std::shared_ptr<const Samples> loadSamples(const pcm::SoundData&, const SampleResolver&, bool whole, std::string& error, LoadTimes* times = nullptr);

}  // namespace pcmshell
