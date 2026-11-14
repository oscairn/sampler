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
    CLAP_VERSION, "org.oscairn.pcm", "Oscairn Sampler", "Oscairn", "https://github.com/oscairn/sampler",
    "", "", "0.2.0", "Sample-playback instrument", kFeatures};

// Device voice caps, read when an instance is created (see plugin.cpp).
inline constexpr const char* kEnvMaxVoices = "OSCAIRN_PCM_MAX_VOICES";               // 1..64
inline constexpr const char* kEnvMaxVoicesPerSound = "OSCAIRN_PCM_MAX_VOICES_PER_SOUND"; // 1..64
// For comparisons with the sample-engine fork only: "hermite" plays Hermite interpolation (its own).
inline constexpr const char* kEnvInterpolation = "OSCAIRN_PCM_INTERPOLATION";
}  // namespace pcmshell
