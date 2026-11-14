// SPDX-License-Identifier: AGPL-3.0-or-later
// What a process whose mappings are locked (mlockall, as the instrument's plugin workers run) holds after opening a
// large pack, checking one record and decoding blocks of it: VmLck and VmRSS from /proc/self/status (Linux).
//   footprint gen DIR N     N stereo 48 kHz WAV files of 60 s of noise (they barely compress) into DIR
//   footprint measure PACK  mlockall(MCL_CURRENT | MCL_FUTURE), then the pack's first record: open, check, decode
// tools/mlock/footprint.sh builds it against src/packs.cpp and runs both.
#include <sys/mman.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "packs.h"

static void status(const char* when) {
    FILE* f = std::fopen("/proc/self/status", "r");
    char line[256];
    std::printf("%s:", when);
    while (f && std::fgets(line, sizeof line, f))
        if (!std::strncmp(line, "VmLck:", 6) || !std::strncmp(line, "VmRSS:", 6)) { line[std::strcspn(line, "\n")] = 0; std::printf("  %s", line); }
    std::printf("\n");
    if (f) std::fclose(f);
}

static int gen(const char* dir, int n) {
    const uint32_t rate = 48000, frames = rate * 60;
    std::vector<int16_t> pcm(size_t(frames) * 2);
    uint32_t x = 2463534242u;
    for (int k = 0; k < n; ++k) {
        for (auto& v : pcm) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; v = int16_t(x >> 16); }
        const std::string path = std::string(dir) + "/noise" + std::to_string(k) + ".wav";
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) return 1;
        const uint32_t data = frames * 4, riff = 36 + data, fmt = 16, byteRate = rate * 4;
        const uint16_t pcmTag = 1, ch = 2, align = 4, bits = 16;
        std::fwrite("RIFF", 1, 4, f); std::fwrite(&riff, 4, 1, f); std::fwrite("WAVEfmt ", 1, 8, f); std::fwrite(&fmt, 4, 1, f);
        std::fwrite(&pcmTag, 2, 1, f); std::fwrite(&ch, 2, 1, f); std::fwrite(&rate, 4, 1, f); std::fwrite(&byteRate, 4, 1, f);
        std::fwrite(&align, 2, 1, f); std::fwrite(&bits, 2, 1, f); std::fwrite("data", 1, 4, f); std::fwrite(&data, 4, 1, f);
        std::fwrite(pcm.data(), 2, pcm.size(), f);
        std::fclose(f);
    }
    return 0;
}

static int measure(const char* path) {
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) { std::perror("mlockall"); return 1; }
    status("locked");
    std::string error;
    auto pack = pcmshell::openPack(path, error);
    if (!pack || pack->index.empty()) { std::printf("%s\n", error.c_str()); return 1; }
    status("pack open");
    pcmshell::Record r;
    const sp_index_entry e = pack->index[0];
    if (!pcmshell::findRecord(path, e.offset, e.id, 0, 0, r, error) || !pcmshell::checkRecord(r, error)) { std::printf("%s\n", error.c_str()); return 1; }
    auto d = std::make_unique<pcmshell::Decoder>();
    long frames = 0;
    for (uint32_t b = 0; b < 64 && b < r.info.blocks; ++b) frames += long(pcmshell::decodeBlock(r, b, *d));
    std::printf("pack %s: %zu records, record 0: %u blocks; decoded %ld frames\n", path, pack->index.size(), r.info.blocks, frames);
    status("record read");
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 4 && !std::strcmp(argv[1], "gen")) return gen(argv[2], std::atoi(argv[3]));
    if (argc == 3 && !std::strcmp(argv[1], "measure")) return measure(argv[2]);
    std::fprintf(stderr, "usage: footprint gen DIR N | footprint measure PACK\n");
    return 2;
}
