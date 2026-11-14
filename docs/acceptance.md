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
reference was measured in round 3 instead, below.)

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

Below the root (round 3, run pcm-round3-2026-11-14, takes `q_bend*`): the root-48 cycle at note 48 bent
down by the pitch bend (range -1 to -12; a coarse tune would pick another sample: the reference selects
by key plus coarse tune). The reference's response is the same at 1, 5, 7 and 12 semitones down: 0.3 /
0.5 / 0.7 / 0.8 / 0.85 / 0.9 / 0.95 of the source's Nyquist about -0.4 / -0.2 / -1.2 / -2.2 / -2.6 / -3.9 /
-5.0 dB, off-harmonic power -72 to -83 dB. Of the bank's kernels 0.95 fits best (rms 1.15 dB, at most
2.27 dB, at 0.95 of Nyquist 1.3-2.2 dB below); 0.9, the engine's before, was off by up to 7.0 dB, and 1.0
by up to 3.0. The engine now takes 0.95 below 0.95 of the root's step.

| check | tolerance | result |
|---|---|---|
| root, 0.5-0.95 of the source's Nyquist, 44.1 kHz | ±0.5 dB of the reference | pass (largest 0.04 dB) |
| above the root, aliases | at or below the reference's | pass (-81 to -89 dB against -69 to -75) |
| below the root, response | reported (fit rms above) | 0.95: rms 1.15 dB, at most 2.27 dB |
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
  with the filter Thru the EQ is bypassed (all types equal). P.EQ and 2-band with gain: (k).
- Half damper (a sine, release 40): with the switch on and the pedal down, a key-up enters the
  release at the half damper time (103: 4.07 dB/s, 127: 0.61, 64: 121 dB/s, the release table's rates);
  at pedal 64 the index in between in proportion (40 + 63 x 64 / 127 = 72: 60.6 dB/s, table 60.5); pedal
  up: the element's own release; switch off: the pedal holds the note. On the piano, whose envelope is
  still in its decays at the key-up, the note plays on under the pedal exactly as held: the engine
  starts the half damper's release only at the sustain. Round 3 takes `w_*` (run pcm-round3-2026-11-14) confirm
  it and add: a sine still decaying toward a sustain of 80 at the key-up decays on under the pedal as held; half
  pedal before the key-up releases at the index in between (60 dB/s at 64); the pedal moved during the release
  (127, 64, 127, 32, 0) changes the rate each time; the pedal pressed only after the key-up (during the
  element's own release) slows the release to the half damper time. A release entered again keeps the new
  index's rate from where it stands (4.25 dB/s from -35 dB at time 103; the table 4.07), it does not take the
  full-scale segment's time. Engine against reference on the four takes: every rate within 0.1 dB per 0.2 s,
  the transitions within the recorder's timing (50 ms).
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
| bass 35 | +0.20 | 0.79 | 2.38, 2.38 | 2.70, 2.70 | 1.41, 1.41 |
| bass 37 | +0.21 | 0.74 | 2.29, 2.29 | 2.98, 2.95 | 1.48, 1.45 |
| bass 40 | +0.17 | 0.73 | 2.43, 2.31 | 2.95, 2.94 | 1.75, 1.58 |

| check | tolerance | result |
|---|---|---|
| level | ±1 dB | pass: piano -0.12 to -0.43, bass +0.17 to +0.21 |
| octave bands within 40 dB of the loudest | ±2 dB | pass: piano largest 0.52, bass 0.79 |
| times to -10 and -20 dB | ±10 % | pass: piano largest 6 %, bass 5 % |
| release to -30 dB | ±0.1 s or ±10 % | pass: piano largest 0.05 s; bass 40 at the limit (1.58 against 1.75, 9.7 %; a new take of the same note gives 1.58 on the reference, its note-off is the recorder's); after the pedal 0.43 against 0.42 |

The bass's two elements play the same stereo pad samples, the second at coarse tune -12. The bass
rows above are rendered with the sound text corrected for what round 3 measured (D11): the reference
picks an element's sample by the key plus its coarse tune, not by the key. Recorded by layer (round 3,
run pcm-round3-2026-11-14, takes `m_bas_*`): the reference's whole part is the sum of its layers
(gains 1.001 and 1.000, residual -37 dB); the first layer matched the engine already; the second,
played from the key's own region transposed an octave down, was +10.6 dB at 62.5 Hz and -10 dB at 8
kHz off; from the region of the key 12 below (root 30 for key 40, not root 42) every band after 0.5 s
is within 0.2 dB and its release to -30 dB is 1.58 s on both. A sine probe two octaves apart and a saw
an octave below its element's region confirm the selection (takes `m_tune_c12`, `m_down*`). In the
first 0.5 s the bands above 2 kHz are 0.6-1.6 dB low on both layers: open (with D3).

## (h) The part's velocity sensitivity against the reference

Recordings `m_vs_*` and `n_vs_*` (run pcm-round3-2026-11-14): a sine at element velocity sensitivity
+63 (a gate: only the top of the range sounds) and +32 (factor 1: level 40 log10 of the 0-2047
velocity), 14 and 18 depth / offset pairs, each at 7 or 11 velocities; `n_vlim_*`: two elements split
at velocity 64 and panned apart; `n_fvel_*`: a saw through type 0 at cutoff velocity sensitivity +63.
Each note's level is inverted through the element law to the velocity it played.

- Law (`laws::partVelocity2048`): the note's velocity on the element law's 0-2047 scale (16 a step to
  64, then 1023 / 63 a step), times the depth on that scale over 1024, plus twice the offset on that
  scale less 1024, truncated, clamped to 0-2047. Depth 64 and offset 64 leave the velocity as played;
  depth 0 leaves the offset alone. Fit over 150 notes at +32: rms 0.057 dB, at most 0.16 dB; the 98
  gate notes at +63 agree at every velocity. A form centred on 64 ((v - 64) depth / 64 + offset) is off
  by up to 64 steps.
- The moved velocity is the note's for everything: the element velocity limits (offset 96 plays every
  note on the upper element; 1023 on the scale counts as velocity 63, so the velocity is the highest
  step at or below), the level, and the cutoff velocity sensitivity (offset 96 opens the filter from
  velocity 64).
- Engine against reference, note by note: every level take within 0.12 dB; element selection equal on
  all 44 notes of the split takes; probe sound B (offset 69 against 64 at key 60, velocity 80): the
  reference +1.51 dB, the engine +1.50 dB.

Found on the way: the cutoff velocity sensitivity law (the fork's) leaves the saw at +63 23-26 dB too
quiet at velocities 20-40 (D15), and an element after the first plays 5.6 dB below the first with the
filter thru (D16, resolved in (i)).

| check | tolerance | result |
|---|---|---|
| level against the reference, 18 depth / offset pairs x 11 velocities | ±0.25 dB | pass (largest 0.12 dB) |
| element velocity limits under depth / offset | equal | pass (44 of 44 notes) |
| probe sound B, offset 69 against 64 | ±0.25 dB | pass (+1.50 against +1.51) |

## (i) Elements after the first, with the filter thru

Recordings `m_ph*`, `o_*`, `p_*` (run pcm-round3-2026-11-14): sines, saws and white noise on one to four
identical elements, element 2 alone (element 1 off or at level 0), element 8 alone, filter gains 128-255,
the filter thru and type 0 wide open. Two identical elements sum to 1.52 times one, every note alike: the
second is a scaled copy of the first (0.5227, no delay; residual -110 dB).

- With the filter thru, elements 2-8 play 5.63 dB below element 1, whatever the filter gain (which a thru
  filter ignores for every element); with a filter, elements 1 and 2 are alike. The engine takes 15 steps of
  the 0.375 dB grid, -5.625 dB, for elements 2-8 with the filter thru, by the element's number in the part
  (`element_number`, which the converter writes: a converter drops elements that are off, so the order
  of the sound's elements is not the number).
- Engine against reference: every take within 0.03 dB (19 takes), and the split takes of (h) within 0.10 dB.
- Found on the way: with type 0 wide open at the default filter gain (230) the engine is 0.21 dB above the
  reference (the fork's gain law there; D17).

| check | tolerance | result |
|---|---|---|
| level, one to four elements, element 2 and 8 alone, thru and type 0 | ±0.25 dB | pass (largest 0.03 dB with the filter thru; 0.21 dB with type 0) |

Element connection (run pcm-round3-2026-11-14, takes `u_conn*`): a saw through type 0 wide open with its
element connection at thru, insertion A and insertion B, both insertion switches off. All three play at
the same level (-20.364, -20.364, -20.369 dB; the waveforms equal after the recorder's note timing,
residual -66 dB), so `element_connection` changes only the output port (with `split_outputs`); the
engine within 0.2 dB of the reference on all three (the type 0 gain of D17).

## (j) Cutoff velocity sensitivity against the reference

Recordings `t_cv_*` (run pcm-round3-2026-11-14): a saw at key 36 through type 0, resonance 0, key follow 0,
envelope depth 0, at base cutoffs 150-700 and sensitivities -64 to +63 (11 pairs), 11 velocities each. Each
note's octave bands (0.15-0.45 s after its note-on) are matched against the engine's at every base cutoff
(sensitivity 0) to give the cutoff index the note played.

- The fork's law holds wherever the moved index stays at or above 0: the index moves by 0.115 x
  sensitivity x (velocity - 115) (+63 counting as 64, negative sensitivities mirroring the velocity as
  128 - v). Per unit of sensitivity the reference moves -12.95, -10.9, -8.65, -5.83, -4.06, -1.8, 0 and
  +1.26 to +1.58 at velocities 1, 20, 40, 64, 80, 100, 115 and 127 (the law: -13.1, -10.9, -8.6, -5.9, -4.0,
  -1.7, 0, +1.4), the same for every sensitivity and base cutoff.
- Engine against reference, note by note: every note within 40 dB of its take's loudest within 2.1 dB in
  level and 2.5 dB in octave bands (76 notes). Where base plus shift falls below index 0 the reference
  keeps going darker but not as the engine's table (which stops at index 0); those notes are 40-140 dB
  below the take's loudest (D15).

## (k) Element EQ against the reference

Recordings `s_*` (run pcm-round3-2026-11-14), `x_*` and `z_*` (run pcm-round4-2026-11-14): white noise through type 0
wide open, each EQ setting against the EQ thru: the peak (P.EQ) at frequency raw 0-511 (83-251 every 8; 187-255 every 4 at
Q raw 0, 4, 8, 12, 16, 20, 25, 31), Q 0-31, gains 32-96; the 2-band's low shelf at raw 0-511 (46-182 every 8) and high
shelf at raw 0-511 (131-241 every 8), gains 32-96, both bands together, and Q with the 2-band. Each take's response
(1/24-octave bands, 80 Hz-18 kHz) is fitted with a model; every one of the 191 takes within 0.05 dB rms of its model.

- Gains: (raw - 64) x 0.375 dB in both bands, boost and cut symmetric in dB.
- The peak is a biquad whose poles are those of a state-variable section at f = 2 sin(pi F / 44100), damping d = 1 / Q:
  a1 = f^2 + f d - 2, a2 = 1 - f d; the zeros keep the response at 1 at DC and at the Nyquist frequency with the gain
  at the centre (a boost: b = (1 + f d (G - 1) / 2, a1, 1 - f d (G + 1) / 2)); a cut is the boost inverted. With this
  model one frequency per raw value and one Q per raw Q fit every take (a constant-Q biquad needs the centre and Q to
  move with each other, by up to 19 %). Q = 0.7 x 2^(raw / 8): measured 0.701, 0.992, 1.400, 1.979, 2.795, 3.945, 6.10,
  10.22 at raw 0, 4, 8, 12, 16, 20, 25, 31 (the law 0.700, 0.990, 1.400, 1.980, 2.800, 3.960, 6.10, 10.27).
- One frequency table serves the peak and both shelves (the peak and the high shelf agree within 0.02 % at every raw value
  both were measured at); 14 Hz at raw 0 to 19.8 kHz at 255, irregular within its octaves (20-34 raw steps an octave);
  the raw value's low 8 bits index it (300 plays as 44). The high shelf's frequency stays within raw 131-241.
- The shelves are first order through the bilinear transform at K = 1.006 x 2 sin(pi F / 44100), the boost's pole and the
  cut's zero at the corner; Q does not change them.
- Near the top the peak's frequency is replaced by a lower one of its Q's own once the table passes a threshold that also
  depends on Q (Q raw 0: 3.74 kHz from raw 211 on; 8: 8.67 kHz from 239; 31: 14.4 kHz at 255); between the measured Q values
  both are interpolated (inferred).
- Engine against reference (the same takes, the engine's response against its own EQ thru): largest difference in 1/6-octave
  bands 0.24 dB (round 3 takes), 0.46 dB (round 4 frequency takes; raw 30 at 62 Hz, the analysis's lowest band), 0.37 dB
  (the top-of-range takes; Q raw 31 at 10 kHz); mean rms 0.03 dB. At other host rates the bands keep their frequencies in
  Hz (the coefficients at the host's rate; not compared with the reference).

| check | tolerance | result |
|---|---|---|
| response, every EQ take, 1/6-octave bands 62.5 Hz-16 kHz | ±0.5 dB | pass (largest 0.46 dB, 224 takes) |

## (l) Controller boxes against the reference

Recordings `v_*` (run pcm-round3-2026-11-14), `y_*` and `l_*` (run pcm-round4-2026-11-14): one or two boxes from the mod
wheel (and foot controller 1, breath, foot controller 2, the pitch bend) to the fine and coarse tune (sines, read in cents),
the cutoff, resonance and high-pass cutoff (saws and noise, read as the static setting whose octave bands match) and the
element level (sines at element levels 127, 100, 64, 32), the source stepped through 6 or 17 values while a note holds.

- A controller value v (0-127) enters a box as c = 8 v up to 64, then 512 + (v - 64) x 512 / 63 (1024 at 127): the wheel
  at 64 is 512, not 516 (the coarse tune at ratio +63: 1181 cents, not 1190). Foot controller 1 (CC 11) and breath (CC 2)
  map alike; foot controller 2 stays at the top (CC 4 does not move it); the pitch bend enters as its 14-bit value / 16
  (and bends the pitch as well). Every one of 68 fine-tune steps within 0.1 cent of the law.
- A box's output is (curve(c) + o - 1024) x ratio / 8 within +-8192 (o 1024 unipolar, 512 bipolar; ratio +127 plays as
  +128). Each box moves its destination by trunc(output x span / 8192), toward zero, before the boxes on one
  destination are summed (two boxes of +64: 24 cents at wheel 16, where the sum truncated once would give 25); the sum
  stays within +-span (two +64 boxes stop at 100 cents from wheel 64 on).
- Spans: fine tune 100 cents and coarse tune 2400 cents (whole cents, not tune steps); cutoff 1023 index steps (a box
  lands on the static index: base 300 + 255 at ratio +32, wheel 64, within the analysis's 3 steps); resonance 128 (within
  one step); element level 128 steps of the level law (16 boxes at four levels: rms 0.32 dB, the largest errors below
  -30 dB); high-pass cutoff 1023 (one take within 2 steps at 6080; inferred from the cutoff).
- Curves of type 0: parameter 5 is linear; 0, 2 and 8 measured at 17 points (whole cents: +-5 of 1024); 6, 9, 10 and two
  of type 2 are the analog engine's measurements of the same boxes, not repeated here.
- Not measured or not built: the boxes on the envelope times and depths (the filter envelope's depth did not move while
  a note held: read at note-on, not verified), the element LFO, pan, the part LFO; channel pressure's map. The knobs,
  ribbon and switches: (m).
- Engine: core test `controller_boxes` (the fine tune at five wheel values, the sum and clamp, a bipolar box, coarse,
  the element's switch, a curve, the bend as a source; cutoff and level boxes equal to their static settings) and the
  ABI test `box_sources_drive_their_boxes`. Found on the way: a box made the reference's thru-filter sine probes of one
  recording batch play 6-40 dB quieter; the same jobs recorded again played at the normal level (a recorder-state
  artefact; only levels within one take are used).

## (m) The part's own box sources against the reference

Recordings `a_knob*`, `a_ribbon`, `a_sw1`, `a_sw2` (run pcm-round5-2026-11-14): one box on the fine tune (ratio +63,
read in cents against the take with the source at 0), the source a knob's stored part value (knob 1 at 0, 64, 256, 511,
512, 768, 1000, 1023; knob 8 at 768), the ribbon by its controller (CC 16) at 17 values, switches 1 and 2 by theirs
(CC 86, 87) at 0, 1, 63, 64, 65, 127.

- A stored knob value K enters the box as c = K: every one within 0.05 cent of trunc(K x 63 / 8 x 100 / 8192) (512 and
  511 both 49 cents, 1023 98). Knob 8 plays as knob 1. The knob's own controller (CC 17) did not move it in these
  probes.
- The ribbon by CC 16 follows the controllers' map (8 v up to 64, then 512 / 63 a step) at all 17 values within 0.1 cent.
- The switches by CC 86 / 87 also follow that map (63 plays 48 cents, 64 and 65 49): a continuous source; the switch
  itself sends 0 and 127, so the parameter is off (0) or on (1024).
- The filter envelope's depth as a box destination (`a_fegd_*`: a saw through type 0 at cutoff 300, the envelope's levels
  at the top, a note per wheel value through a ratio +32 box): the wheel at 64 and at 127 sound as the static depths
  +16 and +32 within 0.01 dB in every octave band, so its span is 64 raw steps; a held note does not follow the wheel
  (round 3, `v_fegd_*`): the depth is read when the note starts.
- The element LFO's depth through boxes (`b_pmd_*`, `b_amd_*`, `b_pmdw_*`, `a_pmd_*`, `a_amd_*`): a box from knob 1 on the element LFO's
  pitch and amplitude depth at ratios 8, 16, 32, 63 and -32, stored values 16-1023, and the mod wheel through pitch-depth boxes of 16, 32, 63 at
  wheel 32, 64, 96, 127. The depth is one function of the box output o = c x ratio / 8, whatever the ratio or the source
  (wheel and knob at the same o within 0.3 %): peak-to-peak pitch 0 up to o = 32, 4.61 cents from 64, 9.22 at 1024, then
  doubling about every 1024 to 1047 cents at 8056; amplitude 2.56 dB at 1024, 5.14 at 2048, 12.13 at 4092, 82.5 at 8056.
  A negative ratio gives no pitch modulation. The reference's depths come in steps (0.92 cent). The fork's law (K19:
  2^((2 ratio + cc - 253) / 16)) meets this only at large depths (6.45 cents measured against 0.31 at wheel 32, ratio
  16), and K30's amplitude points sit 0.3 dB below these at large depths and far below at small ones; both replaced. Hold-
  outs (batch A, ratio 32, knob 256 / 512 / 768 / 1023): 9.30 / 18.56 / 36.17 / 72.39 cents against the knots' 9.22 /
  18.44 / 35.97 / 71.95 (within 0.9 %); amplitude 5.14 and 12.14 dB against 5.14 and 12.13. Between the knots
  interpolated (unverified). Negative amplitude ratios: not measured (reported).
- Per-box truncation, checked again on static knob values (`b_tr_*`): two, three or four boxes on the fine tune, each
  a fraction of a cent (knob 512 or 1023, ratio +1 or +2). Three boxes of 0.781 cent play 0.00 cents (summed then
  truncated: 2), three of 1.561 play 2.96 (4), two of 1.561 play 2.04 (3), three of 1.562 play 2.96 (4); single boxes
  of 2.344 and 1.561 play 2.04 and 1.02. On this engine's fine tune each box is truncated toward zero before the sum
  (as the round-4 takes `y_fine_sum`: 17 of 17 steps per box, the sum rule misses 6). The analog engine finds the sum
  truncated once on its cutoff. Checked on this engine's other destinations (`e_*`, `f_*`, `g_*`, knob boxes of +-1 and
  +-2, each a fraction of a step): the element level (11 settings against static levels 84-100: per box truncated, all
  11; summed and truncated 8, floored or rounded 7), the coarse tune (5 settings: 111 / 74 / 37 / -111 / 63 cents, the
  sum rules give 112, 75, -112, 65), the cutoff (three +1 boxes from knob 700, 10.94 steps each: the static index 448,
  per box, not 450; three -1: 448, not 446; band-shape distance 0.004 against 0.07). So on this engine every destination
  measured truncates each box before the sum. Two things found on the way: the cutoff's span is 1024, not 1023 (the
  wheel at 127 through +8 and at 64 through one or three +1 boxes land on 448, not 447), and a knob stored at 1023 plays
  as 1024 (a coarse box of +64 at knob 1000 / 1022 / 1023: 2343 / 2395 / 2400 cents), as a ratio of +127 plays +128.
  The analog engine's three +1 boxes on its cutoff (647, not 645) would fit per-box truncation too if its source there
  were 1024 rather than 1023 and its span 1024; that is for its own measurement to settle.
- The amplitude envelope's attack and release boxes (`b_att_*`, `b_rel_*`, `c_*_move*`): span 128 index steps; release
  slopes and attack times equal the static index's at 12 of 14 settings (the two others are analysis failures on the
  fastest releases, 20 ms to -50 dB). The release takes the box at the note-off (the wheel moved from 0 to 127 during
  the hold plays index 72, from 127 to 0 index 40); a wheel moved during the release changes its rate on the reference
  (1328 dB/s against 1871 for the new index from the start), which the engine does not follow (D18). The attack is read
  at note-on (inferred: the probe could not tell).
- The controllers' map above 64 takes whole steps, truncated: 512 + (v - 64) x 512 / 63 rounded down (round 6, batch N: a
  box of +63 on the part LFO's depth, 128 steps a full box, plays depths 78 / 94 / 110 at wheel 80 / 96 / 112, where the exact
  quotient gives 79 / 95 / 111; 45 wheel steps over five boxes all agree with the truncated map).
- Engine: the knobs, ribbon and switches are CLAP parameters (`docs/parameters.md`); ABI test
  `knobs_ribbon_and_switches_drive_their_boxes`; a destination's modulation sounds as the linear box it stands for
  (`modulation_sounds_as_the_equivalent_linear_box`, sample for sample).

## (n) High-pass key follow against the reference

Recordings `a_hkf*` (run pcm-round5-2026-11-14): white noise through type 4 (low-pass wide open, high-pass at 400), the
high-pass key follow at 0, 32, 64, 96 and 127, a note each at keys 24, 48, 60, 72 and 96; half-octave bands of each
note against the same note at key follow 64, the reference against the engine's render of the same probe.

- The high-pass moves by the cutoff key follow's law, (raw - 64) x 3.125 cents a key about key 60: rms 0.60 dB over 262
  bands within 40 dB of the passband (worst 4.6 dB, a noise band), key follow 0 to 127 alike. Key follow 64 is none.
- Engine: `fil_hpf_keyfollow`; core test `filter_types_4_5` (key follow 0 at key 84 lets a 110 Hz tone through a
  440 Hz high-pass; without the law it falls 24 dB).

## (o) Filter cutoff scaling and pitch key follow against the reference

Cutoff scaling (`a_cs_*`, run pcm-round5-2026-11-14): white noise through type 0 at cutoff 512 (key follow 0), break
points and offsets set three ways (all offsets +64; -64 / +64 / -32 / +127 at 36 / 48 / 60 / 72; +100 / -100 / 0 / +50 at
20 / 50 / 70 / 100) and all at -128, twelve keys each; each note matched against the engine's static cutoffs in 2-step
increments by its third-octave band shape.

- The cutoff index moves by 3.125 index steps an offset unit, the offset linear in key between the break points and flat
  beyond: rms 8.4 index steps over 28 notes (worst 22; notes past index 900, where the table tops out, and keys 12, 24
  and 108, where the transposed noise is too narrow to match, left out). A law in cents (37 cents a unit) fits no better
  (rms 78 against 94 cents). How scaling combines with boxes on the cutoff is not measured (the engine adds the scaling
  to the base the boxes move).

Pitch key follow (`i_pkf*`, `j_pkf*`, `a_pkf*`): a sine at keys 48 and 72 about centre 60 at every raw value 0-127, and
five keys about centres 48, 60 and 72 at four values.

- The centre note keeps its pitch; every key from it moves by a fixed number of cents a key: whole cents at every raw
  value (within 0.013), 0 at 64, 100 at 96, 200 at 127 and -200 at 0, in runs of 1 and 5 cents a step (64-74: 1, 74-90:
  5, ...); up and down alike within 0.022 cent. Centres 48 and 72: within 1 cent at 3,840 cents. The table is the
  engine's (`laws::pitchKeyFollowCents`). With an element's coarse tune folded into the key map by the converter, the
  centre is taken against the moved root (not measured together).

## (p) The part LFO against the reference

Recordings (runs pcm-round5-2026-11-14, batch D before its resonance take, and pcm-round6-2026-11-14, batch K): a sine at
key 60 (velocity 100, filter thru) with the part LFO on its pitch (destination 65), level (64) or pan (68); white noise
through type 0 at cutoff 512 for the cutoff (66). The pitch tracked by zero crossings, the level and pan by RMS over
each half of a square's cycle, the cutoff by third-octave band shapes matched against the engine's static cutoffs. Batch
D's takes after its resonance take play loud and level-modulated (a recorder fault) and are not used; batch K re-recorded
them.

| what | measured | engine | tolerance | points |
|---|---|---|---|---|
| speed (Hz) | 2^(-4.26258 + 0.124591 s) at 2-63 (saw resets over 6-70 s; triangle zero crossings); speed 1 a 565 s cycle; 0 none | the same | within 0.7 % | 19 speeds |
| pitch depth (cents of the swing) | whole cents every 4 steps: 6 at 4, 23 at 32, 112 at 64, 531 at 96, 2400 at 127 | the table, linear between knots (between them unverified) | within 0.1 cent at the knots | 33 depths |
| waves | 13 shapes at speed 32: triangle from zero rising, triangle+ 0..1, saw up 0..1, saw down 0..-1, squares of duty 1/4, 1/3, 1/2, 2/3, 3/4, trapezoid (twice the triangle, clipped), S/H 1 uniform within +-1, S/H 2 zero on 28 of 63 cycles, else uniform | the same; S/H by distribution (the sequences differ) | engine against reference pitch: squares rms 0.15 cents, saws 0.6, triangle+ 1.2, triangle and trapezoid 2.0-2.2 (the recorder's onset scatter, about 5 ms, on slopes of up to 740 cents a second) | 13 takes, 213-238 points each; S/H 63 cycles each |
| start phase | 0, 90, 120, 180, 240, 270 degrees: the wave from that phase | the same | rms 2.8-4.4 cents (speed 40) | 6 takes, 108 points each |
| start | 6-14 ms after the note | 10 ms (`laws::kPartLfoLag`) | the scatter | 4 takes |
| envelope | delay, fade in (dB line from -26 dB), hold, fade out (dB line to -26 dB): 0.017476 x 2^(raw / 13.635) s | the same | rms 0.3-1.5 dB of the swing | 15 takes |
| user wave | steps x A / 64 (+63 as +64); 2, 3, 4, 6, 8, 12, 16 steps; slope up / down / both: a straight line to the next step where it rises / falls / always, else held | the same | rms 0.03 cents (6 steps), 0.7-2.3 (slopes) | 10 takes |
| key-on reset | off: one oscillator, started by the first note, not reset later; each on: every note its own from its start (a second note's oscillator from its own start while the first runs on); first on: one, reset at a note while no other key is down | the same | phases within the tracker's resolution | 3 takes, two notes tracked apart |
| box on slot 1's depth (57) | trunc(output x 128 / 8192) depth steps, within 0-127 | the same | every step the law's depth | 45 wheel steps |
| slots | slots 2 and 3 as slot 1; two slots on the pitch add (64 + 64: 224.5 cents; 32 + 96: 555.6 against 554.5); the element's switch for a slot keeps it out | the same | within 1.3 cents | 5 takes |
| element ratio | no effect on the pitch (ratios 0-96, slots 1, 2) or the level (0); the cutoff still at 0 and at about half its swing at 64 | the cutoff's slot scaled by ratio / 127, others unscaled; the pan's and resonance's reported | - | 8 takes |
| element phase offset | no effect: offsets 1-5 on one element, 120 and 180 degrees on a second element two octaves up (each tracked apart), resets each on and first on | no effect; with reset off and more than one element reported | - | 8 takes |
| level | the low half of a square: depth level steps down from level 127 (the top half held at 127) | levelGain(127 - depth) | within 0.17 dB, rms 0.07 | 15 depths |
| pan | raw pan steps added, saturating: 34, 50, 57, 61, 62 at depths 16-80 (the balance law's grid), the far side silent from 96 | the table, linear between 16-step knots | within 0.22 dB | 5 depths and 3 silent |
| cutoff | 8 index steps a depth step | the same | the band-shape fits of engine and reference within 4 index steps | 5 depths, 10 plateaus |

The envelope (batch M, `m_delay*`, `m_fade*`, `m_hold*`, `m_fout*`, `m_dfh`: a square at speed 56 on the pitch, the
swing's size over time): the delay, fade in, hold and fade out take one time law, 0.017476 x 2^(raw / 13.635) s (0.20 s at
48, 0.45 at 64, 2.3 at 96, 5.2 at 112; within 4 % at 40-120 over 11 settings, within 0.02 s below); the fade in rises
along a straight line in dB from -26 dB to full, the fade out falls along one to -26 dB and then stops; a hold of 127 holds
for ever; they follow each other (delay, fade in, hold, fade out: `m_dfh`). Engine against reference over 15 takes: rms
0.3-1.5 dB of the swing's size (floored at -30 dB), the worst points at the segments' edges.

A recorder hazard found on the way: after a take with the part LFO on the cutoff, the reference stays changed for the rest
of the session, across loads (the next sines play 6-11 dB quieter, the pan's swing roughly halves, a level slot stops; the
pitch is not affected); after a take with a high resonance the next takes play loud and level-modulated. Every level, pan
and cutoff figure above comes from takes recorded before either in their session; later takes were re-recorded or left
out.

Not built (reported): tempo sync, the random speed, the resonance destination (its takes clipped), destinations other than
the four.

A box on slot 1's depth (destination 57; batch N, `n_b57_*`: the wheel stepped 0-127 through ratios +32, +63, +127 at depth
0, +63 at depth 32 and -32 at depth 96, a square on the pitch): the depth moves by trunc(output x 128 / 8192) raw steps
within 0-127, read continuously; every one of 45 wheel steps plays the law's depth (swings within 0.5 %, and 0.1 cent at
the small ones). Slots 2 and 3's depth destinations (58, 59) are not measured (reported).

- Engine: `plfo_*` (part), `plfo_box_sw*`, `plfo_ratio*`, `plfo_phase_offset` (element); core test `part_lfo` (the pitch
  square's plateaus, the slot switch, level and pan plateaus, each-on and first-on phases); the fuzz sets every part
  LFO setting at random.

## (q) Element selection: the fork's laws

Not re-measured here: the sample-engine fork measured element selection on the reference (its K51; batches of sine
elements at distinct levels, a note at a time) and this engine carries its rules. Cycle elements take turns per group in
element order, one position per group shared by every key; a note no cycle element of the group covers starts the group
over. Random elements: one 8-bit draw per group, each covering element alike; the draw of 255 picks none unless the part's
first element is among them, then that one. The fork's hold-outs: 120 notes of two random elements, the reference 61 / 59
against the fork's 62 / 58; 400 notes, 219 / 200 against 211 / 187; with a velocity-limited third element at velocity
120, 59 / 42 / 49 against 49 / 48 / 53. The sequences differ by design: the reference's generator is shared by the whole
instrument and runs on across loads; here each sound slot has its own, started again at a load and a reset. The switch
modes follow the part's switches 1 and 2 at note-on (on from half way, the fork's 64 of 127). Legato elements play only in
a mono part, which this engine does not build (reported).

Key-on delay: the fork's 28 knots (K52: hold-outs at 10, 36, 60, 100, 108 within 0.2, 0.4, 9.8, 1.2 and 5.9 ms). Measured
here (run pcm-round6-2026-11-14, `l_kod_*`): time length 40 on a sine, the note held to 2.1 s starts the element 1.40-1.45
s after the note (50 ms analysis bins; the knot is 1.426 s); the note released at 0.5 s, before the delay, starts nothing
(silent over 2.7 s). The engine starts a held-back layer on its own frame and drops it when the note ends first (core test
`key_on_delay`: the onset frame in 128- and 37-frame blocks, a note-off and a choke before the delay leave no voice). A
note length at the tempo (`keyon_delay_note`) is reported.

- Engine: `xa_control` 3-7 and 1 in a poly part, `element_group`; core test `element_selection` (three cycle elements
  in turn beside a normal one in another group, the switch modes, 400 notes of two random elements: one a note, each
  more than 160).

## (r) The part's offsets against the reference

Recordings `l_po*` (run pcm-round6-2026-11-14; batch A's `a_po_*` played quiet and were re-recorded): white noise through
type 0 at cutoff 400 with the part's cutoff offset at -48 to +63, and at 700 with -32, 0, +32; a saw (note 36) through type
0 at resonance 40 with the resonance offset at -16 to +16. Each matched against the engine's static settings by band
shape.

| what | measured | engine | tolerance | points |
|---|---|---|---|---|
| cutoff offset | 8 index steps a unit, at both base cutoffs (400 + 8 o: 144, 272, 336, 464, 528, 656, 784, 904 read 148, 272, 336, 464, 528, 656, 784, 908; 700 - 256: 444) | `part_cutoff`, 8 a unit, within 0-1023 | within 4 index steps (-48: the noise near the bottom of the table, 40 against 16; 700 + 256: the table's top) | 13 notes |
| resonance offset | 2 steps a unit (7, 23, 38, 55, 73 against 8, 24, 40, 56, 72) | `part_resonance`, 2 a unit, within 0-127 | within 2 steps | 5 notes |

| filter envelope depth offset | 2 depth steps a unit (`n_pofeg*`: white noise through type 0 at 300 with the envelope held at its top; base depths 0, 16, 32 with offsets -32 to +32 read as the static depths 2 o away; the reference's own static depths `n_fegref*` read as themselves from -16 to +32) | `part_feg_depth`, 2 a unit, within -64..+63 | the same depth (the reading saturates at 49 near the cutoff's top) | 8 notes |

The offsets act on every element before velocity, scaling, boxes and the part LFO (assumed: the order not measured). The
amplitude envelope's offsets (attack, decay, sustain, release) are not built: the attack and sustain takes suggest 2 steps
a unit.

- Engine: `part_cutoff`, `part_resonance`, `part_feg_depth` (applied when the sound is compiled: no cost while playing); core test
  `part_offsets`.

## (s) Resonance velocity sensitivity against the reference

Recordings `a_rvs_*` (run pcm-round5-2026-11-14) and `l_rvs_*`, `l_rref*` (run pcm-round6-2026-11-14): a saw (note 36)
through type 0 at cutoff 400 at velocities 1, 20, 40, 64, 80, 100 and 127; base resonances 0, 32, 64, 96, 127 with
sensitivities -64, -48, -16, +16, +32, +48, +63 (the batch L takes at element level 80, below clipping). Each note matched
against the engine's static resonances by band shape, the engine's reading turned into the reference's by the reference's
own static resonances 0-127 every 8 (`l_rref*`: the engine reads 64 as 66, 96 as 100, 127 as 120).

| what | measured | engine | tolerance | points |
|---|---|---|---|---|
| resonance at velocity v | the element's less abs(s) x (127 - v) / 126 for s > 0, abs(s) x (v - 1) / 126 for s < 0 (full sensitivity: 64 steps across the velocity range, the base at velocity 127 or 1), within 0-127 | `laws::resonanceAtVelocity` (rounded; +63 as +64), at note-on | rms 0.58 steps, at most 1.7 | 84 notes (12 settings x 7) |

Pitch velocity sensitivity (`a_pvs*`, `l_pvs*`): a sine at key 60, velocities as above, sensitivities -64, -48, -32, -16,
-1, +1, +16, +32, +48, +63: every note moves by v x s x 1200 / 8192 cents (+63 as +64: an octave at full velocity and
sensitivity, nothing at velocity 0): rms 0.29 cents, at most 0.7, over 70 notes (each take's own offset, about 1-2 cents,
removed). Engine: `pitch_velsens` (`laws::pitchVelocityCents`, at note-on); core test `pitch_velocity`.

Random pitch depth (`l_rnd*`): 40 notes of a sine at each of depths 16, 32, 64 and 127. Every pitch lies on an 18.75-cent
grid (1200 / 64); the extremes are +-3, 7, 15 and 31 steps, (depth - 1) / 4; the spread is that of a uniform draw
(standard deviations 31, 80, 153, 337 cents against 37, 75, 162, 336 expected from 40 draws). The engine draws each note's
step from its own generator (the reference's sequence is not reproduced; started again at a reset). Engine:
`pitch_random`; core test `random_pitch`.

- Engine: `fil_res_velsens`; core test `resonance_velocity` (the law's values; a step through resonance 100 at the
  softest velocity rings as resonance 36).

## (t) Filter types 1, 2, 3, 6, 7 and 10 against the reference

Recordings `p_*`, `q_*`, `r_*`, `s_*`, `u_*` (run pcm-round7-2026-11-14) and the fork's runs `fl1`-`fl3`: white noise at
key 60, one element, against a thru take of the same recorder session (each session's first take: a thru take after a
filtered one replays the filtered performance, so later thru takes were not used), 1/6-octave bands from 31 Hz to 15 kHz,
bands within 50 dB of the thru level, an engine render of the same sound against the engine's thru. Resonance takes at
element level 40, a take at resonance 80 or more alone at the end of its recorder session (later takes in a session play at
wrong levels after one). "Fit": the takes the law was fitted on (the fork's runs; for type 3 also the resonance-64 takes
`u_*` and `s_t3_560_r64`); "held out": this round's other takes (cutoffs 32-1023 between the fitted ones, resonance 16-120,
widths 0-127).

| type | law | fit: rms / max dB (points) | held out: rms / max dB (points, takes) |
|---|---|---|---|
| 1 | a low-pass section at the resonance, then one at the same cutoff with damping 1 | 0.44 / 3.53 (625) | 0.43 / 3.23 (480, 14); resonance 88-120: 0.39 / 1.21 (119) |
| 2 | a low-pass section, then the one-pole low-pass at the cutoff | 0.42 / 3.57 (675) | 0.39 / 2.19 (658, 17) |
| 3 | the one-pole low-pass at the cutoff, then a low-pass section 340 cutoff indices higher (below the table's top); past it the section rises 9 cents an index while its damping stays within 0.9 of the section's stability bound | 1.01 / 17.68 (858); this round's 0.62 / 2.72 (336) | below the top 0.63 / 6.49 (454, 11); above, resonance 0: 0.43 / 1.89 (371, 7); resonant (24-127): 1.66 / 21.9 (742, 14), of them 24-32: 0.40 / 1.87 (159, 3) |
| 6 | two high-pass sections at the resonance | below resonance 88: 0.28 / 1.12 (626) | 0.30 / 1.31 (541, 14) |
| 7 | one high-pass section at the resonance | 0.33 / 3.85 (852) | 0.29 / 1.13 (774, 17) |
| 10 | a low-pass section 38.2 cents a width step above 7.5 over the cutoff, a high-pass section as far below, both damping 1 | 0.63 / 2.99 (594) | 0.62 / 2.65 (625, 14) |

- Type 3's section, fitted per take (frequency and damping free) on 13 resonant takes below the top: 340.0 cutoff indices
  above the cutoff (standard deviation 4.2 indices, 64 to 560), its damping the resonance's (within 8 %); the held-out
  takes at resonance 96 (288, 416) and 16-112 (384) sit 1.3, 5.3 and -1 indices from it. In cents the same takes scatter as
  much (3,966 +- 56). Past the top (cutoff above 572) the reference's section goes on rising (about 8.6 kHz at 592, 15 kHz at
  704) where a 44.1 kHz section with that damping would be unstable; at resonance 0 it leaves the one-pole's response.
  The engine holds the section where its damping stays within 0.9 of the stability bound (`laws::sectionTopCents`), which
  keeps it stable at every setting; the misfit left is resonance 56-127 at cutoffs 592-688 (the peak placed below the
  reference's, up to 22 dB off in the bands beside it) and the fork's takes at cutoff 512, resonance 64-127 (the reference
  332.6 indices above the cutoff there, the law 340: up to 17.7 dB beside a sharp peak).
- Key follow (+48 at keys 36 and 84), cutoff velocity sensitivity (+48 at velocities 40 and 127) and a static filter
  envelope offset (+24) on types 3 and 6: the engine's shared cutoff laws, each take against the key-60 thru at velocity 127
  (to 4 kHz at key 36, 10 kHz at key 84): rms 0.16-0.78 dB, at most 2.1, 10 takes.
- Filter gain 255 and 192 on types 0, 1, 3, 6, 7 and 10, each against the session's thru: the engine's medians within
  0.17 dB of the reference's except type 10 (+0.54 dB, its own passband offset; also +0.5 to +1.2 in two held-out takes at width 64).
- Two high-pass sections at resonance 88 and above (type 6; as type 0, the fork's K24): the reference saturates (peaks
  43-55 dB over the passband with harmonics, the stopband filled) where the engine stays linear (peaks to 62 dB at 120):
  rms 23-37 dB over those takes (register D19).
- Engine: `fil_type` 1, 2, 3, 6, 7, 10 (`filterOf`, `laws::filterOffsetCents`, `laws::sectionTopCents`); core test
  `filter_types` (each type's level at 12 tones against the sections' response computed in the test; every built type at
  cutoffs 0-1023 and resonance 0 and 127 stays finite and within the sections' peak gain).

## Known differences (this engine's register)

| # | what | size | status |
|---|---|---|---|
| D1 | Output staging | - | resolved: the reference's, measured (c) |
| D2 | Key-bank pan | - | resolved: summed with the element pan, the balance law (c); key-bank pans between the extremes not measured (no source wave tried has them) |
| D3 | The filter envelope's release starts 15-150 ms later on the reference (growing with the index) | path within 31 cents rms (d) | open; the cutoff table below 264 and the release times within an octave are now measured |
| D4 | Velocity: the pads convert with sensitivity 0; the reference plays velocity 30 0.4 dB softer relative to 100 | 0.4 dB | open (converter or law) |
| D5 | A segment's end waits for the next control step | at most 15 frames | by design |
| D6 | Upward transposition | the cutoff follows the step (f); the reference's slope is gentler than the kernel's (fit rms 2.2-3.5 dB at 1-5 semitones) | open (shape) |
| D7 | Passband at and below the root | - | resolved: flat at the root (f); below it the bank's 0.95, fitted (f, round 3: rms 1.15 dB; the reference's slope is gentler) |
| D8 | Voice end | - | resolved: -96.45 dB, measured (e) |
| D10 | Part Velocity Sensitivity Depth / Offset | - | resolved: measured and built (`vel_depth`, `vel_offset`; (h)); the converter must write them |
| D11 | Probe sound C, the bass (two layers an octave apart) | - | resolved: the converter's (an element's sample is picked by key plus coarse tune, (g)); the first 0.5 s 0.6-1.6 dB low above 2 kHz, open |
| D12 | The release after the sustain pedal goes up | - | resolved: key-off elements start at the pedal's release (g) |
| D13 | A half damper element under the pedal releases only from its sustain | - | resolved: six cases measured (g), including the pedal pressed after the key-up and moved during the release |
| D14 | Key-off elements release 2.0 s after their start whatever their settings | the fork's K42, measured once | as the fork |
| D15 | Cutoff velocity sensitivity where the moved cutoff index falls below 0 | notes 40-140 dB below the loudest differ by up to tens of dB; every note within 40 dB within 2.1 dB (j) | resolved above index 0 (the fork's law verified, j); below it not modeled |
| D16 | Elements 2-8 with the filter thru | - | resolved: 5.625 dB below element 1 by `element_number` (i); the converter must write the number |
| D17 | Filter gain 230 with type 0 wide open | the engine 0.21 dB above the reference (i) | open (the fork's law) |
| D18 | An amplitude envelope release box whose source moves during the release | the reference changes the release's rate; the engine keeps the rate taken at the note-off | open |
| D9 | 88.2 kHz and rates above 132.3 kHz play the plain 44.1 kHz filter sections | response scaled by the rate | not covered |
| D19 | Two high-pass sections (type 6) at resonance 88 and above: the reference saturates | its peaks 43-55 dB with harmonics; the engine linear, to 62 dB (t) | open (as type 0, the fork's K24) |
| D20 | Type 3's section where it passes the top of the table with resonance 56 and above, and at cutoff 512 | up to 22 dB beside the peak at cutoffs 592-688, 17.7 dB at 512 (t) | open: the reference's section above 7290 Hz is not a stable 44.1 kHz section |
| D21 | Type 10's passband | the engine 0.5-1.2 dB louder at width 64 in three takes (t) | open (the fork's law) |
| D22 | A released voice ends once its output gain is below -96 dBFS (`docs/design.md`, the quiet end) | the reference plays on to -96.45 dB below the held level; output below -96 dBFS cut | by design (approved departure) |

Analysis scripts and renders: the measuring machine's scratch area (`acc.py`, `acc2.py`,
`refcmp.py`); not in this repository.
