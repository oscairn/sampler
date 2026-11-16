// SPDX-License-Identifier: AGPL-3.0-or-later
// Hold shared samples once: a part prepared twice, two elements of one sound that play the same waveform at different
// levels, pans and tunings, and the same audio reached through two packs. What a duplicate context costs beyond its
// samples is measured as heap bytes of this process (the allocator's own statistics), against
// what the context declares through oscairn.memory.
#include <samplepack.h>

#include <cstdlib>
#if defined(__APPLE__)
#include <malloc/malloc.h>
#elif defined(__GLIBC__)
#include <malloc.h>
#endif

#include "host.h"

namespace {
// Bytes the process's heap holds in use now (large blocks included); -1 where the allocator gives no count.
long heapInUse() {
#if defined(__APPLE__)
    malloc_statistics_t s{};
    malloc_zone_statistics(nullptr, &s);
    return long(s.size_in_use);
#elif defined(__GLIBC__)
    const struct mallinfo2 m = mallinfo2();
    return long(m.uordblks + m.hblkhd);
#else
    return -1;
#endif
}
// Instances made while this lives decode every sample whole into memory (so a sample's size dominates the heap counts).
struct DecodeWhole {
    DecodeWhole() { setenv("OSCAIRN_PCM_DECODE_WHOLE", "1", 1); }
    ~DecodeWhole() { unsetenv("OSCAIRN_PCM_DECODE_WHOLE"); }
};
std::string soundOf(const char* ref) {
    return std::string("oscairn-pcm-sound/0\nsample ") + ref +
           "\nelement ampeg_decay1=127 ampeg_decay2=127 ampeg_release=50\nregion key=0-127 root=60 element=0 sample=0\n";
}
uint32_t prepare(Inst& a, const std::string& sound) {
    char msg[200] = {};
    const uint32_t id = a.contexts->prepare(a.p, "part", nullptr, 0, patchWith("level=1", sound.c_str()).c_str(), testBase(), msg, sizeof msg);
    if (id == CLAP_INVALID_ID) std::printf("  prepare: %s\n", msg);
    return id;
}
oscairn_memory_t memoryOf(const Inst& a) {
    oscairn_memory_t m{};
    CHECK(a.memory->current(a.p, &m));
    return m;
}
// A waveform file of one key bank over every key and the tone `tone`, installed as <id>.owf in the test library.
std::string writeWaveform(const char* tone, uint8_t loop, uint32_t loopEnd) {
    uint8_t id[32];
    for (int k = 0; k < 32; ++k) id[k] = uint8_t(std::stoi(g_tones.at(tone).id.substr(2 * k, 2), nullptr, 16));
    sp_key_bank b{};
    b.key_lo = 0; b.key_hi = 127; b.vel_lo = 1; b.vel_hi = 127; b.root = 60; b.loop_mode = loop; b.loop_end = loopEnd;
    uint8_t file[256];
    size_t n = 0;
    if (sp_waveform_build(&b, 1, &id, 1, file, sizeof file, &n)) return "";
    uint8_t wid[32];
    sp_waveform_id(file, n, wid);
    std::string hex;
    for (int k = 0; k < 32; ++k) { char h[3]; std::snprintf(h, 3, "%02x", wid[k]); hex += h; }
    if (FILE* f = std::fopen((g_assetDir + "/" + hex + ".owf").c_str(), "wb")) { std::fwrite(file, 1, n, f); std::fclose(f); }
    return hex;
}
// The notes of the rendering below on channel 0, each released: on a key both elements reach, one only A reaches, and
// the edges of B's range. Left and right, one after the other.
std::vector<float> render(const std::string& sound) {
    Inst a;
    if (!a.load(stateWith(sound))) return {};
    int32_t id = 1;
    for (int key : {60, 90, 30, 40, 80}) {
        a.block(128, {on(3, id, 0, int16_t(key), 0.8)});
        a.run(2048);
        a.block(128, {off(5, id++, 0, int16_t(key))});
        a.run(1024);
    }
    a.run(8192);
    CHECK_EQ(a.streaming->sheds(a.p), 0u);
    std::vector<float> out = a.L;
    out.insert(out.end(), a.R.begin(), a.R.end());
    return out;
}
double worstDiff(const std::vector<float>& x, const std::vector<float>& y) {
    if (x.size() != y.size()) return 1e9;
    double w = 0;
    for (size_t i = 0; i < x.size(); ++i) w = std::max(w, double(std::fabs(x[i] - y[i])));
    return w;
}
}  // namespace

// A part prepared again with the same sound (two contexts, one sound): the samples are held once in the process, and
// oscairn.memory reports them once, as its asset bytes. What the duplicate adds to the heap is its own state: a context
// whose sample nothing holds yet costs the sample's whole size more. A second instance loading the same sound shares
// the samples too.
TEST(duplicated_part_holds_its_samples_once) {
    constexpr long kStereo = 150000L * 2 * 2, kTrim = 135001L * 2 * 2, kMono = 96000L * 2;  // decoded bytes of three tones
    DecodeWhole whole;
    Inst a;
    CHECK(a.load(stateWith(soundOf("stereo.osp"))));
    const oscairn_memory_t one = memoryOf(a);
    CHECK(one.asset_bytes >= uint64_t(kStereo) && one.asset_bytes < uint64_t(kStereo) + 64 * 1024);
    long h = heapInUse();
    const uint32_t dup = prepare(a, soundOf("stereo.osp"));
    CHECK(dup != CLAP_INVALID_ID);
    const long duplicate = heapInUse() - h;
    const oscairn_memory_t two = memoryOf(a);
    CHECK_EQ(two.asset_bytes, one.asset_bytes);  // once, not twice
    CHECK_EQ(two.context_bytes, one.context_bytes);
    CHECK_EQ(two.shared_bytes, one.shared_bytes);
    // control: the same state around a tone nothing holds yet
    h = heapInUse();
    CHECK(prepare(a, soundOf("stereo-trim.osp")) != CLAP_INVALID_ID);
    const long fresh = heapInUse() - h;
    CHECK(memoryOf(a).asset_bytes >= two.asset_bytes + uint64_t(kTrim));
    if (h >= 0) {
        std::printf("  a duplicate context adds %ld heap bytes; a context with a new %ld byte sample adds %ld\n", duplicate, kTrim, fresh);
        CHECK(fresh - duplicate >= kTrim * 9 / 10);
    }
    // two instances of the process loading one sound: the second adds its state, not the samples
    Inst p, q;
    h = heapInUse();
    CHECK(p.load(stateWith(soundOf("mono.osp"))));
    const long first = heapInUse() - h;
    h = heapInUse();
    CHECK(q.load(stateWith(soundOf("mono.osp"))));
    const long second = heapInUse() - h;
    CHECK_EQ(memoryOf(q).asset_bytes, memoryOf(p).asset_bytes);  // each instance counts what its sounds use
    if (h >= 0) {
        std::printf("  first instance loading a %ld byte sample: +%ld, second: +%ld\n", kMono, first, second);
        CHECK(first - second >= kMono * 9 / 10);
    }
    // both contexts of the stereo sound sound at once, one voice each
    CHECK(a.contexts->select(a.p, dup, 1));
    a.block(128, {on(0, 1, 0, 60), on(0, 2, 1, 60)});
    a.run(4096);
    CHECK_EQ(a.voices(), 2u);
}

// Two elements of one sound play the same waveform at their own level, pan, tuning and key range: the sound plays what
// each element plays alone, summed (to float rounding), each on its side; the waveform's sample is held once, as in the
// sound of one element.
TEST(elements_of_one_waveform_play_independently_and_share_it) {
    const std::string wf = writeWaveform("sine", 1, 44100);
    CHECK(!wf.empty());
    const std::string env = "ampeg_decay1=127 ampeg_decay2=127 ampeg_release=5";
    const std::string head = "oscairn-pcm-sound/1\nwaveform " + wf + "\n";
    // A: full level, hard left; B: lower, hard right, a fifth up, keys 40 to 80 only
    const std::string a = "element " + env + " waveform=1 amp_level=127 amp_pan=0\n";
    const std::string b = "element " + env + " waveform=1 amp_level=64 amp_pan=127 coarse=7 key_lo=40 key_hi=80\n";
    const std::vector<float> both = render(head + a + b), onlyA = render(head + a), onlyB = render(head + b);
    CHECK(!both.empty() && both.size() == onlyA.size() && both.size() == onlyB.size());
    const size_t n = both.size() / 2;
    auto channel = [&](const std::vector<float>& v, int c) { return std::vector<float>(v.begin() + long(c * n), v.begin() + long((c + 1) * n)); };
    for (int c = 0; c < 2; ++c) {
        std::vector<float> sum(n);
        const auto x = channel(onlyA, c), y = channel(onlyB, c);
        for (size_t i = 0; i < n; ++i) sum[i] = x[i] + y[i];
        const double w = worstDiff(channel(both, c), sum);
        if (w > 1e-5) std::printf("  channel %d: the sound differs from the sum of its elements by %g\n", c, w);
        CHECK(w <= 1e-5);
    }
    // each element on its own side, A louder than B, both sounding
    CHECK(peak(channel(onlyA, 0)) > 0.05f && peak(channel(onlyA, 1)) < 1e-3f * peak(channel(onlyA, 0)));
    CHECK(peak(channel(onlyB, 1)) > 0.02f && peak(channel(onlyB, 0)) < 1e-3f * peak(channel(onlyB, 1)));
    CHECK(peak(channel(onlyA, 0)) > peak(channel(onlyB, 1)));
    // held once: the sound of two elements holds what the sound of one holds
    DecodeWhole whole;
    const std::string wfStereo = writeWaveform("stereo", 0, 0);
    CHECK(!wfStereo.empty());
    const std::string headS = "oscairn-pcm-sound/1\nwaveform " + wfStereo + "\n";
    Inst x, y;
    const long h0 = heapInUse();
    CHECK(x.load(stateWith(headS + a)));
    const long h1 = heapInUse();
    CHECK(y.load(stateWith(headS + a + b)));
    const long h2 = heapInUse();
    CHECK_EQ(memoryOf(y).asset_bytes, memoryOf(x).asset_bytes);
    CHECK(memoryOf(x).asset_bytes >= 150000u * 2 * 2);
    if (h0 >= 0) {
        std::printf("  one element: heap +%ld, two elements of the waveform: +%ld\n", h1 - h0, h2 - h1);
        CHECK((h1 - h0) - (h2 - h1) >= 150000L * 2 * 2 * 9 / 10);  // the second sound's samples were held already
    }
}

// What one more context of an already-held sound costs on the heap (its sound's parsed tables, compiled form and
// state; the samples are shared) stays within the context_bytes the plugin declares, for a plain sound and for one
// whose waveforms are expanded into a second table. The host charges each context after the first at that figure.
TEST(declared_context_bytes_cover_a_duplicate_context) {
    const std::string wf = writeWaveform("stereo", 0, 0);
    CHECK(!wf.empty());
    const std::string expanded = "oscairn-pcm-sound/1\nwaveform " + wf +
                                 "\nelement ampeg_decay1=127 ampeg_decay2=127 ampeg_release=5 waveform=1 amp_level=127 amp_pan=0\n";
    for (const std::string& sound : {soundOf("stereo.osp"), expanded}) {
        {  // first uses in the process (decoder buffers, pools) are not a context's: they are shared bytes
            Inst warm;
            CHECK(warm.load(stateWith(sound)));
            CHECK(prepare(warm, sound) != CLAP_INVALID_ID);
        }
        Inst a;
        CHECK(a.load(stateWith(sound)));
        const uint64_t declared = memoryOf(a).context_bytes;
        const long h = heapInUse();
        CHECK(prepare(a, sound) != CLAP_INVALID_ID);
        const long cost = heapInUse() - h;
        if (h >= 0) {
            std::printf("  a duplicate context of %s sound: declared %llu, measured %ld\n", sound == expanded ? "an expanded" : "a plain",
                        (unsigned long long)declared, cost);
            CHECK(cost > 0);
            CHECK(declared >= uint64_t(cost));
        }
    }
}
