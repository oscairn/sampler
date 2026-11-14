// SPDX-License-Identifier: AGPL-3.0-or-later
// A sound's samples from packs: each sample reference resolved to a record (through the caller's resolver), the
// records read in ascending offset order after one read-ahead batch, and the audio the core needs. Main thread.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../core/pcore.h"
#include "packs.h"

namespace pcmshell {

// A sample reference to its record (false with a reason).
using SampleResolver = std::function<bool(const char* ref, Record&, std::string& error)>;

// What a sound's samples resolve to; the core's views point into it, so it lives while the sound does.
struct Samples {
    std::vector<Record> records;                  // one a sample reference
    std::vector<std::vector<int16_t>> pcm;        // decoded audio the core reads
    std::vector<pcm::SampleView> views;           // one a sample reference
    uint64_t bytes = 0;                           // decoded audio held
};
std::shared_ptr<const Samples> loadSamples(const pcm::SoundData&, const SampleResolver&, std::string& error);

}  // namespace pcmshell
