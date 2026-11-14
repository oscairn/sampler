// SPDX-License-Identifier: AGPL-3.0-or-later
// oscairn.sound-contexts: preparing, selecting and discarding sounds, and that a sound's notes,
// parameters and voices stay its own.
#include "host.h"

namespace {
uint32_t prepare(Inst& a, const char* name, const char* patch, char* msg = nullptr, uint32_t cap = 0) {
    return a.contexts->prepare(a.p, name, nullptr, 0, patch, testBase(), msg, cap);
}
// RMS of the last `n` frames of a channel.
double rms(const std::vector<float>& v, size_t n) {
    double s = 0; const size_t from = v.size() - n;
    for (size_t i = from; i < v.size(); ++i) s += double(v[i]) * v[i];
    return std::sqrt(s / double(n));
}
}  // namespace

TEST(prepare_select_discard_lifecycle) {
    Inst a;
    CHECK(a.contexts->capacity(a.p) >= 2 && a.contexts->capacity(a.p) <= 256);
    oscairn_sound_context_info_t info{};
    CHECK(a.contexts->get(a.p, 0, &info) && info.id == 0 && !std::strcmp(info.name, "Default") && info.selected_channels == 0xffff);
    char msg[128] = {};
    CHECK_EQ(prepare(a, "x", patchWith("level=nope").c_str(), msg, sizeof msg), CLAP_INVALID_ID);
    CHECK(std::strstr(msg, "line 1") != nullptr);
    CHECK_EQ(prepare(a, "x", patchWith("volume=1").c_str(), msg, sizeof msg), CLAP_INVALID_ID);
    CHECK_EQ(prepare(a, "x", patchWith("pan=2").c_str(), msg, sizeof msg), CLAP_INVALID_ID);
    // a sample the host cannot resolve, or no base to resolve it from
    CHECK_EQ(prepare(a, "x", "oscairn-pcm-sound/0\nsample none.wav\nelement\nregion element=0 sample=0\n", msg, sizeof msg), CLAP_INVALID_ID);
    CHECK(std::strstr(msg, "none.wav") != nullptr);
    CHECK_EQ(a.contexts->prepare(a.p, "x", nullptr, 0, kSound, nullptr, msg, sizeof msg), CLAP_INVALID_ID);
    oscairn_sound_parameter_t bad{99, 1};
    CHECK_EQ(a.contexts->prepare(a.p, "x", &bad, 1, nullptr, nullptr, msg, sizeof msg), CLAP_INVALID_ID);
    CHECK_EQ(prepare(a, std::string(64, 'n').c_str(), nullptr, msg, sizeof msg), CLAP_INVALID_ID);
    const uint32_t b = prepare(a, "b", patchWith("level=0.5").c_str());
    CHECK(b != CLAP_INVALID_ID && b != 0);
    CHECK(a.contexts->get(a.p, 1, &info) && info.id == b && info.idle && info.selected_channels == 0);
    CHECK(!a.contexts->select(a.p, b + 100, 0) && !a.contexts->select(a.p, b, 16) && !a.contexts->select(a.p, b, -2));
    CHECK(a.contexts->select(a.p, b, 5));
    CHECK(!a.contexts->discard(a.p, b));      // a selection is queued: pinned
    a.block(128);
    CHECK(a.contexts->get(a.p, 1, &info) && info.selected_channels == (1u << 5) && !info.idle);
    CHECK(!a.contexts->discard(a.p, b));      // selected
    CHECK(!a.contexts->discard(a.p, 0));      // the default is protected
    CHECK(a.contexts->select(a.p, 0, 5));
    a.block(128);
    CHECK(a.contexts->discard(a.p, b));
    CHECK(!a.contexts->get(a.p, 1, &info));
    const uint32_t c = prepare(a, "c", nullptr);
    CHECK(c != CLAP_INVALID_ID && c != b);    // IDs are never reused
    // fill every slot
    uint32_t made = 1;
    while (prepare(a, "f", nullptr) != CLAP_INVALID_ID) ++made;
    CHECK_EQ(made + 1, a.contexts->capacity(a.p) - 0u);
    CHECK_EQ(prepare(a, "g", nullptr, msg, sizeof msg), CLAP_INVALID_ID);
    CHECK(std::strstr(msg, "no free") != nullptr);
}

TEST(sounds_play_apart_and_hold_their_own_voice) {
    Inst a;
    const uint32_t left = prepare(a, "left", patchWith("level=0.8 pan=-1").c_str());
    const uint32_t right = prepare(a, "right", patchWith("level=0.8 pan=1").c_str());
    a.contexts->select(a.p, left, 0);
    a.contexts->select(a.p, right, 1);
    a.block(128);
    // channel 0 plays the left sound, channel 1 the right
    a.block(128, {on(0, 1, 0, 60)});
    a.run(4800);
    CHECK(rms(a.L, 2400) > 0.01 && rms(a.R, 2400) < 1e-6);
    a.clearAudio();
    a.block(128, {off(0, 1, 0, 60), on(0, 2, 1, 60)});
    a.run(24000);
    CHECK(rms(a.R, 2400) > 0.01 && rms(a.L, 2400) < 1e-6);
    // a held note stays on the sound it started on when the channel is selected elsewhere
    a.block(128, {off(0, 2, 1, 60), on(0, 3, 0, 62)});
    a.run(2400);
    a.contexts->select(a.p, right, 0);
    a.block(128, {on(0, 4, 0, 64)});  // new notes follow the new selection
    a.run(4800);
    a.clearAudio();
    a.block(128, {off(0, 4, 0, 64)});
    a.run(24000);
    a.clearAudio();
    a.run(2400);
    CHECK(rms(a.L, 2400) > 0.01);       // note 3 still sounds on the left sound
    a.block(128, {off(0, 3, 0, 62)});
    a.run(24000);
    CHECK_EQ(a.endsTotal(), 4);
    oscairn_sound_context_info_t info{};
    CHECK(a.contexts->get(a.p, 1, &info) && info.active_voices == 0 && info.idle);  // the left sound: no channel, no voice
}

TEST(parameters_reach_only_the_sounds_the_channel_selects) {
    Inst a;
    const uint32_t b = prepare(a, "b", patchWith("level=0.5 pan=0").c_str());
    a.contexts->select(a.p, b, 1);
    a.block(128);
    a.block(128, {on(0, 1, 0, 60, 1.0), on(0, 2, 1, 60, 1.0)});
    a.run(4800);
    const double both = rms(a.L, 2400);
    a.block(128, {param(0, 0, 0.0, 0)});  // level 0 for channel 0's sound only
    a.run(4800);
    const double only = rms(a.L, 2400);
    CHECK(only > 0.3 * both && only < 0.9 * both);  // the other sound still sounds
    // the sound b still holds its own level and the default's value shows on channel 0
    CHECK_EQ(a.value(0), 0.0);
    a.contexts->select(a.p, b, 0);
    a.block(128);
    CHECK(std::fabs(a.value(0) - 0.5) < 1e-6);  // a selection for one channel; channel 0 shows b
    // the default sound was not touched by b's selection: its level is still 0
    a.contexts->select(a.p, 0, 0);
    a.block(128);
    CHECK_EQ(a.value(0), 0.0);
}

TEST(selecting_for_every_channel_reports_the_new_values) {
    Inst a;
    const uint32_t b = prepare(a, "b", patchWith("level=0.3 pan=-0.25 polyphony=5").c_str());
    a.contexts->select(a.p, b, -1);
    a.block(128);
    CHECK(std::fabs(a.value(0) - 0.3) < 1e-6 && std::fabs(a.value(1) + 0.25) < 1e-6 && a.value(2) == 5);
    int changed = 0;
    for (auto& r : a.log) changed += r.type == CLAP_EVENT_PARAM_VALUE;
    CHECK_EQ(changed, 3);  // the host is told, for each parameter that changed
}

TEST(a_sound_plays_only_its_key_range) {
    Inst a;
    char msg[160] = {};
    const uint32_t b = prepare(a, "zone", patchWith("level=1", kZoneSound).c_str(), msg, sizeof msg);
    CHECK(b != CLAP_INVALID_ID);
    CHECK_EQ(prepare(a, "x", "oscairn-pcm-sound/0\nsample sine.wav\nelement fil_cutoff=5000\nregion element=0 sample=0\n", msg, sizeof msg), CLAP_INVALID_ID);  // a raw value out of range
    CHECK(std::strstr(msg, "line 3") != nullptr && std::strstr(msg, "fil_cutoff=5000") != nullptr);
    a.contexts->select(a.p, b, 0);
    a.block(128);
    a.block(128, {on(0, 1, 0, 60)});  // outside the sound's keys: no voice, the note ends at once
    CHECK_EQ(a.ends(1), 1);
    CHECK_EQ(a.voices(), 0u);
    a.block(128, {on(0, 2, 0, 84)});
    a.run(2400);
    CHECK_EQ(a.voices(), 1u);
    CHECK(rms(a.L, 2400) > 1e-3);
}
