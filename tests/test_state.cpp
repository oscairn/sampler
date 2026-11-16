// SPDX-License-Identifier: AGPL-3.0-or-later
// Parameters and state: metadata, values set by events, and a versioned state blob that round
// trips byte for byte and refuses anything malformed without changing the instance.
#include "host.h"

TEST(parameter_metadata) {
    Inst a(false);
    const uint32_t n = a.params->count(a.p);
    CHECK_EQ(n, 27u);  // the shell's 26 and the read-only shed count
    std::vector<clap_id> seen;
    for (uint32_t i = 0; i < n; ++i) {
        clap_param_info_t info{};
        CHECK(a.params->get_info(a.p, i, &info));
        CHECK(std::find(seen.begin(), seen.end(), info.id) == seen.end());
        seen.push_back(info.id);
        CHECK(std::isfinite(info.min_value) && info.min_value <= info.default_value && info.default_value <= info.max_value);
        CHECK(info.name[0] != 0);
        CHECK(std::fabs(a.value(info.id) - info.default_value) < 1e-6);
        char text[64] = {};
        CHECK(a.params->value_to_text(a.p, info.id, info.default_value, text, sizeof text) && text[0]);
        double back = -9;
        CHECK(a.params->text_to_value(a.p, info.id, text, &back) && std::fabs(back - info.default_value) < 1e-2);
    }
    clap_param_info_t none{};
    CHECK(!a.params->get_info(a.p, n, &none));
}

TEST(parameter_events_change_values_and_sound) {
    Inst a;
    a.block(128, {on(0, 1, 0, 60, 1.0)});
    a.run(4800);
    const float loud = peak(a.L, a.L.size() - 1000);
    a.block(128, {param(0, 0, 0.1), param(0, 1, 1.0), param(0, 2, 12.4)});
    a.run(4800);
    CHECK(std::fabs(a.value(0) - 0.1) < 1e-6 && a.value(1) == 1.0 && a.value(2) == 12);  // stepped: rounded
    const float soft = peak(a.L, a.L.size() - 1000);
    CHECK(soft < 0.2f * loud);
    CHECK(peak(a.L, a.L.size() - 1000) < 1e-5f);  // panned hard right: no left
    CHECK(peak(a.R, a.R.size() - 1000) > 0.f);
    a.block(128, {param(0, 0, 7.0), param(0, 99, 1.0), param(0, 1, NAN)});  // clamped, unknown id, non-finite: ignored
    CHECK(a.value(0) == 1.0 && a.value(1) == 1.0);
}

TEST(state_round_trip_is_byte_identical) {
    Inst a;
    a.block(128, {param(0, 0, 0.3), param(0, 1, -0.5), param(0, 2, 10)});
    const std::string s = a.save();
    CHECK(s.rfind("oscairn-sampler-state 3\n", 0) == 0);
    Inst b;
    CHECK(b.load(s));
    CHECK_EQ(b.save() == s, true);
    CHECK(std::fabs(b.value(0) - 0.3) < 1e-6 && std::fabs(b.value(1) + 0.5) < 1e-6 && b.value(2) == 10);
    // and it plays as the saved instance does
    a.clearAudio(); b.clearAudio();
    a.block(128, {on(0, 1, 0, 60)}); b.block(128, {on(0, 1, 0, 60)});
    a.run(4800); b.run(4800);
    CHECK(a.L == b.L && a.R == b.R);
    // the default state round trips too
    Inst d1, d2;
    CHECK(d2.load(d1.save()) && d2.save() == d1.save());
}

TEST(state_refuses_what_is_malformed) {
    Inst a;
    a.block(128, {param(0, 0, 0.3)});
    const std::string good = a.save();
    Inst b;
    const std::string before = b.save();
    const std::string sound = b64(kSound);
    const std::string bad[] = {"", "oscairn-sampler-state 1\n0.5 0 8\n", "oscairn-sampler-state 1\n", "oscairn-sampler-state 0\n0.5 0 8 \"test\" \"\" -\n",
                               "oscairn-sampler-state 1\n2 0 8 \"test\" \"\" -\n", "oscairn-sampler-state 1\nnan 0 8 \"test\" \"\" -\n",
                               "oscairn-sampler-state 1\n0.5 0 0 \"test\" \"\" -\n", "oscairn-sampler-state 1\n0.5 0 8 \"test\" \"\" - 9\n",
                               "oscairn-sampler-state 1\n0.5 0 8 \"test\" \"\" -\ngarbage\n", "not a state",
                               "oscairn-sampler-state 1\n0.5 0 8 \"test\" \"\" " + b64(std::string(kSound).substr(0, std::strlen(kSound) - 30)) + "\n",  // a cut sound
                               "oscairn-sampler-state 1\n0.5 0 8 \"elsewhere\" \"\" " + sound + "\n",                         // samples that do not resolve
                               "oscairn-sampler-state 1\n0.5 0 8 \"test\" \"\" " + b64("oscairn-sampler-sound/0\nsample sine.osp\n") + "\n",  // no region
                               "oscairn-sampler-state 1\n0.5 0 8 \"test\" \"\" " + sound.substr(0, sound.size() - 1) + "\n"};  // not base64
    for (auto& s : bad) { CHECK(!b.load(s)); CHECK(b.save() == before); }
    CHECK(!b.load(std::string(1 << 20, 'x')));
    CHECK(b.save() == before);
    CHECK(b.load(good));
}

TEST(sound_context_snapshot_round_trip) {
    Inst a;
    char msg[64];
    const uint32_t b = a.contexts->prepare(a.p, "bright \"b\"", nullptr, 0, patchWith("level=0.25 pan=0.5 polyphony=7").c_str(), testBase(), msg, sizeof msg);
    CHECK(b != CLAP_INVALID_ID);
    a.contexts->select(a.p, b, 2);
    a.block(128);
    Stream s;
    CHECK(a.contexts->state->save(a.p, &s.out));
    CHECK(s.data.rfind("oscairn-sampler-contexts/3\n", 0) == 0);
    Inst c;
    Stream in; in.data = s.data;
    CHECK(c.contexts->state->load(c.p, &in.in));
    Stream s2;
    CHECK(c.contexts->state->save(c.p, &s2.out));
    // contexts get fresh IDs on load, which the snapshot does not carry
    CHECK(s2.data == s.data);
    oscairn_sound_context_info_t info{};
    CHECK(c.contexts->get(c.p, 1, &info) && !std::strcmp(info.name, "bright \"b\"") && (info.selected_channels == 4));
    // a snapshot that is wrong changes nothing
    const char* bad[] = {"", "oscairn-sampler-contexts/2\n", "oscairn-sampler-contexts/2\n0\n", "oscairn-sampler-contexts/1\n1 \"Default\" 0.5 0 8 \"\" \"\" -\n"};
    for (auto* t : bad) { Stream x; x.data = t; CHECK(!c.contexts->state->load(c.p, &x.in)); }
    Stream s3;
    CHECK(c.contexts->state->save(c.p, &s3.out) && s3.data == s.data);
}

// The plugin previously published under this id was a different engine; a Performance saved against it hands
// its state (a header line, then length-prefixed fields) and its patch text to this plugin, which must refuse
// them and stay as it was.
TEST(state_from_the_plugin_previously_under_this_id_is_refused) {
    auto field = [](const std::string& f) { return std::to_string(f.size()) + "\n" + f + "\n"; };
    const std::string patchText = "<region> sample=kick.wav key=60";
    const std::string foreign = "oscairn-sampler-4\n" + field("") + field("") + field("") + field(patchText) + field("0");
    const std::string withNul = "oscairn-sampler-4\n" + field("") + field("") + field("") + field(std::string("<region> sample=a\0b.wav", 23)) + field("0");
    const std::string snapshot = "oscairn-sampler-contexts-3\n" + field("1") + field("Default") + field("") + field("") + field("") + field(patchText) + field("0");
    Inst a;
    a.block(128, {param(0, 0, 0.3)});
    const std::string before = a.save();
    // this plugin's own state under the header of the id it used before (no reader for that header)
    const std::string oldHeader = "oscairn-pcm-state 3" + before.substr(std::strlen("oscairn-sampler-state 3"));
    CHECK(before.rfind("oscairn-sampler-state 3\n", 0) == 0);
    CHECK(!a.load(oldHeader) && a.save() == before);
    for (const std::string* s : {&foreign, &withNul, &snapshot}) { CHECK(!a.load(*s)); CHECK(a.save() == before); }
    Stream ctxBefore; CHECK(a.contexts->state->save(a.p, &ctxBefore.out));
    for (const std::string* s : {&foreign, &withNul, &snapshot, &oldHeader}) {
        Stream x; x.data = *s;
        CHECK(!a.contexts->state->load(a.p, &x.in));
    }
    Stream ctxAfter; CHECK(a.contexts->state->save(a.p, &ctxAfter.out));
    CHECK(ctxAfter.data == ctxBefore.data);
    char msg[256] = {};
    CHECK_EQ(a.contexts->prepare(a.p, "x", nullptr, 0, patchText.c_str(), testBase(), msg, sizeof msg), CLAP_INVALID_ID);
    CHECK(msg[0] != 0);
    CHECK(a.save() == before);
    // the instance still plays its sound
    a.clearAudio();
    a.block(128, {on(0, 1, 0, 60)});
    a.run(4800);
    CHECK(peak(a.L, 0) > 0.f);
}

TEST(memory_is_declared) {
    Inst a(false);
    oscairn_memory_t m{};
    CHECK(a.memory->estimate(a.p, kRate, 4096, &m));
    CHECK(m.shared_bytes > 0 && m.context_bytes > 0 && m.asset_bytes >= 4096 * 2 && m.asset_bytes < 44100 * 2);  // the default sound: its head in memory, the rest streams
    a.p->activate(a.p, kRate, 1, 4096);
    oscairn_memory_t c{};
    CHECK(a.memory->current(a.p, &c) && c.shared_bytes > 0);
    a.p->deactivate(a.p);
}
