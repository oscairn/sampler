// SPDX-License-Identifier: AGPL-3.0-or-later
// Streaming against time: a stream that comes too late sheds its voice with a short fade, counted, and the note ends
// once; the I/O thread runs beside a rendering thread that never waits for it, and sleeps while nothing streams (meant
// for ThreadSanitizer too).
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>
#include "host.h"

namespace {
const std::string kOneShot = "oscairn-sampler-sound/0\nsample STEREO\nelement ampeg_decay1=127 ampeg_decay2=127 ampeg_release=60\nregion key=0-127 root=60 element=0 sample=0\n";
std::string oneShot() { std::string s = kOneShot; s.replace(s.find("STEREO"), 6, sampleRef("stereo")); return s; }

// Blocks of 128 frames at about the rate they play, the I/O thread filling the rings on its own; `events` in the first.
void paced(Inst& a, long frames, const std::vector<Bytes>& events = {}) {
    const auto t0 = std::chrono::steady_clock::now();
    for (long done = 0; done < frames; done += 128) {
        a.block(uint32_t(std::min<long>(128, frames - done)), done ? std::vector<Bytes>{} : events);
        std::this_thread::sleep_until(t0 + std::chrono::microseconds((done + 128) * 1000000 / long(kRate)));
    }
}

// A streamed note an octave up (its head lasts about 1,500 output frames before it is shed for want of the stream),
// played at about real time: the main port, left then right, from the note on
std::vector<float> pacedNote(Inst& a) {
    a.settle = false;
    a.clearAudio();
    // printed: from the note on (the wake) to the I/O thread's first pass with nothing to do (the ring filled), and its
    // passes with nothing to do over the note
    using Clock = std::chrono::steady_clock;
    const uint64_t p0 = a.streaming->idlePasses(a.p);
    std::atomic<bool> done{false};
    Clock::time_point filled{};
    std::thread watch([&] {
        while (!done && a.streaming->idlePasses(a.p) == p0) std::this_thread::sleep_for(std::chrono::microseconds(50));
        filled = Clock::now();
    });
    const auto t0 = Clock::now();
    a.block(128, {on(3, 1, 0, 72)});
    const auto t1 = Clock::now();
    paced(a, 24000 - 128);
    done = true;
    watch.join();
    std::printf("  note-on block %.2f ms, note on to ring filled %.2f ms (the head lasts about 30 ms), %llu idle passes in the note\n",
                std::chrono::duration<double, std::milli>(t1 - t0).count(), std::chrono::duration<double, std::milli>(filled - t0).count(),
                (unsigned long long)(a.streaming->idlePasses(a.p) - p0));
    paced(a, 12000, {off(7, 1, 0, 72)});
    std::vector<float> out = a.L;
    out.insert(out.end(), a.R.begin(), a.R.end());
    return out;
}

// Whether two renders are the same, printing the first frame where they differ when not
bool same(const std::vector<float>& a, const std::vector<float>& b) {
    size_t d = 0;
    while (d < a.size() && d < b.size() && a[d] == b[d]) ++d;
    if (d < a.size() || d < b.size()) std::printf("  they differ from frame %zu (of %zu, left then right)\n", d, a.size() / 2);
    return a == b;
}

// The I/O thread's passes that found nothing to do over a second of silent blocks
uint64_t idlePassesInASecond(Inst& a) {
    a.settle = false;
    a.run(1280);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const uint64_t n0 = a.streaming->idlePasses(a.p);
    paced(a, long(kRate));
    const uint64_t n = a.streaming->idlePasses(a.p) - n0;
    std::printf("  %llu idle passes in a second\n", (unsigned long long)n);
    return n;
}
}  // namespace

// With the I/O thread stalled the voice plays its head, then is shed with a fade before the head runs out: no click,
// one shed counted, NOTE_END once, nothing left sounding.
TEST(late_stream_sheds_the_voice) {
    Inst a;
    CHECK(a.load(stateWith(oneShot())));
    a.settle = false;
    a.streaming->stall(a.p, true);
    a.block(128, {on(0, 1, 0, 72)});  // an octave up: the 4,096-frame head lasts about 2,000 output frames
    a.run(48000);
    a.streaming->stall(a.p, false);
    CHECK_EQ(a.streaming->sheds(a.p), 1u);
    CHECK_EQ(a.ends(1), 1);
    CHECK_EQ(a.voices(), 0u);
    CHECK(peak(a.L, 0, 1500) > 0.05f);  // it sounded
    CHECK(silent(a, 2100));             // and was gone before its head ran out
    float jump = 0;
    for (size_t i = 1; i < a.L.size(); ++i) jump = std::max(jump, std::fabs(a.L[i] - a.L[i - 1]));
    CHECK(jump < 0.1f);                 // no step: the tone moves at most about 0.06 a frame here
    a.block(128, {off(0, 1, 0, 72)});
    CHECK_EQ(a.ends(1), 1);
}

// A block that cannot be read (an I/O error, a short read) is a late stream: the voice is shed with its fade, counted,
// and the note ends once.
TEST(unreadable_stream_sheds_the_voice) {
    Inst a;
    CHECK(a.load(stateWith(oneShot())));
    a.streaming->failReads(a.p, true);
    a.block(128, {on(0, 1, 0, 72)});
    a.run(48000);
    a.streaming->failReads(a.p, false);
    CHECK_EQ(a.streaming->sheds(a.p), 1u);
    CHECK_EQ(a.ends(1), 1);
    CHECK_EQ(a.voices(), 0u);
    CHECK(peak(a.L, 0, 1500) > 0.05f);  // its head sounded
    a.block(128, {off(0, 1, 0, 72)});
    CHECK_EQ(a.ends(1), 1);
}

// A sound whose record cannot be read is refused at prepare, the message naming its reference.
TEST(unreadable_record_fails_the_prepare) {
    Inst a;
    a.streaming->failReads(a.p, true);
    char msg[256] = {};
    const uint32_t ctx = a.contexts->prepare(a.p, "s", nullptr, 0, patchWith("level=1 pan=0", oneShot().c_str()).c_str(), testBase(), msg, sizeof msg);
    a.streaming->failReads(a.p, false);
    CHECK(ctx == CLAP_INVALID_ID);
    CHECK(std::strstr(msg, sampleRef("stereo").c_str()) != nullptr);
}

// Rendering as fast as it can on one thread while the plugin's I/O thread fills the rings on its own: voices that
// outrun it are shed (counted), every note ends once, the output stays finite and bounded.
TEST(streaming_beside_a_rendering_thread) {
    Inst a;
    CHECK(a.load(stateWith(oneShot())));
    a.settle = false;
    std::thread t([&] {
        int32_t id = 1;
        for (int b = 0; b < 3000; ++b) {
            std::vector<Bytes> ev;
            if (b % 7 == 0) ev.push_back(on(0, id++, 0, int16_t(48 + (b * 5) % 48)));
            if (b % 7 == 3) ev.push_back(off(10, -1, 0, int16_t(48 + ((b - 3) * 5) % 48)));
            a.block(128, ev);
        }
        std::vector<Bytes> up;
        for (int16_t k = 0; k < 128; ++k) up.push_back(off(0, -1, 0, k));
        a.block(128, up);
        a.run(48000 * 4);
        CHECK_EQ(a.voices(), 0u);
        for (int32_t n = 1; n < id; ++n) CHECK_EQ(a.ends(n), 1);
    });
    t.join();
    for (float x : a.L) if (!std::isfinite(x) || std::fabs(x) > 8) { ++g_failures; break; }
    std::printf("  %llu voices shed for late streams\n", (unsigned long long)a.streaming->sheds(a.p));
}

// With a sound prepared and nothing playing the I/O thread sleeps: it looks at most a couple of times in a second of
// silent blocks (it used to look every millisecond).
TEST(idle_io_thread_sleeps) {
    Inst a;
    CHECK(a.load(stateWith(oneShot())));
    CHECK(idlePassesInASecond(a) <= 2);
}

// The first streamed note after a long idle wakes the thread in time: no shed, and the same samples as decoded whole.
TEST(streamed_note_after_idle_starts_on_time) {
    setenv("OSCAIRN_SAMPLER_DECODE_WHOLE", "1", 1);
    std::vector<float> whole;
    {
        Inst w;
        unsetenv("OSCAIRN_SAMPLER_DECODE_WHOLE");
        CHECK(w.load(stateWith(oneShot())));
        whole = pacedNote(w);
    }
    Inst a;
    CHECK(a.load(stateWith(oneShot())));
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    CHECK(same(pacedNote(a), whole));
    CHECK_EQ(a.streaming->sheds(a.p), 0u);
    CHECK(peak(whole) > 0.01f);
}

// Streamed notes started at random points of the thread's way to sleep (after gaps of 0 to 3 ms with nothing
// playing): each would be shed if its wake were lost, and none deadlocks.
TEST(streamed_notes_after_idle_gaps) {
    Inst a;
    std::string snd = oneShot();
    snd.replace(snd.find("ampeg_release=60"), 16, "ampeg_release=0");  // the voice and its stream end soon after the key
    CHECK(a.load(stateWith(snd)));
    a.settle = false;
    uint32_t r = 1;
    for (int32_t id = 1; id <= 60; ++id) {
        paced(a, 2048, {on(0, id, 0, 72)});  // past the head: the stream plays
        paced(a, 128, {off(0, id, 0, 72)});
        for (int b = 0; b < 2000 && a.voices(); ++b) paced(a, 128);
        r = r * 1664525u + 1013904223u;
        std::this_thread::sleep_for(std::chrono::microseconds((r >> 8) % 3000));
        CHECK_EQ(a.ends(id), 1);
    }
    CHECK_EQ(a.streaming->sheds(a.p), 0u);
}

// Two instances share the process's I/O thread: one plays while the other idles, then both idle and it sleeps, then
// the idle one plays alone.
TEST(two_instances_share_the_io_thread) {
    Inst a;
    CHECK(a.load(stateWith(oneShot())));
    std::vector<float> first;
    {
        Inst b;
        CHECK(b.load(stateWith(oneShot())));
        first = pacedNote(b);
        CHECK_EQ(b.streaming->sheds(b.p), 0u);
        CHECK(peak(first) > 0.01f);
        b.run(48000);
        CHECK(idlePassesInASecond(a) <= 2);
    }
    CHECK(same(pacedNote(a), first));
    CHECK_EQ(a.streaming->sheds(a.p), 0u);
}
