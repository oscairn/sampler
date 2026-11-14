# Acceptance: the probe sounds against the fork and the reference

Probe sound A: a 17-region pad with mono and stereo sources (two pad parts of one multi-part setup on
the reference that convert to the same patch), as the converter wrote it for the sample-engine fork
(analog-0.4.12-sampler-0.8.4 output). The patch (17 regions: 16 mono, hard-panned in pairs, and one stereo region over keys 101-127 at
-7.12 dB; sources at 44.1, 40 and 20 kHz; loops of 6k-94k frames; filter type 0, cutoff 244,
resonance 6, the filter envelope at depth +52 with depth velocity sensitivity +39; amplitude attack
30, release 127; velocity sensitivity 0), so one sound text serves both: the patch's SFZ mapped one to
one onto sound format v0. Samples, recordings and renders stay on the measuring machine; nothing of them is in
this repository, and recordings are named here by run and take identifiers only.

## (a) Against the fork's element voice

Run at b9e55b2 (milestone 1). From round 2 the engine follows the reference where the two differ
(staging, pan: (c)), so its levels no longer equal the fork's; the table stands for the voice
machinery it checked.

The same CLAP note events (48 kHz, 128-frame blocks) rendered by the fork's plugin (an Oscairn Sampler
0.8.5 macOS build; its release time at index 127 is origin/Oscairn's 105.7 s, as the equal release
slopes show; loading the setup's own node state) and by this plugin (the
sound prepared through `oscairn.sound-contexts` from its text form, samples through
`oscairn.assets`), with Hermite (the fork's interpolation; `OSCAIRN_PCM_INTERPOLATION=hermite`) for
the null test and separately with the engine's 20-tap kernel. Sequences: single notes at keys 30, 50,
60, 76, 95, 110 (one per region pair, 110 the stereo region), velocity 100, 3 s held; key 60 at
velocities 30 and 127; a four-note chord started off the block grid and released in turn.

Measures: level, RMS over the held part (1-3 s), engine against fork; attack, onset to -1 dB of the
held level (1 ms resolution); release slope, a line fitted to the level 0.2-3 s after the note-off;
octave bands 62.5 Hz-16 kHz of the held part (Welch), the largest difference over the bands within
50 dB of the loudest; the null: the residual after the best fractional time alignment per 100 ms
window, median over 0.15-0.65 s (early) and 2-3 s (late), the worse channel. The fork's read
position is computed in float per block and drifts by up to a frame a few seconds into a note
(stock sfizz's arithmetic; it also stalls each voice's position one frame at its second block), so
the late null is limited by the fork itself: the last column is the fork against itself rendered in
96-frame blocks. The engine renders bit-identically at 96 and 128 frames (and split anywhere).

| sequence | level herm / sinc (dB) | attack fork / herm / sinc (ms) | release slope fork / herm / sinc (dB/s) | bands herm / sinc (dB) | null early / late (dB) | fork 128 vs 96 early / late (dB) |
|---|---|---|---|---|---|---|
| key 30 | +0.000 / +0.000 | 6.0 / 6.0 / 6.0 | -0.380 / -0.380 / -0.380 | 0.01 / 0.08 | -80.4 / -64.5 | -86.5 / -70.5 |
| key 50 | +0.000 / +0.001 | 6.0 / 6.0 / 6.0 | -0.829 / -0.829 / -0.829 | 0.01 / 0.07 | -68.3 / -53.0 | -70.9 / -55.6 |
| key 60 | -0.000 / +0.005 | 6.0 / 6.0 / 6.0 | -1.443 / -1.443 / -1.443 | 0.00 / 0.53 | -71.0 / -53.9 | -86.4 / -79.8 |
| key 76 | -0.000 / +0.045 | 7.0 / 7.0 / 7.0 | -2.698 / -2.699 / -2.701 | 0.05 / 0.67 | -41.4 / -25.0 | -53.0 / -36.2 |
| key 95 | +0.000 / +0.090 | 5.0 / 5.0 / 5.0 | -7.778 / -7.781 / -7.836 | 0.03 / 4.58 | -47.9 / -31.9 | -47.3 / -30.7 |
| key 110 (stereo) | -0.000 / +0.277 | 5.0 / 5.0 / 5.0 | -13.661 / -13.662 / -13.701 | 0.00 / 60.17 | -61.6 / -49.9 | -65.7 / -53.5 |
| key 60, velocity 30 | -0.000 / +0.000 | 6.0 / 6.0 / 6.0 | -3.223 / -3.223 / -3.223 | 0.00 / 0.00 | -81.0 / -64.7 | -93.5 / -89.5 |
| key 60, velocity 127 | -0.000 / +0.023 | 6.0 / 6.0 / 6.0 | -1.286 / -1.286 / -1.293 | 0.00 / 0.49 | -62.1 / -44.6 | -81.5 / -70.8 |
| chord | +0.000 / +0.002 | 158.7 / 158.7 / 158.7 | -1.427 / -1.427 / -1.427 | 0.00 / 0.19 | -61.1 / -44.3 | -60.3 / -43.0 |

The 20-tap kernel's band differences of 4.58 dB (key 95) and 60 dB (key 110) are in the 16 kHz
octave, above the source's band (the 20 kHz source played at key 110 has nothing above 11.2 kHz):
the fork's Hermite images, which the kernel removes. Below the 16 kHz octave every band within 40 dB
of the loudest is within 0.92 dB of the fork; the bands further down hold Hermite's aliases at about
-105 to -115 dB, where the kernel leaves up to 9 dB less.

| check | tolerance | result |
|---|---|---|
| level, Hermite | ±0.05 dB | pass (all within 0.001 dB) |
| level, 20-tap kernel | ±0.3 dB | pass (largest +0.277 dB, the stereo 20 kHz source) |
| attack | ±1 ms | pass (equal at the analysis's 1 ms) |
| release slope, Hermite / 20-tap | ±1 % | pass (largest 0.04 % / 0.75 %) |
| octave bands, Hermite | ±0.2 dB | pass (largest 0.05 dB) |
| octave bands up to the 8 kHz octave within 40 dB of the loudest, 20-tap | ±1 dB | pass (largest 0.92 dB) |
| null, early window, Hermite | -40 dB or lower | pass (worst -41.4 dB, key 76) |
| null, late window | reported, not gated (limited by the fork's own block dependence) | - |

Not reproduced on purpose (known differences from the fork): its one-frame position stall at each
voice's second block, its block-dependent float positions, and its reads past a sample's end at a
loop crossing (these samples end exactly at their loop ends; the engine stitches the loop). The
engine's segment ends wait for the next control step (at most 15 frames); on the pads this touches
nothing measurable (the attack ends on flat decays).

## (b) Against the reference: probe sound A

Recordings: the reference's takes `p2_62`, `p2_70`, `p2_78`, `p2_70v30` of run release-law-2026-11-14 (44.1 kHz; the
part alone at Part Volume 100, effects off; velocity 100, or 30 for `70v30`; note held 2.0 s; 40 s).
The engine and the fork rendered the same notes at 44.1 kHz. Levels: RMS in 0.25 s windows; "held"
is 1-2 s after the onset; tails relative to each take's own held level.

The engine renders without part volume (it belongs to the multi-part setup: the converter carries it as the
part's level); the engine's renders are lowered by -4.125 dB, Part Volume 100 on the reference's
measured part volume law (c), for this comparison.

| note | held: reference | engine | engine - reference |
|---|---|---|---|
| 62, velocity 100 | -28.44 dB | -28.50 | -0.06 dB |
| 70, velocity 100 | -28.78 | -28.81 | -0.02 |
| 78, velocity 100 | -28.79 | -28.77 | +0.02 |
| 70, velocity 30 | -31.63 | -31.26 | +0.37 |

(Before this round the engine, with the fork's staging and pan law, was 9.15-9.56 dB louder: the
fork's staging 2.07 dB, the sfizz pan law's +3.01 dB on the hard-panned mono key banks, and the part
volume 4.125 dB.) At velocity 30 the reference is 0.37 dB softer relative to velocity 100 than the
converted sensitivity 0 gives (D4).

Tail after the note-off, relative to the held level (dB), at 2-second steps from 0.1 s (engine with
this round's cutoff and filter-release laws, (d)):

| note | | 0.1 s | 2.1 | 4.1 | 6.1 | 8.1 | 10.1 | 14.1 | 20.1 | 30.1 | 36.1 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 62 | reference | -0.1 | -2.5 | -20.7 | -49.3 | -51.4 | -50.0 | -55.5 | -58.1 | -64.7 | -68.3 |
| | engine | -0.1 | -2.2 | -19.2 | -48.1 | -49.9 | -49.2 | -54.1 | -56.7 | -63.1 | -66.5 |
| 70 | reference | -0.7 | -4.3 | -30.5 | -56.5 | -56.5 | -55.4 | -53.2 | -64.0 | -63.0 | -72.2 |
| | engine | -0.9 | -3.9 | -29.0 | -56.0 | -56.0 | -55.0 | -53.1 | -63.6 | -62.9 | -71.6 |
| 78 | reference | -0.2 | -6.0 | -39.9 | -56.6 | -43.0 | -45.4 | -64.8 | -50.2 | -70.6 | -64.6 |
| | engine | -0.3 | -5.7 | -39.5 | -56.3 | -42.9 | -45.6 | -66.0 | -49.9 | -71.0 | -65.7 |
| 70, velocity 30 | reference | -1.0 | -10.9 | -36.3 | -53.5 | -53.5 | -52.5 | -50.3 | -61.0 | -60.1 | -69.2 |
| | engine | -1.0 | -10.3 | -38.3 | -53.3 | -53.4 | -52.4 | -50.6 | -60.9 | -60.4 | -69.0 |

Which way the tail differs, settled: with each take referred to its own held level and the part
volume applied (so absolute levels agree, -0.1 to +0.4 dB), the reference's tail is never louder than
the engine's. The earlier claim (the reference's tail 8-10 dB louder from the release on) compared
absolute levels without the 9 dB of staging, pan law and part volume; it is wrong. Before this round
the engine was 5.9-6.9 dB above the reference at 4.1 s (the fork's cutoff table at the bottom and its
filter-release time at index 86); now it is +1.5, +1.5, +0.4 and -2.0 dB.

| check | tolerance | result |
|---|---|---|
| absolute held level (part volume applied) | ±1 dB | pass (-0.11 to +0.29 dB) |
| tail while the filter envelope closes (4.1 s) | ±2 dB | pass (+1.5, +1.5, +0.4 dB; velocity 30 at the limit, -2.0 dB) |
| tail after the filter has closed (6.1-36.1 s) | ±3 dB | pass (largest 1.8 dB, note 62) |

## (c) Level staging, pan and part volume against the reference

Recordings `a_*` of run pcm-round2-2026-11-14 (44.1 kHz, one element, filter Thru,
effects off, velocity sensitivity 0): a mono saw source wave and a stereo pad source wave at note 60 with the
element pan at 1, 8, 16, ..., 127; the hard-panned mono key-bank pairs of the pads' wave at note 70
with element pans 1, 32, 64, 96, 127; part volume 127 to 16; element level 100 and 64. The engine
rendered the same source waves (the extraction is bit-identical to the converted library's).
Level: RMS per channel 0.5-1.2 s after the onset.

- Staging: a full-scale sample at element level 127, part volume 127, pan centre plays at -9.43 dB
  per channel, mono and stereo alike (the fork's staging was 2.07 dB above that; earlier estimated at
  1.84 dB from sine probes, K28).
- Pan: the key bank's pan and the element pan add in raw steps (a key bank's -100..100 as 1..127),
  clamped, and the sum takes the element's balance law (K1): the near side keeps the centre level,
  the far side falls as sin^2. Mono and stereo samples alike. A hard-panned mono key bank therefore
  plays at the centre level on its side (no +3 dB), and with the element pan hard left a right-panned
  key bank sounds at the centre on both sides.
- Part volume: (volume / 127)^2 on the 0.375 dB grid, the element level's law: 120 -1.125, 112 -2.25,
  100 -4.125, 90 -6.0, 80 -7.875, 64 -12.0, 48 -16.875, 32 -24.0, 16 -36.0 dB, every point within 0.01
  dB. It belongs to the multi-part setup (the part's level), not the engine's; the converter writes 0.851 (-1.40
  dB) for the pads' volume 118, where the law gives -1.125 dB: for the converter.

| check | tolerance | result |
|---|---|---|
| mono and stereo, element pans 1-127, each side | ±0.25 dB | pass (largest 0.14 dB, pan 120) |
| hard-panned key banks with element pans 1-127 | ±0.25 dB | pass (largest 0.18 dB, pan 1) |
| element level 100, 64 | ±0.25 dB | pass (0.00, 0.02) |

## (d) Filter laws against the reference

Recordings `b_*` (run pcm-round2-2026-11-14), one element, a saw or a white-noise source wave.

- Cutoff at the bottom of the range: a saw at note 36 (harmonics of 65.46 Hz) through type 0 at
  resonance 0, key follow 0, cutoff 0-384 in steps of 16; the cutoff fitted to the harmonics' levels
  against a model of the two 44.1 kHz sections (the same fit of the engine's renders returns the
  engine's table to 0.5 cents, which checks the method). From 264 up the fork's knots agree within
  35 cents (256 -35, 272 +7, 288 -17, 320 -23, 352 -22, 384 -10); below, the reference sits 60-310
  cents lower (the fork extrapolated below 144 and its knots 144-256 came from noise). The engine now
  takes the measured knots 0-256 (fit residual 0.08-0.26 dB on the harmonics): 0 -5932 cents (14.4 Hz),
  128 -4281, 256 -2773.
- Filter envelope release: the cutoff held at +4800 cents over index 300, released to 0 at indices
  48-104 with the amplitude held; the cutoff path fitted per 93 ms window. Straight in cents and taking
  1.5 x the decay table's time, as the fork, except within each octave of 8 indices: 8k 1.0 (48 0.98,
  64 0.995, 72, 80, 88, 96, 104 within 1.5 %), 8k+2 0.9875 (82, 90), 8k+4 0.973 (76, 84, 92, 100), 8k+6
  0.9525 (86); odd places interpolated. Residual: the path fits within 31 cents rms, but the best fit
  needs the reference's release to start 15-150 ms after the engine's, growing with the index (not the
  recorder's event jitter, which is under 12 ms): not modelled.
- Filter envelope hold (the piano holds 127): 96, 112, 127 against the fork's law (extrapolated past
  80): the reference 1.0 %, 1.3 %, 1.8 % shorter. Kept.
- Types 4 and 5 (white noise, root key): cutoffs 373-1023, HPF cutoffs 0-512: every octave band to 8 kHz
  within 0.35 dB of the reference (an overall +0.14 dB); the 16 kHz octave 0.4-1.1 dB low, the
  interpolation kernel's droop at the root (item (e)).
- Element EQ: the probes did not load on the reference (the recorder kept the previous performance;
  so did a white-noise Thru probe), so EQ is not measured.

## (e) Voice end

Recordings `c_end*`: a sine source wave at element level 127 (and 64 once), release 96, 104, 112, 120. The
reference's release is a straight line in dB at the table's rate down to -96.45 dB below the held
level (96: -96.46, 96 at level 64: -96.43, 104: -96.47, 112: -96.47 on the fitted line at the last
non-zero sample), where the voice ends (digital silence). The take at 120 was too short to reach its
end (92 s) and is not used. The engine now ends there too (it ended at -90 dB, the fork's floor):
end level re held, last 50 ms, reference / engine: 96 -96.39 / -96.25, 104 -96.48 / -96.38, 112
-96.53 / -96.40 dB; end time after the onset 13.39 / 13.45, 26.08 / 26.19, 51.33 / 51.68 s (the
recorder's note-off falls 0-0.35 s off its nominal time). Decays to level 0 keep the fork's -90 dB
floor (K44: the reference holds about -95 dB there; not measured here).

| check | tolerance | result |
|---|---|---|
| level where the voice ends, releases 96-112 | ±0.5 dB | pass (0.10-0.14 dB) |

## (f) Interpolation against the reference

Recordings `d_saw*`: a saw source wave's single-cycle loops (44.1 kHz, root keys 24, 36, 48) played at
their root and 1-11 semitones above, filter Thru. The output's harmonic levels against the stored
cycle's own give the kernel's response at each harmonic's source frequency; the power off the
harmonics (images, aliases) is read against the total. (The white-noise Thru probes did not load on
the reference, see (d); the root-24 region's analysis did not resolve, so below the root the
reference is not measured.)

Response at 0.5 / 0.7 / 0.8 / 0.85 / 0.9 / 0.95 of the source's Nyquist (dB), and off-harmonic power:

| transposition | reference | engine before (20 taps at 0.9) | engine now, 44.1 kHz | engine now, 48 kHz host |
|---|---|---|---|---|
| root (1.000) | 0.0 -0.7 -0.7 -0.4 -0.2 0.0; -78 dB | 0.0 -0.7 -1.9 -3.2 -6.0 -9.8; -90 | 0.0 -0.7 -0.7 -0.4 -0.1 0.0; -90 | 0.0 -1.0 -0.4 -0.4 -1.1 -2.9; -84 |
| +1 (1.059) | -0.4 -2.1 -4.1 -5.6 -6.7 -7.9; -69 | 0.0 -0.4 -2.2 -3.6 -6.4 -9.7; -81 | as before (cutoff 0.9) | -0.0 -0.8 -1.7 -3.1 -6.0 -10.8; -87 |
| +2 (1.122) | -1.6 -4.5 -6.5 -8.1; -70 | -0.4 -0.9 -2.3 -3.9; -74 | -0.4 -2.0 -7.2 -11.8; -87 | -1.0 -1.7 -6.4 -10.9; -92 |
| +5 (1.335) | -5.5 -13.6; -73 | -0.2 -0.3; -61 | -0.5 -11.2; -81 | -0.3 -11.7; -84 |
| +7 (1.498) | -10.5; -75 | -0.3; -62 | -11.1; -89 | -11.2; -86 |
| +11 (1.888) | -17.3; -75 | 0.0; -52 | -17.9; -84 | -18.2; -91 |

- At the root the reference passes the source unchanged: the measured ripple (-0.7 dB at 0.7-0.8) is
  the same in the engine's identity output, so it is the analysis's (the cycle's own spectrum), not a
  kernel. The engine's 0.9 kernel was audibly darker there (-1.9 dB at 17.6 kHz, -6 dB at 19.8 kHz,
  -9.8 dB at 21 kHz), confirmed on noise through types 4 and 5 ((d): the 16 kHz octave 0.4-1.1 dB low).
- Above the root the reference's cutoff follows the step: it rolls off progressively (at half the
  source's Nyquist -0.4 dB at one semitone, -10.5 at seven, -17.3 at eleven) and leaves no aliases
  (-69 to -75 dB off the harmonics, its floor); the fork's and the old kernel's upward transposition
  was not band-limited (aliases up to -52 dB).
- The engine now keeps a bank of 17 kernels (20 taps, Kaiser 7.5, cutoffs 1.0 to 0.2 of the source's
  Nyquist in steps of 0.05) and picks, at each pitch change, the one for the voice's transposition at
  44.1 kHz (`laws::kernelCutoff`): 1.0 from 0.95 to 1.0 (exact at the root: the 1.0 kernel's phase 0
  is the identity), 0.9 below 0.95 (the old design and its image rejection, where the reference is
  not measured), and above 1.0 the cutoff fitted to the reference at each measured transposition,
  linear between, 1 / step beyond. The fit errors are the reference's gentler slope: rms 2.2 dB at one
  and two semitones, 3.5 dB at five, 0.2 and 0.05 dB at seven and eleven. Images of the 1.0 kernel from
  0.95 to 1.0 land above 20 kHz (and at a 48 kHz host between 22.05 and 24 kHz).

| check | tolerance | result |
|---|---|---|
| root, 0.5-0.95 of the source's Nyquist, 44.1 kHz | ±0.5 dB of the reference | pass (largest 0.04 dB) |
| above the root, aliases | at or below the reference's | pass (-81 to -89 dB against -69 to -75) |
| above the root, response | reported (fit rms above) | the shape differs by up to 4.6 dB at one point (+5 semitones, 0.5 of Nyquist: -0.5 against -5.5) |

## (g) Probe sounds B (a piano) and C (a bass) against the reference

Recordings `e_*`: probe sound B, an 854-region stereo piano (18 elements, 854 regions of 854 stereo samples, filter
types 5, 4 and 0, element EQ Boost6, half damper, a key-off element, level scaling, time key follow)
and probe sound C, a two-layer bass (2 elements of a stereo pad wave an octave apart, filter type 4),
each alone, effects off, Part Volume 127; the piano's part Velocity Sensitivity Offset set to 64 (its
own offset, above 64, is not carried by the converter and not built here: at velocity 80 it plays the reference 1.51 dB louder, D10). Piano:
keys 33, 48, 60, 72, 96 at velocities 30, 80, 120, held 3 s; bass: keys 35, 37, 40 at velocity 100,
held 2 s. The engine rendered the same source samples through sound texts written from the
parts' own parameters (the converter is out of scope). Measures: level 0.05-1.0 s after the
onset; octave bands 62.5 Hz-8 kHz over 0.05-1.5 s (Welch), the largest difference within 40 dB of
the loudest after the level difference; the time to 10 and 20 dB below the peak; the release, the
time from the note-off to 30 dB below the level there.

Measured on the way (recordings `f_*`, `h_*`, `i_*`, `j_*`, `k_*`, `l_*`):
- Element EQ: with any filter type but Thru, Boost6, Boost12 and Boost18 are flat gains of +6.02,
  +12.04 and +18.06 dB (every octave band within 0.15 dB) and 2-band at 0 dB is flat (0.02-0.1 dB);
  with the filter Thru the EQ is bypassed (all types equal). Built; P.EQ and 2-band with gain not.
- Half damper (a sine, release 40): with the switch on and the pedal down, a key-up enters the
  release at the half damper time (103: 4.07 dB/s, 127: 0.61, 64: 121 dB/s, the release table's rates);
  at pedal 64 the index in between in proportion (40 + 63 x 64 / 127 = 72: 60.6 dB/s, table 60.5); pedal
  up: the element's own release; switch off: the pedal holds the note. On the piano, whose envelope is
  still in its decays at the key-up, the note plays on under the pedal exactly as held: the engine
  starts the half damper's release only at the sustain (inferred from these two cases).
- Filter gain 200-255 against the fork's law (relative to 230): within 0.44 dB (245: +5.66 measured,
  +5.22 the law).

| take | level eng - ref (dB) | bands (dB) | to -10 dB ref / eng (s) | to -20 dB | release to -30 dB |
|---|---|---|---|---|---|
| piano 33, v30 / 80 / 120 | -0.12 / -0.16 / -0.20 | 0.25 / 0.52 / 0.23 | - | - / - / 3.00, 3.04 | 0.33, 0.32 / 0.30, 0.32 / 0.35, 0.35 |
| piano 48 | -0.16 / -0.22 / -0.24 | 0.12 / 0.08 / 0.05 | 0.99, 1.03 / 0.86, 0.91 / 0.90, 0.91 | 2.00, 2.05 / 1.77, 1.86 / 1.74, 1.83 | 0.35, 0.35 / 0.36, 0.37 / 0.35, 0.35 |
| piano 60 | -0.15 / -0.22 / -0.26 | 0.10 / 0.13 / 0.09 | 1.07, 1.14 / 0.89, 0.93 / 0.72, 0.76 | 2.19, 2.32 / 1.95, 2.07 / 1.71, 1.81 | 0.19, 0.21 / 0.31, 0.33 / 0.31, 0.30 |
| piano 72 | -0.20 / -0.27 / -0.30 | 0.11 / 0.14 / 0.34 | 0.10, 0.09 / 0.28, 0.29 / 0.24, 0.25 | 0.87, 0.90 / 1.06, 1.11 / 0.94, 0.99 | 0.16, 0.21 / 0.18, 0.17 / 0.24, 0.22 |
| piano 96 | -0.30 / -0.40 / -0.43 | 0.10 / 0.20 / 0.20 | 0.08, 0.08 / 0.09, 0.09 / 0.09, 0.09 | 0.18 (all) | 1.69, 1.66 / 2.03, 2.00 / 1.37, 1.38 |
| piano 60 v80, pedal down before the key-up, up at 4.7 s | -0.22 | 0.13 | 0.89, 0.93 | 1.95, 2.07 | after the pedal: 0.42, 0.43 (0.20 before the key-off element moved to the pedal's release) |
| bass 35 | +0.30 | 0.98 (2.4 at 125 Hz in the first 0.5 s) | 2.38, 2.28 | 2.70, 2.64 | 1.41, 1.29 |
| bass 37 | +0.15 | 1.85 (4.1 at 125 Hz in the first 0.5 s) | 2.29, 2.26 | 2.98, 2.49 | 1.48, 1.42 |
| bass 40 | -0.59 | 3.64 (62.5 Hz, +3.9 over 0.5-1.9 s) | 2.43, 2.36 | 2.95, 2.92 | 1.75, 1.31 |

| check | tolerance | result |
|---|---|---|
| level | ±1 dB | pass: piano -0.12 to -0.43, bass -0.59 to +0.30 |
| octave bands within 40 dB of the loudest | ±2 dB | piano pass (largest 0.52); bass FAIL at 62.5-125 Hz (up to 4.1 dB) |
| times to -10 and -20 dB | ±10 % | piano pass (largest 6 %); bass 37 FAIL at -20 dB (2.49 against 2.98: in the release) |
| release to -30 dB | ±0.1 s or ±10 % | piano pass (largest 0.05 s); bass 40 FAIL (1.31 against 1.75); after the pedal pass (0.43 against 0.42) |

The bass's two elements play the same stereo pad samples an octave apart (its samples beat, a
chorus); the differences sit where element 2's fundamental meets element 1's and move with time, so
they look like the two layers' relative phase or tuning, not a level law: not resolved (D11). The
release after the pedal goes up was faster in the engine (D12): under the pedal the reference starts a
key-off element not at the key's release but at the pedal's (no burst at the key-up; +6.5 to +9 dB in
the 50 ms after the pedal goes up, against the held note's level). The engine now does the same.

## Known differences (this engine's register)

| # | what | size | status |
|---|---|---|---|
| D1 | Output staging | - | resolved: the reference's, measured (c) |
| D2 | Key-bank pan | - | resolved: summed with the element pan, the balance law (c); key-bank pans between the extremes not measured (no source wave tried has them) |
| D3 | The filter envelope's release starts 15-150 ms later on the reference (growing with the index) | path within 31 cents rms (d) | open; the cutoff table below 264 and the release times within an octave are now measured |
| D4 | Velocity: the pads convert with sensitivity 0; the reference plays velocity 30 0.4 dB softer relative to 100 | 0.4 dB | open (converter or law) |
| D5 | A segment's end waits for the next control step | at most 15 frames | by design |
| D6 | Upward transposition | the cutoff follows the step (f); the reference's slope is gentler than the kernel's (fit rms 2.2-3.5 dB at 1-5 semitones) | open (shape) |
| D7 | Passband at the root | - | resolved: flat as the reference (f); below the root (0.95 and down) the 0.9 kernel stays, the reference not measured there |
| D8 | Voice end | - | resolved: -96.45 dB, measured (e) |
| D10 | Part Velocity Sensitivity Depth / Offset | the probe piano's offset: +1.51 dB at velocity 80 on the reference | not built here, not carried by the converter |
| D11 | Probe sound C, the bass (two layers an octave apart): bands at 62.5-125 Hz up to 4.1 dB, release up to 0.44 s faster | (g) | open |
| D12 | The release after the sustain pedal goes up | - | resolved: key-off elements start at the pedal's release (g) |
| D13 | A half damper element under the pedal releases only from its sustain | inferred from two cases (g) | unverified beyond them |
| D14 | Key-off elements release 2.0 s after their start whatever their settings | the fork's K42, measured once | as the fork |
| D9 | 88.2 kHz and rates above 132.3 kHz play the plain 44.1 kHz filter sections | response scaled by the rate | not covered |

Analysis scripts and renders: the measuring machine's scratch area (`acc.py`, `acc2.py`,
`refcmp.py`); not in this repository.
