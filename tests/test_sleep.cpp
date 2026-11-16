// SPDX-License-Identifier: AGPL-3.0-or-later
// Sleeping: with nothing sounding and nothing queued the instrument returns CLAP_PROCESS_SLEEP and a block with no
// events costs and changes nothing; every block that has work left says CONTINUE; a note after sleeping renders as it
// does when the idle blocks are rendered anyway (an event in each of them keeps the instrument rendering).
#include <cstring>
#include "host.h"

namespace {
const std::string kOneShot = "oscairn-sampler-sound/0\nsample STEREO\nelement ampeg_decay1=127 ampeg_decay2=127 ampeg_release=60\nregion key=0-127 root=60 element=0 sample=0\n";
std::string streamed() { std::string s = kOneShot; s.replace(s.find("STEREO"), 6, sampleRef("stereo")); return s; }
// the looped sine with a part LFO on the pitch that runs free from the sound's first note
const char* const kFreeLfo =
    "oscairn-sampler-sound/0\npart plfo_wave=0 plfo_speed=40 plfo_keyon_reset=0 plfo_dst1=65 plfo_depth1=120\nsample sine.osp\n"
    "element ampeg_decay1=127 ampeg_decay2=127 ampeg_release=50 plfo_ratio1=127 plfo_box_sw1=1\nregion key=0-127 root=57 element=0 sample=0 loop=0-44100\n";

// Blocks of 128 frames until one returns SLEEP (at most `limit`); how many it took.
int untilSleep(Inst& a, int limit = 2000) {
    int n = 0;
    while (n < limit) { a.block(128); ++n; if (a.last == CLAP_PROCESS_SLEEP) break; }
    return n;
}
}  // namespace

// After a note's release has fully ended the instrument sleeps, and a block the host still calls is exactly silent.
TEST(sleeps_after_a_release_ends) {
    Inst a;
    CHECK(a.load(stateWith(kSound)));
    a.block(128);  // the state's selection applies
    CHECK(a.last == CLAP_PROCESS_SLEEP);
    a.block(128, {on(0, 1, 0, 60)});
    CHECK(a.last == CLAP_PROCESS_CONTINUE);
    for (int i = 0; i < 20; ++i) { a.block(128); CHECK(a.last == CLAP_PROCESS_CONTINUE); }  // held
    a.block(128, {off(0, 1, 0, 60)});
    int released = 0;
    while (a.last != CLAP_PROCESS_SLEEP && released < 200) { ++released; a.block(128); }
    CHECK(released > 40);  // about 155 ms of release is more than 50 blocks: every one of them said CONTINUE
    CHECK_EQ(a.ends(1), 1);
    CHECK_EQ(a.voices(), 0u);
    a.block(128);
    CHECK(a.last == CLAP_PROCESS_SLEEP);
    for (size_t i = a.L.size() - 128; i < a.L.size(); ++i) CHECK(a.L[i] == 0.f && a.R[i] == 0.f);  // the host's 123s were cleared
}

// Something to do keeps the instrument awake: a voice in its release, a voice still streaming, a queued selection
// (the block that applies it runs although the instrument slept, and shows the new sound's values).
TEST(stays_awake_while_there_is_work) {
    Inst a;
    const int beforeLoad = g_processRequests;
    CHECK(a.load(stateWith(streamed())));
    CHECK(g_processRequests > beforeLoad);  // a state load asks the host to process again
    a.block(128);
    CHECK(a.last == CLAP_PROCESS_SLEEP);
    a.block(128, {on(0, 1, 0, 60)});
    for (int i = 0; i < 100; ++i) { a.block(128); CHECK(a.last == CLAP_PROCESS_CONTINUE); }  // playing the streamed region
    a.block(128, {off(0, 1, 0, 60)});
    CHECK(a.last == CLAP_PROCESS_CONTINUE);  // in its release
    untilSleep(a);
    CHECK_EQ(a.ends(1), 1);
    // a selection queued while asleep
    char msg[128] = {};
    const uint32_t b = a.contexts->prepare(a.p, "b", nullptr, 0, patchWith("level=0.5").c_str(), testBase(), msg, sizeof msg);
    CHECK(b != CLAP_INVALID_ID);
    CHECK(a.contexts->select(a.p, b, -1));  // the host processes the next block itself
    a.log.clear();
    a.block(128);
    bool shown = false;
    for (auto& r : a.log) shown |= r.type == CLAP_EVENT_PARAM_VALUE && r.id == 0 && std::fabs(r.value - 0.5) < 1e-6;
    CHECK(shown);
    CHECK(a.last == CLAP_PROCESS_SLEEP);
    // a discarded sound is let go of by the main thread after a block has installed its removal: the discard wakes the host
    CHECK(a.contexts->select(a.p, 0, -1));
    a.block(128);
    const int beforeDiscard = g_processRequests;
    CHECK(a.contexts->discard(a.p, b));
    CHECK(g_processRequests > beforeDiscard);
    a.block(128);
    CHECK(a.last == CLAP_PROCESS_CONTINUE);  // until the main thread has dropped it
    a.p->on_main_thread(a.p);
    a.block(128); a.p->on_main_thread(a.p); a.block(128);
    CHECK(a.last == CLAP_PROCESS_SLEEP);
}

// Rendered while the instrument slept or with the idle blocks rendered anyway (one event in each), a note sounds the
// same, bit for bit: a short sound, a streamed region, and a part LFO that runs free (its phase follows the time).
static void sameAfterSleeping(const std::string& sound, int firstNoteBlocks) {
    std::vector<float> out[2][2];
    for (int variant = 0; variant < 2; ++variant) {  // 0: idle blocks without events (asleep), 1: with one each
        Inst a;
        CHECK(a.load(stateWith(sound)));
        a.block(128, {on(0, 1, 0, 60)});
        for (int i = 0; i < firstNoteBlocks; ++i) a.block(128);
        a.block(128, {off(0, 1, 0, 60)});
        untilSleep(a);
        for (int i = 0; i < 700; ++i) {
            if (variant) a.block(128, {param(0, 0, 1.0)});  // the level it has: changes nothing
            else a.block(128);
            if (i > 20 && !variant) CHECK(a.last == CLAP_PROCESS_SLEEP);
        }
        a.block(128, {on(0, 2, 0, 64)});
        a.run(128 * 120);
        a.block(128, {off(0, 2, 0, 64)});
        untilSleep(a);
        CHECK_EQ(a.ends(1), 1);
        CHECK_EQ(a.ends(2), 1);
        out[variant][0] = a.L; out[variant][1] = a.R;
    }
    CHECK(out[0][0].size() == out[1][0].size());
    CHECK(out[0][0] == out[1][0] && out[0][1] == out[1][1]);
    CHECK(peak(out[0][0]) > 0.05f);
}
TEST(a_note_after_sleeping_is_bit_identical) { sameAfterSleeping(kSound, 10); }
TEST(a_streamed_note_after_sleeping_is_bit_identical) { sameAfterSleeping(streamed(), 30); }
TEST(a_free_running_lfo_keeps_its_phase_through_sleep) { sameAfterSleeping(kFreeLfo, 10); }
