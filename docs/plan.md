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
| Stock set: sampled parts | 5371 | 1892 | 3276 | 2 | 0 | 201 |
| Stock set: drum-kit parts | 701 | 0 | 0 | 701 | 0 | 0 |
| Personal library: sampled parts | 168 | 112 | 39 | 0 | 0 | 17 |

The measurement is complete: every sampled part of the stock set was packed and checked on all of its regions
(0 not measured).

Drum kits are not supported at all today: the sound format has no drum-kit form, so all 701 drum-kit parts of the
stock set (655 Performances) are muted.

In the personal library, the sampled parts' mod-wheel controller boxes: 113 carried (56 in the engine's sound, 57 as
host controls), 57 dropped (destinations the engine does not move yet, or host targets that do not exist yet).

### Gaps

107 gaps of the stock set belong to this engine. "Changes the sound" is a classification of the gap, not a listening test: a stored value away from its default that is not carried counts as sound-affecting whether or not it is heard in a given Performance. Counts are Performances and parts of the stock set that carry the gap; parts switched off or muted are not counted against their other gaps.

| Gap | Performances (stock) | Parts (stock) | Changes the sound | Status |
|---|---|---|---|---|
| Element Connection Switch: elements routed to thru or to the other insertion all go through the insertions (approximated; needs the engine's outputs per insertion, with the host) | 923 | 1138 | yes | open |
| drum-kit part muted: the sound format has no drum-kit form | 655 | 701 | yes | open |
| mono mode (`mono`) | 408 | 479 | yes | open |
| Random Pan Depth | 363 | 471 | yes | open |
| part LFO tempo sync (`plfo_tempo_sync`) | 313 | 378 | yes | open |
| Alternate Pan Depth | 294 | 363 | yes | open |
| controller box destination 81 (`boxN_dst=81`) | 264 | 313 | yes | open |
| portamento (`portamento`) | 243 | 276 | yes | open |
| Velocity Cross Fade | 238 | 303 | yes | open |
| controller box destination 82 (`boxN_dst=82`) | 227 | 280 | yes | open |
| part LFO slot 1 destination (`plfo_dst1`) | 218 | 258 | yes | open |
| controller box source 2 (`boxN_src=2`) | 194 | 246 | yes | open |
| controller box destination 52 (`boxN_dst=52`) | 173 | 185 | yes | open |
| controller box destination 63 (`boxN_dst=63`) | 170 | 200 | yes | open |
| Scaling Pan Depth | 166 | 182 | yes | open |
| filter type 8 (`fil_type=8`) | 140 | 146 | yes | open |
| controller box destination 76 (`boxN_dst=76`) | 134 | 214 | yes | open |
| controller box destination 56 (`boxN_dst=56`) | 134 | 143 | yes | open |
| part LFO slot 1 depth ratio (`plfo_ratio1`) | 130 | 135 | yes | open |
| controller box destination 58 (`boxN_dst=58`) | 129 | 140 | yes | open |
| part LFO slot 2 depth ratio (`plfo_ratio2`) | 127 | 138 | yes | open |
| filter type 21 (`fil_type=21`) | 122 | 134 | yes | open |
| controller box source 18 (`boxN_src=18`) | 117 | 155 | yes | open |
| part LFO slot 2 destination (`plfo_dst2`) | 115 | 123 | yes | open |
| controller box destination 53 (`boxN_dst=53`) | 111 | 133 | yes | open |
| part LFO random speed (`plfo_random_speed`) | 106 | 124 | yes | open |
| controller box destination 54 (`boxN_dst=54`) | 104 | 170 | yes | open |
| controller box curve 0 (`boxN_curve=0`) | 100 | 145 | yes | done (0.9.10): every member of type 0 (parameters 0-10) measured at the wheel's 128 values; counted again with the engine's report on the stock set read through the converter, 0 parts (fully covered sampled parts 1892 to 1933 with the other types below) |
| Detune | 90 | 137 | yes | open |
| controller box destination 66 (`boxN_dst=66`) | 89 | 108 | yes | open |
| filter type 16 (`fil_type=16`) | 85 | 89 | yes | open |
| controller box curve 2 (`boxN_curve=2`) | 75 | 106 | yes | partly done (0.9.10): 14 members measured; counted again, 74 parts (56 Performances) on members not probed |
| controller box destination 61 (`boxN_dst=61`) | 71 | 90 | yes | open |
| controller box destination 84 (`boxN_dst=84`) | 69 | 97 | yes | open |
| controller box destination 77 (`boxN_dst=77`) | 65 | 90 | yes | open |
| part LFO slot 3 depth ratio (`plfo_ratio3`) | 65 | 68 | yes | open |
| controller box destination 69 (`boxN_dst=69`) | 61 | 70 | yes | open |
| element trigger mode legato (`xa_control=1`) | 61 | 64 | yes | open |
| controller box destination 62 (`boxN_dst=62`) | 58 | 67 | yes | open |
| filter type 12 (`fil_type=12`) | 48 | 50 | yes | open |
| filter type 19 (`fil_type=19`) | 45 | 46 | yes | open |
| controller box curve 3 (`boxN_curve=3`) | 43 | 60 | yes | partly done (0.9.10): parameters 1, 2, 5, 6, 7 measured; counted again, 33 parts (25 Performances) |
| controller box source 19 (`boxN_src=19`) | 42 | 64 | yes | open |
| part LFO phase offset (`plfo_phase_offset`) | 42 | 43 | yes | open |
| filter type 11 (`fil_type=11`) | 39 | 43 | yes | open |
| Controller Set 17-32 | 38 | 52 | yes | open |
| Key Controller Set 1 | 34 | 53 | yes | open |
| part LFO slot 3 destination (`plfo_dst3`) | 34 | 37 | yes | open |
| Fine Tune Key Follow Scaling Sensitivity | 33 | 42 | yes | open |
| key-on delay note length, tempo-synced (`keyon_delay_note`) | 33 | 39 | yes | open |
| controller box destination 71 (`boxN_dst=71`) | 31 | 42 | yes | open |
| filter type 14 (`fil_type=14`) | 30 | 33 | yes | open |
| controller box destination 59 (`boxN_dst=59`) | 25 | 33 | yes | open |
| controller box ratio (`boxN_ratio`) | 25 | 27 | yes | open |
| filter type 15 (`fil_type=15`) | 23 | 23 | yes | open |
| controller box destination 83 (`boxN_dst=83`) | 22 | 31 | yes | open |
| controller box destination 377 (`boxN_dst=377`) | 17 | 17 | yes | open |
| controller box source 22 (`boxN_src=22`) | 17 | 17 | yes | open |
| controller box curve 1 (`boxN_curve=1`) | 14 | 15 | yes | open |
| controller box destination 379 (`boxN_dst=379`) | 14 | 14 | yes | open |
| controller box source 20 (`boxN_src=20`) | 13 | 20 | yes | open |
| Key Controller Set 2 | 13 | 17 | yes | open |
| Regions or samples past 1024 | 10 | 10 | yes | open |
| controller box curve 19 (`boxN_curve=19`) | 9 | 28 | yes | open |
| filter type 17 (`fil_type=17`) | 9 | 9 | yes | open |
| controller box destination 70 (`boxN_dst=70`) | 8 | 9 | yes | open |
| controller box source 23 (`boxN_src=23`) | 8 | 9 | yes | open |
| controller box destination 392 (`boxN_dst=392`) | 8 | 8 | yes | open |
| controller box source 21 (`boxN_src=21`) | 7 | 12 | yes | open |
| controller box destination 74 (`boxN_dst=74`) | 7 | 10 | yes | open |
| Element Pan Switch | 6 | 10 | yes | open |
| controller box curve 4 (`boxN_curve=4`) | 6 | 7 | yes | open |
| pitch bend range down (`bend_down`) | 6 | 6 | yes | open |
| controller box destination 378 (`boxN_dst=378`) | 6 | 6 | yes | open |
| controller box curve 15 (`boxN_curve=15`) | 5 | 9 | yes | open |
| controller box source 25 (`boxN_src=25`) | 4 | 7 | yes | open |
| Elements past 32 | 4 | 6 | yes | open |
| controller box curve 13 (`boxN_curve=13`) | 4 | 4 | yes | open |
| filter type 13 (`fil_type=13`) | 4 | 4 | yes | open |
| controller box curve 11 (`boxN_curve=11`) | 3 | 5 | yes | open |
| controller box source 24 (`boxN_src=24`) | 3 | 5 | yes | open |
| Key Controller Set 3 | 3 | 3 | yes | open |
| Micro Tuning Scale | 3 | 3 | yes | open |
| controller box curve 9 (`boxN_curve=9`) | 3 | 3 | yes | done (0.9.10): parameter 0 measured; counted again, 0 parts |
| controller box destination 381 (`boxN_dst=381`) | 3 | 3 | yes | open |
| controller box destination 385 (`boxN_dst=385`) | 3 | 3 | yes | open |
| region (`region`) | 3 | 3 | yes | open |
| controller box curve 10 (`boxN_curve=10`) | 2 | 2 | yes | open |
| controller box curve 12 (`boxN_curve=12`) | 2 | 2 | yes | open |
| controller box curve 18 (`boxN_curve=18`) | 2 | 2 | yes | partly done (0.9.10): parameter 4 measured; counted again, 1 part |
| element LFO amplitude ratio (`elfo_amp_ratio`) | 2 | 2 | yes | open |
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

## 5. Shared building blocks

At the port to Rust the engine is cut into three reusable pieces, so other engines can use them: reading samples
from packs (already a separate library), sample playback (interpolation, looping, streaming), and the per-layer
voice (filter, envelopes, LFOs, controller routing). Each engine compiles the
pieces it uses into its own plugin at a pinned version (a build-time dependency: the shipped plugin stays
self-contained, and the pieces are optimized together with the engine's own loop). Until then new work keeps those pieces separable.

## 6. Next

In build order (the work packages' order across all engines is greedy: the package that brings the most stock
Performances to "no sound-affecting drop" first; this engine's packages are third and eleventh of seventeen).

1. The engine's reported items (2041 stock Performances touched): mono mode (408 Performances), Random Pan Depth
   (363), Alternate Pan Depth (294), part LFO tempo sync (313), the most used controller box destinations (81, 82,
   52, 63, 76), Velocity Cross Fade (238), portamento (243), then the filter types.
2. Element connection to the insertions (923 Performances, approximated today), with the host.
3. Drum kits: a drum-kit form in the sound format and its playback (701 parts, 655 Performances muted today).
