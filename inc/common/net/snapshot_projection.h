/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "shared/snapshot_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WORR_SNAPSHOT_PROJECTION_VERSION 2u

/* Runtime-only view. Pointers and store serials are never hashed. */
typedef struct worr_snapshot_projection_view_v2_s {
    uint32_t struct_size;
    uint32_t schema_version;
    const worr_snapshot_v2 *snapshot;
    const worr_snapshot_player_v2 *player;
    const worr_snapshot_entity_v2 *entities;
    const uint8_t *area_bytes;
    const worr_snapshot_event_ref_v2 *event_refs;
    uint32_t entity_count;
    uint32_t area_byte_count;
    uint32_t event_ref_count;
    uint32_t reserved0;
} worr_snapshot_projection_view_v2;

/*
 * endpoint_hash fingerprints all accepted endpoint metadata and payloads,
 * including receiver chronology, provenance and an exact consumed-command
 * cursor. legacy_parity_hash is deliberately narrower: it compares the
 * transport-visible legacy semantic payload and excludes receiver chronology,
 * server-only provenance, store serials, and the currently untransported
 * consumed-command cursor.
 */
typedef struct worr_snapshot_projection_hashes_v2_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint64_t endpoint_hash;
    uint64_t legacy_parity_hash;
    uint64_t semantic_player_hash;
    uint64_t semantic_entity_hash;
    uint64_t semantic_area_hash;
    uint64_t semantic_event_hash;
} worr_snapshot_projection_hashes_v2;

bool Worr_SnapshotProjectionHashesV2(
    const worr_snapshot_projection_view_v2 *view,
    uint32_t max_entities,
    worr_snapshot_projection_hashes_v2 *hashes_out);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
static_assert(sizeof(worr_snapshot_projection_hashes_v2) == 56,
              "snapshot projection hashes v2 layout changed");
#else
_Static_assert(sizeof(worr_snapshot_projection_hashes_v2) == 56,
               "snapshot projection hashes v2 layout changed");
#endif
