/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/snapshot_projection.h"
#include "common/net/snapshot_timeline.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WORR_CGAME_SNAPSHOT_TIMELINE_EXPORT_V1 \
    "WORR_CGAME_SNAPSHOT_TIMELINE_EXPORT_V1"
#define WORR_CGAME_SNAPSHOT_TIMELINE_API_VERSION 1u

typedef enum worr_cgame_snapshot_reset_reason_v1_e {
    WORR_CGAME_SNAPSHOT_RESET_CONNECTION = 1,
    WORR_CGAME_SNAPSHOT_RESET_MAP = 2,
    WORR_CGAME_SNAPSHOT_RESET_DEMO_SEEK = 3,
    WORR_CGAME_SNAPSHOT_RESET_HARD_RESYNC = 4,
    WORR_CGAME_SNAPSHOT_RESET_UNLOAD = 5,
} worr_cgame_snapshot_reset_reason_v1;

typedef struct worr_cgame_snapshot_timeline_status_v1_s {
    uint32_t struct_size;
    uint32_t api_version;
    uint32_t active_epoch;
    uint32_t last_result;
    uint64_t resets;
    uint64_t consume_attempts;
    uint64_t accepted;
    uint64_t rejected;
    uint64_t last_receive_time_us;
    uint64_t last_endpoint_hash;
    uint64_t last_legacy_parity_hash;
    worr_snapshot_timeline_stats_v1 timeline;
} worr_cgame_snapshot_timeline_status_v1;

/*
 * All projection pointers are borrowed for this call only.  The cgame must
 * validate and copy any retained content before returning; retaining an
 * engine pointer is an ABI violation.  The callback remains audit-only until
 * an explicit staged authority cutover.
 */
typedef struct worr_cgame_snapshot_timeline_export_v1_s {
    uint32_t struct_size;
    uint32_t api_version;
    void (*Reset)(uint32_t snapshot_epoch, uint32_t reason,
                  uint64_t host_time_us);
    bool (*ConsumeCanonicalSnapshot)(
        const worr_snapshot_projection_view_v2 *view,
        const worr_snapshot_projection_hashes_v2 *hashes,
        uint64_t receive_time_us);
    bool (*GetStatus)(worr_cgame_snapshot_timeline_status_v1 *status_out);
} worr_cgame_snapshot_timeline_export_v1;

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
static_assert(offsetof(worr_cgame_snapshot_timeline_export_v1, Reset) == 8,
              "cgame snapshot timeline export header changed");
#else
_Static_assert(offsetof(worr_cgame_snapshot_timeline_export_v1, Reset) == 8,
               "cgame snapshot timeline export header changed");
#endif
