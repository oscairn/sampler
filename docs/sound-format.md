# Sound format v1

A sound is one blob: element parameter sets in the reference's raw units, and what each element plays:
either regions over sample references, or a waveform (an Oscairn Waveform file, named by its id) that
the engine expands into regions when it prepares the sound. `core/sound.cpp` writes, reads, validates
and expands it; the text form is for tests and hand-written sounds. Version 1 only adds to version 0:
a version 0 blob or text reads exactly as before, and a sound without waveforms is written as version 0.
A reader refuses a later version.

## Blob (little-endian)

| offset | field |
|---|---|
| 0 | `OPCM` |
| 4 | u16 version (0 or 1), u16 flags (0) |
| 8 | u32 size of the whole blob |
| 12 | u32 CRC-32 (IEEE) of bytes 16 to the end |
| 16 | u16 samples, u16 elements, u16 regions, u16 part parameters (181 now; a blob with fewer, written before the later ones were added, reads with those at their defaults) |
| 24 | part parameters, i16 each, in `core/partparams.inc` order |
| | samples: u8 length, then the reference's bytes (no NUL): in version 0 a relative path (no `..` segment, no leading `/`); in version 1 `sample:` and the sample's id as 64 lowercase hex digits |
| | elements: u16 count, then count x (u16 parameter index in `core/params.inc` order, i16 value); indices increasing; only values that differ from the default |
| | regions, 32 bytes each: u8 lokey, hikey, lovel, hivel, root; i8 transpose; u8 loop (0 none, 1 forward); u8 0; u16 element; u16 sample; i32 tune (1/100 cent); i32 level (1/100 dB); i16 pan (1/100 of -100..100); u16 0; u32 loop start; u32 loop end (exclusive) |
| | version 1 only: u16 W (0 to 32), then W waveform ids, 32 bytes each |

Element parameters added with version 1 (at the end of `params.inc`; their defaults are version 0's
behaviour): `waveform` (0 to 32: 0 none, else 1 + the index of the element's waveform id), `key_lo`,
`key_hi` (0 to 127; 0 and 127), `vel_lo`, `vel_hi` (1 to 127; 1 and 127), `coarse` (-48 to +48
semitones; 0) and `fine` (-64 to +63, raw; 0). The element's level and pan stay `amp_level` and
`amp_pan`.

Validation refuses: a wrong magic, version, flags or size, a CRC mismatch, counts above the limits
(1024 samples, 32 elements, 1024 regions, 32 waveforms), no element, a part or element value outside its
range, element indices out of order or unknown, a bad reference, a region with a bad range, root, tune
(±12,000 cents), level (-144 to +48 dB), pan, element or sample index, a loop with start not below end,
loop points without a loop, non-zero reserved bytes, trailing bytes; an element naming a waveform the
sound does not list, an element with both a waveform and regions, an element with a waveform whose
key or velocity limits are inverted, and a sound with neither a region nor an element on a waveform. A
version 0 sound needs at least one sample and one region; a version 1 sound whose elements all play
waveforms has none. Every single-byte corruption and every truncation of a test blob is refused
(`tests/core`).

Parameter indices are stable: `params.inc` and `partparams.inc` only grow at their ends; a new
parameter's default must be the behaviour sounds had without it.

## Text form

```
oscairn-pcm-sound/1
part bend_up=12
sample sample:2b7c...(64 hex digits)
waveform 9f04...(64 hex digits)
element ampeg_attack=30 ampeg_release=127 fil_type=0 fil_cutoff=244 waveform=1 coarse=-12 key_hi=95
element amp_level=100
region key=0-33 vel=1-127 root=30 tune=9.38 level=-7.12 pan=-100 element=1 sample=0 loop=544-93904
```

One item a line; the first line is `oscairn-pcm-sound/0` or `oscairn-pcm-sound/1`; `sample`, `waveform`
(version 1 only) and `element` lines are numbered from 0 in their own order; tune, level and pan take
at most two decimals; `loop=start-end` with the end exclusive (an SFZ `loop_end` plus one); a `#` line
is a comment. Errors name their line.

## Expansion of waveforms

When a sound is prepared (never on the audio thread) each element with a waveform gets regions in the
same table the explicit regions fill, after them, element by element in order and, within an element,
key bank by key bank in the waveform's canonical order. For an element with key limits
[key_lo, key_hi], velocity limits [vel_lo, vel_hi], coarse tune c and fine tune s, a key bank b gives at
most one region:

- keys: [b.lo - c, b.hi - c], except that every bank whose lowest key is the lowest key of any bank in
  the waveform starts at key 0 instead, and every bank whose highest key is the highest of the waveform
  ends at key 127 instead; then met with [key_lo, key_hi] (the sample is chosen by key plus coarse tune);
- velocities: b's range met with [vel_lo, vel_hi];
- root: b's root; transpose: b's transpose plus c (the same pitch as root b.root - c, which may lie
  outside 0 to 127);
- tune: b's tune plus s x 100 / 64 cents truncated toward zero, in whole cents;
- level, pan, loop, loop points, start and end: b's;
- an empty key or velocity range gives no region.

A sound whose regions would then exceed 1,024 is refused, as is a key bank whose transpose or tune would
leave its range. Key banks may overlap (a note then starts each region it hits, up to the core's
16 regions a note may start; the coverage report names a sound that would exceed it).

A waveform's id resolves through the host's `resolve` on the reference `waveform:<id>` to its file;
the file must pass every check of the waveform specification and hold the waveform of that id. Its
samples are sample references `sample:<id>`, as direct ones are.

## Samples

A `sample:<id>` reference is resolved by the host's `resolve_sample` to a record in a pack; a version 0
path reference is resolved as the asset `<library>/<path>/<reference>` from the base asset reference the
sound comes with (sound contexts' `prepare`, or the state), and names a pack holding one record. Each
region keeps its first frames in memory and streams the rest (docs/design.md, "Streaming").

A region from a key bank plays the sample's frames from `start` to `end` (exclusive; 0 is the sample's
end): its frame 0 is the sample's frame `start`. Its loop points count from the sample's frame 0, as
the waveform specification states (start <= loop start < loop end <= end).

## What the converter will need

To write this format instead of SFZ for element parts:

- one `element` line (or blob element) per element, its parameters by the ids of `core/params.inc`,
  which are the fork's `oscairn_*` opcode stems (`oscairn_ampeg_attack` is `ampeg_attack`), plus
  `amp_level` (the element level, today `group_volume`), `amp_pan` (today `oscairn_pan`),
  `elfo_pitch_ratio` / `elfo_amp_ratio` (today `oscairn_elfo_pitch_oncc1` / `_amp_oncc1`), and the
  selection ones (`xa_control`, `element_group`, `keyon_delay`, `keyon_delay_note`);
- in version 1, a waveform per element (its id in a `waveform` line, `waveform=N` on the element) with the
  element's `key_lo`, `key_hi`, `vel_lo`, `vel_hi`, `coarse` and raw `fine`, which the engine expands
  ("Expansion of waveforms"); in version 0, one `region` per key bank and element written out by that
  same rule;
- each element's number in the reference's part (`element_number`, 1-32, 1-8 in a part of the reference: the elements that are off are
  dropped, so the order is not the number) and its connection (`element_connection`), with the part's
  `split_outputs` when the host should get the insertion ports;
- the part's bend range (`bend_up`, `bend_down`), its velocity sensitivity depth and offset (`vel_depth`,
  `vel_offset`, raw 0-127, 64 neutral), and `mono` / `portamento` when set (reported as not covered);
- each element's EQ raw as the reference stores it (`eq_type`, `eq_q`, `eq_freq1`, `eq_gain1`, `eq_freq2`, `eq_gain2`; the
  frequencies 0-511 as they are: the engine reads their low 8 bits, as the reference, and keeps the high shelf within
  131-241);
- the part's controller boxes 1-16 in their order, raw: `boxN_src`, `boxN_dst` (the reference's destination number; 0 or
  a box switched off: none), `boxN_ratio` (-128..+127 as displayed), `boxN_pol`, `boxN_bank`, `boxN_curve`, `boxN_p1`,
  `boxN_p2`, and each element's switch for each box (`box_sw1`..`box_sw16`, on by default); the mod wheel's element LFO
  pitch and amplitude boxes stay `elfo_pitch_ratio` / `elfo_amp_ratio` (other sources' boxes on the element LFO's pitch and amplitude depth stay boxes, destinations 64 and 65; the
  depth law is the same, `docs/acceptance.md` (m)); boxes on destinations or from
  sources the engine does not move yet are kept and reported by `notCovered()`;
- each element's HPF Cutoff Key Follow Sensitivity raw (`fil_hpf_keyfollow`, 0-127, 64 none);
- each element's filter cutoff scaling raw (`fil_scaling_key1`-`4`, MIDI notes; `fil_scaling_offset1`-`4`, raw - 128 as
  displayed) and pitch key follow raw (`pitch_keyfollow`, 0-127, 96 = 100 cents a key; `pitch_keyfollow_center`, the
  note); the region table keeps 100 cents a key from its roots, the engine applies the key follow about the centre;
- the part's knob values (`knob1`..`knob8`, raw 0-1023 as stored; default 512): the shell's knob parameters
  start there, and boxes from the knobs (sources 8-15) read them; boxes from the ribbon (6) and switches 1-2 (16, 17)
  read the shell's parameters of those names (`docs/parameters.md`), which the host's controls drive. A plain box
  (one source, one destination, a curve the host has) may instead become a host control-tree target on the
  destination's parameter (ids 14-19, an offset in raw steps; a full box is the destination's span);
- the part LFO raw (`plfo_wave`, `plfo_speed`, `plfo_phase`, `plfo_tempo_sync`, `plfo_tempo_speed`, `plfo_delay`, `plfo_fade_in`,
  `plfo_hold`, `plfo_fade_out`, `plfo_keyon_reset`, `plfo_dst1`-`3` (the reference's LFO destination number),
  `plfo_depth1`-`3`, `plfo_user_cycle`, `plfo_user_slope`, `plfo_user_step1`-`16` (as displayed, -64..+63),
  `plfo_random_speed`) and each element's `plfo_box_sw1`-`3`, `plfo_ratio1`-`3` and `plfo_phase_offset` (0-5);
- the part's cutoff, resonance and filter envelope depth offsets raw as displayed (`part_cutoff`, `part_resonance`,
  `part_feg_depth`, -64..+63): the engine applies them (8, 2 and 2 steps a unit); do not fold them into the elements;
- the samples as references relative to the base it gives the sound (the asset library's folder).
