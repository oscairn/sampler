# Oscairn Sampler: plan

## 1. Status

Version 0.9.5 (the version measured below). Implementation language today: C++ (the DSP core in `core/` and the
plugin shell). Everything in Oscairn's plugin set is to end up in Rust: the engines port after their coverage work, and new effect types are written in Rust from the start.

## 2. Supported

What the engine plays of the reference's sample engine today, in short. The parameter-by-parameter record is
[coverage.md](coverage.md); the plugin parameters are in [parameters.md](parameters.md), the sound format in
[sound-format.md](sound-format.md), the comparison with the reference in [acceptance.md](acceptance.md), cost in
[cost.md](cost.md) and design in [design.md](design.md).

| Area | Played today | Not yet (reported, not approximated) |
|---|---|---|
| Samples and regions | 16-bit PCM, mono and stereo, any source rate; forward loops; regions stream from sample packs; 8-tap interpolation; waveforms expanded into regions (sound format v1); key and velocity limits; coarse and fine tune; region level and pan; up to 32 elements | Velocity Cross Fade; regions or samples past 1024; elements past 32; host rates of 88.2 kHz and above 132.3 kHz |
| Element selection | trigger modes normal, key-off, cycle, random and the switch modes; element groups; key-on delay time | legato trigger mode (`xa_control=1`); key-on delay note length (tempo-synced) |
| Filter | types 0 to 7, 10 and thru; cutoff, resonance, gain, key follow, velocity sensitivities, high-pass cutoff in types 4 and 5, cutoff scaling break points | the other filter types (dual types among them) |
| Envelopes | amplitude, filter and pitch envelopes with their velocity and key time laws; half damper | - |
| Level, pan, EQ | element level and pan, level velocity and key scaling, the element EQ | Random, Alternate and Scaling Pan Depth; Fine Tune Key Follow Scaling Sensitivity |
| LFOs | element LFO (reference mode); part LFO waves, speed, phase, key-on reset, user wave, delay, fade in, hold, fade out; destinations level, pitch, cutoff and pan | part LFO tempo sync, tempo speed, random speed; other part LFO destinations |
| Modulation | controller boxes 1-16 on the destinations and sources listed in [coverage.md](coverage.md); knobs 1-8, ribbon and switches 1-2 as plugin parameters; box destinations as modulatable parameters | other box destinations, sources and curves; Key Controller Sets 1-4; Controller Sets 17-32 |
| Voices | polyphonic play, polyphony limit, velocity depth and offset, pitch bend range, voice shedding, sample-accurate note starts and ends | mono mode; portamento; micro tuning |
| Outputs | nine stereo output ports; each element on its connection's port (insertion A or B) when the part splits its outputs | - |
| Drum kits | - | not supported: the sound format has no drum-kit form, so every drum-kit part is muted |

## 3. Coverage

Measured with Sampler 0.9.5 on 2026-11-17. Two sets of Performances of the reference were converted: its factory (stock) set of 3631 Performances and a personal library of 87 Performances. "Fully covered": the engine's own coverage report lists nothing for that part that the engine does not play; it says nothing about effects, controls, scenes or sequences. "Best effort": the part plays, with at least one item reported. Neither set has a Performance with no sound-affecting drop yet (0 of 3631, 0 of 87): every Performance still needs work somewhere, in this plugin or elsewhere.

| Parts on this engine | Total | Fully covered | Best effort | Muted | Not measured | Switched off |
|---|---|---|---|---|---|---|
| Stock set: sampled parts (provisional, see below) | 5371 | 1654 | 2905 | 2 | 609 | 201 |
| Stock set: drum-kit parts | 701 | 0 | 0 | 701 | 0 | 0 |
| Personal library: sampled parts | 168 | 112 | 39 | 0 | 0 | 17 |

**The stock-set figures for sampled parts are provisional.** A decoding tool the sample-pack step needs was missing
on the measuring machine: 609 parts could not be packed at all and are counted as "not measured" (466 Performances),
and 3911 more parts played but were checked on fewer regions than they have. The gap counts below are therefore
a lower bound for sampled parts, and the fully-covered count may move either way once every part is measured
(unverified). A run with the tool present is needed before these numbers are relied on. The personal library was
not affected.

Drum kits are not supported at all today: the sound format has no drum-kit form, so all 701 drum-kit parts of the
stock set (655 Performances) are muted.

In the personal library, the sampled parts' mod-wheel controller boxes: 113 carried (56 in the engine's sound, 57 as
host controls), 57 dropped (destinations the engine does not move yet, or host targets that do not exist yet).

### Gaps

107 gaps of the stock set belong to this engine. "Changes the sound" is a classification of the gap, not a listening test: a stored value away from its default that is not carried counts as sound-affecting whether or not it is heard in a given Performance. Counts are Performances and parts of the stock set that carry the gap; parts switched off or muted are not counted against their other gaps.

| Gap | Performances (stock) | Parts (stock) | Changes the sound | Status |
|---|---|---|---|---|
| Element Connection Switch: elements routed to thru or to the other insertion all go through the insertions (approximated; needs the engine's outputs per insertion, with the host) | 878 | 1076 | yes | open |
| drum-kit part muted: the sound format has no drum-kit form | 655 | 701 | yes | open |
| mono mode (`mono`) | 381 | 442 | yes | open |
| Random Pan Depth | 363 | 471 | yes | open |
| Alternate Pan Depth | 294 | 363 | yes | open |
| part LFO tempo sync (`plfo_tempo_sync`) | 292 | 344 | yes | open |
| controller box destination 81 (`boxN_dst=81`) | 250 | 294 | yes | open |
| Velocity Cross Fade | 238 | 303 | yes | open |
| portamento (`portamento`) | 230 | 255 | yes | open |
| controller box destination 82 (`boxN_dst=82`) | 217 | 268 | yes | open |
| part LFO slot 1 destination (`plfo_dst1`) | 191 | 220 | yes | open |
| controller box source 2 (`boxN_src=2`) | 177 | 225 | yes | open |
| controller box destination 52 (`boxN_dst=52`) | 169 | 179 | yes | open |
| Scaling Pan Depth | 166 | 182 | yes | open |
| controller box destination 63 (`boxN_dst=63`) | 153 | 172 | yes | open |
| filter type 8 (`fil_type=8`) | 134 | 140 | yes | open |
| controller box destination 76 (`boxN_dst=76`) | 126 | 184 | yes | open |
| part LFO slot 1 depth ratio (`plfo_ratio1`) | 120 | 125 | yes | open |
| controller box destination 58 (`boxN_dst=58`) | 116 | 124 | yes | open |
| controller box destination 56 (`boxN_dst=56`) | 116 | 123 | yes | open |
| part LFO slot 2 depth ratio (`plfo_ratio2`) | 114 | 122 | yes | open |
| filter type 21 (`fil_type=21`) | 111 | 123 | yes | open |
| controller box source 18 (`boxN_src=18`) | 104 | 134 | yes | open |
| controller box destination 53 (`boxN_dst=53`) | 102 | 123 | yes | open |
| part LFO slot 2 destination (`plfo_dst2`) | 95 | 100 | yes | open |
| controller box destination 54 (`boxN_dst=54`) | 93 | 140 | yes | open |
| controller box curve 0 (`boxN_curve=0`) | 92 | 127 | yes | open |
| Detune | 90 | 137 | yes | open |
| part LFO random speed (`plfo_random_speed`) | 86 | 94 | yes | open |
| filter type 16 (`fil_type=16`) | 83 | 86 | yes | open |
| controller box destination 66 (`boxN_dst=66`) | 78 | 87 | yes | open |
| controller box destination 61 (`boxN_dst=61`) | 66 | 81 | yes | open |
| controller box curve 2 (`boxN_curve=2`) | 63 | 79 | yes | open |
| part LFO slot 3 depth ratio (`plfo_ratio3`) | 63 | 65 | yes | open |
| controller box destination 77 (`boxN_dst=77`) | 60 | 81 | yes | open |
| controller box destination 84 (`boxN_dst=84`) | 57 | 82 | yes | open |
| element trigger mode legato (`xa_control=1`) | 54 | 57 | yes | open |
| controller box destination 69 (`boxN_dst=69`) | 53 | 53 | yes | open |
| filter type 19 (`fil_type=19`) | 42 | 43 | yes | open |
| filter type 12 (`fil_type=12`) | 41 | 43 | yes | open |
| part LFO phase offset (`plfo_phase_offset`) | 40 | 41 | yes | open |
| controller box destination 62 (`boxN_dst=62`) | 39 | 44 | yes | open |
| controller box curve 3 (`boxN_curve=3`) | 38 | 53 | yes | open |
| Controller Set 17-32 | 38 | 52 | yes | open |
| filter type 11 (`fil_type=11`) | 38 | 42 | yes | open |
| controller box source 19 (`boxN_src=19`) | 37 | 51 | yes | open |
| Key Controller Set 1 | 34 | 53 | yes | open |
| Fine Tune Key Follow Scaling Sensitivity | 33 | 42 | yes | open |
| part LFO slot 3 destination (`plfo_dst3`) | 31 | 33 | yes | open |
| key-on delay note length, tempo-synced (`keyon_delay_note`) | 30 | 33 | yes | open |
| controller box ratio (`boxN_ratio`) | 24 | 25 | yes | open |
| filter type 14 (`fil_type=14`) | 24 | 24 | yes | open |
| controller box destination 71 (`boxN_dst=71`) | 23 | 29 | yes | open |
| controller box destination 83 (`boxN_dst=83`) | 22 | 31 | yes | open |
| filter type 15 (`fil_type=15`) | 22 | 22 | yes | open |
| controller box destination 59 (`boxN_dst=59`) | 21 | 27 | yes | open |
| controller box destination 377 (`boxN_dst=377`) | 17 | 17 | yes | open |
| controller box source 22 (`boxN_src=22`) | 16 | 16 | yes | open |
| controller box destination 379 (`boxN_dst=379`) | 14 | 14 | yes | open |
| Key Controller Set 2 | 13 | 17 | yes | open |
| controller box curve 1 (`boxN_curve=1`) | 13 | 14 | yes | open |
| controller box source 20 (`boxN_src=20`) | 11 | 17 | yes | open |
| Regions or samples past 1024 | 10 | 10 | yes | open |
| filter type 17 (`fil_type=17`) | 9 | 9 | yes | open |
| controller box destination 70 (`boxN_dst=70`) | 8 | 9 | yes | open |
| controller box source 23 (`boxN_src=23`) | 8 | 9 | yes | open |
| controller box destination 392 (`boxN_dst=392`) | 8 | 8 | yes | open |
| controller box destination 74 (`boxN_dst=74`) | 7 | 10 | yes | open |
| controller box curve 19 (`boxN_curve=19`) | 6 | 20 | yes | open |
| Element Pan Switch | 6 | 10 | yes | open |
| controller box source 21 (`boxN_src=21`) | 6 | 10 | yes | open |
| pitch bend range down (`bend_down`) | 6 | 6 | yes | open |
| controller box curve 4 (`boxN_curve=4`) | 6 | 6 | yes | open |
| controller box destination 378 (`boxN_dst=378`) | 6 | 6 | yes | open |
| Elements past 32 | 4 | 6 | yes | open |
| controller box source 25 (`boxN_src=25`) | 4 | 6 | yes | open |
| controller box curve 15 (`boxN_curve=15`) | 4 | 5 | yes | open |
| controller box curve 13 (`boxN_curve=13`) | 4 | 4 | yes | open |
| filter type 13 (`fil_type=13`) | 4 | 4 | yes | open |
| controller box curve 11 (`boxN_curve=11`) | 3 | 5 | yes | open |
| controller box source 24 (`boxN_src=24`) | 3 | 5 | yes | open |
| Key Controller Set 3 | 3 | 3 | yes | open |
| Micro Tuning Scale | 3 | 3 | yes | open |
| controller box destination 381 (`boxN_dst=381`) | 3 | 3 | yes | open |
| controller box destination 385 (`boxN_dst=385`) | 3 | 3 | yes | open |
| controller box curve 10 (`boxN_curve=10`) | 2 | 2 | yes | open |
| controller box curve 12 (`boxN_curve=12`) | 2 | 2 | yes | open |
| controller box curve 18 (`boxN_curve=18`) | 2 | 2 | yes | open |
| controller box curve 9 (`boxN_curve=9`) | 2 | 2 | yes | open |
| element LFO amplitude ratio (`elfo_amp_ratio`) | 2 | 2 | yes | open |
| region (`region`) | 2 | 2 | yes | open |
| part muted: no element can play | 2 | 2 | yes | open |
| controller box source 29 (`boxN_src=29`) | 1 | 3 | yes | open |
| Key Controller Set 4 | 1 | 1 | yes | open |
| controller box curve 14 (`boxN_curve=14`) | 1 | 1 | yes | open |
| controller box curve 16 (`boxN_curve=16`) | 1 | 1 | yes | open |
| controller box curve 5 (`boxN_curve=5`) | 1 | 1 | yes | open |
| controller box curve 6 (`boxN_curve=6`) | 1 | 1 | yes | open |
| controller box curve 8 (`boxN_curve=8`) | 1 | 1 | yes | open |
| controller box destination 380 (`boxN_dst=380`) | 1 | 1 | yes | open |
| controller box destination 383 (`boxN_dst=383`) | 1 | 1 | yes | open |
| controller box destination 384 (`boxN_dst=384`) | 1 | 1 | yes | open |
| controller box destination 391 (`boxN_dst=391`) | 1 | 1 | yes | open |
| controller box destination 72 (`boxN_dst=72`) | 1 | 1 | yes | open |
| controller box destination 80 (`boxN_dst=80`) | 1 | 1 | yes | open |
| controller box source 28 (`boxN_src=28`) | 1 | 1 | yes | open |
| part cutoff offset (`part_cutoff`) | 1 | 1 | yes | open |

Only in the personal library (not in the stock set):

| Gap | Performances (personal) | Parts (personal) | Changes the sound | Status |
|---|---|---|---|---|
| controller box curve 7 (`boxN_curve=7`) | 3 | 5 | yes | open |

## 4. Port to Rust

Status: not started.

A port counts as done when all of these hold: the test suite and the cost gates below are carried over and pass against the Rust build; a saved state of the C++ version opens and plays the same; the plugin id, the parameter ids and the sound format stay the same. For this plugin:

- Tests: the plugin-level suite in `tests/` (driving the built plugin through its binary interface only, including
  the fuzz rounds, allocation, state, sharing, streaming and thread tests) and the core's own tests, including
  `coverage_table`, which holds [coverage.md](coverage.md) to the engine's coverage report.
- Cost gates: `tools/cost/cg.sh` against `tools/cost/baseline.txt` and `tools/cost/cache-baseline.txt`
  ([cost.md](cost.md)).
- Saved state: `oscairn-sampler-state 3` and the snapshot `oscairn-sampler-contexts/3` open; states 2 and 1 still
  load as they do today.
- Same plugin id (`org.oscairn.sampler`), parameter ids 0-26 ([parameters.md](parameters.md)) and sound format
  versions 0 and 1, text and blob ([sound-format.md](sound-format.md)).

## 5. Next

In build order (the work packages' order across all engines is greedy: the package that brings the most stock
Performances to "no sound-affecting drop" first; this engine's packages are third and eleventh of seventeen).

1. Measure again with the decoding tool present, so the 609 unmeasured parts and the 3911 partly checked ones count.
   Until then the order below rests on provisional numbers.
2. The engine's reported items (1942 stock Performances touched): mono mode (381 Performances), Random Pan Depth
   (363), Alternate Pan Depth (294), part LFO tempo sync (292), the most used controller box destinations (81, 82,
   52, 63, 76), Velocity Cross Fade (238), portamento (230), then the filter types.
3. Element connection to the insertions (878 Performances, approximated today), with the host.
4. Drum kits: a drum-kit form in the sound format and its playback (701 parts, 655 Performances muted today).
