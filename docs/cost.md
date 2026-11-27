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
mono at most 11,000 and stereo at most 19,500 instructions a block with the interpolation kernel; rest at
most 200; shedding at most 2,000 a shed voice (the shed block and the next over two blocks at rest);
and no case more than 10 % over `tools/cost/baseline.txt`. It needs valgrind, so it is a documented
script run in the aarch64 container
(`docker run --rm -v "$PWD":/src:ro --tmpfs /out:exec IMAGE sh /src/tools/cost/cg.sh /src /out check`),
not a CTest test. The streamed plans (`mono-stream`, `stereo-stream`: a 4,096-frame head in memory, the rest and
the loop from the core's stream rings, filled between blocks outside the measured window) are held to the same
per-voice gates: they measure what reading streamed audio costs the audio thread. Last run (0.9.0, the 8-tap kernel):
every gate passes (mono 9,272, stereo 12,166, streamed mono 9,304, streamed stereo 12,195, rest 174, shedding 1,751 and
1,537 a voice for 16 and 32 voices). (0.7.0 code before its
kernel sharing for type 10): every gate passes (mono 10,746, stereo 15,220, streamed mono 10,791, streamed stereo 15,259,
rest 160, shedding 1,989 and 1,747 a voice for 16 and 32 voices). (0.6.0, 9fcec60: mono 10,734, stereo 15,222, streamed
mono 10,778, streamed stereo 15,261, rest 193, shedding 1,907 and 1,704.) (0.5.0: 10,595, 15,065, 193, 1,904 and 1,688; 0.4.0: 10,557, 15,027, 192,
1,868 and 1,667.)

The per-element level parameters (0.9.16) add 16 instructions a voice to the gate's mono case (9,288 against 9,272) and
nothing to rest (174). The element voice pool stays at 128 a plan: at 256 the gate's instructions are unchanged (mono 9,214,
stereo 12,176 a voice, rest 174), but `sizeof(pcm::Core)` grows from 3,936,448 to 5,724,352 bytes (776 bytes a voice in
each of the 18 pools, all constructed at once) and the cache gate breaks: first-level data read misses per voice-block,
mono at 1 / 8 / 32 / 64 voices 34.5 / 13.8 / 131.0 / 160.9 at 128 against 111.2 / 45.6 / 152.6 / 177.6 at 256, stereo 65.7 /
19.1 / 128.3 / 154.4 against 253.8 / 83.2 / 164.7 / 191.0 (the fields' rows are twice as far apart).

## Instructions per block (aarch64, NEON)

| voices | mono, 8-tap | mono, Hermite | stereo, 8-tap | stereo, Hermite |
|---|---|---|---|---|
| 0 (rest) | 174 | 174 | 174 | 174 |
| 1 | 32,638 | 25,486 | 43,193 | 36,985 |
| 4 | 43,117 | 35,797 | 54,383 | 47,839 |
| 8 | 79,928 | 65,384 | 102,462 | 89,390 |
| 16 | 153,903 | 124,911 | 199,444 | 173,316 |
| 32 | 302,455 | 244,567 | 394,454 | 342,214 |
| 64 | 598,824 | 483,144 | 783,153 | 678,689 |
| **a voice (8 to 32)** | **9,272** | 7,466 | **12,166** | 10,534 |

(0.9.0, the 8-tap bank. 0.4.0 with the 20-tap bank: 37,340 / 25,532 / 53,980 / 37,148 for one voice, 10,557 / 7,477 /
15,027 / 10,525 a voice. Other cases, a voice from 8 to 32, 8-tap: without the filter mono 7,502, stereo 8,826; with the
element EQ's peak mono 9,516, stereo 12,718; just released, types 0 / 1 / 2-3 / 4 / 5 / 6 / 7 / 10: mono 10,985 / 11,499 /
10,947 / 11,157 / 10,343 / 10,987 / 10,249 / 11,357, stereo 13,879 / 14,415 / 13,385 / 14,109 / 12,663 / 13,815 / 12,455 /
14,309: the voice-budget estimate's figures.)

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
| mono, 20-tap | 10,744 | 12,000 (+11.7 %) | 12,000 (+11.7 %) |
| stereo, 20-tap | 15,232 | 16,398 (+7.7 %) | 16,398 (+7.7 %) |

8 voices over 5 sounds: 102,028 instructions a block against 91,502 for the same 8 voices in one sound (+11.5 %; the
bench's own zeroing and summing of its 11 bus buffers, 4,836 a block measured at rest, taken off). At rest nothing of
this runs: the core returns before it looks at a bus, 174 instructions a block with 1 sound installed and the same
core path with 5 or 8 (the bench's buffers aside). The
shell adds, per process call, a pass over the 16 channels and over the sounds in use (choosing each one's bus), and
zeroes the ports the host activated; ports never activated are not touched. Gates on the same run (with the quiet
end of a release, `docs/design.md`): mono 10,744, stereo 15,232, streamed mono 10,788, streamed stereo 15,271, rest
174, shedding 1,935 and 1,721 a voice. Memory: the
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
with the sound. A context holds its own sound, so they are part of `oscairn.memory`'s context bytes, declared as the
worst case from the format's maxima (1,024 samples and regions, 32 elements): the patch, the sound, three tables'
worth (parsed, expanded, and the blob, which is no larger than a table), the compiled sound (110,512), the sample
table (records, views, stream sources and buffer pointers for every sample and region) and a 64 KiB allowance,
1,709,292 bytes. Measured as heap bytes for one more context of a held sound (macOS arm64): 443,664 for a plain
sound, 771,344 for one with waveforms, 1,069,248 and 1,101,488 for the same at the maxima.

## Data cache misses (0.9.0)

The device's audio path is bound by memory as much as by instructions: its four cores share one 512 KB second-level
cache, each has a 32 KB first-level data cache. `tools/cost/cache.sh` runs the held cases under valgrind's cache
simulator (cachegrind's, through callgrind for one dump a block) set to those caches (`--D1=32768,4,64
--LL=524288,16,64`) and reports the first-level and last-level data read misses a block and per voice-block for 1, 8,
32 and 64 voices at mixed pitches (keys 48-72, so up to 13 different kernels). **Gate** (`cache.sh SRC OUT check`, run
in CI on aarch64 beside the instruction gate): no case's misses per voice-block more than 25 % + 2 over
`tools/cost/cache-baseline.txt`. The simulator is LRU and has the cache to itself: it counts what the core's own data
does, not what other cores evict.

| voices | mono, 20-tap (0.8.0) D1 / LL | mono, 8-tap (0.9.0) D1 / LL | stereo, 20-tap D1 / LL | stereo, 8-tap D1 / LL | mono, Hermite D1 / LL |
|---|---|---|---|---|---|
| 1 | 361.5 / 1.8 | 59.9 / 1.8 | 125.0 / 3.3 | 73.6 / 3.3 | 2.8 / 1.8 |
| 8 | 241.7 / 0.3 | 23.3 / 0.3 | 267.6 / 0.6 | 31.7 / 0.6 | 8.5 / 0.3 |
| 16 | 391.3 / 0.3 | 36.2 / 0.3 | 364.9 / 0.5 | 49.8 / 0.5 | - |
| 32 | 438.6 / 0.7 | 136.5 / 0.2 | 431.1 / 1.9 | 128.8 / 0.4 | 28.4 / 0.2 |
| 64 | 451.9 / 0.7 | 163.2 / 0.1 | 434.7 / 1.4 | 161.7 / 0.2 | 87.0 / 0.1 |

(Misses per voice-block. Data reads a block, 32 voices: mono 112,365 with the 20-tap bank, 67,645 with the 8-tap.)

**Table and state bytes touched per voice per block** (computed from the layout, a 128-frame block):

| | 20-tap bank (0.8.0) | 8-tap bank (0.9.0) |
|---|---|---|
| kernel row | 160 bytes (20 taps and their 20 differences), 2-3 cache lines | 32 bytes (8 taps; the next row gives the slope), one row pair 64 bytes |
| a ratio whose fraction stays put (the root at the source's rate, an exact octave) | 160 bytes | 64 bytes |
| an exact octave down (two fractions) | 320 bytes | 128 bytes |
| any other ratio (a 44.1 kHz source at a 48 kHz host is one, at the root too) | 128 of the kernel's 257 rows: 20,480 bytes (the whole 41,120-byte kernel over two blocks) | the kernel's 65 rows: 2,080 bytes (33 lines) |
| a pitch sweep (the kernel picked anew each 16-frame control step) | up to 8 kernels' rows: 8 x 16 rows x 160 = 20,480 bytes | up to 8 x 17 rows x 32 = 4,352 bytes |
| the whole bank | 699,040 bytes, built on the heap in each instance | 42,240 bytes of constant data, one copy for every process that maps the binary |
| kernels the cost cases touch at 16 / 32 voices | 5 / 10: 205,600 / 411,200 bytes | 4 / 13: 8,448 / 27,456 bytes |
| the voice's vector fields (59 floats), position, step, ramp, kernel offset | 264 bytes | 264 bytes |
| its scalar side (read at the control steps) | up to 512 bytes | up to 512 bytes |
| sample frames read (16-bit, about 128 x the step + the taps) | mono about 275 bytes at a 48 kHz host's root, stereo 550 | mono about 250, stereo 500 |
| **a voice at an ordinary ratio, mono** | **about 21.5 KB** | **about 3.1 KB** |

**Device, before** (0.8.0, `pcbench-time-neon` on the target, 4 x Cortex-A53, one 512 KB second-level cache shared by
the four cores; per-voice slope from 8 to 32 held voices, microseconds per 128-frame block; the instrument's own engine at
rest / playing / stopped):

| case | at rest | playing | stopped |
|---|---|---|---|
| mono, 20-tap | 24.9 | 31.3 | 14.0 |
| stereo, 20-tap | 26.2 | 31.3 | 17.5 |
| mono, Hermite | 4.9 | 5.0 | 4.5 |
| stereo, Hermite | 7.4 | 7.6 | 6.7 |

The first voice costs about 36 us; with the engine stopped 16 voices take 184 us and 32 take 420 us; hosted, one note
of a pad (two mono regions) about 67 us. Against callgrind (10.6 k instructions a mono voice, about 11 us if every load
hit) the 20-tap voice ran at 1.3 to 3 times its instruction time while Hermite stayed within its own: the bank missed in cache
on nearly every voice. Why: a voice at any ordinary ratio (every note of a 44.1 kHz sample at a 48 kHz host) reads 128
rows of 160 bytes a block from its 41 KB kernel and the rest of it the next block, so even one voice cycles more than
the 32 KB first-level cache through it (the simulator's 362 misses a block for one voice); voices at different pitches
use different kernels, and the cost cases' 32 voices touch 10 of them, 411 KB, which with the samples (120 KB mono,
240 KB stereo) and the pools no longer fits the 512 KB second-level cache the four cores share. The simulator, LRU and
alone in the cache, shows few last-level misses even there (0.7 a voice-block): the step between 16 and 32 voices on the
device (5 kernels, 206 KB, against 10) is that shared cache's eviction (inferred, not simulated). The first voice's 36 us
is mostly instructions: a lane computes four voices' slots whether or not they hold voices (37,340 instructions for one
voice against 47,993 for four), plus its cold misses.

**Device, after** (0.9.0; to be filled from the target with the binaries of this round, timed the same way):

| case | at rest | playing | stopped |
|---|---|---|---|
| mono, 8-tap | | | |
| stereo, 8-tap | | | |
| mono, Hermite | | | |
| stereo, Hermite | | | |
| first voice; 16 / 32 voices stopped; one pad note hosted | | | |

Expected from the counts (not measured): 9,272 and 12,166 instructions a mono and stereo voice-block, at the 0.1.0
core's measured 0.99 and 0.94 ns an instruction about 9 and 11 us a voice, with the table no longer the cause of misses
(2 KB a voice, 27 KB for the cost cases' 13 kernels).

## One or two voices in a lane (0.9.0)

A lane computes four voice slots in vector registers whether or not they hold voices, so one voice costs most of a
lane. Instructions and first-level data read misses a block, valgrind with the device's caches (as `cache.sh`),
`PC_SOUNDS=K pcbench cg held N x K` (N notes in each of K sounds, each sound on its own bus; the bench's zeroing and
summing of its bus buffers, about 4,800 a block at rest, included):

| case | mono, 8-tap | mono, Hermite | stereo, 8-tap | stereo, Hermite |
|---|---|---|---|---|
| 1 voice | 32,645; 60.0 | 25,493; 3.0 | 43,200; 80.8 | 36,992; 52.4 |
| 2 voices, one sound | 36,157; 80.6 | 28,949; 35.8 | 46,938; 107.6 | 40,618; 77.5 |
| 1 voice in each of 5 sounds | 81,928; 288.5 | 68,024; 123.9 | 103,892; 461.5 | 91,172; 275.4 |
| 2 in each of 5 | 128,120; 461.8 | 107,144; 230.4 | 161,747; 705.8 | 142,355; 442.6 |
| 1 in each of 8 | 95,152; 389.8 | 81,056; 197.7 | 117,814; 545.1 | 104,710; 342.0 |
| 2 in each of 8 | 178,989; 1,134.4 | 150,877; 379.1 | 224,818; 1,389.7 | 198,626; 744.9 |

One mono voice costs 32,645 instructions, 3.5 times the per-voice slope (9,272); a second voice in the same lane adds
3,512 (its control step and scalar side). Of the single voice's cost the kernel's taps for the lane's four slots are
about 7,150 more than Hermite's lane; the rest is the lane's vector work (window feed, envelope, filter, mix), the same
for one voice or four, and 174 of fixed work a block. Its misses are not the problem: 60 a block against a slope of 174
a voice. Skipping the empty slots' taps (bit-identical output) saved 2,232 instructions for one voice but cost 1,000 to
13,000 more for 2 to 16 voices (the compiler no longer unrolls the slot loop), so it is not used. A gate of one voice at
most 1.5 times the slope (13,908 mono) is not met and not set; reaching it needs the lane's vector work done at width
1 or 2 for a lane with one or two voices, where a scalar filter takes about as many instructions as the vector
one (by operation count, not measured): not done. `pcbench time` times these cases on the device.

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
