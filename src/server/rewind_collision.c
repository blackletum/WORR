/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "server.h"
#include "server/rewind_collision.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#define WORR_REWIND_COLLISION_FNV_OFFSET UINT64_C(14695981039346656037)
#define WORR_REWIND_COLLISION_FNV_PRIME UINT64_C(1099511628211)
#define WORR_REWIND_COLLISION_HASH_DOMAIN UINT32_C(0x314d4252) /* RBM1 */

typedef struct rewind_collision_range_s {
    uintptr_t begin;
    uintptr_t end;
} rewind_collision_range_t;

static bool range_make(const void *pointer, size_t size,
                       rewind_collision_range_t *range_out)
{
    uintptr_t begin;

    if (!pointer || !size || !range_out)
        return false;
    begin = (uintptr_t)pointer;
    if (size > UINTPTR_MAX - begin)
        return false;
    range_out->begin = begin;
    range_out->end = begin + size;
    return true;
}

static bool range_aligned(const void *pointer, size_t alignment)
{
    return pointer && alignment && ((uintptr_t)pointer % alignment) == 0;
}

static bool ranges_overlap(rewind_collision_range_t a,
                           rewind_collision_range_t b)
{
    return a.begin < b.end && b.begin < a.end;
}

static bool float_valid(float value)
{
    return isfinite((double)value);
}

static float canonical_float(float value)
{
    return value == 0.0f ? 0.0f : value;
}

static bool vector_valid(const float vector[3])
{
    return vector && float_valid(vector[0]) && float_valid(vector[1]) &&
           float_valid(vector[2]);
}

static bool bounds_valid(const float mins[3], const float maxs[3])
{
    return vector_valid(mins) && vector_valid(maxs) &&
           mins[0] <= maxs[0] && mins[1] <= maxs[1] &&
           mins[2] <= maxs[2];
}

static uint64_t hash_byte(uint64_t hash, uint8_t value)
{
    return (hash ^ value) * WORR_REWIND_COLLISION_FNV_PRIME;
}

static uint64_t hash_u32(uint64_t hash, uint32_t value)
{
    unsigned shift;

    for (shift = 0; shift < 32; shift += 8)
        hash = hash_byte(hash, (uint8_t)(value >> shift));
    return hash;
}

static uint64_t hash_float(uint64_t hash, float value)
{
    uint32_t bits;

    value = canonical_float(value);
    memcpy(&bits, &value, sizeof(bits));
    return hash_u32(hash, bits);
}

static uint64_t asset_hash(const worr_rewind_collision_asset_v1 *asset)
{
    uint64_t hash = WORR_REWIND_COLLISION_FNV_OFFSET;
    unsigned axis;

    hash = hash_u32(hash, WORR_REWIND_COLLISION_HASH_DOMAIN);
    hash = hash_u32(hash, WORR_REWIND_COLLISION_SCHEMA_VERSION);
    hash = hash_u32(hash, asset->handle.map_epoch);
    hash = hash_u32(hash, asset->handle.asset_id);
    hash = hash_u32(hash, asset->kind);
    hash = hash_u32(hash, asset->source_model_index);
    hash = hash_u32(hash, asset->map_checksum);
    for (axis = 0; axis < 3; ++axis)
        hash = hash_float(hash, asset->local_mins[axis]);
    for (axis = 0; axis < 3; ++axis)
        hash = hash_float(hash, asset->local_maxs[axis]);

    /* Zero is reserved for an invalid/uninitialized handle. */
    return hash ? hash : UINT64_MAX;
}

static const bsp_t *rewind_collision_map(void)
{
    if ((sv.state != ss_loading && sv.state != ss_game) ||
        sv.worr_snapshot_epoch == 0 || !sv.cm.cache ||
        !sv.cm.cache->models || sv.cm.cache->nummodels < 1) {
        return NULL;
    }
    return sv.cm.cache;
}

static bool source_model_index_for_asset(const bsp_t *bsp,
                                         uint32_t asset_id,
                                         uint32_t *model_index_out)
{
    uint32_t model_index;

    if (!bsp || !model_index_out || asset_id == 0 ||
        asset_id >= (uint32_t)bsp->nummodels) {
        return false;
    }
    model_index = asset_id + 1u;
    if (bsp->nummodels >= MODELINDEX_PLAYER &&
        model_index >= MODELINDEX_PLAYER) {
        ++model_index;
    }
    if (model_index == MODELINDEX_PLAYER ||
        (svs.csr.max_models != 0 && model_index >= svs.csr.max_models)) {
        return false;
    }
    *model_index_out = model_index;
    return true;
}

static bool asset_id_for_source_model(const bsp_t *bsp,
                                      uint32_t model_index,
                                      uint32_t *asset_id_out)
{
    uint32_t asset_id;

    if (!bsp || !asset_id_out || model_index <= 1u ||
        model_index == MODELINDEX_PLAYER ||
        (svs.csr.max_models != 0 && model_index >= svs.csr.max_models)) {
        return false;
    }
    asset_id = model_index - 1u;
    if (bsp->nummodels >= MODELINDEX_PLAYER &&
        asset_id >= MODELINDEX_PLAYER) {
        --asset_id;
    }
    if (asset_id == 0 || asset_id >= (uint32_t)bsp->nummodels)
        return false;
    *asset_id_out = asset_id;
    return true;
}

static bool build_asset(const bsp_t *bsp, uint32_t asset_id,
                        worr_rewind_collision_asset_v1 *asset_out)
{
    const mmodel_t *model;
    worr_rewind_collision_asset_v1 asset;
    uint32_t source_model_index;
    unsigned axis;

    if (!bsp || !asset_out ||
        !source_model_index_for_asset(bsp, asset_id,
                                      &source_model_index)) {
        return false;
    }
    model = &bsp->models[asset_id];
    if (!model->headnode || !bounds_valid(model->mins, model->maxs))
        return false;

    memset(&asset, 0, sizeof(asset));
    asset.struct_size = sizeof(asset);
    asset.schema_version = WORR_REWIND_COLLISION_SCHEMA_VERSION;
    asset.handle.map_epoch = sv.worr_snapshot_epoch;
    asset.handle.asset_id = asset_id;
    asset.kind = WORR_REWIND_COLLISION_ASSET_INLINE_BRUSH;
    asset.source_model_index = source_model_index;
    asset.map_checksum = (uint32_t)sv.cm.checksum;
    for (axis = 0; axis < 3; ++axis) {
        asset.local_mins[axis] = canonical_float(model->mins[axis]);
        asset.local_maxs[axis] = canonical_float(model->maxs[axis]);
    }
    asset.handle.asset_hash = asset_hash(&asset);
    *asset_out = asset;
    return true;
}

static bool q_gameabi get_map_identity(
    worr_rewind_collision_map_v1 *map_out)
{
    const bsp_t *bsp;
    worr_rewind_collision_map_v1 map;

    if (!range_aligned(map_out, _Alignof(worr_rewind_collision_map_v1)))
        return false;
    bsp = rewind_collision_map();
    if (!bsp)
        return false;

    memset(&map, 0, sizeof(map));
    map.struct_size = sizeof(map);
    map.schema_version = WORR_REWIND_COLLISION_SCHEMA_VERSION;
    map.map_epoch = sv.worr_snapshot_epoch;
    map.map_checksum = (uint32_t)sv.cm.checksum;
    map.inline_model_count = (uint32_t)bsp->nummodels - 1u;
    *map_out = map;
    return true;
}

static bool q_gameabi resolve_inline_brush(
    uint32_t expected_map_epoch, uint32_t game_model_index,
    worr_rewind_collision_asset_v1 *asset_out)
{
    const bsp_t *bsp;
    worr_rewind_collision_asset_v1 asset;
    uint32_t asset_id;

    if (!range_aligned(asset_out,
                       _Alignof(worr_rewind_collision_asset_v1))) {
        return false;
    }
    bsp = rewind_collision_map();
    if (!bsp || expected_map_epoch == 0 ||
        expected_map_epoch != sv.worr_snapshot_epoch ||
        !asset_id_for_source_model(bsp, game_model_index, &asset_id) ||
        !build_asset(bsp, asset_id, &asset) ||
        asset.source_model_index != game_model_index) {
        return false;
    }
    *asset_out = asset;
    return true;
}

static bool handle_equal(worr_rewind_collision_asset_handle_v1 lhs,
                         worr_rewind_collision_asset_handle_v1 rhs)
{
    return lhs.map_epoch == rhs.map_epoch && lhs.asset_id == rhs.asset_id &&
           lhs.asset_hash == rhs.asset_hash;
}

static bool request_valid(
    const worr_rewind_collision_trace_request_v1 *request)
{
    return request &&
           range_aligned(request,
                         _Alignof(worr_rewind_collision_trace_request_v1)) &&
           request->struct_size == sizeof(*request) &&
           request->schema_version == WORR_REWIND_COLLISION_SCHEMA_VERSION &&
           request->asset.map_epoch != 0 && request->asset.asset_id != 0 &&
           request->asset.asset_hash != 0 && request->reserved0 == 0 &&
           vector_valid(request->start) && vector_valid(request->end) &&
           bounds_valid(request->mins, request->maxs) &&
           vector_valid(request->origin) && vector_valid(request->angles);
}

static bool trace_output_valid(const trace_t *trace)
{
    return trace && float_valid(trace->fraction) && trace->fraction >= 0.0f &&
           trace->fraction <= 1.0f && vector_valid(trace->endpos) &&
           vector_valid(trace->plane.normal) &&
           float_valid(trace->plane.dist) &&
           vector_valid(trace->plane2.normal) &&
           float_valid(trace->plane2.dist);
}

static bool q_gameabi trace_transformed(
    const worr_rewind_collision_trace_request_v1 *request,
    trace_t *trace_out)
{
    const bsp_t *bsp;
    worr_rewind_collision_asset_v1 asset;
    rewind_collision_range_t request_range;
    rewind_collision_range_t output_range;
    trace_t trace;

    if (!request_valid(request) ||
        !range_aligned(trace_out, _Alignof(trace_t)) ||
        !range_make(request, sizeof(*request), &request_range) ||
        !range_make(trace_out, sizeof(*trace_out), &output_range) ||
        ranges_overlap(request_range, output_range)) {
        return false;
    }
    bsp = rewind_collision_map();
    if (!bsp || request->asset.map_epoch != sv.worr_snapshot_epoch ||
        !build_asset(bsp, request->asset.asset_id, &asset) ||
        !handle_equal(request->asset, asset.handle)) {
        return false;
    }

    memset(&trace, 0, sizeof(trace));
    trace.fraction = 1.0f;
    CM_TransformedBoxTrace(&trace, request->start, request->end,
                           request->mins, request->maxs,
                           bsp->models[asset.handle.asset_id].headnode,
                           (int32_t)request->contents_mask, request->origin,
                           request->angles, svs.csr.extended);
    trace.ent = NULL;
    if (!trace_output_valid(&trace))
        return false;
    *trace_out = trace;
    return true;
}

static const worr_rewind_collision_import_v1 rewind_collision_import = {
    sizeof(rewind_collision_import),
    WORR_REWIND_COLLISION_API_VERSION,
    get_map_identity,
    resolve_inline_brush,
    trace_transformed,
};

const worr_rewind_collision_import_v1 *SV_RewindCollisionImportV1(void)
{
    return &rewind_collision_import;
}
