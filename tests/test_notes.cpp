// SPDX-License-Identifier: AGPL-3.0-or-later
// Note lifetime: every note the host starts gets exactly one NOTE_END, on every path that ends
// it, and none for a note that never started.
#include <random>
#include <set>
#include "host.h"

TEST(descriptor_and_ports) {
    auto& lib = library();
    CHECK(lib.factory != nullptr);
    if (!lib.factory) return;
    CHECK_EQ(lib.factory->get_plugin_count(lib.factory), 1u);
    const auto* d = lib.factory->get_plugin_descriptor(lib.factory, 0);
    CHECK(d && !std::strcmp(d->id, kId));
    CHECK(d && d->clap_version.major == CLAP_VERSION_MAJOR);
    bool instrument = false;
    for (auto f = d ? d->features : nullptr; f && *f; ++f) instrument |= !std::strcmp(*f, CLAP_PLUGIN_FEATURE_INSTRUMENT);
    CHECK(instrument);
    Inst a(false);
    auto* ap = static_cast<const clap_plugin_audio_ports_t*>(a.p->get_extension(a.p, CLAP_EXT_AUDIO_PORTS));
    auto* np = static_cast<const clap_plugin_note_ports_t*>(a.p->get_extension(a.p, CLAP_EXT_NOTE_PORTS));
    CHECK(ap && ap->count(a.p, false) == 9 && ap->count(a.p, true) == 0);
    for (uint32_t k = 0; k < 9; ++k) {  // main, sound buses 1-8 (1 and 2 also to insertion A and B): stable ids 0-8
        clap_audio_port_info_t ai{};
        CHECK(ap && ap->get(a.p, k, false, &ai) && ai.channel_count == 2 && ai.id == k && bool(ai.flags & CLAP_AUDIO_PORT_IS_MAIN) == (k == 0));
    }
    clap_note_port_info_t ni{};
    CHECK(np && np->count(a.p, true) == 1 && np->get(a.p, 0, true, &ni));
    CHECK((ni.supported_dialects & CLAP_NOTE_DIALECT_CLAP) && (ni.supported_dialects & CLAP_NOTE_DIALECT_MIDI));
    CHECK(a.contexts && a.budget && a.memory && a.state && a.params);
    CHECK(a.budget && a.budget->honors_volume(a.p));
}

TEST(note_ends_once_after_release) {
    Inst a;
    a.block(128, {on(10, 1, 0, 60)});
    a.run(4800);
    CHECK(peak(a.L) > 0.01f && peak(a.R) > 0.01f);
    CHECK_EQ(a.ends(1), 0);
    a.block(128, {off(0, 1, 0, 60)});
    a.run(48000 / 2);
    CHECK_EQ(a.ends(1), 1);
    const auto* e = a.endOf(1);
    CHECK(e && e->key == 60 && e->channel == 0 && e->port == 0);
    CHECK(silent(a, a.L.size() - 4800));
    CHECK(!a.badStatus && !a.badTime);
}

TEST(refused_note_on_still_ends) {
    Inst a;
    a.block(256, {on(5, 7, 0, 60, 0.8, 1), on(6, 8, 16, 60), on(7, 9, 0, 128), on(8, 10, -1, 60), on(9, 11, 0, -1)});
    for (int id = 7; id <= 11; ++id) CHECK_EQ(a.ends(id), 1);
    const auto* e = a.endOf(7);
    CHECK(e && e->port == 1 && e->key == 60 && e->time == 5);  // the fields it came with, at its time
    e = a.endOf(8); CHECK(e && e->channel == 16);
    e = a.endOf(9); CHECK(e && e->key == 128);
    CHECK_EQ(a.voices(), 0u);
    a.run(4800);
    CHECK_EQ(a.endsTotal(), 5);
    CHECK(silent(a, 0));
}

TEST(choke_ends_once_with_a_fade) {
    Inst a;
    a.block(128, {on(0, 1, 0, 60), on(0, 2, 0, 64)});
    a.run(2400);
    a.block(256, {choke(0, 1, 0, 60)});
    CHECK_EQ(a.ends(1), 1);  // the 3 ms fade ends inside this block
    CHECK_EQ(a.ends(2), 0);
    const auto* e = a.endOf(1);
    CHECK(e && e->frame >= 2528 + int(kChoke) - 1 && e->frame < 2528 + 256);  // at the end of the segment that finishes the fade
    a.run(4800);
    CHECK_EQ(a.ends(1), 1);
    CHECK_EQ(a.voices(), 1u);
}

TEST(wildcards) {
    Inst a;
    a.block(128, {on(0, 1, 0, 60), on(0, 2, 1, 62), on(0, 3, 1, 64), on(0, 4, 2, 65)});
    a.run(1200);
    a.block(128, {off(0, -1, 1, -1)});  // every key of channel 1
    a.run(24000);
    CHECK(a.ends(1) == 0 && a.ends(2) == 1 && a.ends(3) == 1 && a.ends(4) == 0);
    a.block(128, {off(0, -1, -1, 65)});  // key 65 on every channel
    a.run(24000);
    CHECK(a.ends(4) == 1 && a.ends(1) == 0);
    a.block(128, {choke(0, -1, -1, -1)});  // everything
    a.run(2400);
    for (int id = 1; id <= 4; ++id) CHECK_EQ(a.ends(id), 1);
    CHECK_EQ(a.voices(), 0u);
}

TEST(midi_notes_pedal_and_all_notes_off) {
    Inst a;
    // note ID -1 for raw MIDI: one NOTE_END (id -1) per note-on
    a.block(128, {midi(0, 0x90, 60, 100), midi(0, 0x91, 62, 100), midi(0, 0x90, 64, 0)});  // velocity 0 on 64: a note-off for a note not held
    a.run(1200);
    CHECK_EQ(a.endsTotal(), 0);
    a.block(128, {midi(0, 0xb0, 64, 127), midi(1, 0x80, 60, 0)});  // pedal down, then release 60: it rings on
    a.run(24000);
    CHECK_EQ(a.endsTotal(), 0);
    a.block(128, {midi(0, 0xb0, 64, 0)});  // pedal up: it releases
    a.run(24000);
    CHECK_EQ(a.endsTotal(), 1);
    a.block(128, {midi(0, 0xb1, 123, 0)});  // all notes off, channel 1
    a.run(24000);
    CHECK_EQ(a.endsTotal(), 2);
    a.block(128, {midi(0, 0x90, 70, 100), midi(0, 0x90, 70, 100), midi(0, 0xb0, 120, 0)});  // two on one key, then all sound off
    a.run(2400);
    CHECK_EQ(a.endsTotal(), 4);
    CHECK_EQ(a.voices(), 0u);
}

TEST(reset_is_immediate_and_says_nothing_about_earlier_notes) {
    Inst a;
    a.block(128, {on(0, 1, 0, 60), on(0, 2, 0, 62), on(0, 3, 0, 64), off(100, 3, 0, 64)});
    a.run(2400);
    a.p->reset(a.p);
    CHECK_EQ(a.voices(), 0u);
    a.block(128);
    CHECK(silent(a, a.L.size() - 128));
    a.run(48000);
    CHECK_EQ(a.endsTotal(), 0);  // no NOTE_END (or anything else) for notes 1, 2 and 3
    for (auto& r : a.log) CHECK(r.type != CLAP_EVENT_NOTE_END && r.type != CLAP_EVENT_NOTE_OFF && r.type != CLAP_EVENT_NOTE_CHOKE);
    // a note a snapshot load owed an end, then reset: forgotten too
    Stream snap;
    a.contexts->state->save(a.p, &snap.out);
    a.block(128, {on(0, 4, 0, 60)});
    a.run(1200);
    CHECK(a.contexts->state->load(a.p, &snap.in));
    a.p->reset(a.p);
    a.run(1280);
    CHECK_EQ(a.endsTotal(), 0);
    // a stale off for a forgotten id does nothing, and the instance plays as new
    a.block(128, {off(0, 1, 0, 60), on(0, 5, 0, 60)});
    a.run(2400);
    CHECK(peak(a.L, a.L.size() - 2400) > 0.01f);
    a.block(128, {off(0, 5, 0, 60)});
    a.run(24000);
    CHECK_EQ(a.endsTotal(), 1);
    CHECK_EQ(a.ends(5), 1);
}

TEST(snapshot_load_ends_held_notes_once) {
    Inst a;
    Stream snap;
    CHECK(a.contexts->state->save(a.p, &snap.out));
    a.block(128, {on(0, 1, 0, 60), on(0, 2, 1, 62)});
    a.run(1200);
    CHECK(a.contexts->state->load(a.p, &snap.in));
    a.run(256);
    CHECK(a.ends(1) == 1 && a.ends(2) == 1);
    a.run(24000);
    CHECK_EQ(a.endsTotal(), 2);
    CHECK_EQ(a.voices(), 0u);
}

TEST(volume_expression_scales_the_note) {
    Inst a;
    a.block(128, {on(0, 1, 0, 60, 1.0)});
    a.run(4800);
    const float full = peak(a.L, a.L.size() - 1000);
    a.block(128, {volume(0, 1, 0, 60, 0.5)});
    a.run(4800);
    const float half = peak(a.L, a.L.size() - 1000);
    CHECK(full > 0.01f && half > 0.4f * full && half < 0.6f * full);
}

// A randomised run: thousands of blocks of notes started, released, choked and wildcarded in
// every dialect, with resets, snapshot loads, state loads, selections, parameter changes and
// changing voice budgets. Every CLAP note-on id must end exactly once, and no id that never
// started may end; MIDI notes (id -1) must end once each.
static void randomRun(unsigned seed, const char* instanceCap, int blocks) {
    if (instanceCap) setenv("OSCAIRN_SAMPLER_MAX_VOICES", instanceCap, 1);
    Inst a;
    unsetenv("OSCAIRN_SAMPLER_MAX_VOICES");
    std::mt19937 rng(seed);
    auto pick = [&](int n) { return int(rng() % unsigned(n)); };
    // prepare two more sounds and keep selecting among them
    uint32_t ids[3] = {0, CLAP_INVALID_ID, CLAP_INVALID_ID};
    char msg[128];
    ids[1] = a.contexts->prepare(a.p, "b", nullptr, 0, patchWith("level=0.4 pan=-0.5").c_str(), testBase(), msg, sizeof msg);
    ids[2] = a.contexts->prepare(a.p, "c", nullptr, 0, patchWith("level=0.6 pan=0.5 polyphony=3").c_str(), testBase(), msg, sizeof msg);
    CHECK(ids[1] != CLAP_INVALID_ID && ids[2] != CLAP_INVALID_ID);
    Stream snap, st;
    a.contexts->state->save(a.p, &snap.out);
    a.state->save(a.p, &st.out);
    int32_t nextId = 1; int midiOns = 0;
    std::set<int32_t> started;
    // A reset forgets the notes sounding then: they get no END. Read them (notes(), same thread)
    // before each reset, and the ones a snapshot load still owes an END until the next block.
    std::set<int32_t> forgotten, owedIds; int forgottenMidi = 0, owedMidi = 0;
    auto sounding = [&](std::set<int32_t>& ids, int& midiVoices) {
        oscairn_voice_budget_note_t n[64];
        const uint32_t k = a.budget->notes(a.p, n, 64);
        for (uint32_t i = 0; i < k; ++i) { if (n[i].note_id >= 0) ids.insert(n[i].note_id); else midiVoices += int(n[i].voices); }
    };
    for (int b = 0; b < blocks; ++b) {
        std::vector<Bytes> ev;
        std::vector<uint32_t> times;
        const uint32_t frames = uint32_t(64 + pick(65));
        const int n = pick(4);
        for (int i = 0; i < n; ++i) times.push_back(uint32_t(pick(int(frames))));
        std::sort(times.begin(), times.end());
        for (uint32_t t : times) {
            const int16_t ch = int16_t(pick(3)), key = int16_t(36 + pick(12));
            switch (pick(14)) {
                case 0: case 1: case 2: case 3: { const int32_t id = nextId++; started.insert(id); ev.push_back(on(t, id, ch, key, 0.1 + 0.9 * pick(10) / 10.0)); break; }
                case 4: ev.push_back(off(t, pick(2) ? -1 : pick(nextId), pick(2) ? -1 : ch, pick(2) ? -1 : key)); break;
                case 5: ev.push_back(off(t, int32_t(1 + pick(std::max(1, nextId - 1))), ch, key)); break;
                case 6: ev.push_back(choke(t, int32_t(1 + pick(std::max(1, nextId - 1))), ch, key)); break;
                case 7: ev.push_back(choke(t, -1, pick(2) ? -1 : ch, pick(2) ? -1 : key)); break;
                case 8: ev.push_back(midi(t, uint8_t(0x90 | ch), uint8_t(key), uint8_t(1 + pick(127)))); ++midiOns; break;
                case 9: ev.push_back(midi(t, uint8_t((pick(2) ? 0x80 : 0x90) | ch), uint8_t(key), 0)); break;
                case 10: ev.push_back(midi(t, uint8_t(0xb0 | ch), uint8_t(pick(3) == 0 ? 123 : pick(2) ? 64 : 120), uint8_t(pick(128)))); break;
                case 11: ev.push_back(param(t, clap_id(pick(3)), pick(2) ? 0.2 + pick(8) / 10.0 : double(1 + pick(64)), int16_t(pick(2) ? -1 : ch))); break;
                case 12: ev.push_back(on(t, nextId, int16_t(pick(2) ? 16 : 0), int16_t(pick(2) ? 128 : key), 0.5, int16_t(pick(2)))); started.insert(nextId++); break;
                default: ev.push_back(volume(t, int32_t(1 + pick(std::max(1, nextId - 1))), ch, key, pick(5) / 2.0)); break;
            }
        }
        // out of band between blocks
        if (b % 97 == 13) {
            sounding(forgotten, forgottenMidi);
            forgotten.insert(owedIds.begin(), owedIds.end()); forgottenMidi += owedMidi; owedIds.clear(); owedMidi = 0;
            a.p->reset(a.p);
        }
        if (b % 211 == 50) { sounding(owedIds, owedMidi); Stream s; s.data = snap.data; a.contexts->state->load(a.p, &s.in); }
        if (b % 173 == 7) { Stream s; s.data = st.data; a.state->load(a.p, &s.in); }
        if (b % 37 == 3) a.contexts->select(a.p, ids[pick(3)] == 0 ? 0 : ids[pick(3)], int16_t(pick(4) == 3 ? -1 : pick(3)));
        if (b % 61 == 5) {
            const uint32_t lim = pick(3) ? uint32_t(pick(12)) : 0;
            oscairn_voice_budget_context_t c{ids[1], uint32_t(pick(4)), uint32_t(pick(3)), uint8_t(pick(11)), 0};
            a.budget->set_limits(a.p, lim, &c, 1);
        }
        a.block(frames, ev);
        owedIds.clear(); owedMidi = 0;  // a processed block delivered the owed ENDs
    }
    // finish: nothing may stay held, everything must have ended
    a.block(128, {off(0, -1, -1, -1), midi(0, 0xb0, 123, 0), midi(0, 0xb1, 123, 0), midi(0, 0xb2, 123, 0)});
    a.run(48000);
    a.block(128, {choke(0, -1, -1, -1)});
    a.run(4800);
    CHECK_EQ(a.voices(), 0u);
    int ids_ended = 0, midiEnds = 0, extra = 0;
    std::map<int32_t, int> per;
    for (auto& r : a.log) if (r.type == CLAP_EVENT_NOTE_END) { if (r.id < 0) ++midiEnds; else ++per[r.id]; }
    for (auto id : started) { if (forgotten.count(id)) { if (per[id] == 0) ++ids_ended; else { ++extra; std::printf("  END for note %d forgotten by a reset\n", id); } } else if (per[id] == 1) ++ids_ended; else { ++extra; if (extra < 5) std::printf("  note %d ended %d times\n", id, per[id]); } }
    for (auto& [id, k] : per) if (!started.count(id)) { ++extra; std::printf("  END for unknown id %d\n", id); }
    CHECK_EQ(ids_ended, int(started.size()));
    CHECK_EQ(extra, 0);
    CHECK_EQ(midiEnds, midiOns - forgottenMidi);
    CHECK(!a.badStatus && !a.badTime);
    std::printf("  seed %u: %d blocks, %zu CLAP notes and %d MIDI notes, each ended once or forgotten by a reset (%zu CLAP, %d MIDI)\n", seed, blocks, started.size(), midiOns, forgotten.size(), forgottenMidi);
}
TEST(random_note_lifetime) {
    randomRun(1, nullptr, 6000);
    randomRun(2, "5", 6000);
    randomRun(3, "1", 3000);
}

// Output ports: a sound with split_outputs plays each element on its connection's port (0 thru, 1 to
// insertion A, 2 to insertion B); without it, or with a host that gives the main port only, everything
// is on the main port, the same samples; so it is when the host gives the ports but never activates them.
TEST(output_ports) {
    const std::string routed = "oscairn-sampler-sound/0\npart split_outputs=1\nsample sine.osp\n"
        "element ampeg_decay1=127 ampeg_decay2=127 element_connection=1\nelement ampeg_decay1=127 ampeg_decay2=127 element_connection=2\n"
        "region key=0-127 root=57 element=0 sample=0 loop=0-44100\nregion key=0-127 root=69 element=1 sample=0 loop=0-44100\n";
    auto energy = [](const std::vector<float>& x) { double e = 0; for (float v : x) e += double(v) * v; return e; };
    std::vector<float> sum;
    for (int mode = 0; mode < 4; ++mode) {  // 0: split, three ports; 1: split, main only; 2: not split, three ports; 3: split, three not activated
        Inst a;
        a.outputs = mode == 1 ? 1 : 3;
        a.activatePorts = mode != 3;
        std::string snd = routed;
        if (mode == 2) snd.replace(snd.find("split_outputs=1"), 15, "split_outputs=0");
        CHECK(a.load(stateWith(snd)));
        a.block(128, {on(0, 1, 0, 60, 0.8)});
        a.run(4096);
        if (mode == 0) {
            CHECK(energy(a.L) < 1e-12);                       // nothing thru
            CHECK(energy(a.PL[0]) > 1 && energy(a.PL[1]) > 1); // element 1 on A, element 2 on B
            sum.resize(a.L.size());
            for (size_t i = 0; i < sum.size(); ++i) sum[i] = a.PL[0][i] + a.PL[1][i];
        } else {
            CHECK(energy(a.L) > 1);
            if (mode == 2) { CHECK(energy(a.PL[0]) < 1e-12 && energy(a.PL[1]) < 1e-12); }
            double worst = 0;
            for (size_t i = 0; i < sum.size() && i < a.L.size(); ++i) worst = std::max(worst, double(std::fabs(a.L[i] - sum[i])));
            CHECK(worst < 1e-6);  // the main port alone carries what the split ports did
        }
    }
}

// Each controller box source reaches its box: a box on the element level at ratio -64 silences the note with its
// source at the top and leaves it whole at the bottom (mod wheel CC 1, breath CC 2, foot controller CC 11, channel
// pressure as MIDI and as the CLAP pressure expression).
TEST(box_sources_drive_their_boxes) {
    struct { int src; Bytes lo, hi; } cases[] = {
        {1, midi(0, 0xb0, 1, 0), midi(0, 0xb0, 1, 127)}, {7, midi(0, 0xb0, 2, 0), midi(0, 0xb0, 2, 127)},
        {3, midi(0, 0xb0, 11, 0), midi(0, 0xb0, 11, 127)}, {2, midi(0, 0xd0, 0, 0), midi(0, 0xd0, 127, 0)},
    };
    for (auto& c : cases) {
        const std::string snd = "oscairn-sampler-sound/0\npart box1_src=" + std::to_string(c.src) + " box1_dst=60 box1_ratio=-64\nsample sine.osp\n"
                                "element ampeg_decay1=127 ampeg_decay2=127 ampeg_release=50\nregion key=0-127 root=57 element=0 sample=0 loop=0-44100\n";
        Inst a;
        CHECK(a.load(stateWith(snd)));
        a.block(128, {c.lo, on(0, 1, 0, 60, 1.0)});
        a.run(4800);
        const float whole = peak(a.L, a.L.size() - 1000);
        a.block(128, {c.hi});
        a.run(4800);
        const float down = peak(a.L, a.L.size() - 1000);
        if (!(whole > 0.01f && down < 1e-4f * whole)) { ++g_failures; std::printf("  source %d: %g then %g\n", c.src, whole, down); }
    }
    // the pressure note expression (CLAP), on the note's channel
    Inst a;
    CHECK(a.load(stateWith("oscairn-sampler-sound/0\npart box1_src=2 box1_dst=60 box1_ratio=-64\nsample sine.osp\n"
                           "element ampeg_decay1=127 ampeg_decay2=127 ampeg_release=50\nregion key=0-127 root=57 element=0 sample=0 loop=0-44100\n")));
    a.block(128, {on(0, 1, 0, 60, 1.0)});
    a.run(4800);
    const float whole = peak(a.L, a.L.size() - 1000);
    clap_event_note_expression_t e{};
    e.header = {sizeof e, 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_EXPRESSION, 0};
    e.expression_id = CLAP_NOTE_EXPRESSION_PRESSURE; e.note_id = 1; e.port_index = 0; e.channel = 0; e.key = 60; e.value = 1.0;
    a.block(128, {bytes(e)});
    a.run(4800);
    CHECK(whole > 0.01f && peak(a.L, a.L.size() - 1000) < 1e-4f * whole);
}

// Pitch bend moves the pitch by the part's bend range (default +2 / -2 semitones): the played 220 Hz sine, measured from
// its positive-going zero crossings over the last half second, is two semitones up at full bend up and two down at full
// bend down; the mod wheel with no box assigned changes nothing.
TEST(pitch_bend_moves_the_pitch) {
    auto hz = [](const std::vector<float>& v) {
        const size_t from = v.size() - 24000;
        size_t first = 0, last = 0, n = 0;
        for (size_t i = from + 1; i < v.size(); ++i)
            if (v[i - 1] < 0 && v[i] >= 0) { if (!n++) first = i; last = i; }
        return n > 1 ? double(n - 1) * kRate / double(last - first) : 0.0;
    };
    struct { uint8_t lsb, msb; double semitones; } cases[] = {{0, 64, 0}, {127, 127, 2}, {0, 0, -2}};
    for (auto& c : cases) {
        Inst a;
        a.block(128, {midi(0, 0xe0, c.lsb, c.msb), on(0, 1, 0, 57, 1.0)});
        a.run(48000);
        const double want = 220 * std::pow(2.0, c.semitones / 12), got = hz(a.L);
        if (!(std::fabs(got - want) < 0.005 * want)) { ++g_failures; std::printf("  bend %d: %g Hz, wanted %g\n", int(c.semitones), got, want); }
    }
    Inst wheel, plain;
    wheel.block(128, {midi(0, 0xb0, 1, 127), on(0, 1, 0, 57, 1.0)});
    plain.block(128, {on(0, 1, 0, 57, 1.0)});
    wheel.run(9600); plain.run(9600);
    CHECK(peak(plain.L) > 0.01f && wheel.L == plain.L && wheel.R == plain.R);
}

// One-shot regions (no loop) whose sample ends before the note and before the release would: the voice ends at the
// sample's end, active_voices returns to 0 and each note gets one NOTE_END, as a normal element and as a key-off
// element, in any block size; also 26 such notes and a minute of silence.
TEST(one_shot_regions_end_their_voices) {
    const char* snd = "oscairn-sampler-sound/0\nsample sine.osp\n"
                      "element ampeg_decay1=127 ampeg_decay2=127 ampeg_release=127\n"
                      "element ampeg_decay1=127 ampeg_decay2=127 ampeg_release=127 xa_control=2\n"
                      "region key=0-127 root=57 element=0 sample=0\nregion key=0-127 root=57 element=1 sample=0\n";
    for (uint32_t block : {128u, 96u, 64u, 32u}) {
        Inst a;
        CHECK(a.load(stateWith(snd)));
        a.block(block, {on(0, 1, 0, 57, 1.0)});
        a.run(72000, block);  // 1.5 s: past the 1 s sample
        a.block(block, {off(0, 1, 0, 57)});
        a.run(48000 * 4, block);  // the key-off element's 1 s and more
        if (!(a.voices() == 0 && a.ends(1) == 1)) { ++g_failures; std::printf("  block %u: %u voices, %d ends\n", block, a.voices(), a.ends(1)); }
        CHECK(silent(a, a.L.size() - 48000));
    }
    Inst a;
    CHECK(a.load(stateWith(snd)));
    for (int n = 0; n < 26; ++n) {
        a.block(128, {on(0, n + 1, 0, int16_t(40 + n), 1.0)});
        a.run(4800);
        a.block(128, {off(0, n + 1, 0, int16_t(40 + n))});
    }
    a.run(48000 * 60);
    CHECK_EQ(a.voices(), 0u);
    int ok = 0;
    for (int n = 0; n < 26; ++n) ok += a.ends(n + 1) == 1;
    CHECK_EQ(ok, 26);
}

// Pad-shaped voices end by the release rule: the amplitude envelope's release falls in dB at its index's rate (seconds
// per 60 dB) and the voice ends when the envelope reaches -96.45 dB, from any level (a release entered again keeps its
// rate from where it stands), so a release at index i lasts at most per60(i) x 96.45 / 60. The longest, index 127, is
// 105.72 s per 60 dB: 169.95 s. Forward loops, the filter envelope closing in its release, the release index moved by a
// box or a parameter every block, the destination offsets set every block, the half-damper pedal moved every block:
// active_voices returns to 0 within that bound (plus a block), not before the unmodulated release's own end, and each
// note gets one NOTE_END.
TEST(pad_voices_end_by_the_release_rule) {
    const double per60_127 = 105.719, per60_110 = 26.4298;  // laws::releaseSecondsPer60dB(127), (110)
    const long bound127 = long(per60_127 * 96.45 / 60 * kRate) + 256, bound110 = long(per60_110 * 96.45 / 60 * kRate) + 256;
    const std::string pad = "oscairn-sampler-sound/0\npart box1_src=1 box1_dst=78 box1_ratio=63\nsample sine.osp\n"
                            "element ampeg_attack=30 ampeg_decay1=127 ampeg_decay2=127 ampeg_release=110 fil_type=0 fil_cutoff=600 fileg_depth=40 "
                            "fileg_release=100 fileg_release_level=0 half_damper=1 half_damper_time=127\n"
                            "region key=0-127 root=57 element=0 sample=0 loop=0-44100\n";
    struct Case { const char* what; int mode; long bound; };
    const Case cases[] = {{"release 110, still", 0, bound110}, {"the box on the release moved every block (to index 127)", 1, bound127},
                          {"the release parameter set and modulated every block", 2, bound127}, {"offsets on cutoff and level every block", 3, bound110},
                          {"the half-damper pedal moved every block", 4, bound127}};
    for (const Case& c : cases) {
        Inst a;
        CHECK(a.load(stateWith(pad)));
        a.block(128, {midi(0, 0xb0, 1, 0), on(0, 1, 0, 57, 1.0)});
        a.run(48000);
        a.block(128, {off(0, 1, 0, 57)});
        long t = 0, end = -1;
        for (int b = 0; t < c.bound + 48000; ++b, t += 128) {
            std::vector<Bytes> ev;
            switch (c.mode) {
            case 1: ev.push_back(midi(0, 0xb0, 1, uint8_t(b % 2 ? 127 : 0))); break;
            case 2: ev.push_back(param(0, 24, b % 2 ? 17 : -40)); ev.push_back(mod(0, 24, b % 3 ? 5 : -5)); break;
            case 3: ev.push_back(param(0, 14, double(b % 200 - 100))); ev.push_back(mod(0, 16, b % 2 ? 10 : -10)); break;
            case 4: ev.push_back(midi(0, 0xb0, 64, uint8_t(b * 7 % 128))); break;
            }
            a.block(128, ev);
            if (end < 0 && a.voices() == 0) end = t;
        }
        if (!(end >= 0 && end <= c.bound && a.ends(1) == 1)) { ++g_failures; std::printf("  %s: ended at %ld frames (bound %ld), %d NOTE_ENDs\n", c.what, end, c.bound, a.ends(1)); }
        // still: it ends once its output is below -96 dBFS, its envelope at -96 + 9.43 (staging) + 5.45 (fil_gain 230) dB
        const long quiet110 = long(per60_110 * (96 - 9.43 - 5.45) / 60 * kRate);
        if (c.mode == 0 && std::labs(end - quiet110) > 512) { ++g_failures; std::printf("  %s: ended at %ld, the quiet end at %ld frames\n", c.what, end, quiet110); }
    }
}

// Key assign (part parameter key_assign). The test sound with key_assign set to single (0) or multi (1).
static std::string keyAssignSound(int v) {
    return "oscairn-sampler-sound/0\npart key_assign=" + std::to_string(v) + "\nsample sine.osp\n"
           "element ampeg_decay1=127 ampeg_decay2=127 ampeg_release=50\n"
           "region key=0-127 root=57 element=0 sample=0 loop=0-44100\n";
}
// RMS of the main port's left channel over [from, to).
static double rmsL(const Inst& a, size_t from, size_t to) {
    double s = 0;
    for (size_t i = from; i < to; ++i) s += double(a.L[i]) * a.L[i];
    return std::sqrt(s / double(to - from));
}
// Key 57 (the root: 220 Hz) struck n times with the pedal down, 4800 frames apart (22 whole cycles: the notes in phase),
// each strike's key let up just before the next (at the same frame); then 4800 frames more.
static constexpr long kStrikes = 4800;
static void restrike(Inst& a, int n) {
    a.block(96, {midi(0, 0xb0, 64, 127), on(0, 1, 0, 57, 1.0)});
    a.run(kStrikes - 96, 96);
    for (int k = 2; k <= n; ++k) {
        a.block(96, {off(0, k - 1, 0, 57), on(0, k, 0, 57, 1.0)});
        a.run(kStrikes - 96, 96);
    }
}

// Single: a key struck again ends the sound's earlier note on it, held by the pedal, with the 3 ms choke fade and its
// NOTE_END; one voice sounds, at one strike's level. Multi: the notes stack, as for a sound saved without the parameter
// (the default is multi).
TEST(key_assign_single_ends_the_earlier_note_on_its_key) {
    const int n = 8;
    Inst a;
    CHECK(a.load(stateWith(keyAssignSound(0))));
    restrike(a, n);
    const double one = rmsL(a, 2400, kStrikes), last = rmsL(a, (n - 1) * kStrikes + 2400, n * kStrikes);
    CHECK(one > 0.01 && std::fabs(last / one - 1) < 0.02);
    CHECK_EQ(a.voices(), 1u);
    for (int k = 1; k < n; ++k) {  // each ended once, when the fade from the next strike finished
        const auto* e = a.endOf(k);
        const long t = long(k) * kStrikes;
        CHECK(a.ends(k) == 1 && e && e->key == 57 && e->channel == 0 && e->frame >= t + long(kChoke) - 1 && e->frame < t + long(kChoke) + 2 * 96);
    }
    CHECK_EQ(a.ends(n), 0);
    a.block(96, {off(0, n, 0, 57), midi(0, 0xb0, 64, 0)});
    a.run(24000);
    CHECK_EQ(a.endsTotal(), n);
    CHECK_EQ(a.voices(), 0u);

    Inst m;
    CHECK(m.load(stateWith(keyAssignSound(1))));
    restrike(m, n);
    const double mOne = rmsL(m, 2400, kStrikes), mLast = rmsL(m, (n - 1) * kStrikes + 2400, n * kStrikes);
    CHECK(mOne > 0.01 && mLast > 0.9 * n * mOne);  // in phase: n times one note's level
    CHECK_EQ(m.voices(), uint32_t(n));
    CHECK_EQ(m.endsTotal(), 0);
    m.block(96, {off(0, n, 0, 57), midi(0, 0xb0, 64, 0)});
    m.run(24000);
    CHECK_EQ(m.endsTotal(), n);

    Inst d;  // kSound has no key_assign: the default, multi
    restrike(d, n);
    CHECK_EQ(d.voices(), uint32_t(n));
    CHECK_EQ(d.endsTotal(), 0);
}

// Single ends the notes of its own sound on its own key only: another key, and another sound sharing the instance on the
// same key, play on; a released note still ringing on the key ends too.
TEST(key_assign_single_leaves_other_keys_and_sounds) {
    Inst a;
    CHECK(a.load(stateWith(keyAssignSound(0))));
    char msg[256] = {};
    const uint32_t other = a.contexts->prepare(a.p, "other", nullptr, 0, patchWith("level=1").c_str(), testBase(), msg, sizeof msg);
    CHECK(other != CLAP_INVALID_ID);
    a.contexts->select(a.p, other, 1);
    a.block(128, {on(0, 1, 0, 60), on(0, 2, 0, 64), on(0, 3, 1, 60)});
    a.run(2400);
    a.block(128, {off(0, 1, 0, 60)});  // released: in its release (about 155 ms)
    a.run(1280);
    CHECK_EQ(a.ends(1), 0);
    a.block(128, {on(0, 4, 0, 60)});
    a.run(1280);
    CHECK(a.ends(1) == 1 && a.ends(2) == 0 && a.ends(3) == 0 && a.ends(4) == 0);
    CHECK_EQ(a.voices(), 3u);
    a.block(128, {off(0, 2, 0, 64), off(0, 3, 1, 60), off(0, 4, 0, 60)});
    a.run(24000);
    CHECK_EQ(a.endsTotal(), 4);
}

// The earlier note fades out: struck again a quarter cycle off the first note's phase (the old note near its peak), the
// largest step from one frame to the next stays within that of the two notes' sum at rest, no cut.
TEST(key_assign_single_fades_the_earlier_note) {
    Inst a;
    CHECK(a.load(stateWith(keyAssignSound(0))));
    a.block(96, {on(0, 1, 0, 57, 1.0)});
    a.run(kStrikes - 96, 96);
    const size_t t = size_t(kStrikes) + 55;  // 55 frames: a quarter of the 218-frame cycle
    a.block(96, {on(55, 2, 0, 57, 1.0)});
    a.run(4800, 96);
    CHECK(a.ends(1) == 1 && a.ends(2) == 0);
    float steady = 0, cut = 0;
    for (size_t i = 2401; i < size_t(kStrikes); ++i) steady = std::max(steady, std::fabs(a.L[i] - a.L[i - 1]));
    for (size_t i = t - 8; i < t + kChoke + 8; ++i) cut = std::max(cut, std::fabs(a.L[i] - a.L[i - 1]));
    CHECK(steady > 0 && cut < 2.5f * steady);
}
