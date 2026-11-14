// SPDX-License-Identifier: AGPL-3.0-or-later
// Runs the tests against a built plugin: pcm_tests <plugin.clap> [name filter]. The tests reach the
// plugin only through the CLAP ABI; its samples come from a "test" asset library this harness
// writes into a temporary folder (a looped 220 Hz sine, 16-bit 44.1 kHz WAV).
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include "check.h"
#include "host.h"

std::vector<TestCase>& testCases() { static std::vector<TestCase> v; return v; }
int g_failures = 0;
const char* g_pluginPath = nullptr;
std::string g_assetDir;

static void writeSine(const std::string& path) {
    const uint32_t n = 44100, rate = 44100;
    std::vector<int16_t> pcm(n);
    for (uint32_t i = 0; i < n; ++i) pcm[i] = int16_t(std::lround(16384.0 * std::sin(2 * 3.14159265358979 * 220.0 * i / rate)));
    FILE* f = std::fopen(path.c_str(), "wb");
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + n * 2); std::fwrite("WAVEfmt ", 1, 8, f); u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
    std::fwrite("data", 1, 4, f); u32(n * 2); std::fwrite(pcm.data(), 2, n, f);
    std::fclose(f);
}

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: pcm_tests <plugin.clap> [filter]\n"); return 2; }
    g_pluginPath = argv[1];
    const char* tmp = std::getenv("TMPDIR");
    std::string dir = std::string(tmp && *tmp ? tmp : "/tmp") + "/pcm-tests-XXXXXX";
    if (!mkdtemp(&dir[0])) { std::printf("cannot make a temporary folder\n"); return 2; }
    g_assetDir = dir;
    writeSine(dir + "/sine.wav");
    int run = 0;
    for (auto& t : testCases()) {
        if (argc > 2 && !std::strstr(t.name, argv[2])) continue;
        const int before = g_failures;
        t.fn();
        ++run;
        std::printf("%s %s\n", g_failures == before ? "ok  " : "FAIL", t.name);
    }
    std::printf("%d tests, %d failed checks (allocation interposer: %s)\n", run, g_failures, allocGuardName());
    std::remove((dir + "/sine.wav").c_str());
    rmdir(dir.c_str());
    return g_failures ? 1 : 0;
}
