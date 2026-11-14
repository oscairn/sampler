# CLAP parameters

Per sound (a parameter event with a channel reaches the sounds that channel selects; without one, every selected
sound). The ids are stable: a new parameter gets a new id, an id is never reused or renumbered. Every parameter but
id 26 is automatable; the box destinations (ids 14-24) are also modulatable (`CLAP_PARAM_IS_MODULATABLE`). Id 26 is
the instance's, read-only (`CLAP_PARAM_IS_READONLY`), not the sound's.

| id | name | patch-text key | range | default | what it does |
|---|---|---|---|---|---|
| 0 | Level | `level` | 0..1 | 1 | the sound's level, linear (1 = the sound's own level) |
| 1 | Pan | `pan` | -1..1 | 0 | balance |
| 2 | Polyphony Limit | `polyphony` | 1..64, stepped | 64 | the most voices the sound keeps |
| 3-10 | Knob 1-8 | `knob1`..`knob8` | 0..1 | the sound's own value (part parameter `knobN` / 1023; its default 512) | the part's knob N as a controller box source (sources 8-15); a box sees round(value x 1023), the top (1023) as 1024 |
| 11 | Ribbon | `ribbon` | 0..1 | 0 | the ribbon as a box source (6); a box sees it as the controller value round(value x 127) |
| 12, 13 | Switch 1, Switch 2 | `switch1`, `switch2` | 0 or 1, stepped | 0 | the part's switches 1 and 2 as box sources (16, 17): off 0, on 1024 |
| 14 | Cutoff | `cutoff` | -1024..1024 | 0 | the box destination cutoff: raw index steps added to every element's |
| 15 | Resonance | `resonance` | -128..128 | 0 | the box destination resonance, raw steps |
| 16 | Element Level | `element_level` | -128..128 | 0 | the box destination element level, raw steps |
| 17 | Coarse Tune | `coarse` | -2400..2400 | 0 | the box destination coarse tune, cents |
| 18 | Fine Tune | `fine` | -100..100 | 0 | the box destination fine tune, cents |
| 19 | High-Pass Cutoff | `hpf_cutoff` | -1024..1024 | 0 | the box destination high-pass cutoff (filter types 4 and 5), raw index steps |
| 20 | Filter EG Depth | `feg_depth` | -64..64 | 0 | the box destination filter envelope depth, raw steps; read when a note starts (as the reference reads its box) |
| 21, 22 | Element LFO Pitch Depth, Element LFO Amp Depth | `elfo_pitch_mod`, `elfo_amp_mod` | -8192..8192 | 0 | the box destinations element LFO pitch / amplitude depth in box-output units (a full box is 8192), added to the boxes' outputs and the mod wheel's element LFO ratios before the depth law |
| 23, 24 | AEG Attack, AEG Release | `attack`, `release` | -128..128 | 0 | the box destinations amplitude envelope attack (read at note-on) and release (read at the note-off), index steps |
| 25 | Output Bus | `bus` | 0..8, stepped | 0 | the output port the sound plays on: 0 main, n sound bus n (see [Output ports](#output-ports)) |
| 26 | Late Stream Sheds | (none) | 0..2^32-1, read-only | 0 | element voices the instance shed for want of streamed audio since it was made: a late stream, or no free stream (the count the plugin's `org.oscairn.pcm.streaming/draft-1` extension returns) |

A host passes no MIDI controller for the ribbon, the knobs or the switches (only CC 1, 2, 4, 11, 64-67, 120, 123 and
pitch bend reach a plugin), so they are parameters. The maps were measured on the reference through a box on the fine
tune (`docs/acceptance.md` (m)): a stored knob value enters a box as itself, the ribbon as its controller (CC 16) by
the controllers' map, the switches as their controllers at 0 and 127. A knob starts at the sound's stored value
(`prepare` text, or a state that holds no knobs); a value given in the `prepare` shell line or its parameter list wins.

## Box destinations (ids 14-24)

Each destination the core's boxes move is a parameter, so that a converter can map a plain source-to-destination box
onto the host's control tree with a host-side curve instead of an in-engine box. Its value is an offset in the
destination's raw steps (cents for the tunings), saved with the sound. A modulation (`CLAP_EVENT_PARAM_MOD`, the
amount in the same unit) adds to it. The sum, toward zero, joins the destination's box steps before their clamp to
+-span (the box law: each box adds trunc(output x span / 8192)), and the destination stays within its range; it
reaches every element (no element switch applies). So a modulation of a = m x span (m normalised, 1 a full box) moves
the destination as a linear box whose output is m x 8192 does. A modulation is not saved, not shown as the value, and
stays with the sound slot until the host changes it; a modulation for one note (a note id or a key) is not applied.

For a linear box both paths give the same result: the ABI test `modulation_sounds_as_the_equivalent_linear_box` plays
the mod wheel at 127 through a box of ratio +8 on the cutoff against a modulation of 128.9 steps (128 toward zero) (and ratio +63 on
the fine tune against 98.4375 cents), sample for sample the same. In-engine boxes stay for what a host curve does not
express: the reference's curve shapes, several boxes summed and clamped, the bipolar offset, an element's switch.
Coverage (`pcm::notCovered`) judges the sound with its boxes; a value or modulation set from outside can take a
destination beyond what was measured, as a knob can.

## Output ports

Nine stereo output ports with stable ids: 0 Main and 1-8 sound bus 1-8 (port n has id n). A sound whose Output Bus is
n plays all of its voices on port n; a change moves its voices at once. Ports 1 and 2 have a second reading, the part's
element routing: while one sound alone is selected on the MIDI channels and its Output Bus is 0, a sound that splits
its outputs (part parameter `split_outputs`) sends each element on its connection, 1 to insertion A (port 1) and 2 to
insertion B (port 2), the rest to Main. In every other case port n is sound bus n. A host that shares the instance
among parts never groups a sound that splits its outputs with others; if a second sound is selected anyway, the first
sound's elements fall back to its own bus with the rest of it (and split again once it is alone). With
`clap.audio-ports-activation` a port plays only once the host has activated it: a port never activated costs nothing
and its sound plays on Main.

Parts sharing one instance (`oscairn.sound-snapshots/draft-4`): each part's sound has its own channel and Output Bus.
`save_sound` writes one sound alone, as the Default of a whole snapshot, on every channel and on Main (Output Bus 0);
`sound_values` gives ids 0-25 of a sound no channel selects. A loaded state or snapshot selects its sound on every
channel (a CLAP state: from the next block). While a sound retires (no channel selects it) and the host gives a
ring-out buffer, what it plays on Main goes into the buffer's channels 0 and 1, or 2 and 3 once `choke_retiring` fades
it out over the choke time; what it plays on other ports stays there.

## Editor hints (`oscairn.ui`)

Every parameter has a hint: Level (role level, priority 1), Pan (pan), Cutoff (cutoff, 2), Resonance (resonance, 3),
Element Level (level 1), Filter EG Depth (filter envelope, 4); the element LFO depths are a group, AEG Attack (attack, 5), AEG Release (release, 6); the knobs are one group, the ribbon with the switches another (the switches as switches), the
tunings with the high-pass cutoff a third; Output Bus has the output bus role (`OSCAIRN_ROLE_OUTPUT_BUS`).

## State

The CLAP state `oscairn-pcm-state 3` and the sound-contexts snapshot `oscairn-pcm-contexts/3` save ids 0-25 (not the
modulation), then the sound. A state 2 (ids 0-24) and a state 1 (ids 0-2 only) still load, on Main and with their
knobs at the sound's values; an older snapshot does not.
