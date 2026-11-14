// SPDX-License-Identifier: AGPL-3.0-or-later
#include "samples.h"

#include <cstdio>
#include <cstring>

#define DR_FLAC_IMPLEMENTATION
#define DR_WAV_IMPLEMENTATION
#include <dr_flac.h>
#include <dr_wav.h>

namespace pcmshell {

pcm::SampleView SampleBuffer::view() const {
    pcm::SampleView v;
    v.data = pcm.data() + pcm::kGuardPre * channels;
    v.frames = frames; v.channels = channels; v.rate = rate;
    return v;
}

bool decodeFile(const std::string& path, SampleBuffer& out, std::string& error) {
    char magic[4] = {};
    if (FILE* f = std::fopen(path.c_str(), "rb")) { const size_t n = std::fread(magic, 1, 4, f); std::fclose(f); if (n != 4) { error = "too short"; return false; } }
    else { error = "cannot open"; return false; }
    unsigned channels = 0, rate = 0; drflac_uint64 frames = 0;
    int16_t* data = nullptr;
    if (!std::memcmp(magic, "fLaC", 4)) data = drflac_open_file_and_read_pcm_frames_s16(path.c_str(), &channels, &rate, &frames, nullptr);
    else if (!std::memcmp(magic, "RIFF", 4) || !std::memcmp(magic, "RF64", 4)) {
        drwav_uint64 n = 0;
        data = drwav_open_file_and_read_pcm_frames_s16(path.c_str(), &channels, &rate, &n, nullptr);
        frames = n;
    } else { error = "not a WAV or FLAC file"; return false; }
    if (!data) { error = "cannot decode"; return false; }
    bool ok = (channels == 1 || channels == 2) && rate > 0 && frames > 0 && frames < (1ull << 31);
    if (!ok) error = channels > 2 ? "more than two channels" : "empty or too long";
    else {
        out.channels = uint8_t(channels); out.rate = float(rate); out.frames = uint32_t(frames);
        out.pcm.assign((pcm::kGuardPre + frames + pcm::kGuardPost) * channels, 0);
        std::memcpy(out.pcm.data() + pcm::kGuardPre * channels, data, size_t(frames) * channels * sizeof(int16_t));
    }
    drflac_free(data, nullptr);  // both libraries free with the C allocator by default
    return ok;
}

}  // namespace pcmshell
