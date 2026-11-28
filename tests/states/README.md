# Saved test states

What each published version of the Sampler saved, kept so that the current build can be tested against it
(`tests/test_saved_states.cpp`, in the `abi` test). One folder a version, 0.9.5 to 0.9.17 (0.9.16 was not published).
Their saved format is the sound-contexts snapshot the host stores for a sound: `oscairn-sampler-contexts/3` from 0.9.5 to
0.9.15, `oscairn-sampler-contexts/4` from 0.9.17. The current build reads both (and the older plain `oscairn-sampler-state`
forms 1 to 4; it refuses contexts/2 and /1). The plain CLAP state files are not kept here.

Each folder holds three files, all with the published Init sound (the one in the `org.oscairn.sampler.init` package of the
same version):

- `default.state`: every parameter at its default.
- `params-1.state`: level 0.6, pan -0.4, polyphony limit 12, knob 1 0.25, coarse tune -300, fine tune 20.
- `params-2.state`: level 0.9, pan 0.5, polyphony limit 5, ribbon 0.7, switch 1 on, cutoff 100, AEG release 10.

`init.owf` is the Init sound's waveform file (92 bytes: a key map naming one sample, no audio). The sample itself is not
kept: the test generates it with `tools/release/init_library.py` and checks it has the id the waveform names.

## How they were made

For each version, the published macOS arm64 build was taken from that version's archive in the package repository (the
archive's sha256 checked against its record) and extracted to a temporary folder, with the Init library archive of the same
version. Then, with the host's `oscairn-plugin-check`:

    oscairn-plugin-check <that version's .clap> org.oscairn.sampler synth [--param ID=VALUE ...] \
        --library <Init library archive> --sound <Init sound id> --keep-archive <out.olib>

and the Performance's `nodes/node-1/state/state.clap` in the kept archive is the file. The check passes on the default
run. On the runs with `--param` its one failing line is "the store says the open Performance plays it (used by 1)": the
edited Performance no longer shares its saved state with the library sound. The state it saved is what the files hold, and
the check's other lines (parameters as saved, plays there) pass.

## What the test checks

For every file in every folder: the current build opens it; each parameter set above reads back; and it renders the same,
within 1e-6, as the same file from 0.9.17 and as the current build's own save of it (a note played through the tests'
host). The tests render the Init sample generated as above, so this holds the loader and the parameters, not the sound of
the old builds.
