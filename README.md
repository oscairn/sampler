# Oscairn Sampler

A sample-playback instrument for [Oscairn](https://github.com/oscairn/oscairn), as a CLAP plugin
for macOS and Linux. Its plugin id is still the working id `org.oscairn.pcm`.

Licence: AGPL-3.0-or-later (`LICENSE`); third-party material keeps its own licence (`third_party/NOTICES.md`).

Status (milestone 1): the DSP core (`core/`, a plain C++ library) plays the element voice of the first
probe sound (a 17-region pad with mono and stereo sources): 16-bit PCM samples held in memory (mono and
stereo, any source rate, forward loops), a 20-tap table interpolator, filter type 0 (two low-pass
sections), the amplitude, filter and pitch envelopes with the reference's measured segment shapes,
the element LFO's reference mode, velocity, key and level laws, pan and level, sample-accurate
note starts, voice ends and shedding. A sound is the reference's raw parameter values in sound
format v0 (`docs/sound-format.md`); what the core does not cover is reported, not approximated
(`docs/coverage.md`). How it compares with the sample-engine fork and the reference:
`docs/acceptance.md`; cost: `docs/cost.md`; design and the interpolation study: `docs/design.md`.
The shell does CLAP, parameters, state, sample assets (`oscairn.assets`, listed through
`oscairn.asset-dependencies`) and the Oscairn instrument contract (`oscairn.sound-contexts`,
`oscairn.voice-budget`, `oscairn.memory`), using the Oscairn SDK's helpers.

A sound's patch text (sound contexts' `prepare`): an optional `shell` line of the shell's values by their
keys (`level= pan= polyphony= knob1= ... cutoff=`; `docs/parameters.md` lists every parameter and its key),
then the sound in its text form or as `oscairn-pcm-blob/0 <base64>`; sample references are paths
relative to the base asset reference `prepare` is given, resolved through the host.

## Build and test

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

This builds `build/oscairn-pcm.clap`, `build/pcm_tests`, `build/pcmcore_tests` (the core's own
tests, linked directly) and `build/pcbench` (bench, render, noise test, coverage; render and coverage
read sounds of format v0 or v1 with their packs and waveforms from a store folder: `tools/pcbench.cpp`). The instruction-cost gate is `tools/cost/cg.sh` (callgrind, aarch64;
`docs/cost.md`). The tests load the built `.clap` and drive it only through the CLAP ABI and the
Oscairn extension structs (`tests/host.h`); they link no plugin code, and their samples come from a
"test" asset library the harness writes. Heap allocations inside `process()` are counted by
interposing the C allocator in the test process.

Device voice caps, read when an instance is created: `OSCAIRN_PCM_MAX_VOICES` (1 to 64, the
instance) and `OSCAIRN_PCM_MAX_VOICES_PER_SOUND` (1 to 64, each sound). Anything else is no cap.

Third-party code and its licences: `third_party/NOTICES.md` (the reference's measured laws are
carried over from the sample-engine fork under its BSD-2-Clause licence).

Release: `tools/release/` (see `release.conf`); a tag `oscairn-vX.Y.Z` triggers `.github/workflows/release.yml`.
