// SPDX-License-Identifier: AGPL-3.0-or-later
// oscairn.voice-budget: the host's limits, the order voices are given up in, per-sound limits,
// the device caps from the environment, and what notes() and active_voices report.
#include "host.h"

namespace {
struct Env {
    explicit Env(const char* instance, const char* sound = nullptr) {
        set("OSCAIRN_SAMPLER_MAX_VOICES", instance); set("OSCAIRN_SAMPLER_MAX_VOICES_PER_SOUND", sound);
    }
    ~Env() { set("OSCAIRN_SAMPLER_MAX_VOICES", nullptr); set("OSCAIRN_SAMPLER_MAX_VOICES_PER_SOUND", nullptr); }
    static void set(const char* n, const char* v) { if (v) setenv(n, v, 1); else unsetenv(n); }
};
// Notes ended so far, as a set of ids.
std::vector<int> ended(const Inst& a) {
    std::vector<int> v;
    for (auto& r : a.log) if (r.type == CLAP_EVENT_NOTE_END) v.push_back(r.id);
    std::sort(v.begin(), v.end());
    return v;
}
}  // namespace

TEST(host_limit_gives_up_the_oldest_held) {
    Inst a;
    a.limits(3);
    for (int i = 0; i < 6; ++i) {
        a.block(128, {on(0, 1 + i, 0, 60 + i)});
        a.run(640);  // the 3 ms fade finishes
        CHECK(a.voices() <= 3u);
    }
    CHECK(a.voices() == 3u);
    CHECK((ended(a) == std::vector<int>{1, 2, 3}));
    // raising the limit keeps what sounds; lowering it takes voices at the next block
    a.limits(0);
    a.block(128, {on(0, 7, 0, 70)});
    a.run(640);
    CHECK_EQ(a.voices(), 4u);
    a.limits(2);
    a.run(640);
    CHECK_EQ(a.voices(), 2u);
    CHECK((ended(a) == std::vector<int>{1, 2, 3, 4, 5}));
}

TEST(released_voices_go_before_held_and_the_quietest_first) {
    Inst a;
    a.limits(3);
    // three notes, loudness by volume expression (velocity does not change this sound); all released: the
    // quietest releasing one goes first
    a.block(128, {on(0, 1, 0, 60), on(0, 2, 0, 62), on(0, 3, 0, 64), volume(0, 1, 0, 60, 0.9), volume(0, 2, 0, 62, 0.2), volume(0, 3, 0, 64, 0.5)});
    a.run(1280);
    a.block(128, {off(0, 1, 0, 60), off(0, 2, 0, 62), off(0, 3, 0, 64)});
    a.run(256);
    a.block(128, {on(0, 4, 0, 66, 0.8)});
    a.run(640);
    CHECK((ended(a) == std::vector<int>{2}));
    a.block(128, {on(0, 5, 0, 67, 0.8)});
    a.run(640);
    CHECK((ended(a) == std::vector<int>{2, 3}));  // 3 (0.5) before 1 (0.9), while 4 is held
    a.block(128, {on(0, 6, 0, 69, 0.8)});
    a.run(640);
    CHECK((ended(a) == std::vector<int>{1, 2, 3}));
    a.block(128, {on(0, 7, 0, 71, 0.8)});  // only held voices remain: the oldest goes
    a.run(640);
    CHECK((ended(a) == std::vector<int>{1, 2, 3, 4}));
    CHECK_EQ(a.voices(), 3u);
}

TEST(per_sound_limit_and_reserve) {
    Inst a;
    char msg[64];
    const uint32_t b = a.contexts->prepare(a.p, "b", nullptr, 0, kSound, testBase(), msg, sizeof msg);
    a.contexts->select(a.p, b, 1);
    a.limits(0, {{b, 2, 0, 5, 0}});  // sound b: at most 2 voices
    a.block(128);
    for (int i = 0; i < 4; ++i) { a.block(128, {on(0, 10 + i, 1, 60 + i)}); a.run(640); }  // each reaches full level before the next: the oldest then goes
    CHECK((ended(a) == std::vector<int>{10, 11}));
    // the default sound is not limited by b's entry
    for (int i = 0; i < 4; ++i) { a.block(128, {on(0, 20 + i, 0, 60 + i)}); a.run(640); }
    CHECK_EQ(a.voices(), 6u);
    // a reserve on the default sound (ID 0): its held voices are not given up for b's note
    Inst c;
    const uint32_t b2 = c.contexts->prepare(c.p, "b", nullptr, 0, kSound, testBase(), msg, sizeof msg);
    c.contexts->select(c.p, b2, 1);
    c.limits(2, {{0, 0, 2, 5, 0}, {b2, 0, 0, 5, 0}});
    c.block(128);
    c.block(128, {on(0, 1, 0, 60), on(0, 2, 0, 62)});
    c.run(1280);
    c.block(128, {on(0, 3, 1, 64)});  // b's own note has to take one of the default's reserved voices
    c.run(640);
    // the default sound's two held voices are its reserve: the only voice that may go is b's own new one
    CHECK_EQ(c.voices(), 2u);
    CHECK((ended(c) == std::vector<int>{3}));
}

TEST(polyphony_parameter_limits_the_sound) {
    Inst a;
    a.block(128, {param(0, 2, 2)});
    for (int i = 0; i < 5; ++i) { a.block(128, {on(0, 1 + i, 0, 60 + i)}); a.run(640); }
    CHECK_EQ(a.voices(), 2u);
    CHECK((ended(a) == std::vector<int>{1, 2, 3}));
    CHECK(a.value(2) == 2);
}

TEST(device_caps_parse_like_analog) {
    // valid: a whole decimal 1..64 (instance and each sound); anything else is no cap
    struct Case { const char* v; uint32_t expect; } cases[] = {{"3", 3}, {"1", 1}, {"64", 64}, {"65", 0}, {"0", 0}, {"-2", 0}, {"abc", 0}, {"3x", 0}, {"", 0}, {"129", 0}, {" 3", 3 /*strtol skips leading space*/}};
    for (auto& c : cases) {
        Env env(c.v);
        Inst a;
        for (int i = 0; i < 70; ++i) a.block(128, {on(0, 1 + i, 0, 30 + i % 60)});
        a.run(640);
        const uint32_t want = c.expect ? c.expect : 64;  // no cap: the pool (64 voices)
        if (a.voices() != want) std::printf("  MAX_VOICES=\"%s\": %u voices, wanted %u\n", c.v, a.voices(), want);
        CHECK_EQ(a.voices(), want);
    }
    {
        Env env(nullptr, "65");  // above the per-sound ceiling: ignored
        Inst a;
        for (int i = 0; i < 70; ++i) a.block(128, {on(0, 1 + i, 0, 30 + i % 60)});
        a.run(640);
        CHECK_EQ(a.voices(), 64u);
    }
    {
        Env env(nullptr, "2");  // each sound: 2
        Inst a;
        char msg[64];
        const uint32_t b = a.contexts->prepare(a.p, "b", nullptr, 0, kSound, testBase(), msg, sizeof msg);
        a.contexts->select(a.p, b, 1);
        a.block(128);
        for (int i = 0; i < 5; ++i) a.block(128, {on(0, 1 + i, 0, 60 + i), on(0, 11 + i, 1, 60 + i)});
        a.run(640);
        CHECK_EQ(a.voices(), 4u);
    }
}

TEST(caps_stand_without_a_host_limit_and_bound_a_higher_one) {
    Env env("4");
    Inst a;
    for (int i = 0; i < 6; ++i) a.block(128, {on(0, 1 + i, 0, 60 + i)});
    a.run(640);
    CHECK_EQ(a.voices(), 4u);
    a.limits(10);  // higher than the cap: the cap bounds it
    for (int i = 6; i < 9; ++i) a.block(128, {on(0, 1 + i, 0, 60 + i)});
    a.run(640);
    CHECK_EQ(a.voices(), 4u);
    a.limits(2);  // lower than the cap: the host's stands
    a.run(640);
    CHECK_EQ(a.voices(), 2u);
}

TEST(full_pool_cuts_a_voice_and_every_note_still_ends) {
    Inst a;
    for (int i = 0; i < 100; ++i) a.block(64, {on(0, 1 + i, 0, 20 + i % 90)});
    CHECK(a.voices() <= 64u);
    a.run(4800);
    CHECK(a.voices() <= 64u);
    a.block(128, {off(0, -1, -1, -1)});
    a.run(24000);
    for (int i = 0; i < 100; ++i) CHECK_EQ(a.ends(1 + i), 1);
}

TEST(notes_and_active_voices_report) {
    Inst a;
    a.block(128, {on(0, 1, 0, 60, 1.0), on(0, 2, 3, 62, 1.0), volume(0, 2, 3, 62, 0.25)});
    a.run(2400);
    double cost = -1;
    CHECK_EQ(a.budget->active_voices(a.p, &cost), 2u);
    CHECK(cost >= 0);
    oscairn_voice_budget_note_t n[8]{};
    CHECK_EQ(a.budget->notes(a.p, n, 8), 2u);
    CHECK_EQ(a.budget->notes(a.p, n, 1), 1u);
    a.budget->notes(a.p, n, 8);
    float l1 = 0, l2 = 0;
    for (auto& x : n) { if (x.note_id == 1) { l1 = x.loudness; CHECK(x.channel == 0 && x.key == 60 && x.voices == 1); } if (x.note_id == 2) { l2 = x.loudness; CHECK(x.channel == 3 && x.key == 62); } }
    CHECK(l1 > 0.9f && l1 <= 1.01f && l2 > 0.2f && l2 < 0.3f);
    a.block(128, {off(0, 1, 0, 60)});  // releasing voices are still counted until they end
    CHECK_EQ(a.voices(), 2u);
    a.run(24000);
    CHECK_EQ(a.voices(), 1u);
}

// ---- draft 2: importance ----------------------------------------------------------------------
namespace {
// Three regions on every key: a note runs three layers.
constexpr const char* kThreeLayers =
    "oscairn-sampler-sound/0\nsample sine.osp\nelement ampeg_decay1=127 ampeg_decay2=127 ampeg_release=50\n"
    "region key=0-127 root=57 element=0 sample=0 loop=0-44100\nregion key=0-127 root=64 element=0 sample=0 loop=0-44100\n"
    "region key=0-127 root=69 element=0 sample=0 loop=0-44100\n";
struct Report {
    std::array<oscairn_voice_budget_importance_t, 16> c{};
    uint32_t n = 0;
    const oscairn_voice_budget_importance_t* of(uint32_t id) const { for (uint32_t i = 0; i < n; ++i) if (c[i].context == id) return &c[i]; return nullptr; }
};
Report importance(const Inst& a) { Report r; r.n = a.budget2->importance(a.p, r.c.data(), uint32_t(r.c.size())); return r; }
// The SDK's order within a context: the tier, one not just struck, then the level less a decibel a second of age.
bool ordered(const oscairn_voice_budget_importance_t& c) {
    for (uint32_t i = 1; i < c.count; ++i) {
        const auto &x = c.least[i - 1], &y = c.least[i];
        const bool xs = x.flags & OSCAIRN_VOICE_BUDGET_STRUCK, ys = y.flags & OSCAIRN_VOICE_BUDGET_STRUCK;
        const double ax = x.level_db - std::min(x.age_ms / 1000.0, 10.0), ay = y.level_db - std::min(y.age_ms / 1000.0, 10.0);
        if (x.tier != y.tier ? x.tier > y.tier : xs != ys ? xs : ax > ay + 1e-3) return false;
    }
    return true;
}
}  // namespace

TEST(draft_2_answers_with_the_same_table_and_importance) {
    Inst a;
    CHECK(a.budget2 != nullptr && a.budget2 == a.budget);
    CHECK(a.budget2 && a.budget2->importance != nullptr);
    CHECK_EQ(importance(a).n, 0u);  // nothing sounds
}

TEST(importance_lists_each_sounds_least_important_notes_weighted_by_layers) {
    Inst a;
    char msg[64];
    const uint32_t b = a.contexts->prepare(a.p, "b", nullptr, 0, kThreeLayers, testBase(), msg, sizeof msg);
    a.contexts->select(a.p, b, 1);
    a.block(128);
    std::vector<Bytes> ev;
    for (int i = 0; i < 20; ++i) ev.push_back(on(0, 1 + i, 0, int16_t(40 + i)));
    for (int i = 0; i < 3; ++i) ev.push_back(on(0, 100 + i, 1, int16_t(60 + i)));
    for (int i = 0; i < 20; ++i) ev.push_back(volume(1, 1 + i, 0, int16_t(40 + i), 0.1 + 0.04 * i));
    a.block(128, ev);
    a.run(2560);
    const Report r = importance(a);
    CHECK_EQ(r.n, 2u);
    const auto* one = r.of(0);
    const auto* three = r.of(b);
    CHECK(one && three);
    if (!one || !three) return;
    CHECK_EQ(one->voices, 20u);
    CHECK_EQ(one->count, 16u);  // at most OSCAIRN_VOICE_BUDGET_LEAST
    CHECK(one->weight == 20.f);
    CHECK(ordered(*one));
    for (uint32_t i = 0; i < one->count; ++i) CHECK(one->least[i].weight == 1.f && one->least[i].tier == 2 && one->least[i].age_ms >= 50);
    CHECK(one->least[0].level_db < one->least[15].level_db);  // the quietest first
    CHECK_EQ(three->voices, 3u);
    CHECK_EQ(three->count, 3u);
    CHECK(three->weight == 9.f);
    CHECK(ordered(*three));
    for (uint32_t i = 0; i < three->count; ++i) CHECK(three->least[i].weight == 3.f);
    CHECK_EQ(a.voices(), 23u);  // active_voices counts notes, as the report does
}

TEST(importance_released_then_pedaled_then_held_and_struck) {
    Inst a;
    // 1 held loud; 2 released quiet; 3 key up under the sustain pedal (channel 1)
    a.block(128, {on(0, 1, 0, 60), on(0, 2, 0, 62), on(0, 3, 1, 64), volume(1, 2, 0, 62, 0.3), midi(2, 0xb1, 64, 127)});
    a.run(2560);
    a.block(128, {off(0, 2, 0, 62), off(0, 3, 1, 64)});
    Report r = importance(a);
    const auto* c = r.of(0);
    CHECK(c && c->count == 3 && c->voices == 3);
    if (!c || c->count != 3) return;
    CHECK(c->least[0].tier == 0 && c->least[1].tier == 1 && c->least[2].tier == 2);
    CHECK(c->least[0].level_db < c->least[2].level_db && c->least[0].level_db > -60);
    for (uint32_t i = 0; i < 3; ++i) CHECK(!(c->least[i].flags & OSCAIRN_VOICE_BUDGET_STRUCK));
    a.block(128, {on(0, 4, 0, 66)});  // a note struck under 50 ms ago: flagged, after the other held one
    r = importance(a);
    c = r.of(0);
    CHECK(c && c->count == 4 && (c->least[3].flags & OSCAIRN_VOICE_BUDGET_STRUCK) && c->least[3].tier == 2 && c->least[3].age_ms < 50);
}

TEST(lowered_limit_gives_up_exactly_what_importance_listed) {
    // six held notes, the quietest listed first: 2 (0.2), 5 (0.3), 3, 6, 4, 1
    const double vol[6] = {0.9, 0.2, 0.5, 0.7, 0.3, 0.6};
    auto six = [&](Inst& a) {
        std::vector<Bytes> ev;
        for (int i = 0; i < 6; ++i) ev.push_back(on(0, 1 + i, 0, int16_t(60 + i)));
        for (int i = 0; i < 6; ++i) ev.push_back(volume(1, 1 + i, 0, int16_t(60 + i), vol[i]));
        a.block(128, ev);
        a.run(2560);
        const Report r = importance(a);
        CHECK(r.of(0) && r.of(0)->count == 6 && ordered(*r.of(0)));
    };
    {   // the order moves after the list (2 and 5 loudest now, 1 quietest), and still the listed two go
        Inst a;
        six(a);
        a.block(128, {volume(0, 2, 0, 61, 1.0), volume(0, 5, 0, 64, 1.0), volume(0, 1, 0, 60, 0.1)});
        a.limits(0, {{0, 4, 0, 5, 0}});
        a.guard = true;
        a.block(128);
        a.guard = false;
        if (allocGuardAvailable()) CHECK_EQ(a.allocations, 0);
        a.run(640);
        CHECK((ended(a) == std::vector<int>{2, 5}));
        CHECK_EQ(a.voices(), 4u);
    }
    {   // a listed note ends by itself before the drop: the next listed one goes in its place
        Inst a;
        six(a);
        a.block(128, {choke(0, 2, 0, 61)});
        a.run(640);
        CHECK((ended(a) == std::vector<int>{2}));
        a.limits(0, {{0, 4, 0, 5, 0}});
        a.run(640);
        CHECK((ended(a) == std::vector<int>{2, 5}));
        CHECK_EQ(a.voices(), 4u);
    }
    {   // a new note takes a listed note's slot: it is not the listed one, so the listed 5 and 3 go, though it is quieter
        Inst a;
        six(a);
        a.block(128, {choke(0, 2, 0, 61)});
        a.run(640);
        a.block(128, {on(0, 7, 0, 70), volume(1, 7, 0, 70, 0.05)});
        a.run(2560);
        a.limits(0, {{0, 4, 0, 5, 0}});
        a.run(640);
        CHECK((ended(a) == std::vector<int>{2, 3, 5}));
        CHECK_EQ(a.voices(), 4u);
    }
}

// Three held notes at the limit, the quietest listed first (2, then 3, then 1), then the levels move: 2 loudest, 1 quietest.
// A new note gives up the note the host was told (2) once a report was made; without a report, victim()'s (1).
static std::vector<int> newNoteAtTheLimit(bool report) {
    Inst a;
    a.limits(3);
    a.block(128, {on(0, 1, 0, 60), on(0, 2, 0, 62), on(0, 3, 0, 64), volume(1, 1, 0, 60, 0.9), volume(1, 2, 0, 62, 0.2), volume(1, 3, 0, 64, 0.5)});
    a.run(2560);
    if (report) {
        const Report r = importance(a);
        CHECK(r.of(0) && r.of(0)->count == 3);
    }
    a.block(128, {volume(0, 2, 0, 62, 1.0), volume(0, 1, 0, 60, 0.1)});
    a.block(128, {on(0, 4, 0, 66)});
    a.run(640);
    return ended(a);
}

TEST(a_new_note_at_the_limit_gives_up_the_first_listed_note) {
    CHECK((newNoteAtTheLimit(true) == std::vector<int>{2}));
}

TEST(a_new_note_at_the_limit_without_a_report_gives_up_victims_note) {
    CHECK((newNoteAtTheLimit(false) == std::vector<int>{1}));  // as before the importance report was used to make room
}

TEST(no_allocation_in_importance) {
    if (!allocGuardAvailable()) { std::printf("  allocation guard unavailable in this build: not checked\n"); return; }
    Inst a;
    for (int i = 0; i < 40; ++i) a.block(64, {on(0, 1 + i, 0, int16_t(30 + i))});
    oscairn_voice_budget_importance_t out[16];
    allocGuardEnable(true);
    const uint32_t n = a.budget2->importance(a.p, out, 16);
    allocGuardEnable(false);
    CHECK_EQ(allocGuardCount(), 0);
    CHECK_EQ(n, 1u);
}
