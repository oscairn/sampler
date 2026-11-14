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
| Key On Delay Tempo Sync Switch | Off, On | no (reported) | K52-K53 in the fork | - |
| Half Damper Switch | Off, On | yes: `half_damper` (the sustain pedal: `Core::setSustain`, `keyUp`) | measured here (acceptance g) | sine probes; the probe piano under the pedal (acceptance g) |
| LFO Speed Range | Normal, Extended | reference mode: carried and ignored, as the reference ignores them (K18); the settings mode (`elfo_mode=1`) is not covered | K18, K35 | core test `element_lfo` (reference mode) |
| Key Controller Set 1 Switch | Off, On | no | - | - |
| Key Controller Set 2 Switch | Off, On | no | - | - |
| Key Controller Set 3 Switch | Off, On | no | - | - |
| Key Controller Set 4 Switch | Off, On | no | - | - |
| Wave Select | 0=Preset, 1=User, 2–17=Library1–16, 18-25=Library17–24 | yes: the region table and its sample references | converter | pads (acceptance a) |
| Element Group Number | 1–8 | `element_group`: normal only; any other element trigger mode is reported | - | - |
| Wave Number | 1 – 7620 (USR, Library: 1 – 1024) | yes: the region table and its sample references | converter | pads (acceptance a) |
| Element Pan | L63 – C – R63 | yes: `amp_pan`, summed with the key bank's pan, the balance law | K1; measured here (acceptance c) | acceptance c: pans 1-127, mono, stereo and panned key banks |
| Random Pan Depth | 0 – 127 | no | - | - |
| Alternate Pan Depth | L64 – C – R63 | no | - | - |
| Scaling Pan Depth | −64 – +63 | no | - | - |
| Element trigger mode | normal, legato, key-off, cycle, random, switch off, switch 1 on, switch 2 on | Normal and Key Off (`xa_control=2`: from the note's end, the key's release or under the sustain pedal the pedal's (measured, acceptance g), released 2.0 s after its start); others reported | K42 (the fork's, BSD) | core test; the probe piano's key-off element in its tails (acceptance g) |
| Note Limit Low | C-2 – G8 | yes: clipped into the region table by the converter | converter | pads (acceptance a) |
| Note Limit High | C-2 – G8 | yes: clipped into the region table by the converter | converter | pads (acceptance a) |
| Velocity Limit Low | 1 – 127 | yes: clipped into the region table by the converter | converter | pads (acceptance a) |
| Velocity Limit High | 1 – 127 | yes: clipped into the region table by the converter | converter | pads (acceptance a) |
| Velocity Cross Fade | 0 – 127 | no (not in the sound format; the converter must report a non-zero value) | K55 | the probe piano and bass use 0 |
| Key On Delay Time Length | 0 – 127 | no: `keyon_delay` (reported) | K52-K53 in the fork | - |
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
| Pitch Velocity Sensitivity | −64 – +63 | no (100 cents a key) | - | - |
| Random Pitch Depth | 0 – 127 | no (100 cents a key) | - | - |
| Pitch Key Follow Sensitivity | −200% – +200% | no (100 cents a key) | - | - |
| Pitch Key Follow Sensitivity Center Note | C-2 – G8 | no (100 cents a key) | - | - |
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
| Filter Resonance Velocity Sensitivity | −64 – +63 | no | - | - |
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
| Filter Cutoff Scaling Break Point 1 | C-2 – E8 | no | - | - |
| Filter Cutoff Scaling Break Point 2 | C#-2 – F8 | no | - | - |
| Filter Cutoff Scaling Break Point 3 | D-2 – F#8 | no | - | - |
| Filter Cutoff Scaling Break Point 4 | D#-2 – G8 | no | - | - |
| Filter Cutoff Scaling Offset 1 | −128 – +127 | no | - | - |
| Filter Cutoff Scaling Offset 2 | 〃 | no | - | - |
| Filter Cutoff Scaling Offset 3 | 〃 | no | - | - |
| Filter Cutoff Scaling Offset 4 | 〃 | no | - | - |
| Filter Cutoff Key Follow Sensitivity | −200% – +200% | yes: `fil_keyfollow` | K21 | pads (acceptance a) |
| HPF Cutoff Key Follow Sensitivity | −200% – +200% | no | - | - |
| LFO Wave | Saw, Triangle, Square | reference mode: `elfo_wave` carried and ignored, as the reference ignores them (K18); the settings mode is not covered | K18, K35 | core test `element_lfo` (reference mode) |
| LFO Key On Reset | Off, On | reference mode: `elfo_keyon_reset` carried and ignored, as the reference ignores them (K18); the settings mode is not covered | K18, K35 | core test `element_lfo` (reference mode) |
| LFO Delay Time | 0 – 127 | reference mode: `elfo_delay` carried and ignored, as the reference ignores them (K18); the settings mode is not covered | K18, K35 | core test `element_lfo` (reference mode) |
| LFO Speed | 0 – 63 | reference mode: `elfo_speed` carried and ignored, as the reference ignores them (K18); the settings mode is not covered | K18, K35 | core test `element_lfo` (reference mode) |
| LFO Amplitude Modulation Depth | 0 – 127 | reference mode: `elfo_amp_depth` carried and ignored, as the reference ignores them (K18); the settings mode is not covered | K18, K35 | core test `element_lfo` (reference mode) |
| LFO Pitch Modulation Depth | 0 – 127 | reference mode: `elfo_pitch_depth` carried and ignored, as the reference ignores them (K18); the settings mode is not covered | K18, K35 | core test `element_lfo` (reference mode) |
| LFO Filter Modulation Depth | 0 – 127 | reference mode: `elfo_filter_depth` carried and ignored, as the reference ignores them (K18); the settings mode is not covered | K18, K35 | core test `element_lfo` (reference mode) |
| LFO Fade In Time | 0 – 127 | reference mode: `elfo_fade` carried and ignored, as the reference ignores them (K18); the settings mode is not covered | K18, K35 | core test `element_lfo` (reference mode) |
| LFO Extended Speed | 0 – 415 | reference mode: carried and ignored, as the reference ignores them (K18); the settings mode is not covered | K18, K35 | core test `element_lfo` (reference mode) |
| Controller Set 1-16 Switch, LFO Box 1-3 Switch | Off, On | the mod wheel's element LFO pitch and amplitude sets only (`elfo_pitch_ratio`, `elfo_amp_ratio`); other boxes no | K19, K30, K36 | core test `element_lfo` |
| EQ Type, EQ Q, EQ 1/2 Frequency and Gain | | `eq_*`: Boost6/12/18 (flat +6.02/+12.04/+18.06 dB), Thru, 2-band at 0 dB, and any setting with the filter Thru (bypassed); P.EQ and 2-band with gain reported | measured here (acceptance g) | white noise and saw probes; the probe piano (Boost6) |
| Part LFO Phase Offset, Destination Depth Ratios | | no (K17) | - | - |

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
| AEG / FEG offsets (Attack, Decay, Sustain, Release, FEG Depth), Filter Cutoff, Resonance | added to the element values by the converter (steps not measured there) | converter | - |
| Part LFO (all), User LFO | no (K17) | - | - |
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
and pitch bend: 4,800 blocks, about 4,700 notes. Output finite and below 64, no allocation in
process(). Run on this round's builds: NEON (arm64), SSE2 (x86_64 under Rosetta) and scalar
(`PV_SCALAR`), each passing (peak 4.29 in all three).
