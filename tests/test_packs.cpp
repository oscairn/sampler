// SPDX-License-Identifier: AGPL-3.0-or-later
// Samples from Oscairn Sample Packs: the test host's packs (made by the samplepack tool from generated tones) and
// what the plugin plays from them.
#include <samplepack.h>

#include "host.h"

// The samplepack library is linked and agrees with its specification's worked sample id (docs/format.md, 20 A).
TEST(samplepack_sample_id) {
    const int16_t pcm[4] = {0, 1, -1, 32767};
    uint8_t id[SP_SAMPLE_ID_BYTES];
    CHECK_EQ(sp_sample_id_pcm(44100, 1, pcm, 4, id), SP_OK);
    char hex[65];
    for (int i = 0; i < 32; ++i) std::snprintf(hex + 2 * i, 3, "%02x", id[i]);
    CHECK(std::string(hex) == "1e272d15841dbddadd5503dec6a3ac43020b40c9c0a730cc5cd2f66101301338");
}

namespace {
std::string soundOf(const std::string& ref, const char* region = "key=0-127 root=57 loop=0-44100") {
    return "oscairn-pcm-sound/0\nsample " + ref + "\nelement ampeg_decay1=127 ampeg_decay2=127 ampeg_release=50\nregion " + region + " element=0 sample=0\n";
}
// Renders a note of `sound` (loaded through the CLAP state) for `frames`, released half way.
std::vector<float> play(const std::string& sound, long frames = 48000, int key = 60) {
    Inst a;
    if (!a.load(stateWith(sound))) { std::printf("  the sound did not load\n"); ++g_failures; return {}; }
    a.block(128, {on(5, 1, 0, int16_t(key))});
    a.run(frames / 2);
    a.block(128, {off(0, 1, 0, int16_t(key))});
    a.run(frames / 2);
    return a.L;
}
}  // namespace

// A sample:<id> reference plays the record resolve_sample names, exactly as the same audio from a one-record pack.
TEST(sample_references_resolve) {
    const auto byId = play(soundOf(sampleRef("sine"))), byPath = play(soundOf("sine.osp"));
    CHECK(!byId.empty() && byId == byPath);
    CHECK(peak(byId) > 0.1f);
}

// A sample the host does not hold, or a record that holds another sample, fails the sound with a reason.
TEST(sample_references_fail_with_reasons) {
    Inst a;
    char msg[256] = {};
    const std::string missing = soundOf("sample:" + std::string(64, '0'));
    CHECK_EQ(a.contexts->prepare(a.p, "x", nullptr, 0, missing.c_str(), testBase(), msg, sizeof msg), CLAP_INVALID_ID);
    CHECK(std::strstr(msg, "no sample") != nullptr);
    g_store[std::string(64, 'f')] = g_store.at(g_tones.at("sine").id);  // the host names the wrong record
    const std::string wrong = soundOf("sample:" + std::string(64, 'f'));
    CHECK_EQ(a.contexts->prepare(a.p, "x", nullptr, 0, wrong.c_str(), testBase(), msg, sizeof msg), CLAP_INVALID_ID);
    CHECK(std::strstr(msg, "holds another sample") != nullptr || std::strstr(msg, "bad sample record") != nullptr);
    g_store.erase(std::string(64, 'f'));
}

namespace {
// A note of `sound` from `tone`, with the plugin's I/O thread filling every ring before each block (or every region
// decoded whole when `whole`).
std::vector<float> render(const std::string& sound, bool whole, int key, long frames) {
    if (whole) setenv("OSCAIRN_PCM_DECODE_WHOLE", "1", 1);
    std::vector<float> out;
    {
        Inst a;
        unsetenv("OSCAIRN_PCM_DECODE_WHOLE");
        if (!a.load(stateWith(sound))) { std::printf("  the sound did not load\n"); ++g_failures; return {}; }
        a.block(128, {on(3, 1, 0, int16_t(key))});
        a.run(frames * 3 / 4, 128);
        a.block(100, {off(7, 1, 0, int16_t(key))});
        a.run(frames / 4, 128);
        CHECK_EQ(a.streaming->sheds(a.p), 0u);
        out = a.L;
        out.insert(out.end(), a.R.begin(), a.R.end());
    }
    return out;
}
}  // namespace

// The main correctness test: audio played from packs, streamed, is the same audio decoded whole, sample for sample:
// one-shots, every way a loop is kept (in the head, its body whole with the frames before it streamed once, its body
// whole starting inside the head, too long to keep and streamed), mono and stereo, at the root and transposed up.
TEST(streamed_audio_equals_whole) {
    const char* regions[] = {"", " loop=2000-3900", " loop=50000-60000", " loop=1000-9000", " loop=10000-140000", " loop=70000-70100", " loop=1000-60000"};
    for (const char* tone : {"mono", "stereo"})
        for (const char* loop : regions)
            for (int key : {60, 79, 96}) {
                if (std::string(tone) == "mono" && std::string(loop).find("140000") != std::string::npos) continue;  // past its end
                const std::string snd = "oscairn-pcm-sound/0\nsample " + sampleRef(tone) + "\nelement ampeg_decay1=127 ampeg_decay2=127 ampeg_release=60\n"
                                        "region key=0-127 root=60 element=0 sample=0" + loop + "\n";
                const auto w = render(snd, true, key, 96000), s = render(snd, false, key, 96000);
                if (w.empty() || w != s) { std::printf("  %s%s key %d differs\n", tone, loop, key); ++g_failures; }
                CHECK(peak(s) > 0.01f);
            }
}

// The comparison above compares two different things: decoded whole holds the tone in memory, streamed only its head.
TEST(streaming_holds_only_heads) {
    const std::string snd = "oscairn-pcm-sound/0\nsample " + sampleRef("stereo") + "\nelement\nregion element=0 sample=0\n";
    uint64_t bytes[2] = {};
    for (int whole = 0; whole < 2; ++whole) {
        if (whole) setenv("OSCAIRN_PCM_DECODE_WHOLE", "1", 1);
        Inst a;
        unsetenv("OSCAIRN_PCM_DECODE_WHOLE");
        char msg[256];
        const uint32_t id = a.contexts->prepare(a.p, "s", nullptr, 0, snd.c_str(), testBase(), msg, sizeof msg);
        CHECK(id != CLAP_INVALID_ID);
        oscairn_memory_t m{};
        a.memory->current(a.p, &m);
        bytes[whole] = m.asset_bytes;
    }
    CHECK(bytes[1] >= 150000u * 4);                                   // the stereo tone whole
    CHECK(bytes[0] < 150000u * 4 / 4);                                // streamed: the default sound's head and the tone's
}

// Packs are read at their offsets, never mapped: with a sound loaded and a streamed note playing, no pack file is in
// the process's mappings (a process whose mappings are locked holds only what it read). Linux only (/proc).
TEST(packs_are_not_mapped) {
#ifdef __linux__
    Inst a;
    CHECK(a.load(stateWith(soundOf(sampleRef("stereo"), "key=0-127 root=60"))));
    a.block(128, {on(5, 1, 0, 72)});
    a.run(9600);
    FILE* f = std::fopen("/proc/self/maps", "r");
    CHECK(f != nullptr);
    char line[1024];
    int mapped = 0;
    while (f && std::fgets(line, sizeof line, f)) if (std::strstr(line, ".osp")) { ++mapped; std::printf("  mapped: %s", line); }
    if (f) std::fclose(f);
    CHECK_EQ(mapped, 0);
#endif
}
