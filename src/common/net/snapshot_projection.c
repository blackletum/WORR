/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/snapshot_projection.h"

#include <string.h>

static const uint64_t fnv_offset_basis = UINT64_C(14695981039346656037);
static const uint64_t fnv_prime = UINT64_C(1099511628211);

static uint64_t hash_u8(uint64_t hash, uint8_t value)
{
    return (hash ^ value) * fnv_prime;
}

static uint64_t hash_u16(uint64_t hash, uint16_t value)
{
    hash = hash_u8(hash, (uint8_t)value);
    return hash_u8(hash, (uint8_t)(value >> 8));
}

static uint64_t hash_u32(uint64_t hash, uint32_t value)
{
    unsigned int i;
    for (i = 0; i < 4; ++i)
        hash = hash_u8(hash, (uint8_t)(value >> (i * 8)));
    return hash;
}

static uint64_t hash_u64(uint64_t hash, uint64_t value)
{
    unsigned int i;
    for (i = 0; i < 8; ++i)
        hash = hash_u8(hash, (uint8_t)(value >> (i * 8)));
    return hash;
}

static uint64_t begin_hash(uint32_t domain)
{
    uint64_t hash = fnv_offset_basis;
    hash = hash_u32(hash, UINT32_C(0x574f5252));
    return hash_u32(hash, domain);
}

static uint64_t hash_float(uint64_t hash, float value)
{
    uint32_t bits;
    if (value == 0.0f)
        bits = 0;
    else
        memcpy(&bits, &value, sizeof(bits));
    return hash_u32(hash, bits);
}

static uint64_t hash_floats(uint64_t hash, const float *values,
                            uint32_t count)
{
    uint32_t i;
    for (i = 0; i < count; ++i)
        hash = hash_float(hash, values[i]);
    return hash;
}

static uint64_t hash_identity(uint64_t hash,
                              worr_event_entity_ref_v1 identity)
{
    hash = hash_u32(hash, identity.index);
    return hash_u32(hash, identity.generation);
}

static uint64_t semantic_player_hash(const worr_snapshot_player_v2 *player)
{
    const worr_prediction_state_v1 *movement = &player->movement;
    uint64_t hash = begin_hash(UINT32_C(0x53504c32)); /* SPL2 */
    uint32_t i;

    hash = hash_u32(hash, WORR_SNAPSHOT_MODEL_REVISION);
    hash = hash_identity(hash, player->controlled_entity.identity);
    hash = hash_u64(hash, player->component_mask);
    hash = hash_u32(hash, (uint32_t)movement->movement_type);
    hash = hash_floats(hash, movement->origin, 3);
    hash = hash_floats(hash, movement->velocity, 3);
    hash = hash_u16(hash, movement->movement_flags);
    hash = hash_u16(hash, movement->movement_time_ms);
    hash = hash_u16(hash, (uint16_t)movement->gravity);
    hash = hash_u8(hash, (uint8_t)movement->view_height);
    hash = hash_floats(hash, movement->delta_angles, 3);
    hash = hash_floats(hash, player->view_angles, 3);
    hash = hash_floats(hash, player->view_offset, 3);
    hash = hash_floats(hash, player->kick_angles, 3);
    hash = hash_floats(hash, player->gun_angles, 3);
    hash = hash_floats(hash, player->gun_offset, 3);
    hash = hash_floats(hash, player->screen_blend, 4);
    hash = hash_floats(hash, player->damage_blend, 4);
    hash = hash_u16(hash, player->gun_index);
    hash = hash_u16(hash, player->gun_frame);
    hash = hash_u8(hash, player->gun_skin);
    hash = hash_u8(hash, player->gun_rate);
    hash = hash_u8(hash, player->rdflags);
    hash = hash_u8(hash, player->team_id);
    hash = hash_float(hash, player->fov);
    for (i = 0; i < WORR_SNAPSHOT_STATS_CAPACITY; ++i)
        hash = hash_u16(hash, (uint16_t)player->stats[i]);
    return hash;
}

static uint64_t semantic_entity_hash(const worr_snapshot_entity_v2 *entity)
{
    uint64_t hash = begin_hash(UINT32_C(0x53454e32)); /* SEN2 */
    uint32_t i;

    hash = hash_u32(hash, WORR_SNAPSHOT_MODEL_REVISION);
    hash = hash_identity(hash, entity->generation.identity);
    hash = hash_u64(hash, entity->component_mask);
    hash = hash_floats(hash, entity->origin, 3);
    hash = hash_floats(hash, entity->angles, 3);
    hash = hash_floats(hash, entity->old_origin, 3);
    for (i = 0; i < 4; ++i)
        hash = hash_u16(hash, entity->model_index[i]);
    hash = hash_u16(hash, entity->frame);
    hash = hash_u16(hash, entity->sound);
    hash = hash_u32(hash, entity->skin);
    hash = hash_u32(hash, entity->solid);
    hash = hash_u64(hash, entity->effects);
    hash = hash_u32(hash, entity->renderfx);
    hash = hash_float(hash, entity->alpha);
    hash = hash_float(hash, entity->scale);
    hash = hash_float(hash, entity->loop_volume);
    hash = hash_float(hash, entity->loop_attenuation);
    hash = hash_identity(hash, entity->owner);
    hash = hash_u32(hash, (uint32_t)entity->old_frame);
    return hash_u8(hash, entity->instance_bits);
}

static uint64_t semantic_entity_list_hash(
    const worr_snapshot_entity_v2 *entities, uint32_t count)
{
    uint64_t hash = begin_hash(UINT32_C(0x534c5332)); /* SLS2 */
    uint32_t i;
    hash = hash_u32(hash, count);
    for (i = 0; i < count; ++i)
        hash = hash_u64(hash, semantic_entity_hash(&entities[i]));
    return hash;
}

static uint64_t semantic_event_list_hash(
    const worr_snapshot_event_ref_v2 *events, uint32_t count)
{
    uint64_t hash = begin_hash(UINT32_C(0x53455632)); /* SEV2 */
    uint32_t i;
    hash = hash_u32(hash, count);
    for (i = 0; i < count; ++i) {
        hash = hash_u32(hash, events[i].carrier_ordinal);
        hash = hash_u32(hash, events[i].semantic_version);
        hash = hash_u64(hash, events[i].semantic_hash);
    }
    return hash;
}

bool Worr_SnapshotProjectionHashesV2(
    const worr_snapshot_projection_view_v2 *view,
    uint32_t max_entities,
    worr_snapshot_projection_hashes_v2 *hashes_out)
{
    worr_snapshot_projection_hashes_v2 output;
    uint64_t exact_player;
    uint64_t exact_entities;
    uint64_t exact_area;
    uint64_t exact_events;
    uint64_t hash;
    uint32_t parity_flags;

    if (!view || !hashes_out || max_entities == 0 ||
        view->struct_size != sizeof(*view) ||
        view->schema_version != WORR_SNAPSHOT_PROJECTION_VERSION ||
        view->reserved0 != 0 || !view->snapshot || !view->player ||
        (view->entity_count != 0 && !view->entities) ||
        (view->area_byte_count != 0 && !view->area_bytes) ||
        (view->event_ref_count != 0 && !view->event_refs) ||
        view->snapshot->entity_range.count != view->entity_count ||
        view->snapshot->area_range.count != view->area_byte_count ||
        view->snapshot->event_range.count != view->event_ref_count ||
        !Worr_SnapshotValidateV2(view->snapshot, max_entities) ||
        !Worr_SnapshotPlayerHashV2(view->player, max_entities,
                                   &exact_player) ||
        !Worr_SnapshotEntityListHashV2(view->entities, view->entity_count,
                                       max_entities, &exact_entities) ||
        !Worr_SnapshotAreaHashV2(view->area_bytes, view->area_byte_count,
                                 &exact_area) ||
        !Worr_SnapshotEventRefsHashV2(view->event_refs,
                                      view->event_ref_count,
                                      &exact_events) ||
        exact_player != view->snapshot->player_hash ||
        exact_entities != view->snapshot->entity_hash ||
        exact_area != view->snapshot->area_hash ||
        exact_events != view->snapshot->event_hash) {
        return false;
    }

    memset(&output, 0, sizeof(output));
    output.struct_size = sizeof(output);
    output.schema_version = WORR_SNAPSHOT_PROJECTION_VERSION;
    output.semantic_player_hash = semantic_player_hash(view->player);
    output.semantic_entity_hash =
        semantic_entity_list_hash(view->entities, view->entity_count);
    output.semantic_area_hash = exact_area;
    output.semantic_event_hash =
        semantic_event_list_hash(view->event_refs, view->event_ref_count);

    hash = begin_hash(UINT32_C(0x45504832)); /* EPH2 */
    hash = hash_u64(hash, view->snapshot->snapshot_hash);
    hash = hash_u64(hash, exact_player);
    hash = hash_u64(hash, exact_entities);
    hash = hash_u64(hash, exact_area);
    hash = hash_u64(hash, exact_events);
    output.endpoint_hash = hash;

    parity_flags = view->snapshot->flags &
        (WORR_SNAPSHOT_FLAG_KEYFRAME | WORR_SNAPSHOT_FLAG_COMPLETE |
         WORR_SNAPSHOT_FLAG_LEGACY_PROJECTION);
    hash = begin_hash(UINT32_C(0x50525932)); /* PRY2 */
    hash = hash_u32(hash, WORR_SNAPSHOT_ABI_VERSION);
    hash = hash_u32(hash, WORR_SNAPSHOT_MODEL_REVISION);
    hash = hash_u32(hash, parity_flags);
    hash = hash_u32(hash, view->snapshot->snapshot_id.epoch);
    hash = hash_u32(hash, view->snapshot->snapshot_id.sequence);
    hash = hash_u32(hash, view->snapshot->base_id.epoch);
    hash = hash_u32(hash, view->snapshot->base_id.sequence);
    hash = hash_identity(hash, view->snapshot->controlled_entity.identity);
    hash = hash_u32(hash, view->entity_count);
    hash = hash_u32(hash, view->area_byte_count);
    hash = hash_u32(hash, view->event_ref_count);
    hash = hash_u64(hash, output.semantic_player_hash);
    hash = hash_u64(hash, output.semantic_entity_hash);
    hash = hash_u64(hash, output.semantic_area_hash);
    hash = hash_u64(hash, output.semantic_event_hash);
    output.legacy_parity_hash = hash;
    *hashes_out = output;
    return true;
}
