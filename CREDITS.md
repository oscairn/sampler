# Credits

- **CLAP:** the CLAP contributors. The vendored API headers in `third_party/clap` keep their
  [MIT licence](third_party/clap/LICENSE) and [provenance](third_party/clap/upstream.json).
- **Oscairn plugin SDK:** the extension headers and sound-contexts helper in
  `third_party/oscairn-clap`, MIT ([licence](third_party/oscairn-clap/LICENSE),
  [provenance](third_party/oscairn-clap/upstream.json)).
- **dr_wav:** David Reid ([project](https://github.com/mackron/dr_libs)), public domain
  (Unlicense) or MIT No Attribution. `third_party/dr_wav/dr_wav.h`, unchanged, decodes WAV
  samples and is compiled into the plugin ([provenance](third_party/dr_wav/upstream.json)).
  The research tools in `research/` still build against a separately installed libsndfile
  (LGPL-2.1-or-later); the plugin does not use it.
- **liquidsfz:** Stefan Westerfeld ([project](https://github.com/swesterfeld/liquidsfz),
  LGPL-2.1-or-later). The release curve and default level follow its behaviour, measured with the
  tools in `research/`, which build against a separate checkout of it; no liquidsfz code is in
  the Sampler.
