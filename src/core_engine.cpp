// SPDX-License-Identifier: AGPL-3.0-or-later
// engine.h on the DSP core (core/). A sound is compiled on the main thread into a triple buffer per
// sound context, and the audio thread installs the newest one before it renders or starts a note:
// no lock and no data race between the two threads. A staged entry keeps its sound (and so its
// samples) alive; the main thread drops an entry's sound only when it overwrites that entry, which
// is never the installed one, and installing a context's new sound ends the voices of its old one.
// A discarded context publishes no sound, and once that is installed, takes back the entry the audio
// thread left and empties it (drop).
#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>

#include "engine.h"
#include "streamer.h"

namespace pcmshell {

namespace {

// Nanoseconds per instruction on the device (Cortex-A53), measured for this core: 64 voices of the
// pads' plan, thread CPU mean against callgrind's count, 0.99 ns mono and 0.94 ns stereo with the
// 20-tap kernel (10.5 and 13.7 us a voice a block; docs/cost.md)
constexpr double kNanosPerInstruction = 1.0;

class CoreEngine final : public Engine {
public:
    CoreEngine() : core_(new pcm::Core) { attachStreams(core_->streams(), pcm::kStreams); }
    ~CoreEngine() override { detachStreams(core_->streams()); }
    void prepare(double sr, uint32_t maxBlock) override {
        std::lock_guard<std::mutex> g(write_);
        core_->prepare(sr > 0 ? sr : 48000);
        for (auto& c : installed_) c = nullptr;
        for (uint32_t c = 0; c < kContexts; ++c)  // nothing renders now: a discarded context's sounds go at once
            if (dropping_[c]) for (auto& e : staged_[c].buf) e.sound.reset();
        maxBlock_ = maxBlock ? maxBlock : 128;
        for (auto& s : staged_) s.middle.store(1, std::memory_order_relaxed), s.back = 0, s.front = 2;
    }
    // the core is preallocated with the instance; a context holds its staged sounds' compiled form
    // (samples are counted as assets by the shell; the stream pool is in the core)
    EngineMemory memory(double, uint32_t) const override { return {sizeof(CoreEngine) + sizeof(pcm::Core), sizeof(Patch) + sizeof(pcm::Compiled) * 3}; }
    void loadPatch(uint32_t c, const Patch& p) override {
        if (c >= kContexts) return;
        std::lock_guard<std::mutex> g(write_);
        dropping_[c] = false;
        publish(c, p);
    }
    bool drop(uint32_t c) override {
        if (c >= kContexts) return true;
        std::lock_guard<std::mutex> g(write_);
        Staged& s = staged_[c];
        if (!dropping_[c]) { publish(c, Patch{}); dropping_[c] = true; return false; }
        if (s.middle.load(std::memory_order_acquire) & kDirty) return false;  // not installed yet
        publish(c, Patch{});          // the writer gets back the entry the audio thread left
        s.buf[s.back].sound.reset();  // so no entry holds a sound now
        return true;
    }
    bool keySounds(uint32_t c, int16_t key, double vel) override {
        sync();
        if (c >= kContexts || !installed_[c]) return false;
        return pcm::keySounds(*installed_[c], key, std::max(1, int(std::lround(vel * 127))));
    }
    void noteOn(uint32_t v, uint32_t c, int16_t ch, int16_t key, double vel) override {
        sync();
        core_->noteOn(v, c, ch, key, float(vel));
    }
    void noteOff(uint32_t v) override { core_->noteOff(v); }
    void choke(uint32_t v, uint32_t frames) override { core_->choke(v, frames); }
    void kill(uint32_t v) override { core_->kill(v); }
    void setVolume(uint32_t v, double g) override { core_->setVolume(v, float(g)); }
    void setParam(uint32_t c, Param p, double x) override {
        if (c >= kContexts || p >= kParamCount) return;
        sync();  // a sound published before this event is installed first, so the event is not undone
        value_[c][p] = float(x);
        apply(c, p);
    }
    void modParam(uint32_t c, Param p, double a) override {
        if (c >= kContexts || p < kDest0 || p > kDestLast) return;
        sync();
        mod_[c][p - kDest0] = float(a);
        apply(c, p);
    }
    void setPitchBend(int16_t ch, double x) override { core_->setPitchBend(ch, float(x)); }
    void setModWheel(int16_t ch, double x) override { core_->setModWheel(ch, float(x)); }
    void setSource(int16_t ch, int src, double x) override { core_->setSource(ch, src, float(x)); }
    void keyUp(uint32_t v) override { core_->keyUp(v); }
    void setSustain(int16_t ch, double x) override { core_->setSustain(ch, float(x)); }
    void render(float* const* l, float* const* r, uint32_t n, uint32_t from) override { sync(); core_->render(l, r, n, from); }
    uint32_t voiceCount() const override { return core_->voiceCount(); }
    VoiceInfo voice(uint32_t v) const override {
        const auto s = core_->voice(v);
        return {s.sounding, s.releasing, s.level, s.age};
    }
    SoundCost cost(uint32_t c) const override {
        if (c >= kContexts) return {0, 0};
        return {cost_[c].voice.load(std::memory_order_relaxed), cost_[c].rest.load(std::memory_order_relaxed)};
    }
    void reset() override { sync(); core_->reset(); }
    uint64_t streamSheds() const override { return core_->streamSheds(); }
    void setHermite(bool on) override { core_->setInterp(on ? pcm::Interp::Hermite : pcm::Interp::Sinc); }

private:
    static constexpr uint32_t kDirty = 4;
    // main thread, under write_: the sound for context c into the back entry, then published
    void publish(uint32_t c, const Patch& p) {
        Staged& s = staged_[c];
        Entry& e = s.buf[s.back];
        e.sound = p.sound;
        for (uint32_t i = 0; i < kParamCount; ++i) e.value[i] = p.p[i].get();
        const double voice = p.sound && p.sound->compiled ? p.sound->compiled->noteCost : 0;
        const double rest = pcm::estimateCost({1, pcm::Interp::Sinc, pcm::Filter::Thru}).rest;
        cost_[c].voice.store(voice * kNanosPerInstruction * 1e-3 * maxBlock_ / 128, std::memory_order_relaxed);
        cost_[c].rest.store(rest * kNanosPerInstruction * 1e-3 * maxBlock_ / 128, std::memory_order_relaxed);
        s.back = s.middle.exchange(s.back | kDirty, std::memory_order_acq_rel) & 3;
    }

    struct Entry { std::shared_ptr<const Sound> sound; float value[kParamCount] = {}; };
    // a value (and a destination's modulation) to the core: the level and pan, a box source, a destination's steps
    void apply(uint32_t c, Param p) {
        if (p == kLevel || p == kPan) core_->setGain(c, value_[c][kLevel], value_[c][kPan]);
        else if (p >= kKnob1 && p <= kKnob8) core_->setControl(c, 8 + int(p - kKnob1), value_[c][p]);
        else if (p == kRibbon) core_->setControl(c, 6, value_[c][p]);
        else if (p == kSwitch1 || p == kSwitch2) core_->setControl(c, 16 + int(p - kSwitch1), value_[c][p]);
        else if (p >= kDest0 && p <= kDestLast) core_->setOffset(c, int(p - kDest0), value_[c][p] + mod_[c][p - kDest0]);
    }
    struct Staged {  // triple buffer: the writer owns `back`, the reader `front`, `middle` is exchanged
        Entry buf[3];
        std::atomic<uint32_t> middle{1};
        uint32_t back = 0, front = 2;
    };
    // audio thread: install every sound the main thread published since the last call
    void sync() {
        for (uint32_t c = 0; c < kContexts; ++c) {
            Staged& s = staged_[c];
            if (!(s.middle.load(std::memory_order_acquire) & kDirty)) continue;
            s.front = s.middle.exchange(s.front, std::memory_order_acq_rel) & 3;
            const Entry& e = s.buf[s.front];
            installed_[c] = e.sound && e.sound->compiled && e.sound->compiled->loaded ? e.sound->compiled.get() : nullptr;
            core_->install(c, installed_[c]);
            for (uint32_t i = 0; i < kParamCount; ++i) { value_[c][i] = e.value[i]; apply(c, Param(i)); }
        }
    }
    std::unique_ptr<pcm::Core> core_;
    Staged staged_[kContexts];
    const pcm::Compiled* installed_[kContexts] = {};
    struct { std::atomic<double> voice{0}, rest{0}; } cost_[kContexts];
    float value_[kContexts][kParamCount] = {};
    float mod_[kContexts][kDestLast - kDest0 + 1] = {};  // the host's modulation, kept with the context across sounds
    uint32_t maxBlock_ = 128;
    bool dropping_[kContexts] = {};  // under write_: drop() published no sound for the context
    std::mutex write_;  // main-thread writers only
};

}  // namespace

std::unique_ptr<Engine> makeEngine() { return std::make_unique<CoreEngine>(); }

}  // namespace pcmshell
