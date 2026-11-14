// SPDX-License-Identifier: AGPL-3.0-or-later
// The plugin's identity: the one place its name, id and version are written in source. The
// descriptor is a literal because tools/release/version.py reads the version out of it.
// (Also named in CMakeLists.txt, PCM_NAME, and tools/release/release.conf.)
#pragma once
#include <clap/clap.h>

namespace pcmshell {
inline const char* const kFeatures[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SAMPLER,
                                        CLAP_PLUGIN_FEATURE_STEREO, nullptr};
inline const clap_plugin_descriptor_t kDescriptor = {
    CLAP_VERSION, "org.oscairn.pcm", "Oscairn Sampler (preview)", "Oscairn", "https://github.com/oscairn/sampler",
    "", "", "0.5.0", "Sample-playback instrument", kFeatures};

// Device voice caps, read when an instance is created (see plugin.cpp).
inline constexpr const char* kEnvMaxVoices = "OSCAIRN_PCM_MAX_VOICES";               // 1..64
inline constexpr const char* kEnvMaxVoicesPerSound = "OSCAIRN_PCM_MAX_VOICES_PER_SOUND"; // 1..64
// For comparisons with the sample-engine fork only: "hermite" plays Hermite interpolation (its own).
inline constexpr const char* kEnvInterpolation = "OSCAIRN_PCM_INTERPOLATION";
// For comparisons only: "1" decodes every region whole when a sound loads (nothing streams).
inline constexpr const char* kEnvDecodeWhole = "OSCAIRN_PCM_DECODE_WHOLE";
// The plugin's own extension (struct in plugin.cpp): element voices shed for late streams (Core::streamSheds), and
// test hooks that stall the I/O thread and wait until it has filled every ring.
inline constexpr const char* kExtStreaming = "org.oscairn.pcm.streaming/draft-1";
}  // namespace pcmshell
