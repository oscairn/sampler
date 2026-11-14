// SPDX-License-Identifier: AGPL-3.0-or-later
// Runs the tests against a built plugin: pcm_tests <plugin.clap> [name filter]. The tests reach the
// plugin only through the CLAP ABI. Their samples are generated tones, packed by the samplepack tool
// into a temporary folder: a "test" asset library holding one-record packs (sine.osp: a looped 220 Hz
// sine, 16-bit 44.1 kHz) and a store pack holding every tone, which the test host's resolve_sample
// answers from.
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <samplepack.h>
#include "check.h"
#include "host.h"

std::vector<TestCase>& testCases() { static std::vector<TestCase> v; return v; }
int g_failures = 0;
const char* g_pluginPath = nullptr;
std::string g_assetDir;
std::map<std::string, StoreEntry> g_store;
std::map<std::string, TestTone> g_tones;

static void writeWav(const std::string& path, const TestTone& t) {
    FILE* f = std::fopen(path.c_str(), "wb");
    const uint32_t bytes = uint32_t(t.pcm.size() * 2), ch = t.channels;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f); u32(16); u16(1); u16(uint16_t(ch)); u32(t.rate); u32(t.rate * 2 * ch); u16(uint16_t(2 * ch)); u16(16);
    std::fwrite("data", 1, 4, f); u32(bytes); std::fwrite(t.pcm.data(), 2, t.pcm.size(), f);
    std::fclose(f);
}
// The tones: a sine, and longer partials with a little noise (so the pack's encoder predicts, as with real audio).
static void makeTones() {
    auto tone = [](const char* name, uint32_t frames, uint32_t ch, uint32_t rate, double hz, double noise) {
        TestTone t; t.channels = ch; t.rate = rate; t.frames = frames; t.pcm.resize(size_t(frames) * ch);
        uint32_t r = 2463534242u;
        for (uint32_t i = 0; i < frames; ++i)
            for (uint32_t c = 0; c < ch; ++c) {
                r ^= r << 13; r ^= r >> 17; r ^= r << 5;
                const double x = 2 * 3.14159265358979 * hz * (1 + 0.003 * c) * i / rate;
                const double v = std::sin(x) + 0.3 * std::sin(3 * x + c) + 0.1 * std::sin(7.1 * x) + noise * (double(r) / 4294967296.0 - 0.5);
                t.pcm[size_t(i) * ch + c] = int16_t(std::lround(noise ? 9000.0 * v : 16384.0 * std::sin(x)));
            }
        g_tones[name] = t;
    };
    tone("sine", 44100, 1, 44100, 220, 0);
    tone("mono", 96000, 1, 44100, 196, 0.2);
    tone("stereo", 150000, 2, 48000, 261.6, 0.2);
    tone("short", 3000, 2, 48000, 440, 0.2);  // fits its pack window whole
    // parts of two tones, for key banks that play frames [start, end) of the whole
    auto trim = [](const char* name, const char* of, uint32_t start, uint32_t end) {
        TestTone t = g_tones.at(of);
        t.pcm.assign(t.pcm.begin() + size_t(start) * t.channels, t.pcm.begin() + size_t(end) * t.channels);
        t.frames = end - start;
        g_tones[name] = t;
    };
    trim("mono-trim", "mono", 1234, 80000);
    trim("stereo-trim", "stereo", 5000, 140001);
}
// Packs the tones with the samplepack tool: <name>.osp each, and store.osp holding them all.
static bool packTones(const std::string& dir) {
    std::string all;
    for (auto& [name, t] : g_tones) {
        const std::string wav = dir + "/" + name + ".wav";
        writeWav(wav, t);
        const std::string cmd = std::string("\"") + SAMPLEPACK_CLI + "\" pack -o \"" + dir + "/" + name + ".osp\" \"" + wav + "\" > /dev/null";
        if (std::system(cmd.c_str())) { std::printf("samplepack failed: %s\n", cmd.c_str()); return false; }
        all += " \"" + wav + "\"";
        uint8_t id[SP_SAMPLE_ID_BYTES];
        if (sp_sample_id_pcm(t.rate, t.channels, t.pcm.data(), t.frames, id)) return false;
        for (int i = 0; i < 32; ++i) { char h[3]; std::snprintf(h, 3, "%02x", id[i]); t.id += h; }
    }
    const std::string store = dir + "/store.osp";
    if (std::system((std::string("\"") + SAMPLEPACK_CLI + "\" pack -o \"" + store + "\"" + all + " > /dev/null").c_str())) return false;
    // the store's index: every sample id at its record's offset
    std::string bytes;
    if (FILE* f = std::fopen(store.c_str(), "rb")) { char b[65536]; size_t n; while ((n = std::fread(b, 1, sizeof b, f)) > 0) bytes.append(b, n); std::fclose(f); }
    sp_index_entry e[16]; size_t n = 0;
    if (sp_pack_index(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), e, 16, &n)) return false;
    for (size_t i = 0; i < n; ++i) {
        std::string hex;
        for (int k = 0; k < 32; ++k) { char h[3]; std::snprintf(h, 3, "%02x", e[i].id[k]); hex += h; }
        g_store[hex] = {store, e[i].offset};
    }
    for (auto& [name, t] : g_tones) std::remove((dir + "/" + name + ".wav").c_str());
    return g_store.size() == g_tones.size();
}

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: pcm_tests <plugin.clap> [filter]\n"); return 2; }
    g_pluginPath = argv[1];
    const char* tmp = std::getenv("TMPDIR");
    std::string dir = std::string(tmp && *tmp ? tmp : "/tmp") + "/pcm-tests-XXXXXX";
    if (!mkdtemp(&dir[0])) { std::printf("cannot make a temporary folder\n"); return 2; }
    g_assetDir = dir;
    makeTones();
    if (!packTones(dir)) { std::printf("cannot make the test packs\n"); return 2; }
    int run = 0;
    for (auto& t : testCases()) {
        if (argc > 2 && !std::strstr(t.name, argv[2])) continue;
        const int before = g_failures;
        t.fn();
        ++run;
        std::printf("%s %s\n", g_failures == before ? "ok  " : "FAIL", t.name);
    }
    std::printf("%d tests, %d failed checks (allocation interposer: %s)\n", run, g_failures, allocGuardName());
    for (auto& [name, t] : g_tones) std::remove((dir + "/" + name + ".osp").c_str());
    std::remove((dir + "/store.osp").c_str());
    rmdir(dir.c_str());
    return g_failures ? 1 : 0;
}
