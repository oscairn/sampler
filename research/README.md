# Research tools

Research executables, not part of the plugin build. Both compile against a separate liquidsfz
checkout. The findings are in Oscairn's
[sample players and formats](https://github.com/oscairn/oscairn/blob/main/docs/sample-players-and-formats.md).

```sh
git clone https://github.com/swesterfeld/liquidsfz.git /tmp/oscairn-liquidsfz
git -C /tmp/oscairn-liquidsfz checkout 4cc901d18b316cd377276f7eb6b8c325329b9860
```

**`liquidsfz_probe.cpp`**: whether liquidsfz shares sample data between instances, with its own
generated two-second mono sample. From this repository's root:

```sh
c++ -std=c++17 -O2 -pthread -I /tmp/oscairn-liquidsfz/lib \
  $(pkg-config --cflags sndfile) research/liquidsfz_probe.cpp \
  /tmp/oscairn-liquidsfz/lib/*.cc $(pkg-config --libs sndfile) -o /tmp/oscairn-liquidsfz-probe
/tmp/oscairn-liquidsfz-probe /tmp/oscairn-liquidsfz-probe-assets
```

**`liquidsfz_compare.cpp`**: renders the same one-region patches through liquidsfz and the built
Sampler and compares them. After `cmake -B build && cmake --build build`:

```sh
c++ -std=c++17 -O2 -pthread -I third_party/clap/include -I third_party/oscairn-clap/include \
  -I /tmp/oscairn-liquidsfz/lib $(pkg-config --cflags sndfile) research/liquidsfz_compare.cpp \
  /tmp/oscairn-liquidsfz/lib/*.cc $(pkg-config --libs sndfile) -o /tmp/oscairn-liquidsfz-compare
/tmp/oscairn-liquidsfz-compare build/oscairn-sampler.clap /tmp/oscairn-liquidsfz-compare-out
```
