// SPDX-License-Identifier: AGPL-3.0-or-later
// No allocation in process(). The harness counts every heap allocation the audio thread makes while the
// plugin's process() runs (alloc_guard.cpp), so this holds for the plugin whatever it is written
// in. The check first proves the interposer sees allocations, so a silent guard cannot pass, and that
// it counts the audio thread only (the plugin's I/O thread may run beside process()).
#include <atomic>
#include <cstdlib>
#include <thread>
#include "host.h"

TEST(allocation_guard_sees_allocations) {
    if (!allocGuardAvailable()) { std::printf("  allocation guard unavailable in this build: not checked\n"); return; }
    allocGuardEnable(true);
    void* volatile p = std::malloc(100);      // volatile: the compiler may not elide the pair
    char* volatile q = new char[100];          // operator new lives in the C++ runtime, another image
    allocGuardEnable(false);
    const long n = allocGuardCount();
    std::free(p); delete[] q;
    CHECK(n >= 2);
    if (n < 2) std::printf("  the interposer counted %ld allocations of 2\n", n);
    std::atomic<bool> go{false};
    std::thread other([&] { while (!go.load()) {} void* volatile r = std::malloc(100); std::free(r); });  // started before: its start allocates
    allocGuardEnable(true);
    go = true;
    other.join();
    allocGuardEnable(false);
    CHECK_EQ(allocGuardCount(), 0);
}

TEST(no_allocation_in_process) {
    if (!allocGuardAvailable()) { std::printf("  allocation guard unavailable in this build: not checked\n"); return; }
    Inst a;
    char msg[64];
    const uint32_t b = a.contexts->prepare(a.p, "b", nullptr, 0, patchWith("level=0.4 pan=0.3").c_str(), testBase(), msg, sizeof msg);
    a.block(128);  // warm up: the first block may touch lazily bound symbols
    a.guard = true;
    long worst = 0;
    auto go = [&](uint32_t frames, std::vector<Bytes> ev = {}) { a.block(frames, ev); worst = std::max(worst, a.allocations); };
    go(128);                                                                      // idle
    go(128, {on(0, 1, 0, 60), on(10, 2, 0, 64), on(20, 3, 1, 67, 1.0), midi(30, 0x90, 40, 90)});
    go(500, {param(5, 0, 0.5), param(6, 1, -0.3), param(7, 2, 3), volume(8, 1, 0, 60, 0.5), midi(9, 0xe0, 0, 100), midi(10, 0xb0, 1, 90)});
    a.guard = false; a.contexts->select(a.p, b, 1); a.guard = true;               // select is main thread: outside
    go(128, {on(0, 4, 1, 50), on(1, 5, 9, 50), on(2, 6, 0, 50, 0.5, 3), on(3, 7, 0, 200)});    // a selection applied, refused notes
    a.guard = false; a.limits(2, {{b, 1, 0, 3, 0}}); a.guard = true;
    go(128, {on(0, 8, 0, 51)});                                                   // budget give-ups
    go(256, {off(0, 1, 0, 60), off(5, -1, 1, -1), choke(10, 2, 0, 64), midi(11, 0xb0, 64, 127), midi(12, 0x80, 40, 0)});
    go(256, {midi(0, 0xb0, 64, 0), midi(1, 0xb0, 123, 0)});
    for (int i = 0; i < 100; ++i) go(64, {on(0, 100 + i, 0, 20 + i % 80)});     // the pool fills, cuts, every note ends
    a.guard = false; a.limits(0); a.p->reset(a.p); a.guard = true;                // reset is outside process()
    go(128);                                                                      // the owed NOTE_ENDs
    for (int i = 0; i < 300; ++i) go(uint32_t(32 + i % 200), {on(0, 1000 + i, 0, 30 + i % 50), off(10, 1000 + i - 3, 0, 30 + (i - 3) % 50)});
    a.guard = false;
    CHECK_EQ(worst, 0);
    if (worst) std::printf("  %ld allocation(s) inside process()\n", worst);
    CHECK(a.endsTotal() > 100);
}

// The same with streaming: voices that start streams, read them, release their rings, loop through a streamed body
// and are shed when the I/O thread stalls; the I/O thread runs beside process() throughout.
TEST(no_allocation_in_process_while_streaming) {
    if (!allocGuardAvailable()) { std::printf("  allocation guard unavailable in this build: not checked\n"); return; }
    Inst a;
    const std::string snd = "oscairn-sampler-sound/0\nsample " + sampleRef("stereo") + "\nsample " + sampleRef("mono") +
                            "\nelement ampeg_decay1=127 ampeg_decay2=127 ampeg_release=40\n"
                            "region key=0-63 root=60 element=0 sample=0\nregion key=64-127 root=60 element=0 sample=1 loop=10000-90000\n";
    CHECK(a.load(stateWith(snd)));
    a.settle = false;
    a.block(128);
    a.guard = true;
    long worst = 0;
    auto go = [&](uint32_t frames, std::vector<Bytes> ev = {}) { a.block(frames, ev); worst = std::max(worst, a.allocations); };
    for (int i = 0; i < 400; ++i) go(128, {on(0, 1 + i, 0, int16_t(40 + (i * 7) % 60)), off(64, i - 4, 0, int16_t(40 + ((i - 4) * 7) % 60))});
    a.guard = false; a.streaming->stall(a.p, true); a.guard = true;
    for (int i = 0; i < 200; ++i) go(128, {on(0, 1000 + i, 0, int16_t(70 + i % 20))});  // late streams: shed
    a.guard = false; a.streaming->stall(a.p, false); a.guard = true;
    std::vector<Bytes> up;
    for (int16_t k = 0; k < 128; ++k) up.push_back(off(0, -1, 0, k));
    go(128, up);
    for (int i = 0; i < 400; ++i) go(128);
    a.guard = false;
    CHECK_EQ(worst, 0);
    CHECK(a.streaming->sheds(a.p) > 0);
    CHECK_EQ(a.voices(), 0u);
}
