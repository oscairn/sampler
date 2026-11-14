# Coverage

Every element parameter of the reference's sample engine (from the research repository's
`docs/params` list, in its order) and the part parameters a sample part's voice depends on. Columns:
what the core implements and under which sound-format id; the law and where it was measured (the
sample-engine fork's known-differences register, K1-K66, in its `doc/oscairn-plan.md`); what it
was checked against here. "pads (acceptance a)" means rendered on probe sound A (a 17-region pad) and
compared with the fork's element voice (`docs/acceptance.md`); "law only" means the fork's law is
carried over and runs, but nothing here exercised it beyond the core's tests. A sound using anything
marked "no", or a value the core does not reproduce, is reported by `pcm::notCovered()` (and
`pcbench coverage`) when it changes the sound; parameters the converter folds into the region table
need no engine support.

The core test `coverage_table` holds this file to `notCovered()`: every parameter id is named in a
row; the ids of a row whose "implemented" column starts with "yes" are never reported, those of a row
starting with "no" are reported at every value but the default (any other start: partial).

## Element

| parameter | values | implemented | law (source) | verified |
|---|---|---|---|---|
| Element Switch | Off, On | yes (the converter drops elements that are off) | - | - |
| Key On Delay Tempo Sync Switch | Off, On | no (on: the converter writes the note length, `keyon_delay_note`, which is reported) | K53 in the fork | - |
| Half Damper Switch | Off, On | yes: `half_damper` (the sustain pedal: `Core::setSustain`, `keyUp`) | measured here (acceptance g) | sine probes; the probe piano under the pedal (acceptance g) |
| LFO Speed Range | Normal, Extended | reference mode: carried and ignored, as the reference ignores them (K18); the settings mode (`elfo_mode=1`) is not covered | K18, K35 | core test `element_lfo` (reference mode) |
| Key Controller Set 1 Switch | Off, On | no | - | - |
| Key Controller Set 2 Switch | Off, On | no | - | - |
| Key Controller Set 3 Switch | Off, On | no | - | - |
| Key Controller Set 4 Switch | Off, On | no | - | - |
| Wave Select | 0=Preset, 1=User, 2–17=Library1–16, 18-25=Library17–24 | yes: the region table and its sample references | converter | pads (acceptance a) |
| Element Group Number | 1–8 | yes: `element_group`, the group a cycle or random element takes turns in | the fork's (K51, measured there) | core test `element_selection` |
| Wave Number | 1 – 7620 (USR, Library: 1 – 1024) | yes: the region table and its sample references | converter | pads (acceptance a) |
| Element Pan | L63 – C – R63 | yes: `amp_pan`, summed with the key bank's pan, the balance law | K1; measured here (acceptance c) | acceptance c: pans 1-127, mono, stereo and panned key banks |
| Random Pan Depth | 0 – 127 | no | - | - |
| Alternate Pan Depth | L64 – C – R63 | no | - | - |
| Scaling Pan Depth | −64 – +63 | no | - | - |
| Element trigger mode | normal, legato, key-off, cycle, random, switch off, switch 1 on, switch 2 on | yes: `xa_control`. Normal; key off (2: from the note's end, the key's release or under the sustain pedal the pedal's (measured, acceptance g), released 2.0 s after its start); cycle (3: per group the next element in order covering the note, from where the group left off; a note no cycle element of the group covers starts it over); random (4: per group one draw among the covering elements, each alike); switch off / switch 1 on / switch 2 on (5-7: by the part's switches 1 and 2 at the note, on from half way); legato (1) never plays in a poly part (a mono part is reported) | K42 and K51 (the fork's laws, measured there; acceptance q) | core tests `key_off_elements`, `element_selection`; the probe piano's key-off element in its tails (acceptance g) |
| Note Limit Low | C-2 – G8 | yes: clipped into the region table by the converter | converter | pads (acceptance a) |
| Note Limit High | C-2 – G8 | yes: clipped into the region table by the converter | converter | pads (acceptance a) |
| Velocity Limit Low | 1 – 127 | yes: clipped into the region table by the converter | converter | pads (acceptance a) |
| Velocity Limit High | 1 – 127 | yes: clipped into the region table by the converter | converter | pads (acceptance a) |
| Velocity Cross Fade | 0 – 127 | no (not in the sound format; the converter must report a non-zero value) | K55 | the probe piano and bass use 0 |
| Key On Delay Time Length | 0 – 127 | yes: `keyon_delay`: the element starts that long after the note, on its own frame (28 knots, linear between); a note that ends first never starts it | K52 (the fork's knots, measured there; acceptance q) | core test `key_on_delay` |
| Element number (its place in the part, 1-8) | - | yes: `element_number`, written by the converter: elements 2-8 with the filter thru play 5.625 dB below element 1 | measured here (acceptance i) | sines at elements 1, 2 (with and without element 1), 8, filter gains 128-255 |
| Element Connection Switch | Thru, InsA, InsB | yes: `element_connection`: with the part's `split_outputs` the element plays on output port 1 (to insertion A) or 2 (to insertion B), else on the main port, at the thru level | measured here: sent to either insertion (both off) the element plays at the thru level (within 0.005 dB; acceptance i) | ABI test `output_ports` |
| Key On Delay Note Length | 5 – 21 (16th, 8th/3, 16th., 8th, 4th/3, 8th., 4th, 2th/3, 4th., 2nd, Whole/3, 2nd., 4thX4, 4thX5, 4thX6, 4thX7, 4thX8) | no: `keyon_delay_note` (reported) | K52-K53 in the fork | - |
| Element Level | 0 – 127 | yes: `amp_level` | K1 (0.375 dB grid) | law only (127 on the pads) |
| Level Velocity Sensitivity | −64 – +63 | yes: `amp_velsens`, `amp_veloffset`, `amp_velcurve` | K1-K4 | pads (sensitivity 0); the probe piano (+24 to +30, acceptance g); core test `level_scaling_and_velocity` (curve 0) |
| Level Velocity Offset | 0 – 127 | yes: `amp_velsens`, `amp_veloffset`, `amp_velcurve` | K1-K4 | pads (sensitivity 0); the probe piano (+24 to +30, acceptance g); core test `level_scaling_and_velocity` (curve 0) |
| Level Sensitivity Key Curve | 00 – 04 | yes: `amp_velsens`, `amp_veloffset`, `amp_velcurve` | K1-K4 | pads (sensitivity 0); the probe piano (+24 to +30, acceptance g); core test `level_scaling_and_velocity` (curve 0) |
| AEG Attack Time | 0 – 127 | yes: `ampeg_*`, the fork's segment shapes sample for sample (a segment's end waits for the next control step, at most 15 frames) | K5-K10, K44-K45, K49-K50, K66 | pads (acceptance a) (attack 30, release 127, flat decays) |
| AEG Decay 1 Time | 0 – 127 | yes: `ampeg_*`, the fork's segment shapes sample for sample (a segment's end waits for the next control step, at most 15 frames) | K5-K10, K44-K45, K49-K50, K66 | pads (acceptance a) (attack 30, release 127, flat decays) |
| AEG Decay 2 Time | 0 – 127 | yes: `ampeg_*`, the fork's segment shapes sample for sample (a segment's end waits for the next control step, at most 15 frames) | K5-K10, K44-K45, K49-K50, K66 | pads (acceptance a) (attack 30, release 127, flat decays) |
| Half Damper Time | 0 – 127 | yes: `half_damper_time`, the release index under the pedal, in proportion to the pedal | measured here (acceptance g) | 64, 103, 127 at pedal 127; 103 at pedal 64 |
| AEG Release Time | 0 – 127 | yes: `ampeg_*`, the fork's segment shapes sample for sample (a segment's end waits for the next control step, at most 15 frames) | K5-K10, K44-K45, K49-K50, K66 | pads (acceptance a) (attack 30, release 127, flat decays) |
| AEG Initial Level | 0 – 127 | yes: `ampeg_*`, the fork's segment shapes sample for sample (a segment's end waits for the next control step, at most 15 frames) | K5-K10, K44-K45, K49-K50, K66 | pads (acceptance a) (attack 30, release 127, flat decays) |
| AEG Attack Level | 0 – 127 | yes: `ampeg_*`, the fork's segment shapes sample for sample (a segment's end waits for the next control step, at most 15 frames) | K5-K10, K44-K45, K49-K50, K66 | pads (acceptance a) (attack 30, release 127, flat decays) |
| AEG Decay 1 Level | 0 – 127 | yes: `ampeg_*`, the fork's segment shapes sample for sample (a segment's end waits for the next control step, at most 15 frames) | K5-K10, K44-K45, K49-K50, K66 | pads (acceptance a) (attack 30, release 127, flat decays) |
| AEG Decay 2 Level | 0 – 127 | yes: `ampeg_*`, the fork's segment shapes sample for sample (a segment's end waits for the next control step, at most 15 frames) | K5-K10, K44-K45, K49-K50, K66 | pads (acceptance a) (attack 30, release 127, flat decays) |
| AEG Time Velocity Segment | Attack, Atk+Dcy, Decay, Atk+Rls, All | yes: `ampeg_veltime*`, `ampeg_keytime*` | K10 | core test `level_scaling_and_velocity` (time shift); pads 0 |
| AEG Time Velocity Sensitivity | −64 – +63 | yes: `ampeg_veltime*`, `ampeg_keytime*` | K10 | core test `level_scaling_and_velocity` (time shift); pads 0 |
| AEG Time Key Follow Sensitivity | −64 – +63 | yes: `ampeg_veltime*`, `ampeg_keytime*` | K10 | core test `level_scaling_and_velocity` (time shift); pads 0 |
| AEG Time Key Follow Center Note | C-2 – G8 | yes: `ampeg_veltime*`, `ampeg_keytime*` | K10 | core test `level_scaling_and_velocity` (time shift); pads 0 |
| Level Scaling Break Point 1 | C-2 – E8 | yes: `amp_scaling_*`, `amp_keyfollow` | K40 | the probe piano (offsets 0 to -10; acceptance g) |
| Level Scaling Break Point 2 | C#-2 – F8 | yes: `amp_scaling_*`, `amp_keyfollow` | K40 | law only (unused on the pads) |
| Level Scaling Break Point 3 | D-2 – F#8 | yes: `amp_scaling_*`, `amp_keyfollow` | K40 | law only (unused on the pads) |
| Level Scaling Break Point 4 | D#-2 – G8 | yes: `amp_scaling_*`, `amp_keyfollow` | K40 | law only (unused on the pads) |
| Level Scaling Offset 1 | −128 – +127 | yes: `amp_scaling_*`, `amp_keyfollow` | K40 | law only (unused on the pads) |
| Level Scaling Offset 2 | 〃 | yes: `amp_scaling_*`, `amp_keyfollow` | K40 | law only (unused on the pads) |
| Level Scaling Offset 3 | 〃 | yes: `amp_scaling_*`, `amp_keyfollow` | K40 | law only (unused on the pads) |
| Level Scaling Offset 4 | 〃 | yes: `amp_scaling_*`, `amp_keyfollow` | K40 | law only (unused on the pads) |
| Level Key Follow Sensitivity | −64 – +63 | yes: `amp_scaling_*`, `amp_keyfollow` | K40 | law only (unused on the pads) |
| AEG Time Key Follow Sensitivity Release Adjustment | 0 – 127 | yes: `ampeg_veltime*`, `ampeg_keytime*` | K10 | core test `level_scaling_and_velocity` (time shift); pads 0 |
| Coarse Tune | −48 – +48 | yes: folded into the region table by the converter, the key map moved down by the coarse tune (the reference picks the sample by key plus coarse tune) | measured here (acceptance g; exact: -12 is 0.000 cents) | the probe bass (acceptance g); sine and saw probes |
| Fine Tune | −64 – +63 | yes: folded into the region's tune by the converter: s x 100 / 64 cents, truncated toward zero (whole cents) | measured here (round 3, takes `m_fine*`, `r_fine_*`: every step, rms 0.047 cents, at most 0.18; rounding or flooring miss by a cent on half the steps) | pads (fine 0) |
| Pitch Velocity Sensitivity | −64 – +63 | yes: `pitch_velsens`: v x s x 1200 / 8192 cents at note-on (+63 as +64) | measured here (acceptance s) | 10 sensitivities at 7 velocities: rms 0.29 cents over 70 notes; core test `pitch_velocity` |
| Random Pitch Depth | 0 – 127 | yes: `pitch_random`: each note a whole number of 18.75 cents, uniform within +-(depth - 1) / 4 steps (the sequence differs from the reference's) | measured here (acceptance s) | 160 notes at 4 depths; core test `random_pitch` |
| Pitch Key Follow Sensitivity | −200% – +200% | yes: `pitch_keyfollow`, `pitch_keyfollow_center`: the centre note at its pitch, every key from it at the measured cents a key | measured here at every raw value (acceptance o): whole cents within 0.013 | core test `pitch_key_follow` |
| Pitch Key Follow Sensitivity Center Note | C-2 – G8 | yes: `pitch_keyfollow_center` (with `pitch_keyfollow`) | measured here (acceptance o): centres 48, 60, 72 within 1 cent | core test `pitch_key_follow` |
| Fine Tune Key Follow Scaling Sensitivity | −64 – +63 | no (100 cents a key) | - | - |
| PEG Hold Time | 0 – 127 | yes: `pitcheg_*`, read once a control step, the pitch ramped across it | K11-K13 | pads (levels neutral); moving pitch: core test `moving_pitch_envelope` |
| PEG Attack Time | 0 – 127 | yes: `pitcheg_*`, read once a control step, the pitch ramped across it | K11-K13 | pads (levels neutral); moving pitch: core test `moving_pitch_envelope` |
| PEG Decay 1 Time | 0 – 127 | yes: `pitcheg_*`, read once a control step, the pitch ramped across it | K11-K13 | pads (levels neutral); moving pitch: core test `moving_pitch_envelope` |
| PEG Decay 2 Time | 0 – 127 | yes: `pitcheg_*`, read once a control step, the pitch ramped across it | K11-K13 | pads (levels neutral); moving pitch: core test `moving_pitch_envelope` |
| PEG Release Time | 0 – 127 | yes: `pitcheg_*`, read once a control step, the pitch ramped across it | K11-K13 | pads (levels neutral); moving pitch: core test `moving_pitch_envelope` |
| PEG Hold Level | −128 – +127 (−4800 – +4800 [cent]) | yes: `pitcheg_*`, read once a control step, the pitch ramped across it | K11-K13 | pads (levels neutral); moving pitch: core test `moving_pitch_envelope` |
| PEG Attack Level | 〃 | yes: `pitcheg_*`, read once a control step, the pitch ramped across it | K11-K13 | pads (levels neutral); moving pitch: core test `moving_pitch_envelope` |
| PEG Decay 1 Level | 〃 | yes: `pitcheg_*`, read once a control step, the pitch ramped across it | K11-K13 | pads (levels neutral); moving pitch: core test `moving_pitch_envelope` |
| PEG Decay 2 Level | 〃 | yes: `pitcheg_*`, read once a control step, the pitch ramped across it | K11-K13 | pads (levels neutral); moving pitch: core test `moving_pitch_envelope` |
| PEG Release Level | 〃 | yes: `pitcheg_*`, read once a control step, the pitch ramped across it | K11-K13 | pads (levels neutral); moving pitch: core test `moving_pitch_envelope` |
| PEG Depth | −64 – +63 | yes: `pitcheg_*`, read once a control step, the pitch ramped across it | K11-K13 | pads (levels neutral); moving pitch: core test `moving_pitch_envelope` |
| PEG Time Velocity Sensitivity Segment | Attack, Atk+Dcy, Decay, Atk+Rls, All | yes: `pitcheg_veltime*`, `pitcheg_keytime*`, `pitcheg_veldepth*` | K13 | law only |
| PEG Time Velocity Sensitivity | −64 – +63 | yes: `pitcheg_veltime*`, `pitcheg_keytime*`, `pitcheg_veldepth*` | K13 | law only |
| PEG Depth Velocity Sensitivity | −64 – +63 | yes: `pitcheg_veltime*`, `pitcheg_keytime*`, `pitcheg_veldepth*` | K13 | law only |
| PEG Depth Velocity Sensitivity Curve | 00 – 04 | yes: `pitcheg_veltime*`, `pitcheg_keytime*`, `pitcheg_veldepth*` | K13 | law only |
| PEG Time Key Follow Sensitivity | −64 – +63 | yes: `pitcheg_veltime*`, `pitcheg_keytime*`, `pitcheg_veldepth*` | K13 | law only |
| PEG Time Key Follow Sensitivity Center Note | C-2 – G8 | yes: `pitcheg_veltime*`, `pitcheg_keytime*`, `pitcheg_veldepth*` | K13 | law only |
| Filter Type | LPF24D, LPF24A, LPF18, LPF18s, LPF12+HPF12, LPF6+HPF12, HPF24D, HPF12, BPF12D, BPFw, BPF6, BEF12, BEF6, DualLPF, DualHPF, DualBPF, DualBEF, LPF12+BPF6, Thru | `fil_type`: types 0, 4, 5 and 18 (thru); every other type is reported by notCovered() | K23 (the fork's models, BSD) | type 0: pads (acceptance a, b); 4 and 5: noise against the reference (acceptance d) |
| Filter Cutoff Frequency | 0 – 1023 | yes: `fil_cutoff`, `fil_resonance`, `fil_gain`, `fil_keyfollow` | K20-K22, K29; 0-256 measured here (acceptance d) | pads (acceptance a) (244, 6, 230, 74) |
| Filter Cutoff Velocity Sensitivity | −64 – +63 | yes: `fil_velsens` | K34, verified here (acceptance j); below cutoff index 0 not modeled (D15) | 11 base cutoff / sensitivity pairs at 11 velocities: every note within 40 dB of the loudest within 2.1 dB (acceptance j) |
| Filter Resonance/ Width | 0 – 127 | yes: `fil_cutoff`, `fil_resonance`, `fil_gain`, `fil_keyfollow` | K20-K22, K29 | pads (acceptance a) (244, 6, 230, 74) |
| Filter Resonance Velocity Sensitivity | −64 – +63 | yes: `fil_res_velsens`: the resonance less abs(s) x (127 - v) / 126 (s positive, +63 as +64) or abs(s) x (v - 1) / 126 (negative), rounded, at note-on | measured here (acceptance s) | 12 settings at 7 velocities: rms 0.58 steps over 84 notes; core test `resonance_velocity` |
| HPF Cutoff Frequency | 0 – 1023 | yes: `fil_hpf_cutoff` in types 4 and 5 (a fixed section, damping 1, no key follow) | K23 | acceptance d (0-512) |
| Distance | −128 – +127 | carried (`fil_distance`); acts only in the dual types, not covered | - | - |
| Filter Gain | 0 – 255 | yes: `fil_cutoff`, `fil_resonance`, `fil_gain`, `fil_keyfollow` | K20-K22, K29 | pads (acceptance a) (244, 6, 230, 74) |
| FEG Hold Time | 0 – 127 | yes: `fileg_*`, read at each control step's first frame (the fork's coefficient update) | K33 (the pitch envelope's law, 75 cents a step); the release time measured here (acceptance d) | pads (acceptance a, b); release 48-104 and hold 96-127 against the reference (acceptance d) |
| FEG Attack Time | 0 – 127 | yes: `fileg_*`, read at each control step's first frame (the fork's coefficient update) | K33 (the pitch envelope's law, 75 cents a step); the release time measured here (acceptance d) | pads (acceptance a, b); release 48-104 and hold 96-127 against the reference (acceptance d) |
| FEG Decay 1 Time | 0 – 127 | yes: `fileg_*`, read at each control step's first frame (the fork's coefficient update) | K33 (the pitch envelope's law, 75 cents a step); the release time measured here (acceptance d) | pads (acceptance a, b); release 48-104 and hold 96-127 against the reference (acceptance d) |
| FEG Decay 2 Time | 0 – 127 | yes: `fileg_*`, read at each control step's first frame (the fork's coefficient update) | K33 (the pitch envelope's law, 75 cents a step); the release time measured here (acceptance d) | pads (acceptance a, b); release 48-104 and hold 96-127 against the reference (acceptance d) |
| FEG Release Time | 0 – 127 | yes: `fileg_*`, read at each control step's first frame (the fork's coefficient update) | K33 (the pitch envelope's law, 75 cents a step); the release time measured here (acceptance d) | pads (acceptance a, b); release 48-104 and hold 96-127 against the reference (acceptance d) |
| FEG Hold Level | −128 – +127 (−9600 – +9600 [cent]) | yes: `fileg_*`, read at each control step's first frame (the fork's coefficient update) | K33 (the pitch envelope's law, 75 cents a step); the release time measured here (acceptance d) | pads (acceptance a, b); release 48-104 and hold 96-127 against the reference (acceptance d) |
| FEG Attack Level | 〃 | yes: `fileg_*`, read at each control step's first frame (the fork's coefficient update) | K33 (the pitch envelope's law, 75 cents a step); the release time measured here (acceptance d) | pads (acceptance a, b); release 48-104 and hold 96-127 against the reference (acceptance d) |
| FEG Decay 1 Level | 〃 | yes: `fileg_*`, read at each control step's first frame (the fork's coefficient update) | K33 (the pitch envelope's law, 75 cents a step); the release time measured here (acceptance d) | pads (acceptance a, b); release 48-104 and hold 96-127 against the reference (acceptance d) |
| FEG Decay 2 Level | 〃 | yes: `fileg_*`, read at each control step's first frame (the fork's coefficient update) | K33 (the pitch envelope's law, 75 cents a step); the release time measured here (acceptance d) | pads (acceptance a, b); release 48-104 and hold 96-127 against the reference (acceptance d) |
| FEG Release Level | 〃 | yes: `fileg_*`, read at each control step's first frame (the fork's coefficient update) | K33 (the pitch envelope's law, 75 cents a step); the release time measured here (acceptance d) | pads (acceptance a, b); release 48-104 and hold 96-127 against the reference (acceptance d) |
| FEG Depth | −64 – +63 | yes: `fileg_*`, read at each control step's first frame (the fork's coefficient update) | K33 (the pitch envelope's law, 75 cents a step); the release time measured here (acceptance d) | pads (acceptance a, b); release 48-104 and hold 96-127 against the reference (acceptance d) |
| FEG Time Velocity Sensitivity Segment | Attack, Atk+Dcy, Decay, Atk+Rls, All | yes: `fileg_veltime*`, `fileg_keytime*`, `fileg_veldepth*` | K33-K34 | pads (acceptance a) (depth velocity 39, curve 2: velocities 30, 100, 127) |
| FEG Time Velocity Sensitivity | −64 – +63 | yes: `fileg_veltime*`, `fileg_keytime*`, `fileg_veldepth*` | K33-K34 | pads (acceptance a) (depth velocity 39, curve 2: velocities 30, 100, 127) |
| FEG Depth Velocity Sensitivity | −64 – +63 | yes: `fileg_veltime*`, `fileg_keytime*`, `fileg_veldepth*` | K33-K34 | pads (acceptance a) (depth velocity 39, curve 2: velocities 30, 100, 127) |
| FEG Depth Velocity Sensitivity Curve | 00 – 04 | yes: `fileg_veltime*`, `fileg_keytime*`, `fileg_veldepth*` | K33-K34 | pads (acceptance a) (depth velocity 39, curve 2: velocities 30, 100, 127) |
| FEG Time Key Follow Sensitivity | −64 – +63 | yes: `fileg_veltime*`, `fileg_keytime*`, `fileg_veldepth*` | K33-K34 | pads (acceptance a) (depth velocity 39, curve 2: velocities 30, 100, 127) |
| FEG Time Key Follow Sensitivity Center Note | C-2 – G8 | yes: `fileg_veltime*`, `fileg_keytime*`, `fileg_veldepth*` | K33-K34 | pads (acceptance a) (depth velocity 39, curve 2: velocities 30, 100, 127) |
| Filter Cutoff Scaling Break Point 1 | C-2 – E8 | yes: `fil_scaling_key1`: the cutoff index moves by 3.125 x the offset at the key (linear between the break points, flat beyond) | measured here (acceptance o): rms 8.4 index steps over 28 notes | core test `controller_boxes` (scaling cases) |
| Filter Cutoff Scaling Break Point 2 | C#-2 – F8 | yes: `fil_scaling_key2`: the cutoff index moves by 3.125 x the offset at the key (linear between the break points, flat beyond) | measured here (acceptance o): rms 8.4 index steps over 28 notes | core test `controller_boxes` (scaling cases) |
| Filter Cutoff Scaling Break Point 3 | D-2 – F#8 | yes: `fil_scaling_key3`: the cutoff index moves by 3.125 x the offset at the key (linear between the break points, flat beyond) | measured here (acceptance o): rms 8.4 index steps over 28 notes | core test `controller_boxes` (scaling cases) |
| Filter Cutoff Scaling Break Point 4 | D#-2 – G8 | yes: `fil_scaling_key4`: the cutoff index moves by 3.125 x the offset at the key (linear between the break points, flat beyond) | measured here (acceptance o): rms 8.4 index steps over 28 notes | core test `controller_boxes` (scaling cases) |
| Filter Cutoff Scaling Offset 1 | −128 – +127 | yes: `fil_scaling_offset1`: the cutoff index moves by 3.125 x the offset at the key (linear between the break points, flat beyond) | measured here (acceptance o): rms 8.4 index steps over 28 notes | core test `controller_boxes` (scaling cases) |
| Filter Cutoff Scaling Offset 2 | 〃 | yes: `fil_scaling_offset2`: the cutoff index moves by 3.125 x the offset at the key (linear between the break points, flat beyond) | measured here (acceptance o): rms 8.4 index steps over 28 notes | core test `controller_boxes` (scaling cases) |
| Filter Cutoff Scaling Offset 3 | 〃 | yes: `fil_scaling_offset3`: the cutoff index moves by 3.125 x the offset at the key (linear between the break points, flat beyond) | measured here (acceptance o): rms 8.4 index steps over 28 notes | core test `controller_boxes` (scaling cases) |
| Filter Cutoff Scaling Offset 4 | 〃 | yes: `fil_scaling_offset4`: the cutoff index moves by 3.125 x the offset at the key (linear between the break points, flat beyond) | measured here (acceptance o): rms 8.4 index steps over 28 notes | core test `controller_boxes` (scaling cases) |
| Filter Cutoff Key Follow Sensitivity | −200% – +200% | yes: `fil_keyfollow` | K21 | pads (acceptance a) |
| HPF Cutoff Key Follow Sensitivity | −200% – +200% | yes: `fil_hpf_keyfollow` (types 4 and 5) | measured here (acceptance n): the cutoff key follow's law, (raw - 64) x 3.125 cents a key about key 60 | white noise at key follow 0, 32, 96, 127 against 64, keys 24-96: rms 0.60 dB (acceptance n); core test `filter_types_4_5` |
| LFO Wave | Saw, Triangle, Square | reference mode: `elfo_wave` carried and ignored, as the reference ignores them (K18); the settings mode is not covered | K18, K35 | core test `element_lfo` (reference mode) |
| LFO Key On Reset | Off, On | reference mode: `elfo_keyon_reset` carried and ignored, as the reference ignores them (K18); the settings mode is not covered | K18, K35 | core test `element_lfo` (reference mode) |
| LFO Delay Time | 0 – 127 | reference mode: `elfo_delay` carried and ignored, as the reference ignores them (K18); the settings mode is not covered | K18, K35 | core test `element_lfo` (reference mode) |
| LFO Speed | 0 – 63 | reference mode: `elfo_speed` carried and ignored, as the reference ignores them (K18); the settings mode is not covered | K18, K35 | core test `element_lfo` (reference mode) |
| LFO Amplitude Modulation Depth | 0 – 127 | reference mode: `elfo_amp_depth` carried and ignored, as the reference ignores them (K18); the settings mode is not covered | K18, K35 | core test `element_lfo` (reference mode) |
| LFO Pitch Modulation Depth | 0 – 127 | reference mode: `elfo_pitch_depth` carried and ignored, as the reference ignores them (K18); the settings mode is not covered | K18, K35 | core test `element_lfo` (reference mode) |
| LFO Filter Modulation Depth | 0 – 127 | reference mode: `elfo_filter_depth` carried and ignored, as the reference ignores them (K18); the settings mode is not covered | K18, K35 | core test `element_lfo` (reference mode) |
| LFO Fade In Time | 0 – 127 | reference mode: `elfo_fade` carried and ignored, as the reference ignores them (K18); the settings mode is not covered | K18, K35 | core test `element_lfo` (reference mode) |
| LFO Extended Speed | 0 – 415 | reference mode: carried and ignored, as the reference ignores them (K18); the settings mode is not covered | K18, K35 | core test `element_lfo` (reference mode) |
| Controller Set 1-16 Switch | Off, On | the element's switch for each controller box: `box_sw*` (default on); the mod wheel's element LFO pitch and amplitude sets are carried as `elfo_pitch_ratio`, `elfo_amp_ratio` (a negative amplitude ratio is reported); their depth and that of boxes on the element LFO's pitch and amplitude depth from any source is one measured function of the box output (it replaces the fork's K19 and K30, which miss small depths); the part LFO's slots: the row below | the box law measured here (acceptance l, m): depth knots at 15 / 12 box outputs | core tests `element_lfo`, `controller_boxes` |
| EQ Type, EQ Q, EQ 1/2 Frequency and Gain | | yes: `eq_*`: Boost6/12/18 (flat +6.02/+12.04/+18.06 dB), Thru, any setting with the filter Thru (bypassed), P.EQ (one biquad) and 2-band (two first-order shelves in one biquad), every raw value | measured here (acceptance g, k): one frequency table, Q = 0.7 x 2^(raw / 8), gains 0.375 dB a step | 191 white-noise takes within 0.46 dB in 1/6-octave bands (acceptance k); the probe piano (Boost6) |
| LFO Box 1-3 Switch | Off, On | yes: `plfo_box_sw1`, `plfo_box_sw2`, `plfo_box_sw3`: the element's switch for each part LFO slot | measured here (acceptance p): off, slot 2 plays alone | core test `part_lfo` |
| Part LFO Destination 1-3 Depth Ratio | 0 – 127 | partial: `plfo_ratio1`, `plfo_ratio2`, `plfo_ratio3`: no effect on the pitch (measured); on another destination a ratio other than 127 is reported | measured here (acceptance p): ratios 0, 32, 64, 96 on slot 1, 64 on slot 2: the pitch swing unchanged | - |
| Part LFO Phase Offset | +0, +90, +120, +180, +240, +270 | partial: `plfo_phase_offset`: no effect on a part's only element (measured); with more than one element a non-zero offset is reported | measured here (acceptance p): offsets 1-5 on one element | - |

## Part

| parameter | implemented | law (source) | verified |
|---|---|---|---|
| Volume, Pan | the multi-part setup's (the converter writes the part's level); the shell's `level` and `pan` per sound are a linear gain and a balance | volume measured: (volume / 127)^2 on the 0.375 dB grid (acceptance c) | acceptance c |
| Note Shift, Detune, Zone Octave Shift | folded into the regions' transpose and tune by the converter | converter | - |
| Pitch Bend Range Upper / Lower | yes: `bend_up`, `bend_down` (semitones) | - | law only |
| Mono/Poly Mode, Key Assign Mode | no: `mono` is reported | K54 in the fork | - |
| Portamento Switch, Time, Mode, Time Mode | no: `portamento` is reported | - | - |
| Velocity Limit Low / High, Note Limit Low / High | clipped into the regions by the converter | converter | - |
| Velocity Sensitivity Depth / Offset | yes: `vel_depth`, `vel_offset`; the note's velocity on the 0-2047 scale, moved as measured, for everything (element velocity limits, level, filter and envelope velocity laws) | measured here (acceptance h) | 18 pairs at 11 velocities (rms 0.057 dB); velocity limits and cutoff velocity sensitivity follow it |
| Filter Cutoff, Resonance offsets | yes: `part_cutoff` (8 index steps a unit), `part_resonance` (2 steps a unit), added to every element's value within its limits before anything else moves it | measured here (acceptance r) | white noise at 13 cutoff offsets (within 4 index steps), a saw at 5 resonance offsets (within 2 steps) |
| AEG / FEG offsets (Attack, Decay, Sustain, Release, FEG Depth) | added to the element values by the converter (steps not measured there; the attack and sustain takes suggest 2 steps a unit, not settled) | converter | - |
| Part LFO wave, speed, phase, key-on reset, user wave | yes: `plfo_wave` (all 13: triangle from zero rising, triangle+ 0..1, saw up 0..1, saw down 0..-1, squares of duty 1/4 to 3/4, trapezoid, the sample and hold waves as distributions, user), `plfo_speed` (doubling every 8.026 steps; 1 a 565 s cycle; 0 none), `plfo_phase` (the start phase), `plfo_keyon_reset` (off: one oscillator from the first note; each on: every note its own; first on: one, started again at a note while no other key is down), `plfo_user_cycle`, `plfo_user_slope`, `plfo_user_step*` | measured here (acceptance p) | 19 speeds within 0.7 %; waves, phases and user waves against the reference's pitch (acceptance p); core test `part_lfo` |
| Part LFO destinations and depths 1-3 | partial: `plfo_dst1`, `plfo_dst2`, `plfo_dst3`, `plfo_depth1`, `plfo_depth2`, `plfo_depth3`: the element level (64: depth level steps), pitch (65: a measured table of cents, 2400 at 127), cutoff (66: 8 index steps a depth step) and pan (68: a saturating table of raw pan steps, the far side silent from 96), summed over the slots before each parameter's limits; other destinations (the resonance among them) are reported | measured here (acceptance p) | plateaus of a square at 33 pitch depths (whole cents), 15 level depths (0.17 dB), 5 pan depths (0.22 dB), 5 cutoff depths (4 index steps), two slots summed (within 1.3 cents) |
| Part LFO delay, fade in, hold, fade out, tempo sync, tempo speed, random speed | partial (not built: reported when a slot moves something): `plfo_delay`, `plfo_fade_in`, `plfo_hold` (127 hold: covered), `plfo_fade_out`, `plfo_tempo_sync`, `plfo_tempo_speed`, `plfo_random_speed` | - | - |
| Controller boxes 1-16 (source, destination, ratio, polarity, curve) | partial: `box*`. Destinations cutoff, resonance, element level, coarse and fine tune, high-pass cutoff, the filter envelope's depth (read at note-on), the element LFO's pitch and amplitude depth, the amplitude envelope's attack (note-on) and release (note-off) (also CLAP parameters, modulatable: `docs/parameters.md`); sources pitch bend, mod wheel, foot controller 1 (CC 11), foot controller 2 (at rest, the top), breath (CC 2), the ribbon, knobs 1-8 and switches 1-2 (CLAP parameters); curves: linear, type 0 with parameter 0, 2, 6, 8, 9, 10 and two of type 2. Other destinations, channel pressure (map not measured), the foot switch, sequenced and envelope-follower sources, other curves are reported | measured here (acceptance l): output (curve(c) + o - 1024) x ratio / 8 within +-8192; a destination moves by the sum of each box's trunc(output x span / 8192) within +-span; spans 1024 / 128 / 128 / 2400 cents / 100 cents / 1024 / 64 (the filter envelope's depth) / 128 (attack, release) (acceptance m), each box truncated before the sum (fine, coarse, level, cutoff); a knob at 1023 plays as 1024; controllers 8 v to 64 then 512 / 63 a step; bend / 16; knobs c = the raw value, the ribbon as a controller (CC 16), switches 0 or 1024 (acceptance m) | fine tune within 0.1 cent at 17 wheel values for wheel, foot, breath and bend; knob 1 at 8 stored values and knob 8 within 0.05 cent, the ribbon at 17 values, switches 1 and 2 by their controllers; sum, clamp and bipolar boxes; level boxes at four element levels rms 0.32 dB; core test `controller_boxes`; ABI tests `modulation_sounds_as_the_equivalent_linear_box`, `knobs_ribbon_and_switches_drive_their_boxes` |
| Assignable knob values 1-8 | yes: `knob1`, `knob2`, `knob3`, `knob4`, `knob5`, `knob6`, `knob7`, `knob8` (raw 0-1023), where the shell's knob parameters start | measured here (acceptance m) | ABI test `knobs_ribbon_and_switches_drive_their_boxes` |
| Output ports (where the elements' insertion routing goes) | yes: `split_outputs` (set by the converter): 1 puts each element on its connection's port (0 main / thru, 1 to insertion A, 2 to insertion B); 0, or a host that gives the main port only, sums everything on the main port | structural | ABI test `output_ports` (the main port alone carries the split ports' sum, sample for sample) |
| Part EQ, insertion effects, sends, dry level, output select | the host's effects chain, outside this plugin | - | - |
| Arpeggio, step sequencer, controller receive/transmit switches | the host's | - | - |

## Regions, samples, rates

| item | implemented | verified |
|---|---|---|
| Sample playback: 16-bit PCM in memory, mono and stereo, any source rate | yes (WAV and FLAC decoded whole by the shell) | pads: 44.1, 40 and 20 kHz sources, mono and stereo |
| Forward loops | yes, stitched across the loop end (a loop shorter than a step included) | core tests; pads |
| A region without a loop | plays to the sample's end and ends there | core tests; the probe piano's one-shot regions (acceptance g; the end itself not compared) |
| Region pan | the key bank's pan added to the element pan in raw steps, then the balance law | acceptance c (hard-panned key banks); intermediate key-bank pans not measured |
| Region level (a key bank's) | yes, dB | pads (the stereo region at -7.12 dB) |
| Output staging | the reference's: -9.43 dB per channel for a full-scale sample at centre | acceptance b, c |
| Interpolation | 20-tap windowed sinc from a bank of 17 cutoffs, picked by the transposition (exact at the root, the cutoff following the step above it, 0.95 below it; Hermite for comparisons) | acceptance f (a saw source wave at the root and 1-11 semitones above) |
| Host sample rates | 44.1 kHz (the plain sections), and rates between 44.1 and 132.3 kHz that are no whole multiple of 44.1 (48, 96 kHz: the allpass form). 88.2 kHz and above 132.3 kHz play the plain 44.1 kHz sections: not covered | core tests at 44.1, 48, 96 kHz |
| Voice end at the release's floor | -96.45 dB below full scale, the release's rate unchanged | acceptance e (releases 96-112) |
| Note-on events | on their exact frame | core tests |
| Note-off events | the releases start on their exact frame | core test `release_mid_block` (mid-block, 96- and 128-frame blocks equal) |
| Shedding | `Core::shed`: the quietest element voices fade over 16 frames | cost gate |

## Robustness

`tests/test_fuzz.cpp` (in the ABI suite): 24 rounds of three random sounds each (every element
parameter at random over its full raw range, up to 6 random regions with random keys, velocities,
roots, transposition ±48, tune ±12 semitones, level -40 to +6 dB, pan, loops or none), 200 blocks
of 1-128 frames per round with random notes, note-offs, chokes, note volume, mod wheel, sustain pedal
and pitch bend, four random controller boxes per sound over every part parameter's range (the built destinations and
sources likelier), breath, foot controller, channel pressure, random stored knob values, and (0.4.0) every shell
parameter's value (knobs, ribbon, switches, box destinations, a little past their ranges) and the destinations'
modulation to twice their spans: 4,800 blocks, about 3,800 notes. Output finite and below 64, no allocation in
process(). `fuzz_every_note_ends` (0.4.0): eight rounds of random sounds (releases and half damper times at most index
90, no time shifts), random notes, pedals, controllers and chokes, then every key and the pedal up and 12 s: no voice
remains and every note-on got exactly one NOTE_END. `one_shot_regions_end_their_voices` and
`pad_voices_end_by_the_release_rule` (0.4.0) hold the voice-end rule: a voice ends when its amplitude envelope's release
reaches -96.45 dB (a release at index i lasts at most its seconds per 60 dB x 96.45 / 60, from any level; entered again,
it keeps its rate from where it stands), or when a region without a loop plays past its sample's end. Run on 0.4.0's
builds: NEON (arm64), SSE2 (x86_64 under Rosetta) and scalar (`PV_SCALAR`), ASan+UBSan on macOS; Linux arm64 release,
ASan+UBSan and TSan.
