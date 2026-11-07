# Oscairn Legacy Sampler

**Deprecated.** Oscairn's sample engine is now the sfizz-based Oscairn Sampler
(`org.oscairn.sampler`). This plugin was the Oscairn Sampler until 2026-11-06; it is kept as
the Oscairn Legacy Sampler, CLAP id `org.oscairn.legacy-sampler`, bundle
`oscairn-legacy-sampler.clap`, so the two can be installed together. Performances saved with
the old id do not find it.

A CLAP instrument for [Oscairn](https://github.com/oscairn/oscairn): it plays SFZ patches over
WAV samples, as native sound contexts, so up to eight prepared sounds play in one instance, each
MIDI channel choosing one. It is growing into an engine that can do everything a Yamaha AWM2
Part can ([Oscairn RFC 0013](https://github.com/oscairn/oscairn/blob/main/docs/rfcs/0013-awm2-class-sample-engine.md)),
specified from Yamaha's documentation and black-box measurements, with no Yamaha code or audio.

It moved here from the Oscairn repository on 2026-10-31, with its history.

## What it plays

Each prepared sound is an SFZ patch in the `oscairn-sfz-baseline-0` profile of
[Oscairn RFC 0008](https://github.com/oscairn/oscairn/blob/main/docs/rfcs/0008-sfz-native-samples-and-conversion.md#baseline-profile-oscairn-sfz-baseline-0)
(one `<region>`; `sample`, key/velocity ranges, `pitch_keycenter`, `pitch_keytrack`,
`transpose`, `tune`, `volume`, `ampeg_release`). Anything else is rejected with its line number.
Samples are 16/24/32-bit PCM or 32-bit float WAV, mono or stereo, 8–192 kHz, at most 64 MiB
decoded each and 256 MiB per instance. Contexts with identical audio share one decoded copy,
identified by content rather than file name.

Playback uses linear interpolation; the sample end finishes the voice (`no_loop`); release falls
exponentially to −60 dB over `ampeg_release` and then stops, the curve liquidsfz uses. Velocity
gain is squared (the SFZ `amp_veltrack=100` default). The overall level matches liquidsfz's
defaults: MIDI volume 100 on a squared curve and centre panning at 1/√2, together −7.2 dB;
CC7/CC10 changes are not implemented yet. Mono samples play on both outputs. Pressure is tracked
per voice but not mapped to sound. The single stereo output is unsaturated.

Samples are dependencies of a saved Performance, not part of it
([Oscairn RFC 0009](https://github.com/oscairn/oscairn/blob/main/docs/rfcs/0009-sound-assets-and-dependencies.md)):
the Sampler asks the host for every file through `oscairn.assets` and never searches the file
system itself. Its `oscairn-sampler-2` snapshot stores, per sound, the library, the patch's
folder there, a normalized SFZ and the sample's SHA-256, and `oscairn.asset-dependencies` lists
them.

## Building

Needs CMake 3.21 and a C++20 compiler. Nothing else: WAV decoding is dr_wav, vendored in
`third_party/`, so the plugin links only the system libraries.

```sh
cmake -B build && cmake --build build   # build/oscairn-legacy-sampler.clap
```

The CLAP headers and the Oscairn plugin SDK (the extension headers and the sound-contexts helper,
both MIT) are vendored in `third_party/`, each with the commit it came from. Oscairn builds the
Sampler itself, fetching this repository at a pinned commit, and then uses its own copy of those
headers.

## Tests

The Sampler's tests drive it through Oscairn's host (sound contexts, prepared sounds, assets,
saving and reopening, worker processes), so they live in the Oscairn repository and run there
against the pinned commit. This repository's CI builds Oscairn with this checkout in place of
the pinned one and runs them, so a change here is tested before Oscairn moves its pin.
To run them locally from an Oscairn checkout:

```sh
cmake -S path/to/oscairn -B build -DFETCHCONTENT_SOURCE_DIR_OSCAIRN_SAMPLER=$PWD
cmake --build build && ctest --test-dir build -R "sampler|worker-recovery"
```

`research/` holds the liquidsfz comparison tools; see [research/README.md](research/README.md).

## Licence

AGPL-3.0-or-later ([LICENSE](LICENSE)). Oscairn loads it through CLAP, as a separate work. The
vendored CLAP and SDK headers in `third_party/` are MIT; the vendored dr_wav is public domain or
MIT-0. See [CREDITS](CREDITS.md).
