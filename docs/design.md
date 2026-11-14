# Design

## Layers

- `core/` is the DSP core: plain C++17, no plugin API, no OS, no threads, no files, no heap. The
  caller owns every object (`pcm::Core`, `pcm::SoundData`, `pcm::Compiled`) and the decoded samples,
  which it hands in as `SampleView`s. The sound format (`sound.cpp`), the coverage report
  (`notCovered`) and the reference's measured laws (`laws.cpp`, `tables.cpp`, carried over from the
  sample-engine fork under its BSD licence) live here too.
- `src/` is the shell: CLAP, parameters, state and snapshot, sample assets (resolved through the
  host's `oscairn.assets`, decoded whole with dr_flac/dr_wav), the Oscairn instrument contract
  through the SDK's helpers. It talks to the core through `src/engine.h` only.

## The voice

A note plays every region of its sound it hits (its key and velocity); each region becomes an
element voice in the pool of its plan. A plan is data, `PlanDesc {channels, interpolator, filter}`;
the stage kernels are plain functions compiled once per plan (`Kernel::run<C, I, F>`), chosen from a
table at run time. Four voices share a vector lane (NEON, SSE2; one on the scalar build). A piece of
up to 16 frames runs, per lane:

1. feed (scalar, per voice): the tap window the piece needs, mapped from virtual (never wrapped)
   32.32 positions to the sample, stitched across a loop end, converted to float once;
2. fetch: the interpolator per sample, the pitch step ramped per sample while it moves, times the
   amplitude envelope `g = min(max(g m + a, lo), hi)`;
3. filter (type 0): the two sections, the fork's 48 kHz form (each unit delay replaced by the
   allpass pinned to one 44.1 kHz sample at the cutoff and at 18 kHz), then the mix into the output
   with the gain ramp; thru plans mix directly.

Control steps fall every 16 frames from the start of each host block, wherever events split it, so
a block renders the same split or not (tested). At a control step each voice's envelopes advance in
double precision to that frame: a straight line in amplitude for attacks, short releases and fades,
a straight line in dB for decays and releases, the fork's segment rules and tables exactly. The
per-sample chain then carries the amplitude from there, so a segment's shape is the fork's sample for
sample; only the start of the next segment waits for the next control step (at most 15 frames).
The pitch and filter envelopes (straight lines in cents) are read at each control step's first frame,
which is where the fork updates its filter coefficients. A voice whose control steps would change
nothing (sustaining, its envelopes still, no gain ramp) skips them until an event marks it.

Events act between pieces on their own frame: a note starts on its frame (its envelopes from the frame
before it), a note-off enters the releases there, a choke fades in a straight line, a shed fades over
16 frames. Positions stay within 32-bit frame indices by rebasing within the loop.

## The interpolator

Requirement: the image rejection the reference measures on transposed broadband noise (the noise test
of run interp-2026-11-13: the reference's white-noise source wave transposed down; the fair
row is the ratio of about 0.42, where the reference leaves its images at -87 to -99 dB over the
11-22 kHz output bands, -74 dB at 9.5-11 kHz). The same test on the engine (`pcbench rawq`, a source
shaped like the reference's wave, 44.1 kHz played at 48 kHz, bands in dB re the 1-3 kHz band):

| kernel | 9-11 kHz | 11-13 | 13-15 | 15-17 | 17-19 | 19-20.5 | 20.5-21.5 kHz | cost a voice-block, mono / stereo |
|---|---|---|---|---|---|---|---|---|
| reference (measured) | -74 | -87 .. -99 over 11-22 kHz | | | | | | |
| Hermite | -38.9 | -29.2 | -30.4 | -39.2 | -58.2 | -63.5 | -53.5 | 7,339 / 10,219 |
| 16 taps, Kaiser 4.5, full band | -57.0 | -69.8 | -67.0 | -65.4 | -67.0 | -70.6 | -71.8 | 9,863 / 13,749 |
| 24 taps, Kaiser 6.9, full band | -60.7 | -91.0 | -88.8 | -88.0 | -88.8 | -92.0 | -95.4 | 11,239 / 17,733 |
| **20 taps, Kaiser 7.5, cutoff 0.9 (the engine's)** | **-76.3** | **-96.2** | **-93.8** | **-91.9** | **-93.3** | **-96.9** | **-98.3** | **10,639 / 14,549** |

Costs: callgrind instructions per element voice per 128-frame block, the pads' plan (filter type 0),
(From 0.2.0 the cutoff is chosen per voice by its transposition from a bank of 17: 1.0 at the root,
0.9 below, following the step above, as measured on the reference: docs/acceptance.md (f).)
8 to 32 held voices (`docs/cost.md`; the 16- and 24-tap rows from builds with `PCM_KERNEL_*`). The 20-tap kernel with its cutoff at 0.9 of the source's Nyquist
meets the reference's rejection in every band of the fair row (24 full-band taps only just do) at
5 % less than 24 taps mono and 18 % less stereo; 16 taps miss it by about 20 dB. Its price is passband
droop near the source's top: the continuous kernel's response is -1.2 dB at 0.8 of the source's
Nyquist and -6 dB at 0.9 (24 full-band taps: 0.0 and -0.6 dB). The reference's droop could not be
isolated (its source rolls off; README of the measurement), so this is a known difference, not a
verified one. 256 phases with linear interpolation between them; a kernel row holds the taps in groups
of four, each followed by its differences to the next phase, one pair load a group. Upward
transposition is not band-limited (as in the fork); the reference's upward behaviour is unresolved by
its measurements (its noise up at notes 84 and above shows no band limit).

`PCM_KERNEL_TAPS`, `_BETA` and `_CUT` build the comparison kernels; Hermite is the core's other
interpolator (`Core::setInterp`), used to null against the fork; the plugin plays it only when
`OSCAIRN_PCM_INTERPOLATION=hermite` is set (a measurement switch).

## Memory and threads

The core is preallocated with the instance (4 pools of 128 element voices, the kernel table, the
scratch windows: `sizeof(pcm::Core)`). A sound's samples are decoded whole on the main thread and
shared between the instance's sounds while any uses them (counted as asset bytes in
`oscairn.memory`). The shell publishes a compiled sound per context through a triple buffer the audio
thread installs from; installing a context's new sound ends the voices of its old one, and the main
thread drops an entry's sound (and so its samples) only when it overwrites that entry, which is never
the installed one. Nothing on the audio thread allocates, locks or blocks (tested by interposing the
allocator).
