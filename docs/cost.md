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
not a CTest test. Last run (0.2.0): every gate passes (mono 10,657, stereo 14,829, rest 195, shedding 1,848 and
1,657 a voice for 16 and 32 voices).

## Instructions per block (aarch64, NEON)

| voices | mono, 20-tap | mono, Hermite | stereo, 20-tap | stereo, Hermite |
|---|---|---|---|---|
| 0 (rest) | 195 | 195 | 195 | 195 |
| 1 | 37,003 | 24,515 | 52,411 | 35,451 |
| 4 | 47,992 | 34,856 | 64,405 | 46,245 |
| 8 | 90,382 | 64,230 | 123,218 | 86,874 |
| 16 | 175,463 | 123,279 | 241,552 | 168,840 |
| 32 | 346,143 | 241,895 | 479,115 | 333,667 |
| 64 | 686,871 | 478,495 | 953,109 | 662,189 |
| **a voice (8 to 32)** | **10,657** | 7,403 | **14,829** | 10,283 |

(0.1.0: 10,639 / 7,339 / 14,549 / 10,219 a voice. The 20-tap kernel now comes from a bank of 17 by
the voice's transposition, which adds a per-voice table offset to each fetch; the bank is 700 KB.)
Other cases, a voice from 8 to 32, 20-tap: without the filter mono 8,993, stereo 11,441; just released
(the cutoff redesigned every control step) type 0 mono 12,224, stereo 16,396; type 4 mono 12,388,
stereo 16,850; type 5 mono 11,510, stereo 15,442. Per block the counts hardly vary (the largest block
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
one of its four voices needs it). The pads' element with a peak, a voice from 8 to 32: mono 10,955, stereo 15,633
instructions a block (the gates: 11,000 and 19,500); without the EQ the gated cases are mono 10,677 and stereo 15,147
(the lane's check). Memory: nine more per-voice fields (the biquad's five coefficients and four states), 36,864 bytes
in the core's eight pools (9 x 4 bytes x 128 voices x 8).

## Shedding

`Core::shed(n)` fades the n quietest element voices out over 16 frames (one control step) and frees
them as they reach silence. Shedding 16 and 32 voices cost 1,848 and 1,657 instructions a shed voice (0.2.0), about 1/8 of a voice-block
plus the scan for the quietest. The shell's own give-ups (the voice budget's order) fade over the
SDK's 3 ms choke instead, as in the analog engine.

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
