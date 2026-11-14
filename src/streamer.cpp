// SPDX-License-Identifier: AGPL-3.0-or-later
#include "streamer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX  // windows.h would define min and max as macros
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace pcmshell {

namespace {

struct Service {
    std::mutex lock;  // held by the thread for one pass or one unit; by main threads to attach, detach and free sources
    std::vector<std::pair<pcm::Stream*, uint32_t>> sets;
    std::thread thread;
    bool running = false;
    std::atomic<bool> stall{false};
    std::atomic<uint64_t> idle{0};  // passes that found nothing to do
    std::condition_variable wake;   // ends the thread's wait early: main threads only (settle, detach), never the audio thread
};
Service& service() { static Service* s = new Service; return *s; }  // never destroyed: no thread to join at exit

int64_t segEnd(const StreamSource& s) { return s.loop ? s.loopEnd : s.stop; }

// Frames the next unit hands over: the rest of the block holding the next frame (up to the loop's end or the stop),
// or up to 1,024 zero frames of a one-shot's tail.
int64_t unitFrames(const pcm::Stream& st, const StreamSource& s) {
    if (st.ioAt < segEnd(s)) {
        const uint32_t b = s.record.info.block_size, g = s.start + uint32_t(st.ioAt);
        return std::min<int64_t>(b - g % b, segEnd(s) - st.ioAt);
    }
    return std::min<int64_t>(st.ioZeros, 1024);
}

void produce(pcm::Stream& st, const StreamSource& s, int64_t n, Decoder& d) {
    const uint32_t c = s.record.info.channels;
    const int64_t w = st.written.load(std::memory_order_relaxed);
    if (st.ioAt < segEnd(s)) {
        const uint32_t b = s.record.info.block_size, g = s.start + uint32_t(st.ioAt), at = g % b;
        const uint32_t got = decodeBlock(s.record, g / b, d);  // a block that does not decode plays silence
        for (int64_t k = 0; k < n; ++k)
            for (uint32_t ch = 0; ch < c; ++ch) st.ring[((w + k) % pcm::kStreamFrames) * c + ch] = at + k < got ? d.ch[ch][at + k] : 0;
        st.ioAt += n;
        if (s.loop && st.ioAt == s.loopEnd) st.ioAt = s.loopStart;
        else if (!s.loop && st.ioAt == s.stop && !s.tail) st.ioDone = true;
    } else {
        for (int64_t k = 0; k < n; ++k)
            for (uint32_t ch = 0; ch < c; ++ch) st.ring[((w + k) % pcm::kStreamFrames) * c + ch] = 0;
        st.ioDone = (st.ioZeros -= n) <= 0;
    }
    st.written.store(w + n, std::memory_order_release);
}

void run() {
#if defined(_WIN32)
    SetThreadDescription(GetCurrentThread(), L"osc-sampler-io");
#elif defined(__APPLE__)
    pthread_setname_np("osc-sampler-io");
#else
    pthread_setname_np(pthread_self(), "osc-sampler-io");
#endif
    Service& sv = service();
    auto d = std::make_unique<Decoder>();
    for (;;) {
        {
            std::unique_lock<std::mutex> g(sv.lock);
            if (!sv.running) return;
            // released rings come back; of the active ones with room for their next unit, the one that runs out first
            pcm::Stream* best = nullptr;
            int64_t bestN = 0;
            float bestT = 0;
            const bool stall = sv.stall.load(std::memory_order_relaxed);
            for (auto& [streams, count] : sv.sets)
                for (uint32_t k = 0; k < count; ++k) {
                    pcm::Stream& st = streams[k];
                    const uint8_t state = st.state.load(std::memory_order_acquire);
                    if (state == pcm::Stream::Released) {
                        st.written.store(0, std::memory_order_relaxed);
                        st.consumed.store(0, std::memory_order_relaxed);
                        st.ioFresh = true;
                        st.state.store(pcm::Stream::Free, std::memory_order_release);
                        continue;
                    }
                    if (state != pcm::Stream::Active) continue;
                    const StreamSource& s = *static_cast<const StreamSource*>(st.source);
                    if (st.ioFresh) { st.ioFresh = false; st.ioDone = false; st.ioAt = s.head; st.ioZeros = s.tail ? pcm::kGuardPost : 0; }
                    if (stall || st.ioDone) continue;
                    const int64_t n = unitFrames(st, s), w = st.written.load(std::memory_order_relaxed);
                    const int64_t c = st.consumed.load(std::memory_order_acquire);
                    if (w + n - c > int64_t(pcm::kStreamFrames)) continue;
                    const float t = float(w - c) / std::max(st.speed.load(std::memory_order_relaxed), 1e-3f);  // output frames left
                    if (!best || t < bestT) { best = &st; bestN = n; bestT = t; }
                }
            if (best) { produce(*best, *static_cast<const StreamSource*>(best->source), bestN, *d); continue; }
            sv.idle.fetch_add(1, std::memory_order_release);
            sv.wake.wait_for(g, std::chrono::milliseconds(1));  // nothing to do: look again in a millisecond
        }
    }
}

}  // namespace

void attachStreams(pcm::Stream* streams, uint32_t count) {
    Service& sv = service();
    std::lock_guard<std::mutex> g(sv.lock);
    sv.sets.emplace_back(streams, count);
    if (!sv.running) { sv.running = true; sv.thread = std::thread(run); }
}

void detachStreams(pcm::Stream* streams) {
    Service& sv = service();
    std::thread t;
    {
        std::lock_guard<std::mutex> g(sv.lock);
        sv.sets.erase(std::remove_if(sv.sets.begin(), sv.sets.end(), [&](const auto& x) { return x.first == streams; }), sv.sets.end());
        if (sv.sets.empty()) { sv.running = false; t = std::move(sv.thread); }
    }
    sv.wake.notify_all();
    if (t.joinable()) t.join();
}

std::unique_lock<std::mutex> holdStreams() { return std::unique_lock<std::mutex>(service().lock); }

void stallStreams(bool on) { service().stall.store(on); }

void settleStreams() {
    Service& sv = service();
    const uint64_t i0 = sv.idle.load(std::memory_order_acquire);
    while (sv.idle.load(std::memory_order_acquire) < i0 + 2) { sv.wake.notify_all(); std::this_thread::sleep_for(std::chrono::microseconds(20)); }
}

}  // namespace pcmshell
