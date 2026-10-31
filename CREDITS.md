# Credits

- **CLAP:** the CLAP contributors. The vendored API headers in `third_party/clap` keep their
  [MIT licence](third_party/clap/LICENSE) and [provenance](third_party/clap/upstream.json).
- **Oscairn plugin SDK:** the extension headers and sound-contexts helper in
  `third_party/oscairn-clap`, MIT ([licence](third_party/oscairn-clap/LICENSE),
  [provenance](third_party/oscairn-clap/upstream.json)).
- **libsndfile:** Erik de Castro Lopo and the libsndfile contributors
  ([project](https://libsndfile.github.io/libsndfile/), LGPL-2.1-or-later). Linked dynamically to
  decode WAV samples; an external build dependency, not vendored. Retain its notices in any
  distributed binary or image.
- **liquidsfz:** Stefan Westerfeld ([project](https://github.com/swesterfeld/liquidsfz),
  LGPL-2.1-or-later). The release curve and default level follow its behaviour, measured with the
  tools in `research/`, which build against a separate checkout of it; no liquidsfz code is in
  the Sampler.
