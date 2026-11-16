// SPDX-License-Identifier: AGPL-3.0-or-later
// A small CLAP host for the tests. Everything goes through the CLAP ABI and the Oscairn extension
// structs: the built .clap is dlopen'ed, its factory creates the plugin, and nothing of the
// plugin's own code or headers is used.
#pragma once
#include <atomic>
#include <clap/clap.h>
#include <dlfcn.h>
#include <oscairn-clap/assets.h>
#include <oscairn-clap/memory.h>
#include <oscairn-clap/sound-contexts.h>
#include <oscairn-clap/voice-budget.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <clap/ext/audio-ports-activation.h>

#include "check.h"

extern const char* g_pluginPath;
extern std::string g_assetDir;  // test_main.cpp: where the "test" library's files are (one-record packs of the tones)
// The generated tones (test_main.cpp), by name, and the test host's store: sample id (hex) -> pack and record offset.
struct TestTone { std::vector<int16_t> pcm; uint32_t frames = 0, channels = 1, rate = 44100; std::string id; };
struct StoreEntry { std::string pack; uint64_t offset = 0, length = 0, preload = 0; };
extern std::map<std::string, TestTone> g_tones;
extern std::map<std::string, StoreEntry> g_store;
// The reference "sample:<id>" of a tone.
inline std::string sampleRef(const char* tone) { return "sample:" + g_tones.at(tone).id; }

// alloc_guard.cpp: counts heap allocations made by any code in the process (the plugin included,
// whatever language it is written in) while enabled. Returns false when the platform has no
// interposer here (sanitizer builds).
bool allocGuardAvailable();
const char* allocGuardName();
void allocGuardEnable(bool on);
long allocGuardCount();

constexpr double kRate = 48000;
constexpr const char* kId = "org.oscairn.sampler";
// The default sound for these tests: a looped 220 Hz sine (sine.osp in the test library) over every
// key, flat envelope, release 50 (from full level the voice ends about 155 ms after the note-off).
constexpr const char* kSound =
    "oscairn-sampler-sound/0\nsample sine.osp\nelement ampeg_decay1=127 ampeg_decay2=127 ampeg_release=50\n"
    "region key=0-127 root=57 element=0 sample=0 loop=0-44100\n";
// The same sound on a key range only.
constexpr const char* kZoneSound =
    "oscairn-sampler-sound/0\nsample sine.osp\nelement ampeg_decay1=127 ampeg_decay2=127 ampeg_release=50\n"
    "region key=84-96 root=57 element=0 sample=0 loop=0-44100\n";
inline std::string b64(const std::string& s) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    for (size_t i = 0; i < s.size(); i += 3) {
        const uint32_t v = uint32_t(uint8_t(s[i])) << 16 | (i + 1 < s.size() ? uint32_t(uint8_t(s[i + 1])) << 8 : 0) | (i + 2 < s.size() ? uint8_t(s[i + 2]) : 0);
        o += t[v >> 18 & 63]; o += t[v >> 12 & 63]; o += i + 1 < s.size() ? t[v >> 6 & 63] : '='; o += i + 2 < s.size() ? t[v & 63] : '=';
    }
    return o;
}
// A CLAP state holding a sound given as its text form (the plugin saves the binary blob).
inline std::string stateWith(const std::string& sound, double level = 1, double pan = 0, int poly = 64) {
    char head[96]; std::snprintf(head, sizeof head, "oscairn-sampler-state 1\n%g %g %d \"test\" \"\" ", level, pan, poly);
    return head + b64(sound) + "\n";
}
// Patch text for prepare: the shell's values, then a sound.
inline std::string patchWith(const std::string& shell, const char* sound = kSound) { return "shell " + shell + "\n" + sound; }
constexpr uint32_t kChoke = 144;  // 3 ms at 48 kHz: the helper's choke time

using Bytes = std::vector<uint8_t>;
template <class T> Bytes bytes(const T& e) { return Bytes(reinterpret_cast<const uint8_t*>(&e), reinterpret_cast<const uint8_t*>(&e) + sizeof e); }
inline Bytes noteEv(uint16_t type, uint32_t t, int32_t id, int16_t ch, int16_t key, double vel = 0.8, int16_t port = 0) {
    clap_event_note_t e{};
    e.header = {sizeof e, t, CLAP_CORE_EVENT_SPACE_ID, type, 0};
    e.note_id = id; e.port_index = port; e.channel = ch; e.key = key; e.velocity = vel;
    return bytes(e);
}
inline Bytes on(uint32_t t, int32_t id, int16_t ch, int16_t key, double vel = 0.8, int16_t port = 0) { return noteEv(CLAP_EVENT_NOTE_ON, t, id, ch, key, vel, port); }
inline Bytes off(uint32_t t, int32_t id, int16_t ch, int16_t key) { return noteEv(CLAP_EVENT_NOTE_OFF, t, id, ch, key, 0); }
inline Bytes choke(uint32_t t, int32_t id, int16_t ch, int16_t key) { return noteEv(CLAP_EVENT_NOTE_CHOKE, t, id, ch, key, 0); }
inline Bytes midi(uint32_t t, uint8_t a, uint8_t b, uint8_t c) {
    clap_event_midi_t e{};
    e.header = {sizeof e, t, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0};
    e.data[0] = a; e.data[1] = b; e.data[2] = c;
    return bytes(e);
}
inline Bytes param(uint32_t t, clap_id id, double v, int16_t ch = -1) {
    clap_event_param_value_t e{};
    e.header = {sizeof e, t, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
    e.param_id = id; e.note_id = -1; e.port_index = -1; e.channel = ch; e.key = -1; e.value = v;
    return bytes(e);
}
inline Bytes mod(uint32_t t, clap_id id, double amount, int16_t ch = -1, int32_t note = -1, int16_t key = -1) {
    clap_event_param_mod_t e{};
    e.header = {sizeof e, t, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_MOD, 0};
    e.param_id = id; e.note_id = note; e.port_index = -1; e.channel = ch; e.key = key; e.amount = amount;
    return bytes(e);
}
inline Bytes volume(uint32_t t, int32_t id, int16_t ch, int16_t key, double v) {
    clap_event_note_expression_t e{};
    e.header = {sizeof e, t, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_EXPRESSION, 0};
    e.expression_id = CLAP_NOTE_EXPRESSION_VOLUME; e.note_id = id; e.port_index = 0; e.channel = ch; e.key = key; e.value = v;
    return bytes(e);
}

// The plugin's own streaming extension (src/identity.h: kExtStreaming): voices shed for late streams, and test hooks.
struct StreamingExt {
    uint64_t (*sheds)(const clap_plugin_t*);
    void (*stall)(const clap_plugin_t*, bool);
    void (*settle)(const clap_plugin_t*);
    void (*failReads)(const clap_plugin_t*, bool);
};

struct Rec { uint16_t type; int32_t id; int16_t port, channel, key; uint32_t time; long frame; double value; };

// What the plugin pushed to the host: fixed storage, so that recording never allocates while the
// allocation guard is counting.
struct OutLog {
    std::array<Rec, 4096> buf{};
    uint32_t n = 0, dropped = 0, frames = 0;
    long base = 0;
    bool badTime = false;
    clap_output_events_t api{this, [](const clap_output_events_t* l, const clap_event_header_t* h) {
        auto* o = static_cast<OutLog*>(l->ctx);
        if (h->time >= o->frames) o->badTime = true;
        Rec r{h->type, -1, 0, 0, 0, h->time, o->base + h->time, 0};
        if (h->type == CLAP_EVENT_NOTE_END || h->type == CLAP_EVENT_NOTE_ON || h->type == CLAP_EVENT_NOTE_OFF || h->type == CLAP_EVENT_NOTE_CHOKE) {
            const auto* e = reinterpret_cast<const clap_event_note_t*>(h);
            r.id = e->note_id; r.port = e->port_index; r.channel = e->channel; r.key = e->key;
        } else if (h->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* e = reinterpret_cast<const clap_event_param_value_t*>(h);
            r.id = int32_t(e->param_id); r.value = e->value;
        }
        if (o->n == o->buf.size()) { ++o->dropped; return false; }
        o->buf[o->n++] = r;
        return true;
    }};
};

struct InList {
    std::vector<Bytes> ev;
    clap_input_events_t api{this, [](const clap_input_events_t* l) { return uint32_t(static_cast<const InList*>(l->ctx)->ev.size()); },
                            [](const clap_input_events_t* l, uint32_t i) { return reinterpret_cast<const clap_event_header_t*>(static_cast<const InList*>(l->ctx)->ev[i].data()); }};
};

struct Stream {
    std::string data;
    size_t pos = 0;
    size_t shortRead = 7;  // short reads on purpose
    clap_ostream_t out{this, [](const clap_ostream_t* s, const void* b, uint64_t n) -> int64_t {
        static_cast<Stream*>(s->ctx)->data.append(static_cast<const char*>(b), size_t(n)); return int64_t(n); }};
    clap_istream_t in{this, [](const clap_istream_t* s, void* b, uint64_t n) -> int64_t {
        auto* st = static_cast<Stream*>(s->ctx);
        const size_t k = std::min<size_t>(size_t(n), std::min(st->shortRead, st->data.size() - st->pos));
        std::memcpy(b, st->data.data() + st->pos, k); st->pos += k; return int64_t(k); }};
};

struct Library {
    void* handle = nullptr;
    const clap_plugin_entry_t* entry = nullptr;
    const clap_plugin_factory_t* factory = nullptr;
    Library() {
        handle = dlopen(g_pluginPath, RTLD_NOW | RTLD_LOCAL);
        if (!handle) { std::printf("  dlopen %s: %s\n", g_pluginPath, dlerror()); ++g_failures; return; }
        entry = static_cast<const clap_plugin_entry_t*>(dlsym(handle, "clap_entry"));
        if (!entry || !entry->init(g_pluginPath)) { std::printf("  no clap_entry or init failed\n"); ++g_failures; entry = nullptr; return; }
        factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    }
};
inline Library& library() { static Library l; return l; }

// The host's oscairn.assets (draft 4): library "test" is g_assetDir, where a reference "waveform:<id>" is the file
// <id>.owf; anything else is unknown; resolve_sample answers from the store pack (g_store).
inline const oscairn_host_assets_t* testAssets() {
    static const oscairn_host_assets_t a = {
        [](const clap_host_t*, oscairn_asset_ref_t* ref, char* path, uint32_t cap, char* msg, uint32_t mcap) {
            const bool wf = !std::strncmp(ref->path, "waveform:", 9);  // installed waveforms: <id>.owf, library empty
            if (std::strcmp(ref->library, wf ? "" : "test")) { std::snprintf(msg, mcap, "no library '%s'", ref->library); return false; }
            const std::string p = g_assetDir + "/" + (wf ? std::string(ref->path + 9) + ".owf" : std::string(ref->path));
            if (FILE* f = std::fopen(p.c_str(), "rb")) std::fclose(f);
            else { std::snprintf(msg, mcap, "no file '%s'", ref->path); return false; }
            std::snprintf(path, cap, "%s", p.c_str());
            return true;
        },
        nullptr, nullptr,
        [](const clap_host_t*, const oscairn_asset_ref_t* ref, char* path, uint32_t cap, oscairn_sample_location_t* at, char* msg, uint32_t mcap) {
            auto it = ref->library[0] || std::strncmp(ref->path, "sample:", 7) ? g_store.end() : g_store.find(ref->path + 7);
            if (it == g_store.end()) { std::snprintf(msg, mcap, "no sample '%s'", ref->path); return false; }
            std::snprintf(path, cap, "%s", it->second.pack.c_str());
            *at = {it->second.offset, it->second.length, it->second.preload};
            return true;
        }};
    return &a;
}
inline std::atomic<int> g_processRequests{0};  // the plugin's request_process calls (a sleeping instrument's wake-ups)
inline const clap_host_t* testHost() {
    static const clap_host_t h = {CLAP_VERSION, nullptr, "pcm-tests", "oscairn", "", "1",
                                  [](const clap_host_t*, const char* id) -> const void* { return !std::strcmp(id, OSCAIRN_EXT_ASSETS_DRAFT_3) || !std::strcmp(id, OSCAIRN_EXT_ASSETS) ? testAssets() : nullptr; },
                                  [](const clap_host_t*) {}, [](const clap_host_t*) { ++g_processRequests; }, [](const clap_host_t*) {}};
    return &h;
}
inline const oscairn_asset_ref_t* testBase() { static oscairn_asset_ref_t b = [] { oscairn_asset_ref_t r{}; std::snprintf(r.library, sizeof r.library, "test"); return r; }(); return &b; }

struct Inst {
    const clap_plugin_t* p = nullptr;
    const clap_plugin_params_t* params = nullptr;
    const clap_plugin_state_t* state = nullptr;
    const oscairn_plugin_sound_contexts_t* contexts = nullptr;
    const oscairn_plugin_voice_budget_t* budget = nullptr;
    const oscairn_plugin_memory_t* memory = nullptr;
    const StreamingExt* streaming = nullptr;
    bool settle = true;         // before each block, wait until the plugin's I/O thread has filled every ring (deterministic)
    std::vector<Rec> log;       // every event the plugin pushed
    std::vector<float> L, R;    // every sample rendered (the main port)
    uint32_t outputs = 1;       // audio output ports the test host gives (1 to 9)
    bool activatePorts = true;  // activate the ports past the main one before the first block
    std::vector<float> PL[8], PR[8];  // ports 1-8 when given
    const oscairn_plugin_sound_snapshots_t* snapshots = nullptr;
    bool ring = false;          // give a ring-out buffer before each block (oscairn.sound-snapshots)
    std::vector<float> RL[2], RR[2];  // its two pairs: the retiring sounds' and the choked ones' main output
    long frame = 0;
    bool guard = false;         // count heap allocations inside process()
    long allocations = 0;
    bool badStatus = false, badTime = false;
    clap_process_status last = CLAP_PROCESS_CONTINUE;  // what the latest block returned

    explicit Inst(bool start = true) {
        auto& lib = library();
        if (!lib.factory) return;
        p = lib.factory->create_plugin(lib.factory, testHost(), kId);
        if (!p) { ++g_failures; return; }
        // A host may refuse a plugin with any of these missing.
        if (!p->init || !p->destroy || !p->activate || !p->deactivate || !p->start_processing || !p->stop_processing ||
            !p->reset || !p->process || !p->get_extension || !p->on_main_thread) { ++g_failures; return; }
        p->init(p);
        p->on_main_thread(p);
        params = static_cast<const clap_plugin_params_t*>(p->get_extension(p, CLAP_EXT_PARAMS));
        state = static_cast<const clap_plugin_state_t*>(p->get_extension(p, CLAP_EXT_STATE));
        contexts = static_cast<const oscairn_plugin_sound_contexts_t*>(p->get_extension(p, OSCAIRN_EXT_SOUND_CONTEXTS));
        budget = static_cast<const oscairn_plugin_voice_budget_t*>(p->get_extension(p, OSCAIRN_EXT_VOICE_BUDGET));
        memory = static_cast<const oscairn_plugin_memory_t*>(p->get_extension(p, OSCAIRN_EXT_MEMORY));
        streaming = static_cast<const StreamingExt*>(p->get_extension(p, "org.oscairn.sampler.streaming/draft-1"));
        snapshots = static_cast<const oscairn_plugin_sound_snapshots_t*>(p->get_extension(p, OSCAIRN_EXT_SOUND_SNAPSHOTS));
        if (!streaming) { std::printf("  no streaming extension\n"); ++g_failures; return; }
        if (start) { p->activate(p, kRate, 1, 4096); p->start_processing(p); }
        // The default sound for these tests (kSound), loaded through the CLAP state.
        if (state) {
            Stream s; s.data = stateWith(kSound);
            if (!state->load(p, &s.in)) { std::printf("  the default test sound did not load\n"); ++g_failures; }
        }
    }
    ~Inst() { if (p) { p->stop_processing(p); p->deactivate(p); p->destroy(p); } }
    Inst(const Inst&) = delete;

    // One block of `frames` with the given events (times within the block, ascending).
    void block(uint32_t frames, const std::vector<Bytes>& events = {}) {
        InList in; in.ev = events;
        OutLog out; out.frames = frames; out.base = frame;
        std::vector<float> l(frames, 0.f), r(frames, 0.f);
        for (auto& x : l) x = 123.f;  // the plugin must overwrite, not add
        for (auto& x : r) x = 123.f;
        std::vector<float> pl[8], pr2[8], ring4[4];
        float* ch[9][2] = {{l.data(), r.data()}};
        for (int k = 0; k < 8; ++k) { pl[k].assign(frames, 123.f); pr2[k].assign(frames, 123.f); ch[k + 1][0] = pl[k].data(); ch[k + 1][1] = pr2[k].data(); }
        clap_audio_buffer_t buf[9]{};
        for (int k = 0; k < 9; ++k) { buf[k].data32 = ch[k]; buf[k].channel_count = 2; }
        float* rch[4];
        for (int k = 0; k < 4; ++k) { ring4[k].assign(frames, 0.f); rch[k] = ring4[k].data(); }
        clap_audio_buffer_t rbuf{}; rbuf.data32 = rch; rbuf.channel_count = 4;
        clap_process_t pr{};
        pr.steady_time = frame; pr.frames_count = frames; pr.in_events = &in.api; pr.out_events = &out.api;
        pr.audio_outputs = buf; pr.audio_outputs_count = outputs;
        if (outputs > 1 && activatePorts && frame == 0) {
            auto act = static_cast<const clap_plugin_audio_ports_activation_t*>(p->get_extension(p, CLAP_EXT_AUDIO_PORTS_ACTIVATION));
            for (uint32_t k = 1; k < outputs; ++k) if (!act || !act->set_active(p, false, k, true, 32)) ++g_failures;
        }
        if (settle) streaming->settle(p);
        if (ring) snapshots->ring_out(p, &rbuf);
        if (guard) allocGuardEnable(true);
        const auto st = p->process(p, &pr);
        if (guard) { allocGuardEnable(false); allocations = allocGuardCount(); }
        last = st;
        if (st == CLAP_PROCESS_ERROR) badStatus = true;
        if (out.badTime) badTime = true;
        if (out.dropped) { std::printf("  test log overflowed\n"); ++g_failures; }
        log.insert(log.end(), out.buf.begin(), out.buf.begin() + out.n);
        L.insert(L.end(), l.begin(), l.end()); R.insert(R.end(), r.begin(), r.end());
        for (uint32_t k = 0; k + 1 < outputs; ++k) { PL[k].insert(PL[k].end(), pl[k].begin(), pl[k].end()); PR[k].insert(PR[k].end(), pr2[k].begin(), pr2[k].end()); }
        if (ring) for (int k = 0; k < 2; ++k) { RL[k].insert(RL[k].end(), ring4[2 * k].begin(), ring4[2 * k].end()); RR[k].insert(RR[k].end(), ring4[2 * k + 1].begin(), ring4[2 * k + 1].end()); }
        frame += frames;
    }
    void run(long frames, uint32_t blockSize = 128) {
        for (long done = 0; done < frames; done += blockSize) block(uint32_t(std::min<long>(blockSize, frames - done)));
    }
    int count(uint16_t type, int32_t id) const { int n = 0; for (auto& r : log) n += r.type == type && r.id == id; return n; }
    int ends(int32_t id) const { return count(CLAP_EVENT_NOTE_END, id); }
    int endsTotal() const { return int(std::count_if(log.begin(), log.end(), [](const Rec& r) { return r.type == CLAP_EVENT_NOTE_END; })); }
    const Rec* endOf(int32_t id) const { for (auto& r : log) if (r.type == CLAP_EVENT_NOTE_END && r.id == id) return &r; return nullptr; }
    uint32_t voices() const { double c = 0; return budget->active_voices(p, &c); }
    double value(clap_id id) const { double v = -1; params->get_value(p, id, &v); return v; }
    std::string save() const { Stream s; return state->save(p, &s.out) ? s.data : std::string("<fail>"); }
    bool load(const std::string& d) const { Stream s; s.data = d; return state->load(p, &s.in); }
    void limits(uint32_t limit, std::vector<oscairn_voice_budget_context_t> ctx = {}) const {
        budget->set_limits(p, limit, ctx.data(), uint32_t(ctx.size()));
    }
    void clearAudio() { L.clear(); R.clear(); }
};

// Peak of |sample| over [from, to) of a rendered channel.
inline float peak(const std::vector<float>& v, size_t from = 0, size_t to = SIZE_MAX) {
    float m = 0;
    for (size_t i = from; i < std::min(to, v.size()); ++i) m = std::max(m, std::fabs(v[i]));
    return m;
}
inline bool silent(const Inst& a, size_t from) { return peak(a.L, from) < 1e-6f && peak(a.R, from) < 1e-6f; }
