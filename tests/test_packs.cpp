// SPDX-License-Identifier: AGPL-3.0-or-later
// Samples from Oscairn Sample Packs: the test host's packs (made by the samplepack tool from generated tones) and
// what the plugin plays from them.
#include <samplepack.h>

#include <chrono>

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
    return "oscairn-sampler-sound/0\nsample " + ref + "\nelement ampeg_decay1=127 ampeg_decay2=127 ampeg_release=50\nregion " + region + " element=0 sample=0\n";
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
    if (whole) setenv("OSCAIRN_SAMPLER_DECODE_WHOLE", "1", 1);
    std::vector<float> out;
    {
        Inst a;
        unsetenv("OSCAIRN_SAMPLER_DECODE_WHOLE");
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
                const std::string snd = "oscairn-sampler-sound/0\nsample " + sampleRef(tone) + "\nelement ampeg_decay1=127 ampeg_decay2=127 ampeg_release=60\n"
                                        "region key=0-127 root=60 element=0 sample=0" + loop + "\n";
                const auto w = render(snd, true, key, 96000), s = render(snd, false, key, 96000);
                if (w.empty() || w != s) { std::printf("  %s%s key %d differs\n", tone, loop, key); ++g_failures; }
                CHECK(peak(s) > 0.01f);
            }
}

// The comparison above compares two different things: decoded whole holds the tone in memory, streamed only its head.
TEST(streaming_holds_only_heads) {
    const std::string snd = "oscairn-sampler-sound/0\nsample " + sampleRef("stereo") + "\nelement\nregion element=0 sample=0\n";
    uint64_t bytes[2] = {};
    for (int whole = 0; whole < 2; ++whole) {
        if (whole) setenv("OSCAIRN_SAMPLER_DECODE_WHOLE", "1", 1);
        Inst a;
        unsetenv("OSCAIRN_SAMPLER_DECODE_WHOLE");
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

namespace {
// Tone k of a set: a short sine at its own pitch, mono 44.1 kHz, 3,000 frames (its record's window holds it whole).
TestTone shortTone(int k) {
    TestTone t; t.frames = 3000; t.pcm.resize(t.frames);
    for (uint32_t i = 0; i < t.frames; ++i) t.pcm[i] = int16_t(std::lround(9000.0 * std::sin(2 * 3.14159265358979 * (100.0 + 3.0 * k) * i / t.rate)));
    uint8_t id[SP_SAMPLE_ID_BYTES];
    sp_sample_id_pcm(t.rate, 1, t.pcm.data(), t.frames, id);
    for (int i = 0; i < 32; ++i) { char h[3]; std::snprintf(h, 3, "%02x", id[i]); t.id += h; }
    return t;
}
// Packs `tones` in that order into `file` of the asset folder (a new file put in place of any there) and points the host's
// store at its records; the records' headers, by sample id.
std::map<std::string, sp_record_info> packInto(const std::string& file, const std::vector<TestTone>& tones) {
    const std::string path = g_assetDir + "/" + file;
    std::string cmd = std::string("\"") + SAMPLEPACK_CLI + "\" pack -o \"" + path + ".new\"";
    for (size_t i = 0; i < tones.size(); ++i) {
        const std::string wav = g_assetDir + "/t" + std::to_string(i) + ".wav";
        writeWav(wav, tones[i]);
        cmd += " \"" + wav + "\"";
    }
    std::map<std::string, sp_record_info> out;
    if (std::system((cmd + " > /dev/null").c_str()) || std::rename((path + ".new").c_str(), path.c_str())) return out;
    for (size_t i = 0; i < tones.size(); ++i) std::remove((g_assetDir + "/t" + std::to_string(i) + ".wav").c_str());
    std::string bytes;
    if (FILE* f = std::fopen(path.c_str(), "rb")) { char b[65536]; size_t n; while ((n = std::fread(b, 1, sizeof b, f)) > 0) bytes.append(b, n); std::fclose(f); }
    std::vector<sp_index_entry> e(tones.size());
    size_t n = 0;
    const auto* data = reinterpret_cast<const uint8_t*>(bytes.data());
    if (sp_pack_index(data, bytes.size(), e.data(), e.size(), &n)) return out;
    for (size_t i = 0; i < n; ++i) {
        std::string hex;
        for (int k = 0; k < 32; ++k) { char h[3]; std::snprintf(h, 3, "%02x", e[i].id[k]); hex += h; }
        g_store[hex] = {path, e[i].offset, e[i].length, e[i].preload};
        sp_pack_record(data, bytes.size(), &e[i], &out[hex]);
    }
    return out;
}
// A sound playing each of `tones` on its own key and velocity band.
std::string soundOfAll(const std::vector<TestTone>& tones) {
    std::string s = "oscairn-sampler-sound/0\n";
    for (const auto& t : tones) s += "sample sample:" + t.id + "\n";
    s += "element ampeg_decay1=127 ampeg_decay2=127 ampeg_release=50\n";
    for (size_t i = 0; i < tones.size(); ++i) {
        const int key = 10 + int(i % 100), vel = int(i / 100) * 31 + 1;
        s += "region key=" + std::to_string(key) + "-" + std::to_string(key) + " vel=" + std::to_string(vel) + "-" + std::to_string(vel + 30) +
             " root=" + std::to_string(key) + " element=0 sample=" + std::to_string(i) + "\n";
    }
    return s;
}
struct Cost { uint64_t bytes; int resolves; double ms; };
// What preparing `sound` in instance `a` reads from packs and asks of the host.
Cost prepareCost(Inst& a, const std::string& sound) {
    char msg[256] = {};
    const uint64_t b = a.streaming->bytesRead(a.p);
    const int r = g_sampleResolves;
    const auto t0 = std::chrono::steady_clock::now();
    const uint32_t id = a.contexts->prepare(a.p, "x", nullptr, 0, sound.c_str(), testBase(), msg, sizeof msg);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (id == CLAP_INVALID_ID) { std::printf("  prepare: %s\n", msg); ++g_failures; }
    return {a.streaming->bytesRead(a.p) - b, g_sampleResolves - r, ms};
}
}  // namespace

// A part of 400 samples: prepared the first time, each record's header and frame table are read once and its window's
// frames once, with one resolve a sample; prepared again while a sound holds those records, nothing is read and the
// host is not asked.
TEST(prepare_reads_each_record_once) {
    std::vector<TestTone> tones;
    for (int k = 0; k < 400; ++k) tones.push_back(shortTone(k));
    const auto records = packInto("many.osp", tones);
    CHECK_EQ(records.size(), tones.size());
    uint64_t heads = 0, windows = 0;
    for (const auto& [id, r] : records) { heads += r.head_len; windows += uint64_t(r.frames) * r.channels * 2; CHECK(r.window_frames == r.frames); }
    const std::string sound = soundOfAll(tones);
    Inst a, b;
    const Cost first = prepareCost(a, sound);
    CHECK_EQ(first.resolves, 400);
    CHECK(first.bytes >= heads + windows && first.bytes - (heads + windows) <= 3 * 4096 + 32 + 400 * 56);  // and the pack's opening: header, trailer, footer
    Cost again = prepareCost(b, sound);
    for (int k = 0; k < 4; ++k) { const Cost c = prepareCost(b, sound); again.ms = std::min(again.ms, c.ms); CHECK_EQ(c.bytes, 0u); }
    CHECK_EQ(again.resolves, 0);
    CHECK_EQ(again.bytes, 0u);
    std::printf("  400 samples: first prepare %llu bytes, %d resolves, %.2f ms; again %llu bytes, %d resolves, %.2f ms\n",
                (unsigned long long)first.bytes, first.resolves, first.ms, (unsigned long long)again.bytes, again.resolves, again.ms);
    for (const auto& t : tones) g_store.erase(t.id);
}

// A pack put in place of another at the same path, with its records at other offsets: a sample found where the old
// pack had it is resolved again, and plays its own audio.
TEST(replaced_pack_is_resolved_again) {
    const TestTone x = shortTone(500), y = shortTone(501), z = shortTone(502);
    CHECK_EQ(packInto("moving.osp", {x, y}).size(), 2u);
    const std::string sx = soundOfAll({x});
    int asked = 0;
    auto hear = [&] {  // x's note, from a sound loaded in a new instance; resolves into `asked`
        Inst d;
        const int r = g_sampleResolves;
        CHECK(d.load(stateWith(sx)));
        asked = g_sampleResolves - r;
        d.block(128, {on(0, 1, 0, 10, 0.1)});
        d.run(2560);
        return d.L;
    };
    Inst a;  // holds x's record where the first pack has it
    CHECK_EQ(prepareCost(a, sx).resolves, 1);
    const std::vector<float> before = hear();
    CHECK_EQ(asked, 0);
    CHECK(peak(before) > 0.05f);
    CHECK_EQ(packInto("moving.osp", {y, x, z}).size(), 3u);  // x moves; y is where x was
    Inst b;
    CHECK_EQ(prepareCost(b, soundOfAll({z})).resolves, 1);  // z lies past the first pack's end: the pack is opened again
    CHECK(hear() == before);
    CHECK_EQ(asked, 1);
    for (const auto* t : {&x, &y, &z}) g_store.erase(t->id);
}

// What the process keeps for a sound's records is what the sound holds: a part's first prepare adds to the heap its
// decoded audio, frame tables and pack index (the asset bytes it reports) and the process's buffer and table entries
// (about 520 bytes a record), and nothing more for the records than a second instance preparing the same sound.
TEST(records_cost_no_heap_past_the_sounds) {
    if (heapInUse() < 0) return;  // no count in sanitizer builds
    std::vector<TestTone> tones;
    for (int k = 0; k < 400; ++k) tones.push_back(shortTone(4000 + k));
    CHECK_EQ(packInto("held.osp", tones).size(), tones.size());
    const std::string sound = soundOfAll(tones);
    Inst a, b;
    char msg[256] = {};
    auto grows = [&](Inst& i) {
        const long h = heapInUse();
        CHECK(i.contexts->prepare(i.p, "x", nullptr, 0, sound.c_str(), testBase(), msg, sizeof msg) != CLAP_INVALID_ID);
        return heapInUse() - h;
    };
    const oscairn_memory_t m0 = [&] { oscairn_memory_t m{}; a.memory->current(a.p, &m); return m; }();
    const long first = grows(a), second = grows(b);
    oscairn_memory_t m1{};
    a.memory->current(a.p, &m1);
    const long assets = long(m1.asset_bytes - m0.asset_bytes);
    std::printf("  first prepare +%ld heap, a second +%ld, assets %ld: %ld bytes a record past them\n", first, second, assets, (first - second - assets) / 400);
    CHECK(first - second - assets <= 768 * 400);
    for (const auto& t : tones) g_store.erase(t.id);
}
