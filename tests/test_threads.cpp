// SPDX-License-Identifier: AGPL-3.0-or-later
// The main-thread calls that may run while another thread processes (prepare, select, discard,
// get, parameter values, CLAP state save and load, memory), run against a rendering
// thread; meant for ThreadSanitizer, and it checks the lifetime invariant still holds.
#include <atomic>
#include <thread>
#include "host.h"

TEST(main_thread_calls_while_processing) {
    Inst a;
    std::atomic<bool> stop{false};
    const std::string state = a.save();
    std::thread audio([&] {
        int32_t id = 1;
        for (int b = 0; !stop && b < 4000; ++b) {
            std::vector<Bytes> ev{on(0, id++, int16_t(b % 3), int16_t(40 + b % 20))};
            if (b % 3 == 0) ev.push_back(off(10, -1, int16_t(b % 3), -1));
            a.block(64, ev);
        }
        stop = true;
    });
    char msg[64];
    for (int i = 0; !stop; ++i) {
        const uint32_t id = a.contexts->prepare(a.p, "t", nullptr, 0, patchWith("level=0.3 pan=0.2").c_str(), testBase(), msg, sizeof msg);
        if (id != CLAP_INVALID_ID) {
            a.contexts->select(a.p, id, int16_t(i % 3));
            oscairn_sound_context_info_t info;
            for (uint32_t s = 0; s < a.contexts->capacity(a.p); ++s) a.contexts->get(a.p, s, &info);
        }
        for (uint32_t s = 1; s < a.contexts->capacity(a.p); ++s) { oscairn_sound_context_info_t info; if (a.contexts->get(a.p, s, &info) && info.idle) a.contexts->discard(a.p, info.id); }
        a.value(0); a.value(1); a.value(2);
        a.load(state);
        a.save();
        oscairn_memory_t m; a.memory->current(a.p, &m);
    }
    audio.join();
    a.block(128, {off(0, -1, -1, -1)});
    a.run(48000);
    int ends = 0;
    for (auto& r : a.log) ends += r.type == CLAP_EVENT_NOTE_END;
    CHECK(ends > 1000);
    CHECK(!a.badStatus && !a.badTime);
}
