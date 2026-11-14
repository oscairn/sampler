// SPDX-License-Identifier: AGPL-3.0-or-later
// Decoded samples for the core: a WAV or FLAC file read whole into 16-bit PCM with the core's zero
// guard frames around it (pcm::kGuardPre, pcm::kGuardPost). Main thread; allocates.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "../core/pcore.h"

namespace pcmshell {

struct SampleBuffer {
    std::vector<int16_t> pcm;  // guard, frames, guard; interleaved
    uint32_t frames = 0;
    uint8_t channels = 0;
    float rate = 0;
    pcm::SampleView view() const;
};
// Reads a WAV or FLAC file (by its content). False with a reason when it cannot.
bool decodeFile(const std::string& path, SampleBuffer& out, std::string& error);

}  // namespace pcmshell
