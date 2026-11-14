// SPDX-License-Identifier: AGPL-3.0-or-later
// Streaming against time: a stream that comes too late sheds its voice with a short fade, counted, and the note ends
// once; the I/O thread runs beside a rendering thread that never waits for it (meant for ThreadSanitizer too).
#include <atomic>
#include <cstring>
#include <thread>
#include "host.h"

namespace {
const std::string kOneShot = "oscairn-pcm-sound/0\nsample STEREO\nelement ampeg_decay1=127 ampeg_decay2=127 ampeg_release=60\nregion key=0-127 root=60 element=0 sample=0\n";
std::string oneShot() { std::string s = kOneShot; s.replace(s.find("STEREO"), 6, sampleRef("stereo")); return s; }
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
