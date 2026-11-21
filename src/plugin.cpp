// SPDX-License-Identifier: AGPL-3.0-or-later
// The plugin shell: CLAP entry, parameters, state, sample assets, and the Oscairn host contract for
// an instrument (oscairn.sound-contexts, oscairn.voice-budget, oscairn.memory,
// oscairn.asset-dependencies), around the DSP core behind engine.h. Sound contexts, selection,
// note ownership, NOTE_END and the voice budget's order come from the Oscairn SDK's helpers
// (oscairn-clap/sound-contexts-plugin.hpp and voice-budget-plugin.hpp); what is written here is
// what is specific to this plugin.
//
// A sound is a sound-format-v1 blob (core/sound.cpp); its waveforms ("waveform:<id>") resolve through the host's
// resolve to files, expanded into regions when the sound is prepared. A sample reference is "sample:<id>", which the host's
// oscairn.assets resolve_sample (draft 4) turns into a record of a pack, or a path relative to a base asset reference
// (a library and a folder in it) naming a one-record pack file. The loader (loader.h) reads what the core needs from
// the records when the sound is prepared or loaded; the state stores the blob and the base, never a local path.
#include <oscairn-clap/memory.h>
#include <oscairn-clap/sound-contexts-plugin.hpp>
#include <oscairn-clap/ui.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>

#include "engine.h"
#include "identity.h"

namespace pcmshell {
namespace {

constexpr const char* kStateHeader = "oscairn-sampler-state 3";      // the CLAP state: the default sound (states 1 and 2,
constexpr const char* kStateHeader2 = "oscairn-sampler-state 2";     // with the values before the knobs or before the bus
constexpr const char* kStateHeader1 = "oscairn-sampler-state 1";     // only, still read)
constexpr const char* kSnapshotHeader = "oscairn-sampler-contexts/3"; // the sound-contexts snapshot (one token)
constexpr clap_id kShedsParam = kParamCount;  // read-only: voices shed for late streams (the streaming extension's count)
constexpr const char* kBlobHeader = "oscairn-sampler-blob/0";         // patch text: a blob in base64
constexpr size_t kMaxStateBytes = 1 << 22;  // eight sounds' blobs in base64
constexpr uint32_t kMaxDeviceVoices = kVoices, kMaxDeviceVoicesPerSound = kVoices;

// A device cap from the environment: a whole decimal number in 1..ceiling, else none (0).
uint32_t envCap(const char* name, long ceiling) {
    const char* e = std::getenv(name);
    char* end = nullptr;
    const long v = e && *e ? std::strtol(e, &end, 10) : 0;
    return e && *e && !*end && v >= 1 && v <= ceiling ? uint32_t(v) : 0;
}
// The tighter of a host limit and a cap; 0 means none.
uint32_t tighter(uint32_t host, uint32_t cap) { return host && (!cap || host < cap) ? host : cap; }

float clampParam(Param p, double x) {
    const auto& i = kParams[p];
    x = std::min(std::max(x, i.min), i.max);
    return float(i.stepped ? std::round(x) : x);
}
bool validValue(Param p, double x) { return std::isfinite(x) && x >= kParams[p].min && x <= kParams[p].max; }
// A decimal read with the C locale's dot and nothing after it, whatever the process locale (strtod/sscanf
// depend on it): "0.5" stays 0.5 even under a comma-decimal host locale.
bool cDec(const char* s, double& out) {
    std::istringstream in(s); in.imbue(std::locale::classic());
    in >> out;
    return !in.fail() && in.eof();
}

std::string base64(const std::vector<uint8_t>& b) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string s;
    for (size_t i = 0; i < b.size(); i += 3) {
        const uint32_t v = uint32_t(b[i]) << 16 | (i + 1 < b.size() ? uint32_t(b[i + 1]) << 8 : 0) | (i + 2 < b.size() ? b[i + 2] : 0);
        s += t[v >> 18 & 63]; s += t[v >> 12 & 63];
        s += i + 1 < b.size() ? t[v >> 6 & 63] : '='; s += i + 2 < b.size() ? t[v & 63] : '=';
    }
    return s;
}
bool unbase64(const std::string& s, std::vector<uint8_t>& out) {
    out.clear();
    if (s.size() % 4) return false;
    auto val = [](char c) { return c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62 : c == '/' ? 63 : -1; };
    for (size_t i = 0; i < s.size(); i += 4) {
        int v[4];
        for (int k = 0; k < 4; ++k) { v[k] = s[i + k] == '=' ? (i + 4 == s.size() && k >= 2 ? 0 : -2) : val(s[i + k]); if (v[k] < 0) return false; }
        const uint32_t x = uint32_t(v[0]) << 18 | uint32_t(v[1]) << 12 | uint32_t(v[2]) << 6 | uint32_t(v[3]);
        out.push_back(uint8_t(x >> 16));
        if (s[i + 2] != '=') out.push_back(uint8_t(x >> 8));
        if (s[i + 3] != '=') out.push_back(uint8_t(x));
    }
    return true;
}

struct Voice : oscairn::contexts::VoiceBase {};

struct Plugin : oscairn::contexts::Engine<Plugin, Voice, Patch, kContexts, kVoices> {
    using Base = oscairn::contexts::Engine<Plugin, Voice, Patch, kContexts, kVoices>;
    static constexpr const char* stateVersion = kSnapshotHeader;
    static constexpr size_t maxStateBytes = kMaxStateBytes;
    static constexpr bool volumeExpressions = true;  // the core multiplies by the note's volume

    const clap_host_t* host;
    const oscairn_host_assets_t* assets = nullptr;
    bool assets4 = false;  // the host answered draft 4 (resolve_sample)
    std::unique_ptr<pcmshell::Engine> core = makeEngine();
    std::array<std::atomic<float>, kParamCount> values{};  // what get_value shows: the sound on channel 0
    oscairn::voices::Limits budget;                         // audio thread: as set_limits gave them
    const uint32_t deviceCap = envCap(kEnvMaxVoices, kMaxDeviceVoices);
    const uint32_t soundCap = envCap(kEnvMaxVoicesPerSound, kMaxDeviceVoicesPerSound);
    const bool decodeWhole = [] { const char* e = std::getenv(kEnvDecodeWhole); return e && !std::strcmp(e, "1"); }();
    std::array<std::atomic<bool>, kPorts> portActive{};  // output ports 1-8 as the host set them (not saved); 0 is always used
    std::array<bool, kVoices> choking{};  // the core was told to fade this voice
    std::array<bool, kVoices> keyUpSent{};  // the core was told the key is up under the pedal
    struct Owed { int32_t id; int16_t channel, key; };
    std::array<Owed, kVoices> owed{};     // notes a snapshot load ended; NOTE_END at the next block's start
    uint32_t owedCount = 0;
    uint32_t shownSlot = 0;               // the sound whose values `values` holds
    std::array<bool, kContexts> dropping{};  // main thread: discarded contexts whose sounds the core still holds
    std::atomic<bool> dropsPending{false};   // some are: each block asks for a main-thread call to drop them
    double sampleRate = 48000;
    uint32_t maxBlock = 0;
    std::atomic<bool> loading{false};     // a snapshot load is running (main thread)
    bool slept = false;                   // audio thread: the last process returned SLEEP
    bool rendered = false;                // audio thread: this block reached the core
    int64_t renderedEnd = -1;             // audio thread: the host's steady_time the core's clock has reached, -1 unknown

    explicit Plugin(const clap_host_t* h) : host(h) {
        plugin = {&kDescriptor, this, init, destroy, activate, deactivate, startProcessing, stopProcessing, reset, process, getExtension, onMainThread};
        for (uint32_t i = 0; i < kParamCount; ++i) values[i].store(float(kParams[i].def));
        const char* interp = std::getenv(kEnvInterpolation);
        core->setHermite(interp && !std::strcmp(interp, "hermite"));
    }

    // ---- sounds and their samples (main thread) ------------------------------------------
    // A sample reference to its record: "sample:<id>" through resolve_sample, a path through resolve (a one-record pack).
    bool resolveSample(const char* ref, const std::string& library, const std::string& path, Record& out, std::string& error) {
        char local[4096] = {}, msg[256] = {};
        oscairn_asset_ref_t r{};
        uint8_t id[SP_SAMPLE_ID_BYTES];
        if (sampleId(ref, id)) {
            if (!assets4 || !assets->resolve_sample) { error = "the host cannot resolve sample references (oscairn.assets draft 4)"; return false; }
            std::snprintf(r.path, sizeof r.path, "%s", ref);  // library empty: an id names the sample wherever it is installed
            std::snprintf(r.sha256, sizeof r.sha256, "%s", ref + 7);
            oscairn_sample_location_t at{};
            if (!assets->resolve_sample(host, &r, local, sizeof local, &at, msg, sizeof msg)) { error = std::string("cannot resolve: ") + msg; return false; }
            return findRecord(local, at.offset, id, at.length, at.preload, out, error);
        }
        if (library.empty()) { error = "a sample path needs a base asset reference"; return false; }
        const std::string full = (path.empty() ? std::string() : path + "/") + ref;
        if (full.size() >= sizeof r.path) { error = "sample path too long"; return false; }
        std::snprintf(r.library, sizeof r.library, "%s", library.c_str());
        std::snprintf(r.path, sizeof r.path, "%s", full.c_str());
        if (!assets->resolve(host, &r, local, sizeof local, msg, sizeof msg)) { error = std::string("cannot resolve: ") + msg; return false; }
        return onlyRecord(local, out, error);
    }
    // A reference "waveform:<id>" to its installed file, through resolve.
    bool resolveWaveform(const char* ref, std::string& out, std::string& error) {
        char local[4096] = {}, msg[256] = {};
        oscairn_asset_ref_t r{};  // library empty, as for a sample id
        std::snprintf(r.path, sizeof r.path, "%s", ref);
        std::snprintf(r.sha256, sizeof r.sha256, "%s", ref + 9);
        if (!assets->resolve(host, &r, local, sizeof local, msg, sizeof msg)) { error = std::string("cannot resolve: ") + msg; return false; }
        out = local;
        return true;
    }
    // A sound from its blob and base: parse and validate, expand its waveforms, resolve every sample and load what the core needs, compile.
    std::shared_ptr<const Sound> buildSound(std::vector<uint8_t> blob, const std::string& library, const std::string& path, std::string& error) {
        auto s = std::make_shared<Sound>();
        s->data.reset(new pcm::SoundData);
        char why[256] = {};
        if (!pcm::readBlob(blob.data(), blob.size(), *s->data, why, sizeof why)) { error = why; return nullptr; }
        s->blob = std::move(blob); s->library = library; s->path = path;
        if (!assets || !assets->resolve) { error = "the host offers no oscairn.assets to resolve samples"; return nullptr; }
        if (s->data->waveforms) {
            s->expanded.reset(new pcm::SoundData);
            if (!expandWaveforms(*s->data, [&](const char* ref, std::string& local, std::string& e) { return resolveWaveform(ref, local, e); }, *s->expanded, error)) return nullptr;
        }
        const pcm::SoundData& play = s->expanded ? *s->expanded : *s->data;
        s->samples = loadSamples(play, [&](const char* ref, Record& r, std::string& e) { return resolveSample(ref, library, path, r, e); }, decodeWhole, error);
        if (!s->samples) return nullptr;
        s->compiled.reset(new pcm::Compiled);
        if (!pcm::compile(play, s->samples->views.data(), *s->compiled, why, sizeof why)) { error = why; return nullptr; }
        return s;
    }
    // What the prepared sounds hold, each once (another instance sharing it counts it too): decoded audio (heads, loop
    // bodies, whole samples), and of their packs each pack's index and each record's frame table.
    uint64_t assetBytes() const {
        std::set<const void*> seen;
        uint64_t n = 0;
        for (const auto& c : contexts)
            if (c.prepared && c.patch.sound && c.patch.sound->samples) {
                for (const auto& b : c.patch.sound->samples->pcm) if (seen.insert(b.get()).second) n += b->size() * sizeof(int16_t);
                for (const auto& r : c.patch.sound->samples->records) {
                    if (r.pack && seen.insert(r.pack.get()).second) n += r.pack->index.size() * sizeof(sp_index_entry);
                    if (r.table && seen.insert(r.table.get()).second) n += r.table->size() * sizeof(uint32_t);
                }
            }
        return n;
    }

    // ---- notes -------------------------------------------------------------------------
    // A note-on the plugin cannot start still ends (the host holds a slot for it until then),
    // with the fields it came with.
    void endNoteAs(const clap_event_note_t& n) {
        if (!output) return;
        clap_event_note_t end{};
        end.header = {sizeof(end), eventTime, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_END, 0};
        end.note_id = n.note_id; end.port_index = n.port_index; end.channel = n.channel; end.key = n.key;
        output->try_push(output, &end.header);
    }
    void noteOn(const clap_event_note_t& n) {
        if (n.port_index != 0 || n.channel < 0 || n.channel > 15 || n.key < 0 || n.key > 127) return endNoteAs(n);
        Base::noteOn(n);
    }
    void noteOff(const clap_event_note_t& n, bool choke = false, bool midi = false) {
        Base::noteOff(n, choke, midi);
        pedalHeld();
    }
    // A key that went up while the sustain pedal holds its note: the core starts the half damper
    // releases (the SDK keeps the note itself until the pedal goes up; its key-off elements start then)
    void pedalHeld() {
        for (uint32_t i = 0; i < kVoices; ++i) {
            const Voice& v = voices[i];
            if (!v.active || v.held) { keyUpSent[i] = false; continue; }
            if (!keyUpSent[i] && sustainDown[v.channel]) { core->keyUp(i); keyUpSent[i] = true; }
        }
    }
    bool accepts(const Patch& p, const clap_event_note_t& n) {
        for (uint32_t i = 0; i < kContexts; ++i)
            if (&contexts[i].patch == &p) return core->keySounds(i, n.key, n.velocity);
        return false;
    }
    uint32_t index(const Voice& v) const { return uint32_t(&v - voices.data()); }
    void start(Voice& v, Context& c) {
        const uint32_t i = index(v);
        const uint32_t slot = uint32_t(&c - contexts.data());
        // key assign single: the sound's notes already on this key (held, released or held by the pedal) fade out over the
        // choke time as a give-up does, and end then
        if (!core->multiKeys(slot))
            for (uint32_t k = 0; k < kVoices; ++k)
                if (k != i && voices[k].active && voices[k].context == slot && voices[k].key == v.key && !voices[k].chokeLength) {
                    fadeOut(int(k));
                    core->choke(k, chokeFrames);
                    choking[k] = true;
                }
        makeRoom(c, i);
        core->noteOn(i, slot, v.channel, v.key, v.velocity);
    }
    void release(Voice& v) { core->noteOff(index(v)); }
    // Every way a note's voice ends (released out, choked, stolen, reset): free the core's slot.
    void voiceFinished(Voice& v) { core->kill(index(v)); choking[index(v)] = false; keyUpSent[index(v)] = false; }
    double loudness(const Voice& v) const { return core->voice(index(v)).level; }
    void applyExpression(const clap_event_note_expression_t& e) {
        Base::applyExpression(e);
        if (e.port_index > 0) return;
        if (e.expression_id == CLAP_NOTE_EXPRESSION_PRESSURE) {  // the channel's pressure (a box source)
            if (e.channel >= 0 && e.channel < 16) core->setSource(e.channel, 2, e.value);
            return;
        }
        if (e.expression_id != CLAP_NOTE_EXPRESSION_VOLUME) return;
        for (uint32_t i = 0; i < kVoices; ++i) if (matches(voices[i], e.note_id, e.key, e.channel)) core->setVolume(i, voices[i].volume);
    }
    void applyMidi(const clap_event_midi_t& e) {
        Base::applyMidi(e);
        if (e.port_index) return;
        const int ch = e.data[0] & 15, kind = e.data[0] & 0xf0;
        if (kind == 0xe0) core->setPitchBend(int16_t(ch), channelBend[ch]);
        if (kind == 0xb0 && e.data[1] == 1) core->setModWheel(int16_t(ch), channelModWheel[ch]);
        // the controller boxes' other sources: breath (CC 2), foot controller 1 (CC 11), channel pressure
        if (kind == 0xb0 && e.data[1] == 2) core->setSource(int16_t(ch), 7, (e.data[2] & 127) / 127.0);
        if (kind == 0xb0 && e.data[1] == 11) core->setSource(int16_t(ch), 3, (e.data[2] & 127) / 127.0);
        if (kind == 0xd0) core->setSource(int16_t(ch), 2, (e.data[1] & 127) / 127.0);
        if (kind == 0xb0 && e.data[1] == 64) core->setSustain(int16_t(ch), (e.data[2] & 127) / 127.0);
        pedalHeld();
    }
    // A sound-context snapshot load (the helper's loadSnapshot calls this): sound stops at once,
    // and a note that was sounding still gets its NOTE_END at the start of the next block
    // (there is no output queue now).
    void resetVoices() {
        for (const auto& v : voices)
            if (v.active && owedCount < owed.size()) owed[owedCount++] = {v.noteId, v.channel, v.key};
        core->reset();
        choking.fill(false);
        Base::resetVoices();
    }
    // CLAP reset: immediate and complete. Every voice ends silently, every note id is forgotten
    // (also the ones a snapshot load still owed an end), and the next block says nothing about them.
    void silence() {
        owedCount = 0;
        core->reset();
        choking.fill(false);
        Base::resetVoices();
    }

    // ---- oscairn.voice-budget -----------------------------------------------------------
    // Past the host's limits and the device caps, voices are given up in voice-budget.h's order
    // (the SDK's victim(), and past a lowered limit what importance listed first) and fade out
    // over the choke time. A unit is a note, which the core gives up only whole. Audio thread only.
    static void setLimits(const clap_plugin_t* p, uint32_t limit, const oscairn_voice_budget_context_t* list, uint32_t count) {
        self(p).budget.set(limit, list, count);
    }
    static uint32_t activeVoices(const clap_plugin_t* p, double* cost) {
        auto& s = self(p);
        if (cost) *cost = s.voiceCost();
        return s.core->voiceCount();
    }
    // One voice's estimated cost (microseconds a block): the costliest note of the sounds in use, from
    // their plans, before any note has sounded.
    double voiceCost() const {
        double worst = 0;
        for (uint32_t i = 0; i < kContexts; ++i)
            if (contexts[i].prepared) worst = std::max(worst, core->cost(i).voiceMicros);
        return worst;
    }
    uint32_t instanceLimit() const { return tighter(budget.limit, deviceCap); }
    oscairn_voice_budget_context_t soundLimits(const Context& c) const {
        auto own = budget.of(c.id, c.selected.load(std::memory_order_relaxed) != 0);
        own.limit = tighter(tighter(own.limit, soundCap), uint32_t(std::lround(clampParam(kPolyphony, c.patch.p[kPolyphony].get()))));
        return own;
    }
    struct Described {
        std::array<oscairn::voices::Candidate, kVoices> c{};
        std::array<bool, kVoices> present{};
        uint32_t keeping(uint32_t id = oscairn::voices::none) const {
            uint32_t n = 0;
            for (uint32_t i = 0; i < kVoices; ++i) n += present[i] && !c[i].ending && (id == oscairn::voices::none || c[i].context == id);
            return n;
        }
    };
    // Every voice but `except`, as the order sees it: one Candidate a note, its weight the layers it runs. Held: its key
    // is down; pedaled: its key is up and the sustain pedal holds it. Its loudness is its gain at the output (the sound's
    // level included) and its age the note-on serial; ageMs is from the host's steady_time, at eventTime.
    void describe(Described& d, uint32_t except) const {
        for (uint32_t i = 0; i < kVoices; ++i) {
            const auto& v = voices[i];
            d.present[i] = v.active && i != except;
            if (!d.present[i]) continue;
            const auto& c = contexts[v.context];
            const auto o = core->output(i);
            const double ageMs = steadyTime < 0 || v.startedAt < 0 ? -1.0 : double(steadyTime + eventTime - v.startedAt) * 1000.0 / voiceRate;
            d.c[i] = {c.id, c.selected.load(std::memory_order_relaxed) != 0, v.held, v.chokeLength != 0, false, o.gain, v.started, v.pedaled, ageMs, float(o.layers)};
        }
    }
    int victim(const Described& d, uint32_t incoming, uint32_t only = oscairn::voices::none) const {
        return oscairn::voices::victim(kVoices, [&](size_t i) { return d.present[i] ? &d.c[i] : nullptr; }, budget, incoming, only);
    }
    // Past a lowered limit: the notes the last importance report listed, in its order, while they sound; then victim()'s.
    int listed(const Described& d, uint32_t only = oscairn::voices::none) {
        return importanceReport.next(kVoices, [&](size_t i) { return d.present[i] ? &d.c[i] : nullptr; }, budget, only);
    }
    // To make room for a note in `incoming`: a listed note first, as listed() gives it; when none is left (next() would fall back
    // to victim() without the incoming note), victim()'s with it.
    int roomVictim(const Described& d, uint32_t incoming, uint32_t only) {
        const int i = listed(d, only);
        if (i >= 0)
            for (uint32_t k = 0; k < importanceReport.count; ++k) {
                const auto& l = importanceReport.lists[k];
                for (uint32_t e = 0; e < l.out.count; ++e)
                    if (l.least[e].index == size_t(i) && l.least[e].c.age == d.c[size_t(i)].age && l.least[e].c.context == d.c[size_t(i)].context) return i;
            }
        return victim(d, incoming, only);
    }
    // Draft 2, after each block: each sound's least important notes (importanceReport is the helper's, kept for next()).
    static uint32_t importance(const clap_plugin_t* p, oscairn_voice_budget_importance_t* lists, uint32_t capacity) {
        auto& s = self(p);
        Described d; s.describe(d, kVoices);
        s.importanceReport.list(kVoices, [&](size_t i) { return d.present[i] ? &d.c[i] : nullptr; }, s.budget);
        return s.importanceReport.write(lists, capacity);
    }
    bool giveUp(Described& d, int i) {
        if (i < 0) return false;
        fadeOut(i);
        core->choke(uint32_t(i), chokeFrames);
        choking[size_t(i)] = true;
        d.c[size_t(i)].ending = true;
        return true;
    }
    // Before a note starts in `c`: room for its voice within the limits.
    void makeRoom(const Context& c, uint32_t self_) {
        Described d; describe(d, self_);
        const auto own = soundLimits(c);
        while (own.limit && d.keeping(c.id) + 1 > own.limit && giveUp(d, roomVictim(d, c.id, c.id))) {}
        const uint32_t cap = instanceLimit();
        while (cap && d.keeping() + 1 > cap && giveUp(d, roomVictim(d, c.id, oscairn::voices::none))) {}
    }
    // At a block's start: owed NOTE_ENDs, then voices past a limit set_limits lowered fade out.
    void keepWithinLimits() {
        for (uint32_t i = 0; i < owedCount; ++i) {
            clap_event_note_t n{}; n.note_id = owed[i].id; n.channel = owed[i].channel; n.key = owed[i].key;
            endNoteAs(n);
        }
        owedCount = 0;
        if (selected[0] != shownSlot) selectedGlobally(contexts[selected[0]], output);  // channel 0 was given another sound
        Described d; describe(d, kVoices);
        for (uint32_t slot = 0; slot < kContexts; ++slot) {
            const auto& c = contexts[slot];
            if (!c.users.load(std::memory_order_acquire)) continue;
            const auto own = soundLimits(c);
            while (own.limit && d.keeping(c.id) > own.limit && giveUp(d, listed(d, c.id))) {}
        }
        const uint32_t cap = instanceLimit();
        while (cap && d.keeping() > cap && giveUp(d, listed(d))) {}
    }

    // ---- sleeping -----------------------------------------------------------------------
    // Audio thread, no locks: a block with no events would make no sound and change nothing but the clock, which process
    // catches up from steady_time. False while any block work is left: a voice in any context (fading and released ones
    // too, and the core's tails and held-back layers), a queued selection, a NOTE_END owed or a selection to show, a
    // discarded sound the main thread still has to let go of, a snapshot load. Streams need nothing from the audio
    // thread (the I/O thread takes released rings back on its own) and the shed counter is read, never pushed.
    bool asleep() const {
        uint32_t voicesLeft = 0;
        for (const auto& c : contexts) voicesLeft += c.voices.load(std::memory_order_relaxed);
        return !voicesLeft && core->idle() && !owedCount && selected[0] == shownSlot
            && selectionHead.load(std::memory_order_acquire) == selectionTail.load(std::memory_order_relaxed)
            && !dropsPending.load(std::memory_order_acquire) && !loading.load(std::memory_order_acquire);
    }
    // Any thread: the host calls process again (it is not calling while asleep). The main-thread changes that give the
    // block work and that the host cannot see ask for it: a snapshot load (owed NOTE_ENDs, the shown sound), a state
    // load, a discarded sound (dropsPending, with the callback process then asks for). The host itself processes the
    // block after an accepted select or a queued chokeRetiring.
    void wake() const { if (host && host->request_process) host->request_process(host); }
    void snapshotLoading(bool on) { loading.store(on, std::memory_order_release); if (!on) wake(); }

    // ---- rendering ----------------------------------------------------------------------
    void render(const clap_process_t* pr, uint32_t from, uint32_t to) {
        for (uint32_t i = 0; i < kVoices; ++i)  // a choke (NOTE_CHOKE or the helper's own) fades in the core too
            if (voices[i].active && voices[i].chokeLength && !choking[i]) {
                core->choke(i, std::max<uint32_t>(1, voices[i].chokeLeft));
                choking[i] = true;
            }
        float* const* out = pr->audio_outputs_count ? pr->audio_outputs[0].data32 : nullptr;
        if (!out || !out[0] || !out[1]) return;
        // ports 1-8 where the host gives and activated them, else their sound joins the main port; then the ring-out
        // buffer's two pairs (oscairn.sound-snapshots), else the main port
        float* left[pcm::kOutputs]; float* right[pcm::kOutputs];
        for (uint32_t k = 0; k < kPorts; ++k) {
            float* const* o = k < pr->audio_outputs_count ? pr->audio_outputs[k].data32 : nullptr;
            const bool own = o && o[0] && o[1] && pr->audio_outputs[k].channel_count >= 2 && (k == 0 || portActive[k].load(std::memory_order_relaxed));
            left[k] = (own ? o[0] : out[0]) + from; right[k] = (own ? o[1] : out[1]) + from;
            if (own) { std::memset(o[0] + from, 0, (to - from) * sizeof(float)); std::memset(o[1] + from, 0, (to - from) * sizeof(float)); }
        }
        for (uint32_t k = 0; k < 2; ++k) {
            left[kPorts + k] = ringOut ? ringOut[2 * k] + from : left[0];
            right[kPorts + k] = ringOut ? ringOut[2 * k + 1] + from : right[0];
        }
        // Each sound in use on its bus. Port n is sound bus n, except while one sound alone is selected on the channels
        // and it plays on bus 0: then a sound that splits its outputs sends its elements to 1 (insertion A) and 2
        // (insertion B). A retiring sound's main output rings out.
        bool alone = true;
        for (uint32_t ch = 1; ch < 16; ++ch) alone &= selected[ch] == selected[0];
        for (uint32_t slot = 0; slot < kContexts; ++slot) {
            if (!contexts[slot].users.load(std::memory_order_acquire)) continue;
            const uint32_t bus = uint32_t(clampParam(kBus, contexts[slot].patch.p[kBus].get()));
            const auto* ring = bus ? nullptr : retiring(slot);
            core->route(slot, ring ? kPorts + uint32_t(ring != ringOut) : bus, alone && !bus);
        }
        rendered = true;
        core->render(left, right, to - from, from);
        for (uint32_t i = 0; i < kVoices; ++i)  // a voice the core finished: its note ends here
            if (voices[i].active && !core->voice(i).sounding) {
                const auto at = eventTime; eventTime = to ? to - 1 : 0;
                finishVoice(voices[i]);
                eventTime = at;
            }
    }

    // ---- parameters ---------------------------------------------------------------------
    template <class F> void forTargets(int16_t channel, F&& f) {
        uint32_t seen = 0;
        for (int ch = 0; ch < 16; ++ch) {
            if (channel >= 0 && channel < 16 && channel != ch) continue;
            const auto slot = selected[ch];
            if (!(seen >> slot & 1)) { seen |= 1u << slot; f(slot); }
        }
    }
    void applyParam(const clap_event_header_t* h) {
        if (h->type == CLAP_EVENT_PARAM_MOD) {  // a destination's modulation, raw steps; not saved, not shown as its value
            const auto* e = reinterpret_cast<const clap_event_param_mod_t*>(h);
            if (e->param_id < kDest0 || e->param_id > kDestLast || !std::isfinite(e->amount) || e->note_id >= 0 || e->key >= 0) return;  // global only
            forTargets(e->channel, [&](uint32_t slot) { core->modParam(slot, Param(e->param_id), e->amount); });
            return;
        }
        if (h->type != CLAP_EVENT_PARAM_VALUE) return;
        const auto* e = reinterpret_cast<const clap_event_param_value_t*>(h);
        if (e->param_id >= kParamCount || !std::isfinite(e->value)) return;
        const auto id = Param(e->param_id); const float x = clampParam(id, e->value);
        forTargets(e->channel, [&](uint32_t slot) {
            contexts[slot].patch.p[id].set(x);
            core->setParam(slot, id, x);
            if (slot == selected[0]) values[id].store(x, std::memory_order_relaxed);
        });
    }
    // After a selection for every channel: the parameters show the selected sound's values.
    void selectedGlobally(Context& next, const clap_output_events_t* out) {
        shownSlot = uint32_t(&next - contexts.data());
        for (uint32_t i = 0; i < kParamCount; ++i) {
            const float v = next.patch.p[i].get();
            if (v == values[i].load(std::memory_order_relaxed)) continue;
            values[i].store(v, std::memory_order_relaxed);
            if (!out) continue;
            clap_event_param_value_t ev{};
            ev.header = {sizeof(ev), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
            ev.param_id = i; ev.note_id = -1; ev.port_index = -1; ev.channel = -1; ev.key = -1; ev.value = v;
            out->try_push(out, &ev.header);
        }
    }

    // oscairn.sound-snapshots draft 4: a sound saved on its own plays on the main port; its values, every writable one
    void alone(Patch& p) { p.p[kBus].set(0); }
    uint32_t patchValues(const Patch& p, oscairn_sound_parameter_t* out, uint32_t capacity) {
        for (uint32_t i = 0; i < kParamCount && i < capacity; ++i) out[i] = {i, p.p[i].get()};
        return kParamCount;
    }

    // ---- patches and state --------------------------------------------------------------
    // The shell's values, then the base (library and folder, quoted) and the blob in base64 ("-": no sound).
    bool writePatch(std::ostream& out, const Patch& p) {
        for (uint32_t i = 0; i < kParamCount; ++i) out << ' ' << p.p[i].get();
        if (!p.sound) out << " \"\" \"\" -";
        else out << ' ' << std::quoted(p.sound->library) << ' ' << std::quoted(p.sound->path) << ' ' << base64(p.sound->blob);
        return bool(out);
    }
    // The knobs a patch does not set start at its sound's stored values.
    static void knobsFromSound(Patch& p, const bool* given) {
        for (int k = 0; k < 8; ++k)
            if (!given[kKnob1 + k]) p.p[kKnob1 + k].set(p.sound && p.sound->data ? float(p.sound->data->part[pcm::PartKnob1 + k]) / 1023.f : float(kKnobDefault));
    }
    bool readPatch(std::istream& in, Patch& p, uint32_t count = kParamCount) {  // count: the values a state 1 holds (3)
        double x[kParamCount];
        for (uint32_t i = 0; i < kParamCount; ++i) x[i] = kParams[i].def;
        for (uint32_t i = 0; i < count; ++i) if (!(in >> x[i]) || !validValue(Param(i), x[i])) return false;
        std::string library, path, b64;
        if (!(in >> std::quoted(library) >> std::quoted(path) >> b64)) return false;
        std::shared_ptr<const Sound> sound;
        if (b64 != "-") {
            std::vector<uint8_t> blob; std::string error;
            if (library.size() >= 64 || path.size() >= 1024 || !unbase64(b64, blob)) return false;
            if (blob.size() < 4 || std::memcmp(blob.data(), "OSMP", 4)) {  // a hand-written state may carry the text form
                pcm::SoundData d; const std::string text(blob.begin(), blob.end());
                if (!pcm::readText(text.c_str(), d, nullptr, 0)) return false;
                blob.resize(pcm::writeBlob(d, nullptr, 0));
                pcm::writeBlob(d, blob.data(), blob.size());
            }
            if (!(sound = buildSound(std::move(blob), library, path, error))) return false;
        }
        for (uint32_t i = 0; i < kParamCount; ++i) p.p[i].set(clampParam(Param(i), x[i]));
        p.sound = sound;
        bool given[kParamCount];
        for (uint32_t i = 0; i < kParamCount; ++i) given[i] = i < count;
        knobsFromSound(p, given);
        return true;
    }
    void loadAll() { for (uint32_t i = 0; i < kContexts; ++i) if (contexts[i].prepared) core->loadPatch(i, contexts[i].patch); }
    void restored() {
        loadAll();
        shownSlot = selected[0];
        for (uint32_t i = 0; i < kParamCount; ++i) values[i].store(contexts[selected[0]].patch.p[i].get(), std::memory_order_relaxed);
    }
    // Main thread, concurrently with process(): the sound is the engine's, not yet selected.
    bool buildPreset(Context& c, std::string&) { core->loadPatch(uint32_t(&c - contexts.data()), c.patch); return true; }
    // Main thread, its last voice ended and unselected: its samples go now, not when the slot is reused.
    void discarded(Context& c) {
        c.patch.sound.reset();
        const uint32_t i = uint32_t(&c - contexts.data());
        if (!core->drop(i)) { dropping[i] = true; dropsPending.store(true, std::memory_order_release); wake(); }
    }

    static bool saveState(const clap_plugin_t* p, const clap_ostream_t* stream) {
        auto& s = self(p);
        std::ostringstream out; out.imbue(std::locale::classic()); out << std::setprecision(9) << kStateHeader << '\n';
        s.writePatch(out, s.contexts[0].patch); out << '\n';
        const auto data = out.str();
        for (size_t at = 0; at < data.size();) {
            const auto n = stream->write(stream, data.data() + at, data.size() - at);
            if (n <= 0 || size_t(n) > data.size() - at) return false;
            at += size_t(n);
        }
        return true;
    }
    static bool loadState(const clap_plugin_t* p, const clap_istream_t* stream) {
        auto& s = self(p);
        std::string data; char buf[4096];
        for (;;) {
            const auto n = stream->read(stream, buf, sizeof buf);
            if (n < 0 || n > int64_t(sizeof buf) || data.size() + size_t(n) > kMaxStateBytes) return false;
            if (!n) break;
            data.append(buf, size_t(n));
        }
        std::istringstream in(data); in.imbue(std::locale::classic());
        std::string line;
        if (!std::getline(in, line) || (line != kStateHeader && line != kStateHeader2 && line != kStateHeader1)) return false;
        Patch patch;
        if (!s.readPatch(in, patch, line == kStateHeader ? kParamCount : line == kStateHeader2 ? kBus : kKnob1)) return false;
        in >> std::ws;
        if (!in.eof()) return false;
        for (uint32_t i = 0; i < kParamCount; ++i) {
            s.contexts[0].patch.p[i].set(patch.p[i].get());
            if (s.contexts[0].selected.load() & 1) s.values[i].store(patch.p[i].get(), std::memory_order_relaxed);
        }
        s.contexts[0].patch.sound = patch.sound;
        s.core->loadPatch(0, patch);
        select(p, 0, -1);  // the state's sound plays on every channel (from the next block)
        s.wake();
        return true;
    }

    // ---- sound contexts: prepare ---------------------------------------------------------
    // Patch text, line by line: an optional "shell" line of the shell's values (level=, pan=,
    // polyphony=), then the sound: its text form (core/sound.cpp) or "oscairn-sampler-blob/0 <base64>".
    // Sample references resolve from `base` through the host's oscairn.assets.
    bool givenShell[kParamCount] = {};  // parsePatchText's: the values its shell line named
    std::shared_ptr<const Sound> parsePatchText(const char* text, const oscairn_asset_ref_t* base, Patch& patch, std::string& error) {
        std::istringstream in(text); in.imbue(std::locale::classic());
        std::string line, sound;
        int n = 0, shellLine = 0;
        bool given[kParamCount] = {};
        while (std::getline(in, line)) {
            ++n;
            std::istringstream ls(line); std::string first;
            if (!(ls >> first) || first != "shell") { sound += line + "\n"; continue; }
            shellLine = n;
            std::string tok;
            while (ls >> tok) {
                const auto eq = tok.find('=');
                uint32_t id = kParamCount;
                for (uint32_t i = 0; i < kParamCount; ++i) if (tok.compare(0, eq, kParams[i].key) == 0 && eq == std::strlen(kParams[i].key)) id = i;
                if (id == kParamCount || eq == std::string::npos) { error = "line " + std::to_string(n) + ": unknown or malformed '" + tok + "'"; return nullptr; }
                double x;
                if (!cDec(tok.c_str() + eq + 1, x) || !validValue(Param(id), x)) { error = "line " + std::to_string(n) + ": bad value in '" + tok + "'"; return nullptr; }
                patch.p[id].set(clampParam(Param(id), x));
                given[id] = true;
            }
        }
        (void)shellLine;
        std::copy(given, given + kParamCount, givenShell);
        if (sound.find_first_not_of(" \t\r\n") == std::string::npos) return nullptr;  // no sound: plays nothing
        std::vector<uint8_t> blob;
        std::istringstream ss(sound); std::string head, b64;
        ss >> head;
        if (head == kBlobHeader) {
            ss >> b64;
            if (!unbase64(b64, blob)) { error = "line 1: bad base64"; return nullptr; }
        } else {
            pcm::SoundData d; char why[256] = {};
            if (!pcm::readText(sound.c_str(), d, why, sizeof why)) { error = why; return nullptr; }
            blob.resize(pcm::writeBlob(d, nullptr, 0));
            pcm::writeBlob(d, blob.data(), blob.size());
        }
        auto s = buildSound(std::move(blob), base ? base->library : "", base ? base->path : "", error);
        if (!s && error.rfind("line", 0) != 0) error = "line 1: " + error;
        return s;
    }
    static uint32_t prepare(const clap_plugin_t* p, const char* name, const oscairn_sound_parameter_t* values, uint32_t count,
                            const char* text, const oscairn_asset_ref_t* base, char* message, uint32_t capacity) {
        auto fail = [&](const std::string& why) { if (message && capacity) std::snprintf(message, capacity, "%s", why.c_str()); return CLAP_INVALID_ID; };
        if (!name || std::strlen(name) > 63) return fail("invalid sound name");
        auto& s = self(p);
        Patch patch; std::string error;
        bool given[kParamCount] = {};
        if (text) {
            patch.sound = s.parsePatchText(text, base, patch, error);
            if (!error.empty()) return fail(error);
            std::copy(s.givenShell, s.givenShell + kParamCount, given);
        }
        for (uint32_t i = 0; i < count; ++i) {
            if (!values || values[i].id >= kParamCount || !validValue(Param(values[i].id), values[i].value)) return fail("invalid parameter");
            patch.p[values[i].id].set(clampParam(Param(values[i].id), values[i].value));
            given[values[i].id] = true;
        }
        knobsFromSound(patch, given);
        auto* c = s.freeSlot();
        if (!c) return fail("no free sound context");
        c->patch = patch;
        s.core->loadPatch(uint32_t(c - s.contexts.data()), patch);
        return s.publish(*c, name);
    }

    // ---- oscairn.asset-dependencies: every waveform and direct sample of every prepared sound ----------
    // An id reference ("waveform:<id>", "sample:<id>") has the library empty and the id as its sha256; the samples a
    // waveform names are the host's to derive from its file.
    std::vector<oscairn_asset_ref_t> dependencies() const {
        std::vector<oscairn_asset_ref_t> v;
        std::set<std::pair<std::string, std::string>> seen;
        auto add = [&](const std::string& library, const std::string& path, const char* id) {
            oscairn_asset_ref_t a{};
            std::snprintf(a.library, sizeof a.library, "%s", library.c_str());
            std::snprintf(a.path, sizeof a.path, "%s", path.c_str());
            if (id) std::snprintf(a.sha256, sizeof a.sha256, "%s", id);
            if (seen.insert({a.library, a.path}).second) v.push_back(a);
        };
        for (const auto& c : contexts) {
            if (!c.prepared || !c.patch.sound) continue;
            const Sound& s = *c.patch.sound;
            for (uint32_t i = 0; i < s.data->waveforms; ++i) {
                char id[65];
                for (int k = 0; k < 32; ++k) std::snprintf(id + 2 * k, 3, "%02x", s.data->waveform[i][k]);
                add("", std::string("waveform:") + id, id);
            }
            for (uint32_t i = 0; i < s.data->samples; ++i) {
                const char* ref = s.data->ref[i];
                if (!std::strncmp(ref, "sample:", 7)) add("", ref, ref + 7);
                else add(s.library, (s.path.empty() ? "" : s.path + "/") + ref, nullptr);
            }
        }
        return v;
    }
    static uint32_t dependencyCount(const clap_plugin_t* p) { return uint32_t(self(p).dependencies().size()); }
    static bool dependencyGet(const clap_plugin_t* p, uint32_t i, oscairn_asset_ref_t* ref) {
        const auto v = self(p).dependencies();
        if (i >= v.size() || !ref) return false;
        *ref = v[i];
        return true;
    }

    // ---- CLAP plugin ----------------------------------------------------------------------
    static bool init(const clap_plugin_t* p) {
        auto& s = self(p);
        if (s.host && s.host->get_extension) {
            s.assets = static_cast<const oscairn_host_assets_t*>(s.host->get_extension(s.host, OSCAIRN_EXT_ASSETS));
            s.assets4 = s.assets != nullptr;
            if (!s.assets) s.assets = static_cast<const oscairn_host_assets_t*>(s.host->get_extension(s.host, OSCAIRN_EXT_ASSETS_DRAFT_3));
        }
        return true;
    }
    static void destroy(const clap_plugin_t* p) { delete &self(p); }
    static bool activate(const clap_plugin_t* p, double rate, uint32_t, uint32_t maxFrames) {
        auto& s = self(p);
        s.sampleRate = rate; s.maxBlock = maxFrames;
        s.setSampleRate(rate);
        s.core->prepare(rate, maxFrames);
        s.loadAll();
        s.slept = false; s.renderedEnd = -1;
        return true;
    }
    static void deactivate(const clap_plugin_t*) {}
    static bool startProcessing(const clap_plugin_t*) { return true; }
    static void stopProcessing(const clap_plugin_t*) {}
    static void reset(const clap_plugin_t* p) { self(p).silence(); }
    // the discarded sounds the core could not let go of yet (prepared again: the new sound replaced them)
    static void onMainThread(const clap_plugin_t* p) {
        auto& s = self(p);
        bool left = false;
        for (uint32_t i = 0; i < kContexts; ++i)
            if (s.dropping[i]) left |= s.dropping[i] = !s.contexts[i].prepared && !s.core->drop(i);
        s.dropsPending.store(left, std::memory_order_release);
    }
    // Frames the host skipped while this returned SLEEP move the core's clock on (the part LFOs' free-running phase) from
    // steady_time; without one (-1) the clock stops while asleep. A block that returned early is one of the skipped.
    static clap_process_status process(const clap_plugin_t* p, const clap_process_t* pr) {
        auto& s = self(p);
        const int64_t now = pr->steady_time;
        if (now < 0) s.renderedEnd = -1;
        else if (s.renderedEnd >= 0) {
            if (s.slept && now > s.renderedEnd) s.core->advance(uint64_t(now - s.renderedEnd));
            s.renderedEnd = now;
        }
        s.rendered = false;
        const auto status = Base::process(p, pr);
        if (s.rendered) s.renderedEnd = now >= 0 ? now + pr->frames_count : -1;
        s.slept = status == CLAP_PROCESS_SLEEP;
        if (s.dropsPending.load(std::memory_order_relaxed) && s.host && s.host->request_callback) s.host->request_callback(s.host);
        return status;
    }

    static uint32_t paramCount(const clap_plugin_t*) { return kParamCount + 1; }
    static bool paramInfo(const clap_plugin_t*, uint32_t i, clap_param_info_t* info) {
        if (i == kShedsParam) {
            *info = {};
            info->id = i; info->flags = CLAP_PARAM_IS_READONLY | CLAP_PARAM_IS_STEPPED;
            std::snprintf(info->name, sizeof info->name, "Late Stream Sheds");
            info->max_value = 4294967295.0;
            return true;
        }
        if (i >= kParamCount) return false;
        *info = {};
        info->id = i;
        info->flags = CLAP_PARAM_IS_AUTOMATABLE | (kParams[i].stepped ? CLAP_PARAM_IS_STEPPED : 0) | (i >= kDest0 && i <= kDestLast ? CLAP_PARAM_IS_MODULATABLE : 0);
        std::snprintf(info->name, sizeof info->name, "%s", kParams[i].name);
        info->min_value = kParams[i].min; info->max_value = kParams[i].max; info->default_value = kParams[i].def;
        return true;
    }
    static bool paramValue(const clap_plugin_t* p, clap_id id, double* v) {
        if (id == kShedsParam) { *v = double(self(p).core->streamSheds()); return true; }
        if (id >= kParamCount) return false;
        *v = self(p).values[id].load(std::memory_order_relaxed);
        return true;
    }
    static bool paramText(const clap_plugin_t*, clap_id id, double v, char* out, uint32_t n) {
        if (id > kShedsParam) return false;
        std::ostringstream o; o.imbue(std::locale::classic());
        o << std::fixed << std::setprecision(id == kShedsParam || kParams[id].stepped ? 0 : 3) << v;
        std::snprintf(out, n, "%s", o.str().c_str());
        return true;
    }
    static bool paramFromText(const clap_plugin_t*, clap_id id, const char* text, double* v) {
        if (id > kShedsParam) return false;
        std::istringstream in(text); in.imbue(std::locale::classic());  // a number at the start, as before: text after it (a unit) is ignored
        in >> *v;
        return !in.fail();
    }
    static void paramFlush(const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t*) {
        auto& s = self(p);
        for (uint32_t i = 0, n = in->size(in); i < n; ++i) s.applyParam(in->get(in, i));
    }

    // oscairn.ui: every parameter has a hint. Roles for the role banks (level, pan, cutoff, resonance, element level 1);
    // the knobs are one group, the ribbon and the switches another (the switches as switches), the tunings and the
    // high-pass cutoff a third.
    static bool paramHint(const clap_plugin_t*, clap_id id, oscairn_param_hint_t* h) {
        if (id > kShedsParam || !h) return false;
        *h = {};
        h->range_high = CLAP_INVALID_ID;
        switch (id) {
        case kLevel: h->role = OSCAIRN_ROLE_LEVEL; h->priority = 1; break;
        case kPan: h->role = OSCAIRN_ROLE_PAN; break;
        case kSwitch1: case kSwitch2: h->widget = OSCAIRN_WIDGET_SWITCH; h->group = 2; break;
        case kRibbon: h->group = 2; break;
        case kDestCutoff: h->role = OSCAIRN_ROLE_CUTOFF; h->priority = 2; break;
        case kDestResonance: h->role = OSCAIRN_ROLE_RESONANCE; h->priority = 3; break;
        case kDestLevel: h->role = OSCAIRN_ROLE_LEVEL_1; break;
        case kDestCoarse: case kDestFine: case kDestHpf: h->group = 3; break;
        case kDestFegDepth: h->role = OSCAIRN_ROLE_FILTER_ENVELOPE; h->priority = 4; break;
        case kDestElfoPitch: case kDestElfoAmp: h->group = 4; break;
        case kDestAttack: h->role = OSCAIRN_ROLE_ATTACK; h->priority = 5; break;
        case kDestRelease: h->role = OSCAIRN_ROLE_RELEASE; h->priority = 6; break;
        case kBus: h->role = OSCAIRN_ROLE_OUTPUT_BUS; break;
        default: if (id >= kKnob1 && id <= kKnob8) h->group = 1; break;
        }
        return true;
    }

    static bool memoryEstimate(const clap_plugin_t* p, double rate, uint32_t frames, oscairn_memory_t* out) {
        auto& s = self(p);
        const auto m = s.core->memory(rate, frames);
        *out = {m.shared + sizeof(Plugin) + sizeof(Decoder), m.perContext, s.assetBytes()};  // the I/O thread's decoder and read buffer (one a process)
        return true;
    }
    static bool memoryCurrent(const clap_plugin_t* p, oscairn_memory_t* out) { return memoryEstimate(p, self(p).sampleRate, self(p).maxBlock, out); }

    static const void* getExtension(const clap_plugin_t*, const char* id);
};

// Nine stereo outputs with stable ids: 0 main, n sound bus n (a sound with its output bus at n plays there). While one
// sound alone is selected and on bus 0, 1 and 2 take its elements to insertion A and B instead (with split_outputs set).
const clap_plugin_audio_ports_t kAudioPorts = {
    [](const clap_plugin_t*, bool isInput) -> uint32_t { return isInput ? 0 : kPorts; },
    [](const clap_plugin_t*, uint32_t i, bool isInput, clap_audio_port_info_t* info) {
        if (isInput || i >= kPorts) return false;
        *info = {};
        info->id = i;
        if (i < 3) std::snprintf(info->name, sizeof info->name, "%s", i ? i == 1 ? "Bus 1 / To Insert A" : "Bus 2 / To Insert B" : "Main");
        else std::snprintf(info->name, sizeof info->name, "Bus %u", i);
        info->flags = i == 0 ? CLAP_AUDIO_PORT_IS_MAIN : 0; info->channel_count = 2; info->port_type = CLAP_PORT_STEREO; info->in_place_pair = CLAP_INVALID_ID;
        return true;
    }};
// A port takes its sound only once the host has activated it (clap.audio-ports-activation); until then, or when it is
// deactivated, it plays on the main port, and the port costs nothing.
const clap_plugin_audio_ports_activation_t kPortsActivation = {
    [](const clap_plugin_t*) { return true; },
    [](const clap_plugin_t* p, bool isInput, uint32_t i, bool on, uint32_t) {
        if (isInput || i >= kPorts) return false;
        if (i) Plugin::self(p).portActive[i].store(on, std::memory_order_relaxed);
        return true;
    }};
const clap_plugin_note_ports_t kNotePorts = {
    [](const clap_plugin_t*, bool isInput) -> uint32_t { return isInput ? 1 : 0; },
    [](const clap_plugin_t*, uint32_t i, bool isInput, clap_note_port_info_t* info) {
        if (!isInput || i) return false;
        *info = {};
        info->id = 0; info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI; info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
        std::snprintf(info->name, sizeof info->name, "Notes");
        return true;
    }};
const clap_plugin_params_t kParamsExt = {Plugin::paramCount, Plugin::paramInfo, Plugin::paramValue, Plugin::paramText, Plugin::paramFromText, Plugin::paramFlush};
const clap_plugin_state_t kState = {Plugin::saveState, Plugin::loadState};
const clap_plugin_state_t kSnapshot = {Plugin::saveSnapshot, Plugin::loadSnapshot};
const oscairn_plugin_sound_contexts_t kSoundContexts = {Plugin::capacity, Plugin::get, Plugin::prepare, Plugin::select, Plugin::discard, &kSnapshot, nullptr};
const oscairn_plugin_sound_snapshots_t kSoundSnapshots = {Plugin::prepareSnapshot, Plugin::setRingOut, Plugin::chokeRetiring, Plugin::saveSound, Plugin::soundValues};
const oscairn_plugin_voice_budget_t kVoiceBudget = {Plugin::honorsVolume, Plugin::setLimits, Plugin::activeVoices, Plugin::voiceNotes, Plugin::importance};
const oscairn_plugin_memory_t kMemory = {Plugin::memoryEstimate, Plugin::memoryCurrent};
const oscairn_plugin_asset_dependencies_t kDependencies = {Plugin::dependencyCount, Plugin::dependencyGet, nullptr};
const oscairn_plugin_ui_t kUi = {Plugin::paramHint, nullptr, nullptr};
// This plugin's own extension: streaming's counter and the tests' hooks (stall the I/O thread, wait for it, fail every
// read of a pack, count its passes that found nothing to do).
const struct {
    uint64_t (*sheds)(const clap_plugin_t*);
    void (*stall)(const clap_plugin_t*, bool);
    void (*settle)(const clap_plugin_t*);
    void (*failReads)(const clap_plugin_t*, bool);
    uint64_t (*idlePasses)(const clap_plugin_t*);
} kStreaming = {[](const clap_plugin_t* p) -> uint64_t { return Plugin::self(p).core->streamSheds(); },
                [](const clap_plugin_t*, bool on) { stallStreams(on); }, [](const clap_plugin_t*) { settleStreams(); },
                [](const clap_plugin_t*, bool on) { failReads(on); }, [](const clap_plugin_t*) { return idleStreamPasses(); }};

const void* Plugin::getExtension(const clap_plugin_t*, const char* id) {
    if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &kAudioPorts;
    if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS_ACTIVATION) || !std::strcmp(id, CLAP_EXT_AUDIO_PORTS_ACTIVATION_COMPAT)) return &kPortsActivation;
    if (!std::strcmp(id, CLAP_EXT_NOTE_PORTS)) return &kNotePorts;
    if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &kParamsExt;
    if (!std::strcmp(id, CLAP_EXT_STATE)) return &kState;
    if (!std::strcmp(id, OSCAIRN_EXT_SOUND_CONTEXTS)) return &kSoundContexts;
    if (!std::strcmp(id, OSCAIRN_EXT_SOUND_SNAPSHOTS)) return &kSoundSnapshots;
    if (!std::strcmp(id, OSCAIRN_EXT_VOICE_BUDGET_DRAFT_2) || !std::strcmp(id, OSCAIRN_EXT_VOICE_BUDGET)) return &kVoiceBudget;
    if (!std::strcmp(id, OSCAIRN_EXT_MEMORY)) return &kMemory;
    if (!std::strcmp(id, OSCAIRN_EXT_UI) || !std::strcmp(id, OSCAIRN_EXT_UI_DRAFT_5) || !std::strcmp(id, OSCAIRN_EXT_UI_DRAFT_4)) return &kUi;
    if (!std::strcmp(id, kExtStreaming)) return &kStreaming;
    if (!std::strcmp(id, OSCAIRN_EXT_ASSET_DEPENDENCIES) || !std::strcmp(id, OSCAIRN_EXT_ASSET_DEPENDENCIES_DRAFT_1)) return &kDependencies;
    return nullptr;
}

const clap_plugin_factory_t kFactory = {
    [](const clap_plugin_factory_t*) -> uint32_t { return 1; },
    [](const clap_plugin_factory_t*, uint32_t i) -> const clap_plugin_descriptor_t* { return i ? nullptr : &kDescriptor; },
    [](const clap_plugin_factory_t*, const clap_host_t* h, const char* id) -> const clap_plugin_t* {
        if (!id || std::strcmp(id, kDescriptor.id)) return nullptr;
        return &(new Plugin(h))->plugin;
    }};

}  // namespace
}  // namespace pcmshell

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION, [](const char*) { return true; }, []() {},
    [](const char* id) -> const void* { return !std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? &pcmshell::kFactory : nullptr; }};
