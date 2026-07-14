/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "shared/shared.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WORR_REWIND_COLLISION_IMPORT_V1 \
    "WORR_REWIND_COLLISION_IMPORT_V1"
#define WORR_REWIND_COLLISION_API_VERSION 1u
#define WORR_REWIND_COLLISION_SCHEMA_VERSION 1u

typedef enum worr_rewind_collision_asset_kind_v1_e {
    WORR_REWIND_COLLISION_ASSET_INVALID = 0,
    WORR_REWIND_COLLISION_ASSET_INLINE_BRUSH = 1,
} worr_rewind_collision_asset_kind_v1;

/*
 * Opaque, map-local immutable collision identity.  The asset id is meaningful
 * only when paired with its server-owned map epoch and asset hash.  This value
 * is process-local and must never be serialized as a legacy protocol field.
 */
typedef struct worr_rewind_collision_asset_handle_v1_s {
    uint32_t map_epoch;
    uint32_t asset_id;
    uint64_t asset_hash;
} worr_rewind_collision_asset_handle_v1;

typedef struct worr_rewind_collision_map_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t map_epoch;
    uint32_t map_checksum;
    uint32_t inline_model_count;
    uint32_t reserved0;
} worr_rewind_collision_map_v1;

/* Pointer-free description copied from one immutable BSP inline model. */
typedef struct worr_rewind_collision_asset_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    worr_rewind_collision_asset_handle_v1 handle;
    uint32_t kind;
    uint32_t source_model_index;
    uint32_t map_checksum;
    uint32_t reserved0;
    float local_mins[3];
    float local_maxs[3];
} worr_rewind_collision_asset_v1;

/*
 * One exact collision query against an immutable asset at an explicit
 * transform.  Ray queries use zero mins/maxs.  No live edict participates in
 * this request.
 */
typedef struct worr_rewind_collision_trace_request_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    worr_rewind_collision_asset_handle_v1 asset;
    uint32_t contents_mask;
    uint32_t reserved0;
    float start[3];
    float end[3];
    float mins[3];
    float maxs[3];
    float origin[3];
    float angles[3];
} worr_rewind_collision_trace_request_v1;

/*
 * Optional game-import extension.  Returned trace surfaces remain owned by
 * the loaded engine collision map.  TraceTransformed always returns ent ==
 * NULL; sgame may attach a generation-validated live entity after the query.
 * Failed callbacks leave caller-owned output byte-identical.
 */
typedef struct worr_rewind_collision_import_v1_s {
    uint32_t struct_size;
    uint32_t api_version;
    bool (q_gameabi *GetMapIdentity)(worr_rewind_collision_map_v1 *map_out);
    bool (q_gameabi *ResolveInlineBrush)(
        uint32_t expected_map_epoch, uint32_t game_model_index,
        worr_rewind_collision_asset_v1 *asset_out);
    bool (q_gameabi *TraceTransformed)(
        const worr_rewind_collision_trace_request_v1 *request,
        trace_t *trace_out);
} worr_rewind_collision_import_v1;

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_REWIND_COLLISION_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define WORR_REWIND_COLLISION_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

WORR_REWIND_COLLISION_STATIC_ASSERT(
    sizeof(worr_rewind_collision_asset_handle_v1) == 16,
    "rewind collision asset handle v1 layout changed");
WORR_REWIND_COLLISION_STATIC_ASSERT(
    offsetof(worr_rewind_collision_asset_handle_v1, asset_hash) == 8,
    "rewind collision asset hash offset changed");
WORR_REWIND_COLLISION_STATIC_ASSERT(
    sizeof(worr_rewind_collision_map_v1) == 24,
    "rewind collision map v1 layout changed");
WORR_REWIND_COLLISION_STATIC_ASSERT(
    sizeof(worr_rewind_collision_asset_v1) == 64,
    "rewind collision asset v1 layout changed");
WORR_REWIND_COLLISION_STATIC_ASSERT(
    offsetof(worr_rewind_collision_asset_v1, handle) == 8,
    "rewind collision asset handle offset changed");
WORR_REWIND_COLLISION_STATIC_ASSERT(
    offsetof(worr_rewind_collision_asset_v1, local_mins) == 40,
    "rewind collision asset mins offset changed");
WORR_REWIND_COLLISION_STATIC_ASSERT(
    offsetof(worr_rewind_collision_asset_v1, local_maxs) == 52,
    "rewind collision asset maxs offset changed");
WORR_REWIND_COLLISION_STATIC_ASSERT(
    sizeof(worr_rewind_collision_trace_request_v1) == 104,
    "rewind collision trace request v1 layout changed");
WORR_REWIND_COLLISION_STATIC_ASSERT(
    offsetof(worr_rewind_collision_trace_request_v1, asset) == 8,
    "rewind collision trace asset offset changed");
WORR_REWIND_COLLISION_STATIC_ASSERT(
    offsetof(worr_rewind_collision_trace_request_v1, start) == 32,
    "rewind collision trace start offset changed");
WORR_REWIND_COLLISION_STATIC_ASSERT(
    offsetof(worr_rewind_collision_trace_request_v1, angles) == 92,
    "rewind collision trace angles offset changed");
WORR_REWIND_COLLISION_STATIC_ASSERT(
    offsetof(worr_rewind_collision_import_v1, GetMapIdentity) == 8,
    "rewind collision map callback offset changed");
WORR_REWIND_COLLISION_STATIC_ASSERT(
    offsetof(worr_rewind_collision_import_v1, ResolveInlineBrush) ==
        offsetof(worr_rewind_collision_import_v1, GetMapIdentity) +
            sizeof(((worr_rewind_collision_import_v1 *)0)->GetMapIdentity),
    "rewind collision resolver callback offset changed");
WORR_REWIND_COLLISION_STATIC_ASSERT(
    offsetof(worr_rewind_collision_import_v1, TraceTransformed) ==
        offsetof(worr_rewind_collision_import_v1, ResolveInlineBrush) +
            sizeof(((worr_rewind_collision_import_v1 *)0)->ResolveInlineBrush),
    "rewind collision trace callback offset changed");
WORR_REWIND_COLLISION_STATIC_ASSERT(
    sizeof(worr_rewind_collision_import_v1) ==
        offsetof(worr_rewind_collision_import_v1, TraceTransformed) +
            sizeof(((worr_rewind_collision_import_v1 *)0)->TraceTransformed),
    "rewind collision import v1 layout changed");

#undef WORR_REWIND_COLLISION_STATIC_ASSERT
