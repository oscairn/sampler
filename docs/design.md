# Design

## Layers

- `core/` is the DSP core: plain C++17, no plugin API, no OS, no threads, no files, no heap. The
  caller owns every object (`pcm::Core`, `pcm::SoundData`, `pcm::Compiled`) and each region's audio,
  which it hands in as `SampleView`s; what does not stay in memory reaches the core's fixed pool of
  `pcm::Stream` rings, handed over with atomics only. The sound format (`sound.cpp`), the coverage report
  (`notCovered`) and the reference's measured laws (`laws.cpp`, `tables.cpp`, carried over from the
  sample-engine fork under its BSD licence) live here too.
- `src/` is the shell: CLAP, parameters, state and snapshot, samples from Oscairn Sample Packs
  (`packs.cpp`, `loader.cpp`: resolved through the host's `oscairn.assets`, read with the samplepack
  library), the process's I/O thread (`streamer.cpp`), the Oscairn instrument contract through the
  SDK's helpers. It talks to the core through `src/engine.h` only.

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

### The quiet end of a release

A released voice (after its note-off; a note the sustain pedal holds is not released, a half damper element releasing
under the pedal included) also ends once its output gain is below -96 dBFS, whichever comes first with the envelope's
own end (-96.45 dB below where its release measures from, (e) in `docs/acceptance.md`) and a one-shot's sample end. It
is a deliberate departure from the envelope's full span, approved by the project's owner: some sounds store releases of
over 100 s per 60 dB, and a voice far below audibility would otherwise keep its cost, and its stream ring and the I/O
thread's decoding, for up to 170 s.

The output gain is a product of the voice's gain terms with its source at full scale (0 dBFS peak), not a signal
detector (`Core::quietLevel`): the amplitude envelope's level times the element's constant gain (velocity and its
part sensitivity, level scaling by key, the key bank's level, the staging, the filter gain and the EQ boost types),
its element level at the top (127) when a controller box or the part LFO moves it (otherwise its index as moved now,
the part's offset included), the note's volume expression, the sound's level, the louder side of the element's and
the sound's pans (both at 1 when the part LFO pans), the filter's resonant peak taken as (1 / damping)^2 for any filter
(two sections at the resonance's damping, the resonance at the top, 127, when a box or the part LFO moves it) and the
element EQ's two bands' boosts added. The element LFO's amplitude only lowers it and the interpolator's
overshoot is not counted. When a release starts, and again when one of these terms changes (the sound's level, the
note's volume, a moved level or resonance), the voice stores the envelope level at which the product reaches -96 dBFS;
the existing end test (after each piece) compares the envelope with it, so it costs one comparison a voice a piece and
nothing a frame. The voice ends through the usual path (NOTE_END once, its stream and slot freed). A resonant filter
ringing after its input stops is ignored by this rule, as by the envelope's. A resonance raised after a voice ended
does not bring it back (only the level boxes and the part LFO are taken at their top).

Events act between pieces on their own frame: a note starts on its frame (its envelopes from the frame
before it), a note-off enters the releases there, a choke fades in a straight line, a shed fades over
16 frames. Positions stay within 32-bit frame indices by rebasing within the loop.

## The interpolator

An 8-tap Kaiser-windowed sinc (beta 9.5), each phase row scaled so its taps sum to 1, 64 phases with linear
interpolation between a row and the next, from a bank of 20 kernels at cutoffs 1.0, 0.95, ..., 0.05 of the
source's Nyquist (`kSinc`, core/kernels.cpp, written by `pcbench kernels` from `kernelTap()`). A voice takes the
kernel its transposition at 44.1 kHz asks for (`laws::kernelCutoff`): 1.0 at and below the root (its row 0 is the
identity, so a 44.1 kHz source at its root plays unchanged), above the root the cutoff fitted to the reference at
each measured transposition (0.88 at one semitone, 0.78 at two, 0.495 at five, 0.33 at seven, 0.08 at eleven,
linear between) and from twelve semitones up 0.05, where an 8-tap kernel is all window. The kernel is picked at
each pitch change (a control step, every 16 frames) and switches there without a fade, as the 20-tap bank did: the
step is the difference of two neighbouring kernels' outputs, at most -25 dB rms against the signal on white noise and
-43 dB on a saw (the 20-tap bank's: -21 and -31 dB), computed from the kernels; deterministic, not cross-faded, and
only while the pitch moves above the root (every kernel below it is the same).

Why 8 taps: the reference behaves as one. Its measured responses (docs/acceptance.md (f)) fit this kernel within
0.06-0.35 dB rms below the root (1-12 semitones), within 0.75 dB at every measured point above it (1-24
semitones) and 0.06 dB rms at 24 semitones down; its images below the root (the output harmonics a single-cycle
loop cannot hold, which only images fill) are about -8, -11, -19 and -32 to -35 dB at 1.0-1.1, 1.1-1.2, 1.2-1.4
and 1.4-1.6 of the source's Nyquist, and this kernel's are within 0.6 dB of them up to 1.4 and within 1.9 dB
beyond, where the 20-tap kernel left 9 to 61 dB less. Longer kernels band-limit more steeply than the reference
and fit its droop worse (the continuous kernels' best fits: 8 taps 0.2 dB rms, 12 taps 0.45, 20 taps 1.0). The noise test of run interp-2026-11-13, which once chose 20 taps (the
reference's images -74 to -99 dB on its white-noise wave 15 semitones down), contradicts every saw measurement;
its premise (that wave's source band and root at that key) was inferred, not measured, and an 8-tap
reference cannot produce it from a full-band source, so it is no longer a requirement. On that test the kernel
leaves -41 / -34 / -41 / -63 dB over 9-17 kHz (the 20-tap bank: -66 / -93 / -89 / -88).

Why 64 phases and coefficient rows: 128 phases measure the same on every row (to 0.01 dB), and a row of the
taps alone (32 bytes; the next row gives the slope) halves the bytes against storing differences. The bank
is 20 x 2,112 bytes = 42,240 bytes of constant data, one copy for every instance and every process that maps
the binary (the 20-tap bank was 699,040 bytes built on the heap in each instance). A voice touches at most
its kernel's 65 rows a block (2,080 bytes, 33 cache lines), 64 bytes at a ratio whose fraction does not move
(an exact octave, the root at the source's rate), and in a pitch sweep at most the rows of the kernels its 8
control steps pick (docs/cost.md).

The cheap path: none. Hermite was tried against the same recordings: below the root it leaves 1 to 9 dB more
images than the reference (and up to 0.7 dB of droop error), above it it aliases 4 to 22 dB above the
reference's (-50 to -66 against -69 to -75 dB off the harmonics); so it is exact enough nowhere a voice can
be put before it plays, and a voice that changed interpolator mid-note would change pools (a plan holds its
interpolator). A released voice's output-gain bound (the quiet-release rule) does not help either: the
Hermite-to-kernel difference is up to the signal's own level near the source's Nyquist, so it is below the
-96 dBFS end only within a few dB of the voice's end. The kernel costs 9,272 instructions a mono voice-block
against Hermite's 7,466 (docs/cost.md) and 2 KB of table. Hermite stays the core's other interpolator
(`Core::setInterp`), used to null against the fork; the plugin plays it only when
`OSCAIRN_SAMPLER_INTERPOLATION=hermite` is set (a measurement switch).

## Memory and threads

The core is preallocated with the instance (4 pools of 128 element voices, the kernel table, the
scratch windows, the stream pool: `sizeof(pcm::Core)`). A sound's audio in memory (heads and loop
bodies, below) is loaded on the main thread and counted as asset bytes in `oscairn.memory`. The shell publishes a compiled sound per context through a triple buffer the audio
thread installs from; installing a context's new sound ends the voices of its old one, and the main
thread drops an entry's sound (and so its samples) only when it overwrites that entry, which is never
the installed one. A discarded sound (its last voice ended, no channel selecting it) goes then, not when its slot
is reused: the shell publishes no sound for the context and, after the next block has installed that, asks for a
main-thread call that takes back and empties the entry the audio thread left. Nothing on the audio thread allocates, locks or blocks (tested by interposing the
allocator).

## The voice budget

`oscairn.voice-budget`, drafts 1 and 2 (the same table for both IDs). The unit the shell gives up is a note: a core
slot starts every layer (region) the note hits and gives them up only together, so the host's limits,
`active_voices` and the n a lowered limit drops by all count notes. Draft 2's `importance` lists each sound's 16
least important notes through the SDK's `Importance` helper, from the same description the give-ups use
(`Plugin::describe`). Weight: the note's layers sounding now. Level: its loudest layer's gain at the output, the
amplitude envelope now times the layer's constant gain (velocity, element and region level, output staging), the
note's volume expression and the sound's level (`pcm::Core::output`, no pass over samples), 0 dB being a
full-scale sample. State: held (key down), pedaled (key up, the sustain pedal holding it) or released; a note below
-60 dBFS counts as released unless struck in the last 50 ms. Age: the note-on serial, and the time since the note-on
from the host's `steady_time`. A new note makes room through the SDK's `victim()`; at a block's start, past a limit
the host lowered, `Importance::next()` gives up the notes the last report listed, in its order, while they still
sound in their slots, then `victim()`'s. Either way the note fades out over the choke time (3 ms) and ends with its
NOTE_END. The device caps and the Polyphony Limit parameter are limits of the same kind.

## Streaming

A sample reference is `sample:<id>` (the host's `resolve_sample` gives the pack and the record's offset,
length and preload span) or a path naming a one-record pack, whose trailer gives them. Each pack file is
opened once per process, read-only, and shared by every instance; it is never mapped (a process whose
mappings are locked, as with `mlockall`, would hold a whole pack): its header page, trailer and index are
read once (the index 56 bytes a record on the heap), a record's preload span (header, frame table,
window) when a sound is loaded, keeping its frame table, and each streamed block's compressed bytes into
the I/O thread's one fixed buffer before decoding (`pread`; `ReadFile` at an offset on Windows). Loading
a sound resolves every reference first, hints every record's preload span in one read-ahead batch
(`posix_fadvise` WILLNEED on Linux, `fcntl` F_RDADVISE on macOS, none on Windows), reads the headers in
ascending offset order, then reads or decodes what stays in memory in ascending order (a second hint
batch covers frames decoded past the windows). Nothing opens a file per sample. A read that fails at
load fails the sound's prepare, naming the reference; a streamed block that cannot be read is a late
stream (the voice is shed with its fade, counted). What this heap holds is reported through `oscairn.memory`: the I/O
thread's decoder with its read buffer (98,304 bytes with the planar output and the decoder's scratch, one a process)
in the shared bytes; each pack's index and each record's frame table (shared by the sounds of the process that use the
same record), with the heads and loop bodies, in the asset bytes. The audio thread never waits for the I/O thread (on
the instrument it runs below the audio threads, in the time they leave): when it falls behind, voices are shed as late
streams.

What stays in memory, per region (frames from the region's start):

- a head: the record's preload window, or 4,096 frames decoded at load when the window is shorter
  (a region from a key bank whose start is not 0 always keeps 4,096 frames from that start: the window
  holds the record's first frames; its end, when not 0, bounds the region),
  with 32 zero frames before it; a region no longer than that is all head, with 1,024 zero frames
  after it (the guard the interpolator reads past an end);
- a forward loop's body, decoded whole at load, when the loop ends past the head and is at most
  16,384 frames long (64 KiB stereo, the most a record's own window may hold: a loop body never
  costs more than the largest window). A looping voice then streams the frames between its head and
  the loop once, and from its first pass on reads only memory: its stream goes back to the pool;
- regions whose record uses blocks other than 256, 512 or 1,024 frames are decoded whole
  (the host's packer writes 1,024).

What stays in memory is shared by every sound of the process that plays the same frames of the same
record (a waveform used by many sounds keeps one copy of each head and loop body); `oscairn.memory`
counts each buffer once per instance.

The rest streams. One I/O thread per process (`osc-sampler-io`, normal scheduling, never
real-time, no memory locking calls), started with the first instance and stopped with the last,
fills every streaming voice's ring of the instances attached to it. A ring is one `pcm::Stream` of
the core's fixed pool: 128 rings of 4,096 frames (16 KiB, stereo-sized), 2 MiB an instance. A voice
takes a free ring at note-on (none free: the layer does not start, counted) and gives it back when
it ends or no longer reads it. The hand-over, one producer and one consumer per ring, lock-free:

- state Free -> Active (audio thread, after setting the source), Active -> Released (audio thread),
  Released -> Free (I/O thread, after its last write); the audio thread never reuses a ring the I/O
  thread may still be writing;
- `written` (I/O thread, release after the frames) and `consumed` (audio thread, release after its
  last read below it); the I/O thread writes stream frame f only while f < consumed + 4,096, so it
  never writes what the voice may read, and the voice reads only below `written`.

The I/O thread works in units of one block (1,024 frames, the rest of a block after an odd start,
or up to a loop's end), decoded with samplepack's decoder (no CRC at play time; a block that does
not decode plays silence). Of the rings with room for their next unit it serves first the one that
runs out first in output time: frames ahead divided by the voice's current step, which the voice
publishes. It holds a mutex for one unit at a time (the audio thread never takes it; main threads
take it to attach, detach and free what streams read) and waits a millisecond when nothing has
room. While no ring is Active it sleeps with no timeout: a voice that makes a ring Active wakes it
(one fence and one atomic exchange on the audio thread, and a semaphore post, which never blocks,
only when the thread sleeps: `sem_post` on Linux, a dispatch semaphore on macOS, `ReleaseSemaphore`
on Windows). A loop too long to keep streams too: after its end the thread continues at its start, so
the ring holds the frames in play order and the voice reads them without wrapping. A one-shot's
stream ends in 1,024 zero frames.

Reading costs the audio thread almost nothing: each piece (at most 16 frames) reads one cached span
of the region's audio (head, loop body or ring) with two compares; only crossing a span's end (a
loop pass, a ring wrap, every 1,024 streamed frames, or 1,152 frames before what is handed over)
takes the slower path, which also hands back what was read and loads `written`.

Late chunks: once fewer than 1,152 frames past a voice's reads are handed over (and more are due),
the voice is shed at its next control step with a 16-frame fade, as `Core::shed` does, and counted
(`Core::streamSheds`, the plugin's `org.oscairn.sampler.streaming/draft-1` extension). 1,152 frames
cover the rest of the control step and the fade at the largest step (32): the fade never reads a
frame not handed over, the voice never waits, and its note ends as any other. The read-ahead is in
frames consumed: the ring holds 4,096 frames, a unit needs 1,024 free, the margin is 1,152, so after
the I/O thread last had room a voice has about 1,900 frames of slack: 40 ms at 48 kHz at its own
pitch, 10 ms two octaves up, 5 ms three octaves up (the design's limit: one unit of every ring
within 5 ms). Voices that play past that are shed, not glitched.
