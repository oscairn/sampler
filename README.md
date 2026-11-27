# Oscairn Sampler

A sample-playback instrument for [Oscairn](https://github.com/oscairn/oscairn), as a CLAP plugin
for macOS, Linux and Windows (all built by CI). Version 0.9.16, a preview: the plugin is shown as
"Oscairn Sampler" and its plugin id is `org.oscairn.sampler`.

Licence: AGPL-3.0-or-later (`LICENSE`); third-party material keeps its own licence (`third_party/NOTICES.md`).

Status: the DSP core (`core/`, a plain C++ library) plays 16-bit PCM samples (mono and stereo, any
source rate, forward loops; each region keeps its head in memory and the rest streams from sample
packs), an 8-tap table interpolator, filter types 0 to 7, 10 and thru, the amplitude, filter and pitch
envelopes, the element and part LFOs, velocity, key and level laws, pan and level, sample-accurate
note starts, voice ends and shedding. "The reference" in these docs is the sampler engine this
project is measured against; its parameters are the sound's raw values (`docs/sound-format.md`), and
what the core does not cover is reported, not approximated (`docs/coverage.md`, which marks
parameters as yes, partial or no). How it compares with the reference: `docs/acceptance.md`; cost:
`docs/cost.md`; design and the interpolation study: `docs/design.md`.
The shell does CLAP, parameters, state, sample assets (`oscairn.assets`, listed through
`oscairn.asset-dependencies`) and the Oscairn instrument contract (`oscairn.sound-contexts`,
`oscairn.voice-budget`, `oscairn.memory`), using the Oscairn SDK's helpers.

A sound's patch text (sound contexts' `prepare`): an optional `shell` line of the shell's values by their
keys (`level= pan= polyphony= knob1= ... cutoff=`; `docs/parameters.md` lists every parameter and its key),
then the sound in its text form or as `oscairn-sampler-blob/0 <base64>`; sample references are paths
relative to the base asset reference `prepare` is given, resolved through the host.

## Build and test

```
git submodule update --init       # third_party/samplepack
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

This builds `build/oscairn-sampler.clap`, `build/pcm_tests`, `build/pcmcore_tests` (the core's own
tests, linked directly) and `build/pcbench` (bench, render, noise test, coverage; render and coverage
read sounds of format v0 or v1 with their packs and waveforms from a store folder: `tools/pcbench.cpp`). The instruction-cost gate is `tools/cost/cg.sh` (callgrind, aarch64;
`docs/cost.md`). The tests load the built `.clap` and drive it only through the CLAP ABI and the
Oscairn extension structs (`tests/host.h`); they link no plugin code, and their samples come from a
"test" asset library the harness writes. Heap allocations inside `process()` are counted by
interposing the C allocator in the test process.

Device voice caps, read when an instance is created: `OSCAIRN_SAMPLER_MAX_VOICES` (1 to 64, the
instance) and `OSCAIRN_SAMPLER_MAX_VOICES_PER_SOUND` (1 to 64, each sound). Anything else is no cap.

Third-party code and its licences: `third_party/NOTICES.md` (the reference's measured laws are
carried over from the sample-engine fork under its BSD-2-Clause licence).

This project is independent and is not affiliated with or endorsed by any instrument manufacturer.
It contains and distributes no manufacturer samples, presets, firmware or code. Product names, where
they appear at all, belong to their owners.

Release: `tools/release/` (see `release.conf`); a tag `oscairn-vX.Y.Z` triggers `.github/workflows/release.yml`.
