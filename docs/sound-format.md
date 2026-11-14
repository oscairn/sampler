# Sound format v0

A sound is one blob: a region table over element parameter sets in the reference's raw units, and
abstract sample references. `core/sound.cpp` writes, reads and validates it; the text form is for
tests and hand-written sounds. Version 0 is the first; a reader refuses any other version.

## Blob (little-endian)

| offset | field |
|---|---|
| 0 | `OPCM` |
| 4 | u16 version (0), u16 flags (0) |
| 8 | u32 size of the whole blob |
| 12 | u32 CRC-32 (IEEE) of bytes 16 to the end |
| 16 | u16 samples, u16 elements, u16 regions, u16 part parameters (143 now; a blob with fewer, written before the later ones were added, reads with those at their defaults) |
| 24 | part parameters, i16 each, in `core/partparams.inc` order |
| | samples: u8 length, then the reference's bytes (a relative path, no NUL, no `..` segment, no leading `/`) |
| | elements: u16 count, then count x (u16 parameter index in `core/params.inc` order, i16 value); indices increasing; only values that differ from the default |
| | regions, 32 bytes each: u8 lokey, hikey, lovel, hivel, root; i8 transpose; u8 loop (0 none, 1 forward); u8 0; u16 element; u16 sample; i32 tune (1/100 cent); i32 level (1/100 dB); i16 pan (1/100 of -100..100); u16 0; u32 loop start; u32 loop end (exclusive) |

Validation refuses: a wrong magic, version, flags or size, a CRC mismatch, counts above the limits
(1024 samples, 32 elements, 1024 regions) or zero, a part or element value outside its range, element
indices out of order or unknown, a bad reference, a region with a bad range, root, tune (±12,000
cents), level (-144 to +48 dB), pan, element or sample index, a loop with start not below end,
loop points without a loop, non-zero reserved bytes, trailing bytes. Every single-byte corruption
and every truncation of a test blob is refused (`tests/core`).

Parameter indices are stable: `params.inc` and `partparams.inc` only grow at their ends; a new
parameter's default must be the behaviour sounds had without it.

## Text form

```
oscairn-pcm-sound/0
part bend_up=12
sample library/pad-c1.flac
element ampeg_attack=30 ampeg_release=127 fil_type=0 fil_cutoff=244 fileg_depth=52
region key=0-33 vel=1-127 root=30 tune=9.38 level=-7.12 pan=-100 element=0 sample=0 loop=544-93904
```

One item a line; `sample` and `element` lines are numbered from 0 in order; tune, level and pan take
at most two decimals; `loop=start-end` with the end exclusive (an SFZ `loop_end` plus one); a `#`
line is a comment. Errors name their line.

## Samples

A reference is resolved by the shell as the asset `<library>/<path>/<reference>` from the base asset
reference the sound comes with (sound contexts' `prepare`, or the state). Milestone 1 decodes WAV or
FLAC whole into 16-bit PCM; the pack and its codec are a later milestone, which the reference's
abstractness leaves room for (a reference may then name a sample in a pack).

## What the converter will need

To write this format instead of SFZ for element parts:

- one `element` line (or blob element) per element, its parameters by the ids of `core/params.inc`,
  which are the fork's `oscairn_*` opcode stems (`oscairn_ampeg_attack` is `ampeg_attack`), plus
  `amp_level` (the element level, today `group_volume`), `amp_pan` (today `oscairn_pan`),
  `elfo_pitch_ratio` / `elfo_amp_ratio` (today `oscairn_elfo_pitch_oncc1` / `_amp_oncc1`), and the
  selection ones (`xa_control`, `element_group`, `keyon_delay`, `keyon_delay_note`);
- one `region` per key bank and element: key and velocity ranges clipped to the element's limits, the
  key bank's root, transpose, tune in cents (with the fine tune s, -64..+63: s x 100 / 64
  cents truncated toward zero, measured at every step); an element's coarse tune moves its key map, since the reference picks the
  sample by key plus coarse tune (measured): the key bank for keys lo..hi at root r serves keys
  lo - c..hi - c at root r - c (the lowest bank still reaches key 0 and the highest key 127, inferred),
  level in dB (the key bank's own; `volume` today), pan as the SFZ `pan` it writes today (-100..100),
  the loop with its end exclusive;
- each element's number in the reference's part (`element_number`, 1-8: the elements that are off are
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
  pitch and amplitude boxes stay `elfo_pitch_ratio` / `elfo_amp_ratio` (other sources' E.LFO PMD / AMD boxes stay boxes, destinations 64 and 65; the
  depth law is the same, `docs/acceptance.md` (m)); boxes on destinations or from
  sources the engine does not move yet are kept and reported by `notCovered()`;
- each element's HPF Cutoff Key Follow Sensitivity raw (`fil_hpf_keyfollow`, 0-127, 64 none);
- the part's assignable knob values (`knob1`..`knob8`, raw 0-1023 as stored; default 512): the shell's knob parameters
  start there, and boxes from the knobs (sources 8-15) read them; boxes from the ribbon (6) and switches 1-2 (16, 17)
  read the shell's parameters of those names (`docs/parameters.md`), which the host's controls drive. A plain box
  (one source, one destination, a curve the host has) may instead become a host control-tree target on the
  destination's parameter (ids 14-19, an offset in raw steps; a full box is the destination's span);
- the samples as references relative to the base it gives the sound (the asset library's folder).
