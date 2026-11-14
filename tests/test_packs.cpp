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
    CHECK(std::strstr(msg, "another sample") != nullptr);
    g_store.erase(std::string(64, 'f'));
}
