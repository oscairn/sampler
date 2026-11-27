// SPDX-License-Identifier: AGPL-3.0-or-later
// The modulation inputs through the ABI: the box destinations as modulatable parameters (a modulation sounds as the
// equivalent linear box, is not saved, and is not shown as the value), the knobs, ribbon and switches as box sources
// (a knob starting at its sound's stored value), and an oscairn.ui hint for every parameter.
#include <cstring>
#include "host.h"
#include <oscairn-clap/ui.h>

namespace {
constexpr clap_id kKnob1 = 3, kRibbon = 11, kSwitch1 = 12, kCutoff = 14, kResonance = 15, kLevel1 = 16, kFine = 18, kRelease = 24;
// A saw-like test sound through filter type 0 (the sine through a low cutoff), with optional part settings.
std::string filtered(const std::string& part) {
    return "oscairn-sampler-sound/0\n" + (part.empty() ? std::string() : "part " + part + "\n") +
           "sample sine.osp\nelement ampeg_decay1=127 ampeg_decay2=127 ampeg_release=50 fil_type=0 fil_cutoff=200 fil_keyfollow=64\n"
           "region key=0-127 root=57 element=0 sample=0 loop=0-44100\n";
}
}  // namespace

// A modulation of a destination by a box's steps sounds as the box, sample for sample: the mod wheel at 127 through a
// linear box of ratio +8 on the cutoff (1024 x 8 / 8 = 1024 of 8192: 128 steps; a modulation of 128.9 too, toward zero), and of ratio
// +63 on the fine tune (8064 of 8192: 98.4375 cents, 98).
TEST(modulation_sounds_as_the_equivalent_linear_box) {
    struct { int dst, ratio; clap_id param; double amount; } cases[] = {{85, 8, kCutoff, 128.9}, {68, 63, kFine, 98.4375}};
    for (auto& c : cases) {
        Inst box, modded, plain;
        CHECK(box.load(stateWith(filtered("box1_src=1 box1_dst=" + std::to_string(c.dst) + " box1_ratio=" + std::to_string(c.ratio)))));
        CHECK(modded.load(stateWith(filtered(""))));
        CHECK(plain.load(stateWith(filtered(""))));
        box.block(128, {midi(0, 0xb0, 1, 127), on(0, 1, 0, 57, 1.0)});
        modded.block(128, {mod(0, c.param, c.amount), on(0, 1, 0, 57, 1.0)});
        plain.block(128, {on(0, 1, 0, 57, 1.0)});
        box.run(9600); modded.run(9600); plain.run(9600);
        CHECK(box.L == modded.L && box.R == modded.R);
        CHECK(box.L != plain.L);  // the box moved something
        // a modulation is not the value, and a state does not keep it
        CHECK_EQ(modded.value(c.param), 0.0);
        Inst back;
        CHECK(back.load(modded.save()));
        back.block(128, {on(0, 1, 0, 57, 1.0)});
        back.run(9600);
        CHECK(back.L == plain.L);
    }
    // a modulation for one note (a note id or a key) is not applied
    Inst a, b;
    a.block(128, {mod(0, kLevel1, -128, -1, 1), on(0, 1, 0, 60, 1.0)});
    b.block(128, {mod(0, kLevel1, -128, -1, -1, 60), on(0, 1, 0, 60, 1.0)});
    a.run(4800); b.run(4800);
    CHECK(peak(a.L, a.L.size() - 1000) > 0.01f && peak(b.L, b.L.size() - 1000) > 0.01f);
}

// The destination's value is an offset in raw steps, saved; it adds to the modulation, the sum within the span.
TEST(destination_values_are_saved_offsets) {
    Inst a;
    a.block(128, {param(0, kLevel1, -128), on(0, 1, 0, 60, 1.0)});  // element level 127 - 128: silent
    a.run(4800);
    CHECK(peak(a.L, a.L.size() - 1000) < 1e-6f);
    a.block(128, {mod(0, kLevel1, 128)});  // back to the element's own level
    a.run(4800);
    const float whole = peak(a.L, a.L.size() - 1000);
    CHECK(whole > 0.01f);
    Inst b;
    CHECK(b.load(a.save()));
    CHECK_EQ(b.value(kLevel1), -128.0);
    b.block(128, {on(0, 1, 0, 60, 1.0)});
    b.run(4800);
    CHECK(peak(b.L, b.L.size() - 1000) < 1e-6f);  // the value came back, the modulation did not
    Inst r;  // the resonance's own range: a value past it is clamped
    r.block(128, {param(0, kResonance, 500)});
    CHECK_EQ(r.value(kResonance), 128.0);
}

// The knobs start at the sound's stored values and drive their boxes; the ribbon and the switches too.
TEST(knobs_ribbon_and_switches_drive_their_boxes) {
    struct { std::string part; clap_id param; double lo, hi; } cases[] = {
        {"knob1=1023 box1_src=8 box1_dst=60 box1_ratio=-64", kKnob1, 0.0, 1.0},
        {"box1_src=6 box1_dst=60 box1_ratio=-64", kRibbon, 0.0, 1.0},
        {"box1_src=16 box1_dst=60 box1_ratio=-64", kSwitch1, 0.0, 1.0},
    };
    for (auto& c : cases) {
        Inst a;
        CHECK(a.load(stateWith(filtered(c.part))));
        CHECK_EQ(a.value(c.param), c.param == kKnob1 ? 1.0 : 0.0);  // the knob from the sound (a state 1 holds no knobs)
        a.block(128, {param(0, c.param, c.lo), on(0, 1, 0, 57, 1.0)});
        a.run(4800);
        const float whole = peak(a.L, a.L.size() - 1000);
        a.block(128, {param(0, c.param, c.hi)});
        a.run(4800);
        const float down = peak(a.L, a.L.size() - 1000);
        if (!(whole > 1e-3f && down < 1e-4f * whole)) { ++g_failures; std::printf("  parameter %u: %g then %g\n", c.param, whole, down); }
    }
    // prepare: a knob named on the shell line wins over the sound's value
    Inst a;
    char msg[128] = {};
    const std::string text = "shell knob1=0.25\n" + filtered("knob1=1023 knob2=0");
    const uint32_t id = a.contexts->prepare(a.p, "k", nullptr, 0, text.c_str(), testBase(), msg, sizeof msg);
    CHECK(id != CLAP_INVALID_ID);
    CHECK(a.contexts->select(a.p, id, 0));
    a.block(128);
    CHECK(std::fabs(a.value(kKnob1) - 0.25) < 1e-6 && a.value(kKnob1 + 1) == 0.0);
}

// Every parameter has a hint; the destinations are modulatable, the shell's values not.
TEST(parameter_hints_and_flags) {
    Inst a(false);
    const auto* ui = static_cast<const oscairn_plugin_ui_t*>(a.p->get_extension(a.p, OSCAIRN_EXT_UI));
    CHECK(ui && ui->param_hint);
    if (!ui || !ui->param_hint) return;
    int roles[64] = {};
    for (uint32_t i = 0; i < a.params->count(a.p); ++i) {
        clap_param_info_t info{};
        CHECK(a.params->get_info(a.p, i, &info));
        oscairn_param_hint_t h{};
        CHECK(ui->param_hint(a.p, info.id, &h));
        if (h.role < 64) ++roles[h.role];
        CHECK_EQ(bool(info.flags & CLAP_PARAM_IS_MODULATABLE), info.id >= kCutoff && info.id <= kRelease);
    }
    oscairn_param_hint_t none{};
    CHECK(!ui->param_hint(a.p, a.params->count(a.p), &none));
    CHECK(roles[OSCAIRN_ROLE_LEVEL] == 1 && roles[OSCAIRN_ROLE_PAN] == 1 && roles[OSCAIRN_ROLE_CUTOFF] == 1 && roles[OSCAIRN_ROLE_RESONANCE] == 1
          && roles[OSCAIRN_ROLE_LEVEL_1] == 1 && roles[OSCAIRN_ROLE_OUTPUT_BUS] == 1);
}

// Element levels (ids 27-58, Element 1-32 Level): an offset in the element level destination's raw steps, added to its
// value and boxes; a held note follows one smoothly, a note after it starts at it, a state keeps it, and at zero nothing moves.
namespace {
constexpr clap_id kElement1 = 27;
const std::string kTwoElements = "oscairn-sampler-sound/0\nsample sine.osp\nelement ampeg_decay1=127 ampeg_decay2=127 ampeg_release=50\n"
    "element ampeg_decay1=127 ampeg_decay2=127 ampeg_release=50\n"
    "region key=0-127 root=57 element=0 sample=0 loop=0-44100\nregion key=0-127 root=57 element=1 sample=0 loop=0-44100\n";
}  // namespace

TEST(element_level_parameters_exist_after_the_shed_count) {
    Inst a(false);
    clap_param_info_t info{};
    CHECK(a.params->get_info(a.p, 26, &info) && info.id == 26 && (info.flags & CLAP_PARAM_IS_READONLY));
    for (uint32_t n = 0; n < 32; ++n) {
        CHECK(a.params->get_info(a.p, kElement1 + n, &info) && info.id == kElement1 + n && !(info.flags & CLAP_PARAM_IS_READONLY));
        CHECK(info.min_value == -128 && info.max_value == 128 && info.default_value == 0);
        CHECK(std::string(info.name) == "Element " + std::to_string(n + 1) + " Level");
    }
}

TEST(held_note_follows_each_element_level_smoothly) {
    for (uint32_t n = 0; n < 2; ++n) {
        Inst a;
        CHECK(a.load(stateWith(kTwoElements)));
        a.block(128, {on(0, 1, 0, 57, 1.0)});
        a.run(4800);
        const float both = peak(a.L, a.L.size() - 1000);
        const size_t at = a.L.size();
        a.block(128, {param(0, kElement1 + n, -128)});  // that element to silence (level index 127 - 128)
        a.run(4800);
        const float one = peak(a.L, a.L.size() - 1000);
        float worst = 0, normal = 0;  // the largest sample step over the change against the steady signal's
        for (size_t i = at + 1; i < at + 2000; ++i) worst = std::max(worst, std::fabs(a.L[i] - a.L[i - 1]));
        for (size_t i = at - 1999; i < at; ++i) normal = std::max(normal, std::fabs(a.L[i] - a.L[i - 1]));
        CHECK(both > 0.01f && std::fabs(one / both - 0.5f) < 0.01f);  // the other element alone
        CHECK(worst <= normal);
        a.block(128, {param(0, kElement1 + n, 0)});  // back
        a.run(4800);
        CHECK(std::fabs(peak(a.L, a.L.size() - 1000) / both - 1.0f) < 1e-4f);
        a.block(128, {param(0, kElement1, -64), param(0, kElement1 + 1, -64)});  // the level law: (63 / 127)^2 = 0.246
        a.run(4800);
        CHECK(std::fabs(peak(a.L, a.L.size() - 1000) / both - 0.246f) < 0.012f);
    }
}

TEST(a_note_starts_at_the_element_level_and_zero_changes_nothing) {
    Inst a, b, c;
    CHECK(a.load(stateWith(kTwoElements)) && b.load(stateWith(kTwoElements)) && c.load(stateWith(kTwoElements)));
    a.block(128, {on(0, 1, 0, 57, 1.0)});
    b.block(128, {param(0, kElement1, 0), param(0, kElement1 + 31, 0), on(0, 1, 0, 57, 1.0)});
    c.block(128, {param(0, kElement1 + 1, -128), on(0, 1, 0, 57, 1.0)});
    a.run(4800); b.run(4800); c.run(4800);
    CHECK(a.L == b.L && a.R == b.R);  // set to the default: bit for bit as without
    CHECK(std::fabs(peak(c.L, c.L.size() - 1000) / peak(a.L, a.L.size() - 1000) - 0.5f) < 0.01f);
}

TEST(element_levels_are_saved_and_an_older_state_loads_with_zero) {
    Inst a;
    CHECK(a.load(stateWith(kTwoElements)));
    a.block(128, {param(0, kElement1 + 1, -128), param(0, kElement1 + 31, 12.5)});
    const std::string s = a.save();
    CHECK(s.rfind("oscairn-sampler-state 4\n", 0) == 0);
    Inst b;
    CHECK(b.load(s) && b.save() == s);
    CHECK_EQ(b.value(kElement1 + 1), -128.0);
    CHECK_EQ(b.value(kElement1 + 31), 12.5);
    b.block(128, {on(0, 1, 0, 57, 1.0)});
    b.run(4800);
    Inst once;
    CHECK(once.load(stateWith(kTwoElements)));
    once.block(128, {on(0, 1, 0, 57, 1.0)});
    once.run(4800);
    CHECK(std::fabs(peak(b.L, b.L.size() - 1000) / peak(once.L, once.L.size() - 1000) - 0.5f) < 0.01f);
    // a state 3: the 26 values before the element levels
    const size_t head = s.find('\n') + 1, line = s.find('\n', head);
    size_t cut = head;
    for (int k = 0; k < 26; ++k) cut = s.find(' ', cut + 1);  // the 26th value's end
    size_t rest = cut;
    for (int k = 0; k < 32; ++k) rest = s.find(' ', rest + 1);
    CHECK(rest < line);
    const std::string old = "oscairn-sampler-state 3\n" + s.substr(head, cut - head) + s.substr(rest, line - rest) + "\n";
    Inst c;
    c.block(128, {param(0, kElement1, 5)});
    CHECK(c.load(old));
    for (uint32_t n = 0; n < 32; ++n) CHECK_EQ(c.value(kElement1 + n), 0.0);
    CHECK(c.save() == once.save());
}
