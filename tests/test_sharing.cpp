// SPDX-License-Identifier: AGPL-3.0-or-later
// Parts sharing one instance: each sound on its own channel and output bus plays as it would alone in an instance of
// its own; a sound saved alone (oscairn.sound-snapshots draft 4) prepares beside and saves the same; a retiring sound
// rings out and choke_retiring fades it; the two readings of ports 1 and 2; the read-only shed count.
#include <fcntl.h>

#include "host.h"

namespace {
constexpr clap_id kBus = 25, kSheds = 26;
constexpr uint32_t kParamsWritable = 26;

// Part k's sound: its own root, envelope and element level; its own level and pan
std::string soundOf(int k) {
    char s[320];
    std::snprintf(s, sizeof s, "oscairn-sampler-sound/0\nsample sine.osp\nelement ampeg_decay1=127 ampeg_decay2=127 ampeg_release=%d amp_level=%d\n"
                  "region key=0-127 root=%d element=0 sample=0 loop=0-44100\n", 30 + 2 * k, 127 - 3 * k, 50 + k);
    return s;
}
double levelOf(int k) { return 0.9 - 0.05 * k; }
double panOf(int k) { return -0.8 + 0.2 * k; }
std::string shellOf(int k) { return "level=" + std::to_string(levelOf(k)) + " pan=" + std::to_string(panOf(k)); }
uint32_t prepare(Inst& a, const std::string& shell, const std::string& sound) {
    char msg[200] = {};
    const uint32_t id = a.contexts->prepare(a.p, "part", nullptr, 0, patchWith(shell, sound.c_str()).c_str(), testBase(), msg, sizeof msg);
    if (id == CLAP_INVALID_ID) std::printf("  prepare: %s\n", msg);
    return id;
}
// The notes of part k on channel ch: two overlapping notes at odd frames, released in turn
void play(Inst& a, int k, int16_t ch, std::vector<std::vector<Bytes>>& blocks) {
    blocks[1].push_back(on(5 + 3 * k, 10 * k + 1, ch, int16_t(60 + k), 0.6 + 0.04 * k));
    blocks[4].push_back(on(70, 10 * k + 2, ch, int16_t(67 - k), 0.9));
    blocks[30].push_back(off(17, 10 * k + 1, ch, int16_t(60 + k)));
    blocks[60].push_back(off(100, 10 * k + 2, ch, int16_t(67 - k)));
    (void)a;
}
double worstDiff(const std::vector<float>& x, const std::vector<float>& y) {
    if (x.size() != y.size()) return 1e9;
    double w = 0;
    for (size_t i = 0; i < x.size(); ++i) w = std::max(w, double(std::fabs(x[i] - y[i])));
    return w;
}
int openFiles() { int n = 0; for (int fd = 0; fd < 4096; ++fd) n += fcntl(fd, F_GETFD) != -1; return n; }
}  // namespace

// n sounds in one instance, sound k on channel k and output bus k + 1 (sound 0 the Default from the state), play what
// each plays alone in an instance of its own on its main port, port by port; the main port and the unused buses stay
// silent; nothing allocates in process(). Equal within float rounding: the shared instance sums its voices' lanes in
// another order.
TEST(shared_sounds_play_as_they_do_alone) {
    for (int n : {2, 5, 8}) {
        Inst a;
        a.outputs = 9;
        CHECK(a.load(stateWith(soundOf(0), levelOf(0), panOf(0))));
        for (int k = 1; k < n; ++k) {
            const uint32_t id = prepare(a, shellOf(k), soundOf(k));
            CHECK(id != CLAP_INVALID_ID);
            CHECK(a.contexts->select(a.p, id, int16_t(k)));
        }
        std::vector<std::vector<Bytes>> blocks(200);
        for (int k = 0; k < n; ++k) blocks[0].push_back(param(0, kBus, k + 1, int16_t(k)));
        for (int k = 0; k < n; ++k) play(a, k, int16_t(k), blocks);
        a.guard = allocGuardAvailable();
        long allocs = 0;
        for (auto& b : blocks) { a.block(128, b); allocs = std::max(allocs, a.allocations); }
        CHECK_EQ(allocs, 0);
        CHECK(silent(a, 0));
        for (int k = 0; k < n; ++k) {
            Inst b;
            CHECK(b.load(stateWith(soundOf(k), levelOf(k), panOf(k))));
            std::vector<std::vector<Bytes>> mine(200);
            play(b, k, 0, mine);
            for (auto& x : mine) b.block(128, x);
            CHECK(peak(b.L) > 0.01f);
            const double dl = worstDiff(a.PL[k], b.L), dr = worstDiff(a.PR[k], b.R);
            if (dl > 1e-5 || dr > 1e-5) std::printf("  %d sounds: bus %d differs by %g, %g\n", n, k + 1, dl, dr);
            CHECK(dl <= 1e-5 && dr <= 1e-5);
        }
        for (int k = n; k < 8; ++k) CHECK(peak(a.PL[k]) == 0 && peak(a.PR[k]) == 0);
        CHECK_EQ(a.endsTotal(), 2 * n);
        CHECK_EQ(a.voices(), 0u);
    }
}

// save_sound writes the sound alone, on the main port; prepared beside in another instance it selects one sound on
// every channel, saves byte for byte the same, and sounds the same. sound_values: every writable value of a sound no
// channel selects, none for a selected one. A state or a snapshot loaded selects its sound on every channel.
TEST(a_sound_saved_alone_prepares_beside_the_same) {
    Inst a;
    a.outputs = 9;
    const uint32_t id = prepare(a, "level=0.7 pan=0.3 bus=4 knob1=0.25", soundOf(3));
    CHECK(a.snapshots && a.snapshots->save_sound && a.snapshots->sound_values && a.snapshots->ring_out && a.snapshots->choke_retiring);
    if (!a.snapshots) return;
    Stream one;
    CHECK(a.snapshots->save_sound(a.p, id, &one.out));
    Stream none;
    CHECK(!a.snapshots->save_sound(a.p, id + 50, &none.out));
    oscairn_sound_parameter_t v[64] = {};
    CHECK_EQ(a.snapshots->sound_values(a.p, id, v, 64), kParamsWritable);
    CHECK(v[kBus].id == kBus && v[kBus].value == 4 && std::fabs(v[0].value - 0.7) < 1e-6 && std::fabs(v[3].value - 0.25) < 1e-6);
    for (uint32_t i = 0; i < kParamsWritable; ++i) CHECK(v[i].id != kSheds);
    CHECK_EQ(a.snapshots->sound_values(a.p, 0, v, 64), 0u);  // the Default: selected
    CHECK_EQ(a.snapshots->sound_values(a.p, id + 50, v, 64), 0u);

    Inst b;
    uint32_t sel[16] = {};
    Stream in; in.data = one.data;
    char msg[200] = {};
    CHECK(b.snapshots->prepare(b.p, &in.in, sel, msg, sizeof msg));
    for (int ch = 0; ch < 16; ++ch) CHECK(sel[ch] == sel[0] && sel[0] != 0);
    Stream again;
    CHECK(b.snapshots->save_sound(b.p, sel[0], &again.out));
    CHECK(again.data == one.data);
    CHECK(b.snapshots->sound_values(b.p, sel[0], v, 64) == kParamsWritable && v[kBus].value == 0);  // alone: on the main port

    // the same notes: a's on channel 2 out of bus 4, b's on every channel out of the main port
    a.contexts->select(a.p, id, 2);
    b.contexts->select(b.p, sel[0], -1);
    a.block(128); b.block(128);
    a.clearAudio(); b.clearAudio();
    for (auto& x : a.PL) x.clear();
    for (auto& x : a.PR) x.clear();
    std::vector<std::vector<Bytes>> ea(80), eb(80);
    ea[0] = {on(9, 1, 2, 64)}; eb[0] = {on(9, 1, 2, 64)};
    ea[20] = {off(3, 1, 2, 64)}; eb[20] = {off(3, 1, 2, 64)};
    for (int i = 0; i < 80; ++i) { a.block(128, ea[size_t(i)]); b.block(128, eb[size_t(i)]); }
    CHECK(peak(b.L) > 0.05f);
    CHECK(worstDiff(a.PL[3], b.L) == 0 && worstDiff(a.PR[3], b.R) == 0);
    CHECK(silent(a, 0));

    // loaded as a whole snapshot, the sound alone plays on every channel
    Inst c;
    Stream whole; whole.data = one.data;
    CHECK(c.contexts->state->load(c.p, &whole.in));
    oscairn_sound_context_info_t info{};
    CHECK(c.contexts->get(c.p, 0, &info) && info.selected_channels == 0xffff);
    // and the CLAP state too, whatever the channels selected before
    Inst d;
    const uint32_t other = prepare(d, "level=1", kSound);
    d.contexts->select(d.p, other, -1);
    d.block(128);
    CHECK(d.load(stateWith(kSound, 0.5)));
    d.block(128);
    CHECK(d.contexts->get(d.p, 0, &info) && info.selected_channels == 0xffff);
}

// A retiring sound's main output rings out into ring-out channels 0 and 1 until its notes finish; at a second change
// choke_retiring fades the older tail out through channels 2 and 3 (the choke time) while the newer tail takes 0 and 1.
// Every note ends once, the voices reach zero, and the discarded sounds let go of their packs.
TEST(retiring_sounds_ring_out_and_choke) {
    Inst a;
    a.ring = true;
    a.block(128);
    const int before = openFiles();
    const std::string snd = "oscairn-sampler-sound/0\nsample " + sampleRef("stereo") +
                            "\nelement ampeg_decay1=127 ampeg_decay2=127 ampeg_release=50\nregion key=0-127 root=60 element=0 sample=0 loop=1000-140000\n";
    const uint32_t first = prepare(a, "level=1", snd);
    CHECK(first != CLAP_INVALID_ID);
    a.contexts->select(a.p, first, -1);
    a.block(128, {on(0, 1, 0, 60)});
    a.run(2400);
    CHECK(peak(a.L) > 0.01f && peak(a.RL[0]) == 0);
    auto change = [&](bool chokeFirst) {
        Stream s; CHECK(a.snapshots->save_sound(a.p, first, &s.out));
        Stream in; in.data = s.data;
        uint32_t sel[16] = {}; char msg[200] = {};
        CHECK(a.snapshots->prepare(a.p, &in.in, sel, msg, sizeof msg));
        if (chokeFirst) CHECK(a.snapshots->choke_retiring(a.p));
        CHECK(a.contexts->select(a.p, sel[0], -1));
        return sel[0];
    };
    const uint32_t second = change(false);
    size_t at = a.L.size();
    a.block(128);
    a.run(2400);
    CHECK(peak(a.L, at) < 1e-6f && peak(a.RL[0], at) > 0.01f && peak(a.RL[1], at) == 0);  // note 1 rings out
    a.block(128, {on(0, 2, 0, 62)});                                                       // note 2 on the new sound
    a.run(2400);
    CHECK(peak(a.L, a.L.size() - 1200) > 0.01f);
    const uint32_t third = change(true);
    at = a.L.size();
    a.block(128);
    // note 1 fades out through channels 2 and 3 within the choke time and ends; note 2 rings out through 0 and 1
    a.run(2400);
    CHECK(peak(a.RL[1], at) > 1e-3f && peak(a.RL[1], at + kChoke + 1) == 0);
    CHECK_EQ(a.ends(1), 1);
    CHECK_EQ(a.ends(2), 0);
    CHECK(peak(a.RL[0], a.L.size() - 1200) > 0.01f && peak(a.L, at) < 1e-6f);
    a.block(128, {off(0, 2, 0, 62)});
    a.run(24000);
    CHECK_EQ(a.ends(2), 1);
    CHECK_EQ(a.endsTotal(), 2);
    CHECK_EQ(a.voices(), 0u);
    CHECK(peak(a.RL[0], a.RL[0].size() - 2400) == 0);
    for (uint32_t id : {first, second}) CHECK(a.contexts->discard(a.p, id));
    for (int i = 0; i < 4; ++i) { a.block(128); a.p->on_main_thread(a.p); }
    CHECK_EQ(openFiles(), before + 1);  // only the sound playing now holds its pack
    a.contexts->select(a.p, 0, -1);
    a.block(128);
    CHECK(a.contexts->discard(a.p, third));
    for (int i = 0; i < 4; ++i) { a.block(128); a.p->on_main_thread(a.p); }
    CHECK_EQ(openFiles(), before);
}

// Ports 1 and 2: insertion A and B for the elements of a sound that splits its outputs only while it is the one sound
// selected and on bus 0. Once a second sound is selected on another channel, or the sound's bus is not 0, they are
// sound buses 1 and 2 and its elements play on its bus with the rest.
TEST(element_routing_only_for_one_sound_on_bus_0) {
    const std::string routed = "oscairn-sampler-sound/0\npart split_outputs=1\nsample sine.osp\n"
        "element ampeg_decay1=127 ampeg_decay2=127 element_connection=1\nelement ampeg_decay1=127 ampeg_decay2=127 element_connection=2\n"
        "region key=0-127 root=57 element=0 sample=0 loop=0-44100\nregion key=0-127 root=69 element=1 sample=0 loop=0-44100\n";
    Inst a;
    a.outputs = 9;
    CHECK(a.load(stateWith(routed)));
    a.block(128, {on(0, 1, 0, 60)});
    a.run(1024);
    size_t at = a.L.size() - 512;
    CHECK(peak(a.L, at) == 0 && peak(a.PL[0], at) > 0.01f && peak(a.PL[1], at) > 0.01f);  // elements to A and B
    // a second sound selected on channel 1: the first sound's elements fall back to its bus, 0
    const uint32_t other = prepare(a, "level=1 bus=3", kSound);
    a.contexts->select(a.p, other, 1);
    a.block(128);
    a.run(1024);
    at = a.L.size() - 512;
    CHECK(peak(a.L, at) > 0.01f && peak(a.PL[0], at) == 0 && peak(a.PL[1], at) == 0);
    a.contexts->select(a.p, 0, 1);  // alone again: split again
    a.block(128);
    a.run(1024);
    at = a.L.size() - 512;
    CHECK(peak(a.L, at) == 0 && peak(a.PL[0], at) > 0.01f && peak(a.PL[1], at) > 0.01f);
    // on bus 5 the whole sound plays on port 5
    a.block(128, {param(0, kBus, 5, 0)});
    a.run(1024);
    at = a.L.size() - 512;
    CHECK(peak(a.L, at) == 0 && peak(a.PL[0], at) == 0 && peak(a.PL[1], at) == 0 && peak(a.PL[4], at) > 0.01f);
    // a port never activated plays into main
    Inst b;
    b.outputs = 9;
    b.activatePorts = false;
    const uint32_t s = prepare(b, "level=1 bus=6", kSound);
    b.contexts->select(b.p, s, -1);
    b.block(128, {on(0, 1, 0, 60)});
    b.run(1024);
    CHECK(peak(b.L, b.L.size() - 512) > 0.01f);
}

// Each sound sharing the instance keeps its own voice limit and its own voice count.
TEST(shared_sounds_keep_their_own_voices) {
    Inst a;
    const uint32_t x = prepare(a, "level=1 bus=1", kSound), y = prepare(a, "level=1 bus=2", kSound);
    a.contexts->select(a.p, x, 1);
    a.contexts->select(a.p, y, 2);
    a.block(128);
    a.limits(0, {{x, 1, 0, 5, 0}});
    a.block(128, {on(0, 1, 1, 60), on(0, 2, 1, 62), on(0, 3, 1, 64), on(0, 4, 2, 60), on(0, 5, 2, 62), on(0, 6, 2, 64)});
    a.run(1024);
    oscairn_sound_context_info_t ix{}, iy{};
    for (uint32_t slot = 0; slot < a.contexts->capacity(a.p); ++slot) {
        oscairn_sound_context_info_t i{};
        if (!a.contexts->get(a.p, slot, &i)) continue;
        if (i.id == x) ix = i;
        if (i.id == y) iy = i;
    }
    CHECK_EQ(ix.active_voices, 1u);
    CHECK_EQ(iy.active_voices, 3u);
    CHECK_EQ(a.voices(), 4u);
    // the instance's memory: the contexts' part once per context, the samples once whatever shares them
    oscairn_memory_t two{};
    CHECK(a.memory->current(a.p, &two));
    CHECK(two.context_bytes > 0);
}

// The read-only parameter shows the voices shed for late streams, the streaming extension's count, and is not written.
TEST(shed_count_parameter_is_read_only) {
    Inst a;
    clap_param_info_t info{};
    CHECK(a.params->get_info(a.p, kSheds, &info) && info.id == kSheds && (info.flags & CLAP_PARAM_IS_READONLY) && !(info.flags & CLAP_PARAM_IS_AUTOMATABLE));
    const std::string snd = "oscairn-sampler-sound/0\nsample " + sampleRef("stereo") + "\nelement ampeg_decay1=127 ampeg_decay2=127 ampeg_release=60\nregion key=0-127 root=60 element=0 sample=0\n";
    CHECK(a.load(stateWith(snd)));
    a.settle = false;
    a.streaming->stall(a.p, true);
    a.block(128, {on(0, 1, 0, 72)});  // its head runs out with the I/O thread stalled: shed
    a.run(48000);
    a.streaming->stall(a.p, false);
    a.block(128, {param(0, kSheds, 0)});  // ignored
    CHECK(a.streaming->sheds(a.p) > 0);
    CHECK_EQ(a.value(kSheds), double(a.streaming->sheds(a.p)));
    oscairn_sound_parameter_t bad{kSheds, 0};
    CHECK_EQ(a.contexts->prepare(a.p, "x", &bad, 1, nullptr, nullptr, nullptr, 0), CLAP_INVALID_ID);
}
