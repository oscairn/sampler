# Credits and source provenance

Oscairn Sampler is under AGPL-3.0-or-later (`LICENSE`). It contains or derives from:

| what | where | licence |
|---|---|---|
| CLAP headers | `third_party/clap` | MIT |
| Oscairn SDK headers | `third_party/oscairn-clap` | MIT |
| dr_flac, dr_wav | `third_party/dr_libs` | public domain or MIT No Attribution |
| The sample-engine fork of sfizz: its measured element laws and envelope tables, and the element voice code `core/core.cpp` re-implements | `core/laws.h`, `core/laws.cpp`, `core/tables.h`, `core/tables.cpp`; parts of `core/core.cpp` | BSD-2-Clause |

Details and upstream sources: `third_party/NOTICES.md` and each `third_party/*/upstream.json`.
Existing copyright and license notices in individual files remain authoritative.
