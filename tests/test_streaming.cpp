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
// It waits by yielding, not sleeping: as an audio device's clock, not a timer a throttled process may stretch by tens of
// milliseconds (then the blocks come in bursts no host makes).
double g_late = 0;      // the latest block's start past its time, ms
long g_lateFrame = 0;   // and the frame it started at (from the first block of the paced calls since g_late was reset)
long g_pacedFrames = 0;
void paced(Inst& a, long frames, const std::vector<Bytes>& events = {}) {
    const auto t0 = std::chrono::steady_clock::now();
    for (long done = 0; done < frames; done += 128) {
        const auto due = [&](long f) { return t0 + std::chrono::microseconds(f * 1000000 / long(kRate)); };
        const double late = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - due(done)).count();
        if (late > g_late) { g_late = late; g_lateFrame = g_pacedFrames + done; }
        a.block(uint32_t(std::min<long>(128, frames - done)), done ? std::vector<Bytes>{} : events);
        while (std::chrono::steady_clock::now() < due(done + 128)) std::this_thread::yield();
    }
    g_pacedFrames += frames;
}

// A paced note counts only if every block started within kMaxLate of its time: a late block is a burst the voice reads
// at once, out of the ring's slack. After a refill a voice an octave up has about 1,900 frames of slack (docs/design.md,
// "Streaming"), 20 ms at step 2; a quarter of it. A note whose blocks came later is played again, kAttempts times at most.
constexpr double kMaxLate = 5;
constexpr int kAttempts = 5;

struct PacedNote {
    std::vector<float> out;  // the main port, left then right, from the note on
    uint32_t sheds = 0;      // voices shed for late streams while it played
    double late = 0;         // ms, its latest block
    double gap = 0;          // ms, the longest time between two of the I/O thread's idle passes once the ring is read
};

// A streamed note an octave up (its head lasts about 1,500 output frames before it is shed for want of the stream),
// played at about real time. Printed: from the note on (the wake) to the I/O thread's first pass with nothing to do (the
// ring filled), its passes with nothing to do over the note, the longest time between two of them once the voice reads
// the ring (the head, 4,096 frames at step 2, lasts about 43 ms; then a unit is handed back every 11 ms), and the latest
// block.
PacedNote pacedNote(Inst& a, const char* what) {
    a.settle = false;
    a.clearAudio();
    using Clock = std::chrono::steady_clock;
    const uint64_t p0 = a.streaming->idlePasses(a.p);
    const uint32_t s0 = uint32_t(a.streaming->sheds(a.p));
    std::atomic<bool> done{false};
    Clock::time_point filled{};
    double gap = 0, gapAt = 0;
    const auto t0 = Clock::now();
    std::thread watch([&] {
        uint64_t n = p0;
        Clock::time_point last{};
        while (!done) {
            const uint64_t m = a.streaming->idlePasses(a.p);
            const auto t = Clock::now();
            if (m != n) {
                if (n == p0) filled = t;
                n = m; last = t;
            }
            const double since = std::chrono::duration<double, std::milli>(t - std::max(last, t0 + std::chrono::milliseconds(45))).count();
            if (n != p0 && since > gap) gap = since, gapAt = std::chrono::duration<double, std::milli>(t - t0).count();
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
        if (n == p0) filled = Clock::now();
    });
    g_late = 0; g_pacedFrames = 0;
    a.block(128, {on(3, 1, 0, 72)});
    const auto t1 = Clock::now();
    g_pacedFrames = 128;
    paced(a, 24000 - 128);
    done = true;
    watch.join();
    const uint64_t passes = a.streaming->idlePasses(a.p) - p0;
    paced(a, 12000, {off(7, 1, 0, 72)});
    PacedNote r;
    r.out = a.L;
    r.out.insert(r.out.end(), a.R.begin(), a.R.end());
    r.sheds = uint32_t(a.streaming->sheds(a.p)) - s0;
    r.late = g_late; r.gap = gap;
    std::printf("  %s: note-on block %.2f ms, ring filled after %.2f ms (the head lasts about 30 ms), %llu idle passes in the note, "
                "at most %.1f ms between two once the ring is read (ending %.0f ms in), blocks up to %.1f ms late (frame %ld)\n",
                what, std::chrono::duration<double, std::milli>(t1 - t0).count(), std::chrono::duration<double, std::milli>(filled - t0).count(),
                (unsigned long long)passes, gap, gapAt, g_late, g_lateFrame);
    return r;
}

// The note, after idleMs of nothing, until an attempt keeps time; false, saying so, when none of kAttempts did (the
// machine, not the plugin: not checked)
bool keptNote(Inst& a, const char* what, int idleMs, PacedNote& r) {
    double worst = 0;
    for (int i = 1; i <= kAttempts; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(idleMs));
        r = pacedNote(a, what);
        if (r.late <= kMaxLate) return true;
        worst = std::max(worst, r.late);
        std::printf("  %s: attempt %d discarded, blocks up to %.1f ms late (at most %.0f)\n", what, i, r.late, kMaxLate);
    }
    std::printf("  could not pace this note on this machine: %d attempts, blocks up to %.1f ms late; not checked\n", kAttempts, worst);
    return false;
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
        whole = pacedNote(w, "decoded whole (nothing streams)").out;  // its timing does not matter
    }
    Inst a;
    CHECK(a.load(stateWith(oneShot())));
    CHECK(peak(whole) > 0.01f);
    PacedNote n;
    if (!keptNote(a, "streamed after 1.5 s idle", 1500, n)) return;
    CHECK(same(n.out, whole));
    CHECK_EQ(n.sheds, 0u);
}

// Streamed notes started at random points of the thread's way to sleep (after gaps of 0 to 3 ms with nothing
// playing): each would be shed if its wake were lost, and none deadlocks. A note whose blocks came late is played again.
TEST(streamed_notes_after_idle_gaps) {
    Inst a;
    std::string snd = oneShot();
    snd.replace(snd.find("ampeg_release=60"), 16, "ampeg_release=0");  // the voice and its stream end soon after the key
    CHECK(a.load(stateWith(snd)));
    a.settle = false;
    uint32_t r = 1;
    int32_t id = 0;
    for (int note = 1; note <= 60; ++note) {
        double worst = 0;
        int i = 1;
        for (; i <= kAttempts; ++i) {
            const uint32_t s0 = uint32_t(a.streaming->sheds(a.p));
            g_late = 0;
            paced(a, 2048, {on(0, ++id, 0, 72)});  // past the head: the stream plays
            paced(a, 128, {off(0, id, 0, 72)});
            for (int b = 0; b < 2000 && a.voices(); ++b) paced(a, 128);
            CHECK_EQ(a.ends(id), 1);
            r = r * 1664525u + 1013904223u;
            std::this_thread::sleep_for(std::chrono::microseconds((r >> 8) % 3000));
            if (g_late <= kMaxLate) { CHECK_EQ(uint32_t(a.streaming->sheds(a.p)) - s0, 0u); break; }
            worst = std::max(worst, g_late);
            std::printf("  note %d: attempt %d discarded, blocks up to %.1f ms late (at most %.0f)\n", note, i, g_late, kMaxLate);
        }
        if (i > kAttempts) {
            std::printf("  could not pace note %d on this machine: %d attempts, blocks up to %.1f ms late; not checked\n", note, kAttempts, worst);
            return;
        }
    }
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
        PacedNote n;
        if (!keptNote(b, "streamed, the other instance idle", 0, n)) return;
        CHECK_EQ(n.sheds, 0u);
        CHECK(peak(n.out) > 0.01f);
        first = n.out;
        b.run(48000);
        CHECK(idlePassesInASecond(a) <= 2);
    }
    PacedNote n;
    if (!keptNote(a, "streamed, the other instance gone", 0, n)) return;
    CHECK(same(n.out, first));
    CHECK_EQ(n.sheds, 0u);
}
