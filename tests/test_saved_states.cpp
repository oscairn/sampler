// SPDX-License-Identifier: AGPL-3.0-or-later
// Saved test states: tests/states/<version>/ holds what each published version saved (see tests/states/README.md). The
// current build must open every one, read back its parameters, and play it as it plays the newest format's.
#include <filesystem>
#include <fstream>

#include <samplepack.h>

#include "host.h"

namespace {
// What each file was saved with: parameter id, value (every other parameter at its default).
struct Saved { const char* file; std::vector<std::pair<clap_id, double>> values; };
const Saved kSaved[] = {
    {"default.state", {{0, 1}, {1, 0}, {2, 64}}},
    {"params-1.state", {{0, 0.6}, {1, -0.4}, {2, 12}, {3, 0.25}, {17, -300}, {18, 20}}},
    {"params-2.state", {{0, 0.9}, {1, 0.5}, {2, 5}, {11, 0.7}, {12, 1}, {14, 100}, {23, 10}}},
};
std::string slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}
}  // namespace

TEST(saved_states_of_every_version_open) {
    namespace fs = std::filesystem;
    const std::string root = std::string(PCM_SOURCE_DIR) + "/tests/states";
    // The init library's sound names its waveform and, through it, its sample by ids. The repository holds the waveform
    // (init.owf) and no sample data: the sample is the tone tools/release/init_library.py generates, packed here.
    const std::string wfId = "775c23eb62640448a6035aae67741652d1c64ef9ec94be803baad3910afa7bcc";
    const std::string sampleId = "2a0fc182eca961490c876b186bee5ad8da7434e3d08d94a93804b7ae2bd3e1b2";
    { std::ofstream f(g_assetDir + "/" + wfId + ".owf", std::ios::binary); const std::string w = slurp(root + "/init.owf"); f.write(w.data(), std::streamsize(w.size())); }
    const std::string wav = g_assetDir + "/init-tone.wav", osp = g_assetDir + "/init-tone.osp";
    const std::string gen = std::string("python3 -I -c \"import sys, struct, wave; sys.path.insert(0, sys.argv[1]); import init_library as i; "
        "w = wave.open(sys.argv[2], 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(i.RATE); "
        "w.writeframes(struct.pack('<%dh' % i.FRAMES, *i.tone()))\" \"") + PCM_SOURCE_DIR + "/tools/release\" \"" + wav + "\"";
    CHECK_EQ(std::system(gen.c_str()), 0);
    CHECK_EQ(std::system((std::string("\"") + SAMPLEPACK_CLI + "\" pack -o \"" + osp + "\" \"" + wav + "\" > /dev/null").c_str()), 0);
    const std::string pack = slurp(osp);
    sp_index_entry entry[1]; size_t entries = 0;
    CHECK_EQ(sp_pack_index(reinterpret_cast<const uint8_t*>(pack.data()), pack.size(), entry, 1, &entries), 0);
    std::string hex;
    for (int k = 0; k < 32 && entries; ++k) { char h[3]; std::snprintf(h, 3, "%02x", entry[0].id[k]); hex += h; }
    CHECK(hex == sampleId);
    g_store[sampleId] = {osp, entry[0].offset, entry[0].length, entry[0].preload};

    auto play = [](Inst& x) { x.block(128, {on(3, 1, 0, 60, 0.8)}); x.run(4096); return x.L; };
    auto open = [](Inst& x, const std::string& data) { Stream s; s.data = data; return x.contexts->state->load(x.p, &s.in); };
    int versions = 0;
    for (const auto& dir : fs::directory_iterator(root)) {
        if (!dir.is_directory()) continue;
        ++versions;
        for (const Saved& s : kSaved) {
            const std::string name = dir.path().filename().string() + "/" + s.file, data = slurp(dir.path().string() + "/" + s.file);
            const std::string newest = slurp(root + "/0.9.17/" + s.file);
            if (data.empty() || newest.empty()) { std::printf("  %s: missing\n", name.c_str()); ++g_failures; continue; }
            Inst a, b, c;
            if (!open(a, data)) { std::printf("  %s: does not open\n", name.c_str()); ++g_failures; continue; }
            for (const auto& [id, v] : s.values)
                if (std::fabs(a.value(id) - v) > 1e-6) { std::printf("  %s: parameter %u reads %g, saved %g\n", name.c_str(), unsigned(id), a.value(id), v); ++g_failures; }
            // It plays as the same sound and values saved by the newest published format do, and as it does when
            // saved again by this build.
            Stream again; CHECK(a.contexts->state->save(a.p, &again.out));
            CHECK(again.data.rfind("oscairn-sampler-contexts/4\n", 0) == 0 && open(b, again.data) && open(c, newest));
            const auto ra = play(a), rb = play(b), rc = play(c);
            double diff = 0, loud = 0;
            for (size_t i = 0; i < ra.size(); ++i) { diff = std::max({diff, double(std::fabs(ra[i] - rb[i])), double(std::fabs(ra[i] - rc[i]))}); loud = std::max(loud, double(std::fabs(ra[i]))); }
            if (!(diff < 1e-6 && loud > 0.001)) { std::printf("  %s: renders differ by %g (peak %g)\n", name.c_str(), diff, loud); ++g_failures; }
        }
    }
    CHECK(versions >= 12);
}
