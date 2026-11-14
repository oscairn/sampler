# Third-party notices

| what | where | licence | upstream |
|---|---|---|---|
| CLAP headers | `third_party/clap` | MIT (`third_party/clap/LICENSE`) | `third_party/clap/upstream.json` |
| Oscairn SDK headers | `third_party/oscairn-clap` | MIT (`third_party/oscairn-clap/LICENSE`) | `third_party/oscairn-clap/upstream.json` |
| The reference's measured element laws and envelope time tables, from the sample-engine fork of sfizz | `core/laws.h`, `core/laws.cpp`, `core/tables.h`, `core/tables.cpp` (each keeps its BSD notice); the parts of `core/core.cpp` re-implementing the fork's element voice code (its header says which) | BSD-2-Clause (`third_party/sfizz-laws/LICENSE`) | `third_party/sfizz-laws/upstream.json` |
| dr_flac, dr_wav (sample decoding in the shell and tools) | `third_party/dr_libs` | public domain or MIT No Attribution (`third_party/dr_libs/LICENSE`) | `third_party/dr_libs/upstream.json` |

Everything else in this repository is the Oscairn project's own, under AGPL-3.0-or-later (`LICENSE`).
