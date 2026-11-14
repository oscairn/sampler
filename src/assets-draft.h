// SPDX-License-Identifier: MIT
// DRAFT COPY of oscairn.assets draft 4 as the oscairn-clap header declares it (the same table, location and text), to
// be replaced by the vendored header when the submodule moves to it. Nothing else in the plugin declares it.
#pragma once
#include <oscairn-clap/assets.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Draft 4: items kept by content and named by their id rather than files. A reference whose path is "sample:<id>" or
 * "waveform:<id>" (64 lowercase hex digits; library empty; sha256 empty or that id) names a sample in an Oscairn
 * Sample Pack or a waveform. resolve answers a waveform reference with the path of its .owf file, and a sample
 * reference with its pack's path. resolve_sample answers a sample reference with where its record is: the pack's
 * path, read-only, and in location the record's offset, length and preload span. Both stay valid while the plugin
 * holds the sound prepared. On failure it returns false with a NUL-terminated reason in message. A host answers
 * drafts 1 to 3 with the same table. */
static const char OSCAIRN_EXT_ASSETS_DRAFT_4[] = "oscairn.assets/draft-4";
typedef struct oscairn_sample_location {
    uint64_t offset;  /* the record's offset in the pack, bytes */
    uint64_t length;  /* the record's length, bytes */
    uint64_t preload; /* bytes from the record's start to the end of its preload window: read these first */
} oscairn_sample_location_t;
typedef struct oscairn_host_assets_draft4 {
    bool (CLAP_ABI *resolve)(const clap_host_t*, oscairn_asset_ref_t* ref, char* path, uint32_t path_capacity,
                             char* message, uint32_t message_capacity);
    bool (CLAP_ABI *library_root)(const void* owner, const char* library_id, const char* min_version, char* path, uint32_t path_capacity); /* draft 2 */
    bool (CLAP_ABI *create)(const clap_host_t*, const char* file, oscairn_asset_ref_t* ref, char* message, uint32_t message_capacity); /* draft 3 */
    bool (CLAP_ABI *resolve_sample)(const clap_host_t*, const oscairn_asset_ref_t* ref, char* path, uint32_t path_capacity,
                                    oscairn_sample_location_t* location, char* message, uint32_t message_capacity); /* draft 4 */
} oscairn_host_assets_draft4_t;

#ifdef __cplusplus
}
#endif
