// SPDX-License-Identifier: AGPL-3.0-or-later
// Samples from Oscairn Sample Packs: the test host's packs (made by the samplepack tool from generated tones) and
// what the plugin plays from them.
#include <samplepack.h>

#include "host.h"

// The samplepack library is linked and agrees with its specification's worked sample id (docs/format.md, 20 A).
TEST(samplepack_sample_id) {
    const int16_t pcm[4] = {0, 1, -1, 32767};
    uint8_t id[SP_SAMPLE_ID_BYTES];
    CHECK_EQ(sp_sample_id_pcm(44100, 1, pcm, 4, id), SP_OK);
    char hex[65];
    for (int i = 0; i < 32; ++i) std::snprintf(hex + 2 * i, 3, "%02x", id[i]);
    CHECK(std::string(hex) == "1e272d15841dbddadd5503dec6a3ac43020b40c9c0a730cc5cd2f66101301338");
}
