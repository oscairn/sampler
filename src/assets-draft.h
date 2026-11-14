// SPDX-License-Identifier: MIT
// DRAFT COPY, to be replaced by the vendored oscairn-clap header once it carries oscairn.assets draft 4:
// the host's assets table with resolve_sample appended, from the pack specification's text (its "Host
// extension and sound format" section). Nothing else in the plugin declares it.
#pragma once
#include <oscairn-clap/assets.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Draft 4 adds resolve_sample: a content-addressed sample reference (path "sample:" + 64 lowercase hex digits, sha256
 * the same id) to the verified pack holding it and the byte offset of its record there, or false with a NUL-terminated
 * reason in message. Main thread only. The path and offset stay valid while the plugin holds the sound prepared. resolve
 * on a "waveform:<id>" reference writes the path of the installed .owf file, whose id the host has verified. A host
 * answers earlier drafts with the same table. */
static const char OSCAIRN_EXT_ASSETS_DRAFT_4[] = "oscairn.assets/draft-4";
typedef struct oscairn_host_assets_draft4 {
    bool (CLAP_ABI *resolve)(const clap_host_t*, oscairn_asset_ref_t* ref, char* path, uint32_t path_capacity,
                             char* message, uint32_t message_capacity);
    bool (CLAP_ABI *library_root)(const void* owner, const char* library_id, const char* min_version, char* path, uint32_t path_capacity);
    bool (CLAP_ABI *create)(const clap_host_t*, const char* file, oscairn_asset_ref_t* ref, char* message, uint32_t message_capacity);
    bool (CLAP_ABI *resolve_sample)(const clap_host_t*, const oscairn_asset_ref_t* ref, char* pack_path, uint32_t cap,
                                    uint64_t* record_offset, char* message, uint32_t message_capacity); /* draft 4 */
} oscairn_host_assets_draft4_t;

#ifdef __cplusplus
}
#endif
