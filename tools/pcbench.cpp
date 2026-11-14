// SPDX-License-Identifier: AGPL-3.0-or-later
// Bench, cost and render driver for the DSP core (core/), 128-frame blocks.
//   pcbench render SOUND STORE RATE SECONDS OUT.f32 INTERP EVENT...   stereo interleaved float32; events
//                         on@t:key:vel(1-127) off@t:key cc@t:64:value (the sustain pedal), on their
//                         exact frame; INTERP sinc | herm;
//                         SOUND a sound text file (format v0 or v1); STORE a folder, searched with its subfolders:
//                         "sample:<id>" is the record of that id in any .osp pack there, "waveform:<id>" the file
//                         <id>.owf, a path reference the one-record pack STORE/reference; every region decoded whole
//   pcbench rawq INTERP SEMITONES IN.s16 OUT.f32   a mono 16-bit 44.1 kHz file played (thru filter,
//                         flat envelope) SEMITONES from its root at 48 kHz, mono float32 (the noise test)
//   pcbench cg CASE PLAN INTERP WARM MEAS TSV      one callgrind dump per measured block (PC_WHOLE=1: one for all;
//                         PC_SOUNDS=N: the sound in N slots, slot k on its own output bus k + 1, notes taken in turn)
//   pcbench time [CPU]    thread-CPU and wall time per block for every case
//   pcbench check         NaN/Inf/subnormal check over random notes (exit 1 on failure)
//   pcbench kernels       prints core/kernels.cpp, the interpolation bank from kernelTap()
//   pcbench coverage SOUND [STORE]   what the sound uses that the core does not cover (a sound with waveforms needs
//                         STORE, as render, to expand them), one line each:
//                         "not covered: element E ID=VALUE: why", "not covered: part ID=VALUE: why" (exit 1 if any)
// CASE: rest | held N | shed N (N held voices shed in one block) | release N (N held voices released,
// their filter envelopes moving the cutoff every control step); PLAN: mono | stereo (the pads'
// element: filter type 0, the pads' envelopes) | mono-thru | stereo-thru | mono-tN | stereo-tN (filter type N
// with the pads' envelopes); any of them with -eq (the element EQ's peak) or -stream (a
// 4,096-frame head in memory, the rest and the loop read from stream rings filled between blocks); 48 kHz;
// built-in samples.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <vector>
#ifdef __linux__
#include <sched.h>
#endif
#if __has_include(<valgrind/callgrind.h>)
#include <valgrind/callgrind.h>
#else
#define CALLGRIND_ZERO_STATS
#define CALLGRIND_DUMP_STATS
#endif
#include "../core/pcore.h"
#include "../core/vsimd.h"
#ifdef PCM_PACKS
#include <filesystem>
#include <map>
#include "../src/loader.h"
#endif
#ifndef PC_FLAGS
#define PC_FLAGS "?"
#endif

using namespace pcm;
constexpr int B = 128;

// The pads' element (probe sound A, a 17-region pad: the values that differ from the defaults).
const char* kPadElement = "element ampeg_attack=30 ampeg_decay2=127 ampeg_release=127 fil_type=0 fil_cutoff=244 fil_resonance=6 "
                          "fileg_decay1=0 fileg_decay2=59 fileg_release=86 fileg_attack_level=128 fileg_release_level=126 "
                          "fileg_depth=52 fileg_veldepth=39";

struct Ev { long frame; bool on; int key; int vel; };

// Built-in samples: a looped band-limited noise at 44.1 kHz (mono) and 40 kHz (stereo), 60000 frames.
struct Builtin {
    std::vector<int16_t> mono, stereo;
    SampleView mv, sv;
    Builtin() {
        const uint32_t n = 60000;
        mono.assign((kGuardPre + n + kGuardPost), 0); stereo.assign((kGuardPre + n + kGuardPost) * 2, 0);
        uint32_t r = 12345; float a = 0, b = 0, c = 0;
        for (uint32_t i = 0; i < n; ++i) {
            r = r * 1664525u + 1013904223u; const float x = float(int32_t(r) >> 8) / 8388608.0f;
            a += 0.3f * (x - a); b += 0.3f * (a - b); c += 0.2f * (x - c);
            mono[kGuardPre + i] = int16_t(b * 20000); stereo[(kGuardPre + i) * 2] = int16_t(b * 20000); stereo[(kGuardPre + i) * 2 + 1] = int16_t(c * 20000);
        }
        mv = {mono.data() + kGuardPre, n, 1, 44100.f};
        sv = {stereo.data() + kGuardPre * 2, n, 2, 40000.f};
    }
};

static bool parseSound(const std::string& text, SoundData& d) {
    char err[256];
    if (!readText(text.c_str(), d, err, sizeof err)) { fprintf(stderr, "sound: %s\n", err); return false; }
    return true;
}

// The engine with voice slots assigned in order (a stand-in for the shell's allocation).
struct Host {
    std::unique_ptr<Core> core{new Core};
    std::unique_ptr<SoundData> data{new SoundData};
    std::unique_ptr<Compiled> sound{new Compiled};
    int keyOf[kMaxVoices];
    bool pedalHeld[kMaxVoices] = {};
    int pedal = 0;
    uint32_t sounds = 1, started = 0;  // the sound installed in this many slots, notes taken in turn (PC_SOUNDS)
    std::vector<float> buses;          // then each slot's output bus: B frames left, B right, a bus each
    Host(double sr, Interp in) { core->prepare(sr); core->setInterp(in); std::fill(keyOf, keyOf + kMaxVoices, -1); }
    bool load(const SampleView* sampleViews, bool perRegion = false) {  // one a sample reference (or one a region)
        char err[256];
        std::vector<SampleView> views(data->regions);
        for (uint32_t i = 0; i < data->regions; ++i) views[i] = sampleViews[perRegion ? i : data->region[i].sample];
        if (!compile(*data, views.data(), *sound, err, sizeof err)) { fprintf(stderr, "compile: %s\n", err); return false; }
        for (uint32_t k = 0; k < sounds; ++k) {
            core->install(k, sound.get()); core->setGain(k, 1, 0);
            if (sounds > 1) core->route(k, k + 1, true);
        }
        if (sounds > 1) buses.assign(2 * B * kOutputs, 0.f);
        return true;
    }
    void apply(const Ev& e) {
        if (e.key == -2) { core->setModWheel(0, float(e.vel) / 127.f); return; }  // the mod wheel (CC 1)
        if (e.key < 0) {  // the sustain pedal (CC 64), as the shell: notes released under it wait for it
            pedal = e.vel;
            core->setSustain(0, float(e.vel) / 127.f);
            if (pedal < 64)
                for (uint32_t v = 0; v < kMaxVoices; ++v) if (pedalHeld[v]) { core->noteOff(v); pedalHeld[v] = false; keyOf[v] = -1; }
            return;
        }
        if (e.on) {
            for (uint32_t v = 0; v < kMaxVoices; ++v)
                if (!core->voice(v).sounding) { core->noteOn(v, started++ % sounds, 0, e.key, float(e.vel) / 127.f); keyOf[v] = e.key; return; }
        } else {
            for (uint32_t v = 0; v < kMaxVoices; ++v)
                if (keyOf[v] == e.key && !pedalHeld[v] && core->voice(v).sounding && !core->voice(v).releasing) {
                    if (pedal >= 64) { core->keyUp(v); pedalHeld[v] = true; } else { core->noteOff(v); keyOf[v] = -1; }
                    return;
                }
        }
    }
    void block(float* L, float* R, const Ev* ev, size_t n) {  // L and R: one buffer, R = L + B
        std::memset(L, 0, 2 * B * 4);
        long at = 0;
        for (size_t i = 0; i < n; ++i) {
            if (ev[i].frame > at) { core->render(L + at, R + at, uint32_t(ev[i].frame - at), uint32_t(at)); at = ev[i].frame; }
            apply(ev[i]);
        }
        if (at < B) core->render(L + at, R + at, uint32_t(B - at), uint32_t(at));
    }
    // the same with each slot on its own bus, the buses then summed into L and R (for the hash)
    void blockOnBuses(float* L, float* R, const Ev* ev, size_t n) {
        std::memset(L, 0, 2 * B * 4);
        std::memset(buses.data(), 0, buses.size() * 4);
        long at = 0;
        auto render = [&](long to) {
            float* l[kOutputs]; float* r[kOutputs];
            for (uint32_t k = 0; k < kOutputs; ++k) { l[k] = buses.data() + 2 * B * k + at; r[k] = l[k] + B; }
            core->render(l, r, uint32_t(to - at), uint32_t(at));
        };
        for (size_t i = 0; i < n; ++i) {
            if (ev[i].frame > at) { render(ev[i].frame); at = ev[i].frame; }
            apply(ev[i]);
        }
        if (at < B) render(B);
        for (uint32_t k = 0; k < kOutputs; ++k) for (int i = 0; i < B; ++i) { L[i] += buses[2 * B * k + i]; R[i] += buses[2 * B * k + B + i]; }
    }
};

static void blockEvents(const std::vector<Ev>& evs, size_t& ei, long b, std::vector<Ev>& out) {
    out.clear();
    while (ei < evs.size() && evs[ei].frame < (b + 1) * B) { Ev x = evs[ei++]; x.frame -= b * B; out.push_back(x); }
}

static std::string slurp(const char* path) {
    std::string t; FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(2); }
    char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) t.append(buf, n);
    fclose(f);
    return t;
}

static Interp interpOf(const char* s) { return !strcmp(s, "herm") ? Interp::Hermite : Interp::Sinc; }

#ifdef PCM_PACKS
// A store folder (see the head): the packs' records by sample id and the waveform files by id.
struct Store {
    std::string root;
    std::map<std::string, std::pair<std::string, sp_index_entry>> records;
    std::map<std::string, std::string> waveforms;
    explicit Store(const std::string& dir) : root(dir) {
        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(dir, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            const auto& p = it->path();
            if (p.extension() == ".owf") waveforms[p.stem().string()] = p.string();
            if (p.extension() != ".osp") continue;
            std::string err;
            if (auto pack = pcmshell::openPack(p.string(), err))
                for (const auto& e : pack->index) {
                    char h[65];
                    for (int k = 0; k < 32; ++k) snprintf(h + 2 * k, 3, "%02x", e.id[k]);
                    records.emplace(h, std::make_pair(p.string(), e));
                }
        }
    }
    bool sample(const char* ref, pcmshell::Record& r, std::string& error) const {
        uint8_t id[SP_SAMPLE_ID_BYTES];
        if (!pcmshell::sampleId(ref, id)) return pcmshell::onlyRecord(root + "/" + ref, r, error);
        const auto it = records.find(ref + 7);
        if (it == records.end()) { error = "no pack in the store holds it"; return false; }
        const sp_index_entry& e = it->second.second;
        return pcmshell::findRecord(it->second.first, e.offset, id, e.length, e.preload, r, error);
    }
    bool waveform(const char* ref, std::string& path, std::string& error) const {
        const auto it = waveforms.find(ref + 9);
        if (it == waveforms.end()) { error = "not in the store"; return false; }
        path = it->second;
        return true;
    }
};
// The sound at `path` as it plays: parsed, its waveforms expanded from the store (none needed without waveforms).
static bool loadSound(const char* path, const char* store, SoundData& d) {
    std::unique_ptr<SoundData> parsed(new SoundData);
    if (!parseSound(slurp(path), *parsed)) return false;
    if (!parsed->waveforms) { d = *parsed; return true; }
    if (!store) { fprintf(stderr, "the sound plays waveforms: give the store\n"); return false; }
    const Store s(store);
    std::string err;
    if (!pcmshell::expandWaveforms(*parsed, [&](const char* ref, std::string& p, std::string& e) { return s.waveform(ref, p, e); }, d, err)) { fprintf(stderr, "%s\n", err.c_str()); return false; }
    return true;
}
static int render(int argc, char** argv) {
    if (argc < 8) return 2;
    const double sr = atof(argv[4]), secs = atof(argv[5]);
    Host h(sr, interpOf(argv[7]));
    if (!loadSound(argv[2], argv[3], *h.data)) return 2;
    const Store store(argv[3]);
    std::string err;
    const auto samples = pcmshell::loadSamples(*h.data, [&](const char* ref, pcmshell::Record& r, std::string& e) { return store.sample(ref, r, e); }, true, err);
    if (!samples) { fprintf(stderr, "%s\n", err.c_str()); return 2; }
    if (!h.load(samples->views.data(), true)) return 2;
    std::vector<Ev> evs;
    for (int i = 8; i < argc; ++i) {
        double t; int k, v = 0;
        if (sscanf(argv[i], "on@%lf:%d:%d", &t, &k, &v) == 3) evs.push_back({long(std::lround(t * sr)), true, k, v});
        else if (sscanf(argv[i], "off@%lf:%d", &t, &k) == 2) evs.push_back({long(std::lround(t * sr)), false, k, 0});
        else if (sscanf(argv[i], "cc@%lf:64:%d", &t, &v) == 2) evs.push_back({long(std::lround(t * sr)), false, -1, v});
        else if (sscanf(argv[i], "cc@%lf:1:%d", &t, &v) == 2) evs.push_back({long(std::lround(t * sr)), false, -2, v});
        else { fprintf(stderr, "bad event %s\n", argv[i]); return 2; }
    }
    std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) { return a.frame < b.frame; });
    const long blocks = long(secs * sr / B) + 1;
    std::vector<float> out; out.reserve(size_t(blocks * B * 2));
    alignas(64) float LR[2 * B]; float* L = LR; float* R = LR + B;
    size_t ei = 0; std::vector<Ev> bev;
    for (long b = 0; b < blocks; ++b) {
        blockEvents(evs, ei, b, bev);
        h.block(L, R, bev.data(), bev.size());
        for (int i = 0; i < B; ++i) { out.push_back(L[i]); out.push_back(R[i]); }
    }
    FILE* f = fopen(argv[6], "wb");
    fwrite(out.data(), 4, out.size(), f); fclose(f);
    return 0;
}
#endif

static int rawq(int argc, char** argv) {
    if (argc < 6) return 2;
    const double st = atof(argv[3]);
    FILE* f = fopen(argv[4], "rb"); if (!f) return 2;
    fseek(f, 0, SEEK_END); const long n = ftell(f) / 2; fseek(f, 0, SEEK_SET);
    std::vector<int16_t> buf(size_t(kGuardPre + n + kGuardPost), 0);
    if (fread(buf.data() + kGuardPre, 2, size_t(n), f) != size_t(n)) return 2;
    fclose(f);
    Host h(48000, interpOf(argv[2]));
    if (!parseSound("oscairn-pcm-sound/0\nsample noise\nelement ampeg_attack=0 ampeg_decay1=127 ampeg_decay2=127\nregion key=0-127 root=60 element=0 sample=0\n", *h.data)) return 2;
    h.data->region[0].tune = int32_t(std::lround(st * 10000));  // the semitones as tune, from key 60
    SampleView v{buf.data() + kGuardPre, uint32_t(n), 1, 44100.f};
    if (!h.load(&v)) return 2;
    const double ratio = std::exp2(st / 12) * 44100.0 / 48000.0;
    const int blocks = int(std::min(4.0 * 48000, (double(n) - 4096) / ratio) / B);
    std::vector<float> out;
    alignas(64) float LR[2 * B]; float* L = LR; float* R = LR + B;
    for (int b = 0; b < blocks; ++b) {
        Ev on{0, true, 60, 127};
        h.block(L, R, &on, b == 0 ? 1 : 0);
        out.insert(out.end(), L, L + B);
    }
    f = fopen(argv[5], "wb"); fwrite(out.data(), 4, out.size(), f); fclose(f);
    return 0;
}

// The cost cases' sound: one region over every key with the pads' element (or thru), mono or stereo.
// A streamed plan's I/O thread, run between blocks (outside the measured window): every active ring filled as far as
// it may be, the region's frames from its head on, its loop (too long to keep: it streams) wrapped.
struct Feed {
    SampleView view;
    void serve(Core& core) const {
        const int c = view.channels;
        for (uint32_t k = 0; k < kStreams; ++k) {
            Stream& st = core.streams()[k];
            const uint8_t state = st.state.load(std::memory_order_acquire);
            if (state == Stream::Released) { st.written = 0; st.consumed = 0; st.state.store(Stream::Free, std::memory_order_release); continue; }
            if (state != Stream::Active || st.source != this) continue;
            int64_t w = st.written.load();
            for (const int64_t until = st.consumed.load(std::memory_order_acquire) + kStreamFrames; w < until; ++w) {
                int64_t r = view.head + w;
                if (r >= view.loopEnd) r = view.loopStart + (r - view.loopStart) % (view.loopEnd - view.loopStart);
                for (int ch = 0; ch < c; ++ch) st.ring[(w % kStreamFrames) * c + ch] = view.data[r * c + ch];
            }
            st.written.store(w, std::memory_order_release);
        }
    }
};
static Feed g_feed;
static bool g_streamed = false;

static bool caseSound(Host& h, const std::string& plan, const Builtin& s) {
    const bool stereo = plan.rfind("stereo", 0) == 0, thru = plan.find("thru") != std::string::npos;
    std::string el = kPadElement;
    if (thru) el += " fil_type=18";
    const size_t t = plan.find("-t");  // -tN: filter type N (4 and 5 the probe piano's and bass's types)
    if (t != std::string::npos && std::isdigit(static_cast<unsigned char>(plan[t + 2]))) el += " fil_type=" + std::to_string(std::atoi(plan.c_str() + t + 2));
    if (plan.find("eq") != std::string::npos) el += " eq_type=1 eq_freq1=157 eq_gain1=88 eq_q=8";  // the element EQ (a peak)
    const std::string text = "oscairn-pcm-sound/0\nsample builtin\n" + el + "\nregion key=0-127 root=60 element=0 sample=0 loop=1000-59000\n";
    if (!parseSound(text, *h.data)) return false;
    if (plan.find("stream") == std::string::npos) return h.load(stereo ? &s.sv : &s.mv);
    g_streamed = true;  // "-stream": a 4,096-frame head in memory, the rest (the loop too) from the rings
    g_feed.view = stereo ? s.sv : s.mv;
    g_feed.view.loopStart = 1000; g_feed.view.loopEnd = 59000; g_feed.view.head = 4096;
    SampleView v = g_feed.view;
    v.stream = &g_feed;
    return h.load(&v);
}
static std::vector<Ev> held(int n) {
    std::vector<Ev> evs;
    for (int k = 0; k < n; ++k) evs.push_back({0, true, 48 + (k % 25), 100});
    return evs;
}

static int cg(int argc, char** argv) {
    if (argc < 8) return 2;
    const std::string c = argv[2];
    int a = 3, n = 0;
    if (c == "held" || c == "shed" || c == "release") n = atoi(argv[a++]);
    const std::string plan = argv[a++];
    const Interp in = interpOf(argv[a++]);
    const double warm = atof(argv[a]), meas = atof(argv[a + 1]);
    FILE* tsv = fopen(argv[argc - 1], "w");
    static Builtin s;
    Host h(48000, in);
    if (const char* e = getenv("PC_SOUNDS")) h.sounds = uint32_t(std::max(1, std::min(int(kMaxSounds), atoi(e))));
    if (!caseSound(h, plan, s)) return 2;
    const long first = long(warm * 48000 / B), total = first + long(meas * 48000 / B);
    std::vector<Ev> evs = held(n), bev;
    const bool whole = getenv("PC_WHOLE") != nullptr;  // one dump for the whole window (per-function profiles)
    alignas(64) float LR[2 * B]; float* L = LR; float* R = LR + B;
    size_t ei = 0; uint64_t hash = 1469598103934665603ULL;
    for (long b = 0; b < total; ++b) {
        blockEvents(evs, ei, b, bev);
        if (g_streamed) g_feed.serve(*h.core);
        const bool m = b >= first;
        if (c == "shed" && b == first) h.core->shed(uint32_t(n));
        if (c == "release" && b == first) for (uint32_t v = 0; v < kMaxVoices; ++v) if (h.core->voice(v).sounding) h.core->noteOff(v);
        if (m && (!whole || b == first)) CALLGRIND_ZERO_STATS;
        if (h.sounds > 1) h.blockOnBuses(L, R, bev.data(), bev.size());
        else h.block(L, R, bev.data(), bev.size());
        if (m && !whole) CALLGRIND_DUMP_STATS;
        if (m) fprintf(tsv, "%ld\t%u\n", b - first, h.core->elementVoices());
        for (int i = 0; i < B; ++i) for (float x : {L[i], R[i]}) { uint32_t bits; memcpy(&bits, &x, 4); hash = (hash ^ bits) * 1099511628211ULL; }
    }
    if (whole) CALLGRIND_DUMP_STATS;
    fclose(tsv);
    printf("impl %s W=%d hash %016llx\n", PV_IMPL, pv::W, (unsigned long long)hash);
    return 0;
}

static double us(const timespec& a, const timespec& b) { return double(b.tv_sec - a.tv_sec) * 1e6 + double(b.tv_nsec - a.tv_nsec) * 1e-3; }
static void stats(const char* what, std::vector<double> x) {
    std::sort(x.begin(), x.end()); double m = 0; for (double v : x) m += v; m /= double(x.size());
    printf("  %-4s mean %7.2f median %7.2f p99 %7.2f max %8.2f us\n", what, m, x[x.size() / 2], x[size_t(double(x.size()) * 0.99)], x.back());
}
static int timeAll(int argc, char** argv) {
    printf("build %s W=%d flags %s\n", PV_IMPL, pv::W, PC_FLAGS);
#ifdef __linux__
    if (argc > 2) { cpu_set_t cs; CPU_ZERO(&cs); CPU_SET(atoi(argv[2]), &cs); printf("pin cpu %s: %s\n", argv[2], sched_setaffinity(0, sizeof cs, &cs) ? "FAILED" : "ok"); }
#else
    (void)argc; (void)argv;
#endif
    static Builtin s;
    for (const char* plan : {"mono", "stereo"})
        for (const char* ip : {"sinc", "herm"})
            for (int n : {0, 1, 4, 8, 16, 32, 64}) {
                Host h(48000, interpOf(ip));
                if (!caseSound(h, plan, s)) return 2;
                const long total = 6 * 48000 / B, first = 2 * 48000 / B;
                std::vector<Ev> evs = held(n), bev;
                std::vector<double> wall, cpu; alignas(64) float LR[2 * B]; float* L = LR; float* R = LR + B; size_t ei = 0; float sink = 0;
                for (long b = 0; b < total; ++b) {
                    blockEvents(evs, ei, b, bev);
                    timespec c0, w0, w1, c1;
                    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &c0); clock_gettime(CLOCK_MONOTONIC, &w0);
                    h.block(L, R, bev.data(), bev.size());
                    clock_gettime(CLOCK_MONOTONIC, &w1); clock_gettime(CLOCK_THREAD_CPUTIME_ID, &c1);
                    sink += L[b & (B - 1)];
                    if (b >= first) { wall.push_back(us(w0, w1)); cpu.push_back(us(c0, c1)); }
                }
                printf("case %s %s held %d: element voices %u (sink %g)\n", plan, ip, n, h.core->elementVoices(), double(sink));
                stats("wall", wall); stats("cpu", cpu);
            }
    return 0;
}

static int check() {
    static Builtin s;
    for (const char* plan : {"mono", "stereo"}) {
        Host h(44100, Interp::Sinc);
        if (!caseSound(h, plan, s)) return 1;
        alignas(64) float LR[2 * B]; float* L = LR; float* R = LR + B; uint32_t r = 1;
        auto rnd = [&] { r = r * 1664525u + 1013904223u; return r >> 8; };
        for (long b = 0; b < 44100 * 20 / B; ++b) {
            std::vector<Ev> ev;
            if (rnd() % 4 == 0) ev.push_back({long(rnd() % B), true, 24 + int(rnd() % 80), 1 + int(rnd() % 127)});
            if (rnd() % 4 == 0) ev.push_back({long(rnd() % B), false, 24 + int(rnd() % 80), 0});
            std::sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) { return a.frame < b.frame; });
            h.block(L, R, ev.data(), ev.size());
            for (int i = 0; i < B; ++i)
                for (float x : {L[i], R[i]})
                    if (!std::isfinite(x) || std::fpclassify(x) == FP_SUBNORMAL || std::fabs(x) > 8) { printf("bad sample %g at block %ld (%s)\n", double(x), b, plan); return 1; }
        }
    }
    printf("check ok\n");
    return 0;
}

int main(int argc, char** argv) {
    const std::string m = argc > 1 ? argv[1] : "";
#ifdef PCM_PACKS
    if (m == "render") return render(argc, argv);
#endif
    if (m == "rawq") return rawq(argc, argv);
    if (m == "cg") return cg(argc, argv);
    if (m == "time") return timeAll(argc, argv);
    if (m == "check") return check();
    if (m == "kernels") {
        printf("// SPDX-License-Identifier: AGPL-3.0-or-later\n// Written by `pcbench kernels` from kernelTap() (core/core.cpp): the interpolation bank (core/voices.h).\n"
               "#include \"pcore.h\"\n\nnamespace pcm {\n\nalignas(64) const float kSinc[kKernelBank * kKernelFloats] = {\n");
        for (int k = 0; k < kKernelBank; ++k) {
            printf("    // cutoff %.2f\n", kernelCut(k));
            for (int ph = 0; ph < kKernelFloats / kTaps; ++ph) {
                printf("   ");
                for (int i = 0; i < kTaps; ++i) printf(" %.8ef,", ph <= kPhases ? double(float(kernelTap(k, ph, i))) : 0.0);
                printf("\n");
            }
        }
        printf("};\n\n}  // namespace pcm\n");
        return 0;
    }
    if (m == "coverage" && argc > 2) {
        std::unique_ptr<SoundData> d(new SoundData);
#ifdef PCM_PACKS
        if (!loadSound(argv[2], argc > 3 ? argv[3] : nullptr, *d)) return 2;
#else
        if (!parseSound(slurp(argv[2]), *d)) return 2;
#endif
        Gap g[256];
        const uint32_t n = notCovered(*d, g, 256);
        for (uint32_t i = 0; i < n && i < 256; ++i)
            if (g[i].part) printf("not covered: part %s=%d: %s\n", kPartParamDefs[g[i].param].id, d->part[g[i].param], g[i].why);
            else if (g[i].param < 0) printf("not covered: region %u: %s\n", g[i].element, g[i].why);
            else printf("not covered: element %u %s=%d: %s\n", g[i].element, kParamDefs[g[i].param].id, d->element[g[i].element].v[g[i].param], g[i].why);
        printf("%u value(s) not covered\n", n);
        return n ? 1 : 0;
    }
    fprintf(stderr, "usage: see the head of tools/pcbench.cpp\n");
    return 2;
}
