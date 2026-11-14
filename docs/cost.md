# Cost

## Method

`tools/cost/cg.sh` builds `tools/pcbench.cpp` with the core at `-O3 -mcpu=cortex-a53` (g++ 13.3,
aarch64 Linux, NEON) and runs each case under callgrind with one dump per measured 128-frame block
at 48 kHz (the first measured block dropped). The sound: the pads' element (filter type 0, the pads'
envelopes, velocity 100) over one region of a built-in looped sample, mono at 44.1 kHz or stereo at
40 kHz, notes spread over keys 48-72 (each note one element voice). Cases: no voice ("rest"), 1 to 64
held voices for each plan and interpolator, the plans without the filter, 8 and 32 voices just
released (the filter envelope moving the cutoff every control step, the costlier steady state), and
every held voice shed in one block.

**Gates** (`cg.sh SRC OUT check`, exit 1 on a break): per element voice, from 8 to 32 held voices,
mono at most 11,000 and stereo at most 19,500 instructions a block with the 20-tap kernel; rest at
most 200; shedding at most 2,000 a shed voice (the shed block and the next over two blocks at rest);
and no case more than 10 % over `tools/cost/baseline.txt`. It needs valgrind, so it is a documented
script run in the aarch64 container
(`docker run --rm -v "$PWD":/src:ro --tmpfs /out:exec IMAGE sh /src/tools/cost/cg.sh /src /out check`),
not a CTest test. The streamed plans (`mono-stream`, `stereo-stream`: a 4,096-frame head in memory, the rest and
the loop from the core's stream rings, filled between blocks outside the measured window) are held to the same
per-voice gates: they measure what reading streamed audio costs the audio thread. Last run (0.7.0 code before its
kernel sharing for type 10): every gate passes (mono 10,746, stereo 15,220, streamed mono 10,791, streamed stereo 15,259,
rest 160, shedding 1,989 and 1,747 a voice for 16 and 32 voices). (0.6.0, 9fcec60: mono 10,734, stereo 15,222, streamed
mono 10,778, streamed stereo 15,261, rest 193, shedding 1,907 and 1,704.) (0.5.0: 10,595, 15,065, 193, 1,904 and 1,688; 0.4.0: 10,557, 15,027, 192,
1,868 and 1,667.)

## Instructions per block (aarch64, NEON)

| voices | mono, 20-tap | mono, Hermite | stereo, 20-tap | stereo, Hermite |
|---|---|---|---|---|
| 0 (rest) | 192 | 192 | 192 | 192 |
| 1 | 37,340 | 25,532 | 53,980 | 37,148 |
| 4 | 47,993 | 35,561 | 65,662 | 47,654 |
| 8 | 89,983 | 65,231 | 125,267 | 89,251 |
| 16 | 174,264 | 124,872 | 245,185 | 173,153 |
| 32 | 343,344 | 244,672 | 485,916 | 341,852 |
| 64 | 680,872 | 483,640 | 966,246 | 678,118 |
| **a voice (8 to 32)** | **10,557** | 7,477 | **15,027** | 10,525 |

(0.3.0: 10,741 / 7,661 / 15,211 / 10,709 a voice; 0.4.0 lets a voice that nothing can modulate skip the box and element
LFO work at its control steps, which also takes back what the controller boxes' fields had added.) (0.2.0: 10,657 / 7,403 / 14,829 / 10,283 a voice; 0.3.0 adds the element EQ's per-lane check and the controller boxes'
fields.) (0.1.0: 10,639 / 7,339 / 14,549 / 10,219 a voice. The 20-tap kernel now comes from a bank of 17 by
the voice's transposition, which adds a per-voice table offset to each fetch; the bank is 700 KB.)
Other cases, a voice from 8 to 32, 20-tap (0.4.0): without the filter mono 8,865, stereo 11,313; just released
(the cutoff redesigned every control step) type 0 mono 12,160, stereo 16,630; type 4 mono 12,321,
stereo 16,786; type 5 mono 11,444, stereo 15,384; with the element EQ's peak mono 10,668, stereo 15,013. Per block the counts hardly vary (the largest block
of every case is within 0.5 % of its mean, the released cases included), so the spread of the
device's times is not the filter's redesign: the device's p99 well above its mean (1,120 against
671 us, 64 mono voices) comes from the machine (cache, memory, scheduling), not from work that
varies by block. The voice-budget estimate (`pcm::estimateCost`) takes the released figures for the
filtered plans, the thru figures for the others (the Hermite plans derived from the 20-tap ones by
the differences), a sound's costliest note as all the regions it starts, and converts at 1.0 ns an
instruction: measured on the device for this core (Cortex-A53, thread CPU mean, 64 voices of the
pads' plan with the 0.1.0 core: 10.5 us a mono voice and 13.7 us a stereo voice with the 20-tap kernel,
4.8 and 7.2 us with Hermite, so 0.99 and 0.94 ns an instruction; scalar builds 4-6 times slower).

The element EQ (0.3.0): a voice whose EQ shapes the sound runs one biquad more a channel (a lane runs it only when
one of its four voices needs it). The pads' element with a peak, a voice from 8 to 32: mono 10,977, stereo 15,697
instructions a block (the gates: 11,000 and 19,500). Memory: nine more per-voice fields (the biquad's five coefficients and four states), 36,864 bytes
in the core's eight pools (9 x 4 bytes x 128 voices x 8). The controller boxes add 40 bytes to each voice's scalar side
(40,960 bytes in the pools), 192 bytes to a compiled sound and 32 bytes to each parsed element (the box switches);
a box costs nothing while its sources stay still (the voice stays steady) and a few hundred instructions a moved voice
in the control step when one moves. 0.4.0: the sound's own sources and destination offsets add 9,152 bytes to the core
(`sizeof(pcm::Core)` 1,518,912 against 1,509,760; the pools 8 bytes a voice more), 704 bytes to a compiled sound; the
plugin's code 423,816 bytes of text on macOS arm64 against 405,780 at 0.3.0 (the interpolation bank, 699,040 bytes,
unchanged). The destinations read at note-on (filter envelope depth, attack) and at the note-off (release) cost a pass
over the sound's boxes there, nothing per block.

0.5.0: the part LFO, element selection, key-on delay, the part's offsets and the velocity, random pitch laws add 38
instructions a voice-block to a voice that uses none of them (mono 10,595 against 10,557: the part LFO's checks in the
control step; the offsets are applied when the sound is compiled, selection, delay and the velocity laws at note-on). A
voice that the part LFO reaches runs it once a control step (its cost a moved voice's, not measured with callgrind). The
voice's scalar side stays at 512 bytes (the LFO's four bytes in padding: at 536 bytes the pools' indexing cost 110
instructions a voice-block more); `sizeof(pcm::Core)` 1,524,224 against 1,518,912 (the held-back layers of key-on delays,
the selection's cycle positions and draws, the LFOs' start clocks); a compiled sound 69,104 bytes against 68,392; the
plugin's code 441,312 bytes of text on macOS arm64 against 423,816 at 0.4.0. New tables: the LFO's pitch depth knots (66
bytes) and pan knots (8), the key-on delay knots (348).

0.7.0: filter types 1, 2, 3, 6, 7 and 10 (`mono-tN`, `stereo-tN` in `cg.sh`'s full run), a voice from 8 to 32, 20-tap,
held and just released (the cutoff redesigned every control step); type 2 runs type 3's plan and was not run separately:

| plan | mono held | mono released | stereo held | stereo released |
|---|---|---|---|---|
| type 0 (the gate's) | 10,746 | 12,409 | 15,220 | 16,907 |
| type 1 | 10,808 | 12,783 | 15,320 | 17,295 |
| types 2, 3 | 10,312 | 12,241 | 14,340 | 16,269 |
| type 6 | 10,744 | 12,427 | 15,156 | 16,839 |
| type 7 | 10,014 | 11,697 | 13,602 | 15,285 |
| type 10 | 10,872 | 12,591 | 15,450 | 17,169 |

Every new plan's held voice is within the mono and stereo gates. A type-0 voice pays 10 to 12 instructions a block more
held than at 0.6.0 and 64 more released (12,409 against 12,345 measured on e1775b6 in the same run: the pools' wider
layout and the design's dispatch); the silent test counts the voices instead of visiting the pools (rest 160 against
193; visiting nine pools of each width it cost 296). Memory: nine filter plans instead of four, so ten more pools,
`sizeof(pcm::Core)` 4,651,328 bytes against 3,638,336 (each pool 100,480 bytes: two more per-voice fields, the first
section's offset and type 3's top); the core's code 371,641 bytes of text against 259,409 (macOS arm64, -O3, the core's
objects; each plan adds its kernels for both widths, interpolators and routings; type 10 runs type 4's). No new table.

## Sounds on their own output buses (0.8.0)

Parts sharing an instance put each sound on its own output bus (`docs/parameters.md`, output ports). A pool whose
voices are all on bus 0 renders as before (the gated cases). Once a pool has a voice off bus 0 and the buses are
distinct buffers, each lane mixes into its own buffer and each of its voices is added into its bus from there, scalar:
about 8 more instructions a voice a frame, whatever the number of buses. `PC_SOUNDS=N pcbench cg ...` installs the
pads' sound in N slots, slot k on bus k + 1, the notes taken in turn (so every lane holds voices of different sounds,
the worst case). Last run (0.8.0, aarch64 container, per element voice from 8 to 32 held voices):

| plan | one sound | 5 sounds | 8 sounds |
|---|---|---|---|
| mono, 20-tap | 10,728 | 11,984 (+11.7 %) | 11,984 (+11.7 %) |
| stereo, 20-tap | 15,216 | 16,382 (+7.7 %) | 16,382 (+7.7 %) |

8 voices over 5 sounds: 101,900 instructions a block against 91,374 for the same 8 voices in one sound (+11.5 %; the
bench's own zeroing and summing of its 11 bus buffers, 4,812 a block measured at rest, taken off). At rest nothing of
this runs: the core returns before it looks at a bus, 192 instructions a block with 1, 5 or 8 sounds installed. The
shell adds, per process call, a pass over the 16 channels and over the sounds in use (choosing each one's bus), and
zeroes the ports the host activated; ports never activated are not touched. Gates on the same run: mono 10,728,
stereo 15,216, streamed mono 10,772, streamed stereo 15,255, rest 192, shedding 1,895 and 1,698 a voice. Memory: the
two per-voice routing fields of 0.7 are gone (16,384 bytes less in the pools); the instance estimate
(`oscairn.memory`) went from 4,762,480 to 4,744,088 shared bytes.

## Shedding

`Core::shed(n)` fades the n quietest element voices out over 16 frames (one control step) and frees
them as they reach silence. Shedding 16 and 32 voices cost 1,904 and 1,688 instructions a shed voice (0.5.0, the gate's run), about 1/8 of a voice-block
plus the scan for the quietest. The shell's own give-ups (the voice budget's order) fade over the
SDK's 3 ms choke instead, as in the analog engine.

## Memory (0.6.0)

What a sound keeps in memory (docs/design.md, "Streaming"), per region of a record with the default
4,096-frame window and 1,024-frame blocks: a head of 32 + 4,096 + 8 frames (8,272 bytes mono, 16,544
stereo), and a forward loop's body whole when it is at most 16,384 frames, (body + 8) frames; a
region of at most 4,096 frames is all head with its 1,024-frame guard. Measured through
`oscairn.memory` (asset bytes): a stereo one-shot of 150,000 frames adds 16,544 bytes, a second region
of it with an 8,000-frame loop body adds 32,032. Buffers are shared by every region and sound of the
process that plays the same frames of the same record.

A synthetic sound of 854 regions over 854 distinct stereo 48 kHz samples of 96,000 frames (2 s), half
one-shots and half looped with an 8,000-frame body near the end, by those rules:

| | bytes |
|---|---|
| heads (854 x 16,544) | 14,128,576 |
| loop bodies (427 x 32,032) | 13,677,664 |
| resident audio | 27,806,240 (26.5 MiB) |
| the same decoded whole (854 x 388,224, with the guards) | 331,543,296 (316.2 MiB) |
| the stream pool (128 rings of 4,096 stereo frames, 16,464 bytes each), per instance | 2,107,392 |

Packs are read at their offsets, never mapped (0.6.1): a process whose mappings are all locked holds what it read, not
the pack. `tools/mlock/footprint.sh` (Linux, `docker run --ulimit memlock=-1`) locks everything with
`mlockall(MCL_CURRENT | MCL_FUTURE)`, opens a 463,347,712-byte pack of 40 records (60 s of stereo noise each), checks its
first record and decodes 64 blocks of it: VmLck 5,764 kB locked before, 5,984 kB after (0.6.0, mapping the pack: 458,244
kB once the pack was open). The I/O thread reads each block's compressed bytes into its one 48 KiB buffer: in
callgrind (user space, aarch64, -O3) decoding every block of a 10 s test tone takes 16,677,813 instructions mono at
44.1 kHz and 38,368,055 stereo at 48 kHz, against 16,832,508 and 38,541,082 from the mapping (the copy itself is the
kernel's, not counted; the record header is no longer checked again on every block).

The stream pool is part of the core (`sizeof(pcm::Core)` 3,638,336 bytes on macOS arm64) and so of
`oscairn.memory`'s shared bytes; each instance has its own. The parsed and expanded sound tables
(`pcm::SoundData`, 314,364 bytes each, two for a sound with waveforms) are main-thread memory freed
with the sound and not reported.

## Time on this Mac (Apple silicon, thread CPU per block, `pcbench time`; the 0.1.0 core, not rerun)

| voices | mono, 20-tap | mono, Hermite | stereo, 20-tap | stereo, Hermite |
|---|---|---|---|---|
| 0 | 0.16 us | 0.16 us | 0.22 us | 0.17 us |
| 1 | 1.98 | 1.55 | 3.42 | 2.47 |
| 8 | 3.90 | 2.75 | 6.71 | 5.09 |
| 32 | 15.36 | 10.62 | 26.58 | 18.65 |
| 64 | 29.50 | 21.78 | 54.14 | 37.43 |

## Device

Measured on the device (Cortex-A53, thread CPU per 128-frame block at 48 kHz, unpinned, idle; the
0.1.0 core, b9e55b2), mean / p99, 64 voices: mono Hermite 305 / 327 us, mono 20-tap 671 / 1,120, stereo
Hermite 463 / 578, stereo 20-tap 874 / 1,120; while a Performance played on the unit, mono 20-tap 758 /
952 and stereo 20-tap 1,031 / 1,407; scalar builds mono 20-tap 2,718 and stereo 5,347. 64 mono voices
take half of the 1,333 us block, 64 stereo about two thirds.

Static aarch64 binaries of the core's time mode for this round (`pcbench time [CPU]`) were built in
the container with `g++ -std=c++17 -O3 -DNDEBUG -mcpu=cortex-a53 -static core/*.cpp tools/pcbench.cpp`
(NEON) and the same with `-DPV_SCALAR`; they are kept outside the repository with their SHA-256 sums,
and passed their `check` mode in the container; the 0.2.0 binaries have not been run on the device.
