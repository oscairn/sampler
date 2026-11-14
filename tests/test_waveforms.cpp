// SPDX-License-Identifier: AGPL-3.0-or-later
// Sound format v1: elements that play a waveform (an Oscairn Waveform file the host resolves), expanded into regions
// when the sound is prepared. A v1 sound plays exactly as the v0 sound with the equivalent regions written out.
#include <samplepack.h>

#include <algorithm>
#include <array>
#include <cstring>

#include "host.h"

namespace {
std::string hexOf(const uint8_t* id) {
    char h[65];
    for (int i = 0; i < 32; ++i) std::snprintf(h + 2 * i, 3, "%02x", id[i]);
    return h;
}
// Writes the waveform of `banks` over the tones `tones` (a bank's sample indexes them) as the host installs it
// (<id>.owf in the test library); its id, or "" when samplepack refuses it.
std::string writeWaveform(const std::vector<sp_key_bank>& banks, const std::vector<const char*>& tones) {
    std::vector<std::array<uint8_t, 32>> ids(tones.size());
    for (size_t i = 0; i < tones.size(); ++i)
        for (int k = 0; k < 32; ++k) ids[i][k] = uint8_t(std::stoi(g_tones.at(tones[i]).id.substr(2 * k, 2), nullptr, 16));
    std::vector<uint8_t> file(20 + 72 * banks.size());
    size_t n = 0;
    if (sp_waveform_build(banks.data(), banks.size(), reinterpret_cast<const uint8_t(*)[32]>(ids.data()), ids.size(), file.data(), file.size(), &n)) return "";
    uint8_t id[32];
    sp_waveform_id(file.data(), n, id);
    const std::string hex = hexOf(id);
    FILE* f = std::fopen((g_assetDir + "/" + hex + ".owf").c_str(), "wb");
    std::fwrite(file.data(), 1, n, f);
    std::fclose(f);
    return hex;
}
sp_key_bank bank(uint8_t lo, uint8_t hi, uint8_t vlo, uint8_t vhi, uint8_t root, uint16_t sample, int8_t transpose = 0, int32_t tune = 0,
                 int32_t level = 0, int16_t pan = 0, uint8_t loop = 0, uint32_t loopStart = 0, uint32_t loopEnd = 0) {
    sp_key_bank b{};
    b.key_lo = lo; b.key_hi = hi; b.vel_lo = vlo; b.vel_hi = vhi; b.root = root; b.transpose = transpose; b.loop_mode = loop;
    b.sample = sample; b.tune = tune; b.level = level; b.pan = pan; b.loop_start = loopStart; b.loop_end = loopEnd;
    return b;
}
// Plays the same notes, released, on a sound; both channels, or empty when it does not load.
std::vector<float> notes(const std::string& sound) {
    Inst a;
    if (!a.load(stateWith(sound))) return {};
    int32_t id = 1;
    for (int key : {5, 30, 44, 45, 50, 52, 53, 62, 68, 69, 76, 77, 90, 91, 120})
        for (int vel : {20, 45, 63, 64, 100, 110}) {
            a.block(128, {on(3, id, 0, int16_t(key), vel / 127.0)});
            a.run(1024);
            a.block(128, {off(5, id++, 0, int16_t(key))});
            a.run(512);
        }
    a.run(48000);
    CHECK_EQ(a.streaming->sheds(a.p), 0u);
    std::vector<float> out = a.L;
    out.insert(out.end(), a.R.begin(), a.R.end());
    return out;
}
const char* kEnv = "ampeg_decay1=127 ampeg_decay2=127 ampeg_release=5";
}  // namespace

// Several key banks (the lowest two and the highest two reach the ends of the keyboard), velocity layers, a left/right
// pair over the same keys, coarse tune up and down, fine tune (truncated toward zero), element limits that cut banks
// and one that leaves a single bank: the same audio as the regions written out by hand.
TEST(waveform_sound_equals_explicit_regions) {
    const std::vector<sp_key_bank> banks = {
        bank(10, 47, 1, 63, 40, 0), bank(10, 47, 64, 127, 40, 1, 2, 0, -600), bank(48, 71, 1, 127, 60, 2, 0, 1250, 0, 0, 1, 0, 44100),
        bank(72, 100, 1, 127, 84, 0, 0, 0, 0, -10000), bank(72, 100, 1, 127, 84, 1, 0, 0, 0, 10000)};
    const std::string wf = writeWaveform(banks, {"mono", "stereo", "sine"});
    CHECK(!wf.empty());
    const std::string v1 = "oscairn-pcm-sound/1\nwaveform " + wf + "\n"
        "element " + kEnv + " waveform=1 coarse=3 fine=-20\n"
        "element " + kEnv + " waveform=1 coarse=-5 fine=33 key_lo=50 key_hi=90 vel_lo=40 vel_hi=100\n"
        "element " + kEnv + " waveform=1 key_lo=60 key_hi=65 vel_hi=50\n";
    // the left/right pair sorts by its samples' ids
    const bool monoFirst = g_tones.at("mono").id < g_tones.at("stereo").id;
    auto pair = [&](const std::string& common) {
        const std::string l = "region " + common + " pan=-100 sample=0\n", r = "region " + common + " pan=100 sample=1\n";
        return monoFirst ? l + r : r + l;
    };
    const std::string v0 = "oscairn-pcm-sound/0\nsample " + sampleRef("mono") + "\nsample " + sampleRef("stereo") + "\nsample " + sampleRef("sine") + "\n"
        "element " + kEnv + "\nelement " + kEnv + "\nelement " + kEnv + "\n"
        // element 0: coarse +3 moves the key map down 3, transposes up 3; fine -20 is -31.25 cents: -31
        "region element=0 key=0-44 vel=1-63 root=40 transpose=3 tune=-31 sample=0\n"
        "region element=0 key=0-44 vel=64-127 root=40 transpose=5 tune=-31 level=-6 sample=1\n"
        "region element=0 key=45-68 root=60 transpose=3 tune=-18.5 loop=0-44100 sample=2\n" +
        pair("element=0 key=69-127 root=84 transpose=3 tune=-31") +
        // element 1: coarse -5, fine +33 is 51.5625 cents: 51; keys 50-90, velocities 40-100
        "region element=1 key=50-52 vel=40-63 root=40 transpose=-5 tune=51 sample=0\n"
        "region element=1 key=50-52 vel=64-100 root=40 transpose=-3 tune=51 level=-6 sample=1\n"
        "region element=1 key=53-76 vel=40-100 root=60 transpose=-5 tune=63.5 loop=0-44100 sample=2\n" +
        pair("element=1 key=77-90 vel=40-100 root=84 transpose=-5 tune=51") +
        // element 2: only the middle bank meets its limits
        "region element=2 key=60-65 vel=1-50 root=60 tune=12.5 loop=0-44100 sample=2\n";
    const auto a = notes(v1), b = notes(v0);
    CHECK(!a.empty() && a == b);
    CHECK(peak(a) > 0.05f);
    std::remove((g_assetDir + "/" + wf + ".owf").c_str());
}

// A waveform the host cannot resolve, a file holding another waveform and a damaged file each fail the sound with a
// reason; a waveform's samples must resolve too.
TEST(waveform_refusals) {
    const std::string wf = writeWaveform({bank(0, 127, 1, 127, 60, 0)}, {"sine"});
    Inst a;
    char msg[256] = {};
    auto prepare = [&](const std::string& id) {
        const std::string snd = "oscairn-pcm-sound/1\nwaveform " + id + "\nelement waveform=1\n";
        return a.contexts->prepare(a.p, "w", nullptr, 0, snd.c_str(), testBase(), msg, sizeof msg);
    };
    CHECK(prepare(wf) != CLAP_INVALID_ID);
    const std::string other(64, 'a');
    CHECK_EQ(prepare(other), CLAP_INVALID_ID);
    CHECK(std::strstr(msg, "cannot resolve") != nullptr);
    std::vector<char> file(100000);
    FILE* f = std::fopen((g_assetDir + "/" + wf + ".owf").c_str(), "rb");
    file.resize(std::fread(file.data(), 1, file.size(), f));
    std::fclose(f);
    f = std::fopen((g_assetDir + "/" + other + ".owf").c_str(), "wb");
    std::fwrite(file.data(), 1, file.size(), f);
    std::fclose(f);
    CHECK_EQ(prepare(other), CLAP_INVALID_ID);
    CHECK(std::strstr(msg, "another waveform") != nullptr);
    file[file.size() - 1] ^= 1;
    f = std::fopen((g_assetDir + "/" + other + ".owf").c_str(), "wb");
    std::fwrite(file.data(), 1, file.size(), f);
    std::fclose(f);
    CHECK_EQ(prepare(other), CLAP_INVALID_ID);
    CHECK(std::strstr(msg, "not a valid waveform") != nullptr);
    std::remove((g_assetDir + "/" + other + ".owf").c_str());
    // a sample the host does not hold
    sp_key_bank b = bank(0, 127, 1, 127, 60, 0);
    std::vector<uint8_t> out(200);
    const uint8_t missing[1][32] = {{1, 2, 3}};
    size_t n = 0;
    CHECK_EQ(sp_waveform_build(&b, 1, missing, 1, out.data(), out.size(), &n), SP_OK);
    uint8_t id[32];
    sp_waveform_id(out.data(), n, id);
    f = std::fopen((g_assetDir + "/" + hexOf(id) + ".owf").c_str(), "wb");
    std::fwrite(out.data(), 1, n, f);
    std::fclose(f);
    CHECK_EQ(prepare(hexOf(id)), CLAP_INVALID_ID);
    CHECK(std::strstr(msg, "no sample") != nullptr);
    std::remove((g_assetDir + "/" + hexOf(id) + ".owf").c_str());
    std::remove((g_assetDir + "/" + wf + ".owf").c_str());
}
