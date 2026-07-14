/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "shared/snapshot_abi.h"

#include <math.h>
#include <string.h>

#define SNAPSHOT_KNOWN_FLAGS                                             \
    ((uint32_t)(WORR_SNAPSHOT_FLAG_KEYFRAME |                            \
                WORR_SNAPSHOT_FLAG_COMPLETE |                            \
                WORR_SNAPSHOT_FLAG_AUTHORITATIVE_GENERATIONS |           \
                WORR_SNAPSHOT_FLAG_LEGACY_PROJECTION |                   \
                WORR_SNAPSHOT_FLAG_PROMOTION_ELIGIBLE |                  \
                WORR_SNAPSHOT_FLAG_TRANSPORT_TRUNCATED))

#define DISCONTINUITY_KNOWN_FLAGS                                        \
    ((uint32_t)(WORR_SNAPSHOT_DISCONTINUITY_INITIAL |                    \
                WORR_SNAPSHOT_DISCONTINUITY_FULL_SNAPSHOT |              \
                WORR_SNAPSHOT_DISCONTINUITY_SEQUENCE_GAP |               \
                WORR_SNAPSHOT_DISCONTINUITY_BASE_JUMP |                  \
                WORR_SNAPSHOT_DISCONTINUITY_RATE_SUPPRESSED |            \
                WORR_SNAPSHOT_DISCONTINUITY_FRAGMENT_STALL |             \
                WORR_SNAPSHOT_DISCONTINUITY_MAP_RESET |                  \
                WORR_SNAPSHOT_DISCONTINUITY_DEMO_REWIND |                \
                WORR_SNAPSHOT_DISCONTINUITY_TRANSPORT_TRUNCATED |        \
                WORR_SNAPSHOT_DISCONTINUITY_HARD_RESYNC |                \
                WORR_SNAPSHOT_DISCONTINUITY_OBSERVER_ATTACH))

#define GENERATION_KNOWN_FLAGS                                           \
    ((uint32_t)(WORR_SNAPSHOT_GENERATION_AUTHORITATIVE |                 \
                WORR_SNAPSHOT_GENERATION_LEGACY_INFERRED |               \
                WORR_SNAPSHOT_GENERATION_EPOCH_RESET))

/* Frozen T02 v1 values.  A T02 schema revision requires a snapshot revision. */
#define PREDICTION_V1_MOVEMENT_TYPE_MIN 0
#define PREDICTION_V1_MOVEMENT_TYPE_MAX 7
#define PREDICTION_V1_KNOWN_FLAGS UINT16_C(0x9fff)

static const uint64_t fnv_offset_basis = UINT64_C(14695981039346656037);
static const uint64_t fnv_prime = UINT64_C(1099511628211);

static uint64_t hash_u8(uint64_t hash, uint8_t value)
{
    hash ^= value;
    return hash * fnv_prime;
}

static uint64_t hash_u16(uint64_t hash, uint16_t value)
{
    hash = hash_u8(hash, (uint8_t)(value & UINT16_C(0xff)));
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

static uint32_t canonical_float_bits(float value)
{
    uint32_t bits;
    if (value == 0.0f)
        return 0;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static uint64_t hash_float(uint64_t hash, float value)
{
    return hash_u32(hash, canonical_float_bits(value));
}

static uint64_t begin_hash(uint32_t domain)
{
    uint64_t hash = fnv_offset_basis;
    hash = hash_u32(hash, UINT32_C(0x574f5252)); /* WORR */
    return hash_u32(hash, domain);
}

static bool bytes_zero(const uint8_t *bytes, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) {
        if (bytes[i] != 0)
            return false;
    }
    return true;
}

static bool floats_finite(const float *values, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) {
        if (!isfinite(values[i]))
            return false;
    }
    return true;
}

static bool floats_zero(const float *values, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) {
        if (values[i] != 0.0f)
            return false;
    }
    return true;
}

static uint64_t hash_floats(uint64_t hash,
                            const float *values,
                            size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i)
        hash = hash_float(hash, values[i]);
    return hash;
}

static uint64_t hash_snapshot_id(uint64_t hash, worr_snapshot_id_v2 id)
{
    hash = hash_u32(hash, id.epoch);
    return hash_u32(hash, id.sequence);
}

static uint64_t hash_event_id(uint64_t hash, worr_event_id_v1 id)
{
    hash = hash_u32(hash, id.stream_epoch);
    return hash_u32(hash, id.sequence);
}

static uint64_t hash_entity_ref(uint64_t hash,
                                worr_event_entity_ref_v1 ref)
{
    hash = hash_u32(hash, ref.index);
    return hash_u32(hash, ref.generation);
}

static uint64_t hash_generation(
    uint64_t hash, worr_snapshot_entity_generation_v2 generation)
{
    hash = hash_entity_ref(hash, generation.identity);
    return hash_u32(hash, generation.provenance_flags);
}

static bool event_id_valid(worr_event_id_v1 id, bool allow_absent)
{
    if (id.stream_epoch == 0 || id.sequence == 0)
        return allow_absent && id.stream_epoch == 0 && id.sequence == 0;
    return true;
}

static int event_id_compare(worr_event_id_v1 left, worr_event_id_v1 right)
{
    if (left.stream_epoch < right.stream_epoch)
        return -1;
    if (left.stream_epoch > right.stream_epoch)
        return 1;
    if (left.sequence < right.sequence)
        return -1;
    if (left.sequence > right.sequence)
        return 1;
    return 0;
}

static bool event_id_advance(worr_event_id_v1 current,
                             uint32_t count,
                             worr_event_id_v1 *advanced)
{
    uint64_t total;
    uint64_t epoch_add;
    worr_event_id_v1 output;

    if (!advanced || !event_id_valid(current, false))
        return false;
    total = (uint64_t)(current.sequence - 1u) + count;
    epoch_add = total / UINT32_MAX;
    if (epoch_add > (uint64_t)UINT32_MAX - current.stream_epoch)
        return false;
    output.stream_epoch = current.stream_epoch + (uint32_t)epoch_add;
    output.sequence = (uint32_t)(total % UINT32_MAX) + 1u;
    *advanced = output;
    return true;
}

bool Worr_SnapshotIdValidV2(worr_snapshot_id_v2 id, bool allow_absent)
{
    if (id.epoch == 0 || id.sequence == 0)
        return allow_absent && id.epoch == 0 && id.sequence == 0;
    return true;
}

bool Worr_SnapshotIdNextV2(worr_snapshot_id_v2 current,
                           worr_snapshot_id_v2 *next)
{
    worr_snapshot_id_v2 output;

    if (!next || !Worr_SnapshotIdValidV2(current, true))
        return false;
    if (current.epoch == 0) {
        output.epoch = 1;
        output.sequence = 1;
    } else if (current.sequence != UINT32_MAX) {
        output.epoch = current.epoch;
        output.sequence = current.sequence + 1u;
    } else {
        if (current.epoch == UINT32_MAX)
            return false;
        output.epoch = current.epoch + 1u;
        output.sequence = 1;
    }
    *next = output;
    return true;
}

bool Worr_SnapshotGenerationValidV2(
    worr_snapshot_entity_generation_v2 generation,
    uint32_t max_entities,
    bool allow_absent)
{
    const uint32_t source_flags =
        generation.provenance_flags &
        (WORR_SNAPSHOT_GENERATION_AUTHORITATIVE |
         WORR_SNAPSHOT_GENERATION_LEGACY_INFERRED);

    if (generation.reserved0 != 0 ||
        (generation.provenance_flags & ~GENERATION_KNOWN_FLAGS) != 0) {
        return false;
    }
    if (generation.identity.index == WORR_EVENT_NO_ENTITY) {
        return allow_absent && generation.identity.generation == 0 &&
               generation.provenance_flags == 0;
    }
    if (!Worr_EventEntityRefValidV1(generation.identity, max_entities,
                                    false)) {
        return false;
    }
    return source_flags == WORR_SNAPSHOT_GENERATION_AUTHORITATIVE ||
           source_flags == WORR_SNAPSHOT_GENERATION_LEGACY_INFERRED;
}

static bool prediction_state_valid(const worr_prediction_state_v1 *state)
{
    return state && state->struct_size == sizeof(*state) &&
           state->schema_version == WORR_PREDICTION_ABI_VERSION &&
           state->movement_type >= PREDICTION_V1_MOVEMENT_TYPE_MIN &&
           state->movement_type <= PREDICTION_V1_MOVEMENT_TYPE_MAX &&
           (state->movement_flags & ~PREDICTION_V1_KNOWN_FLAGS) == 0 &&
           state->reserved0 == 0 && floats_finite(state->origin, 3) &&
           floats_finite(state->velocity, 3) &&
           floats_finite(state->delta_angles, 3);
}

static bool absent_entity_ref(worr_event_entity_ref_v1 ref)
{
    return ref.index == WORR_EVENT_NO_ENTITY && ref.generation == 0;
}

bool Worr_SnapshotEntityValidateV2(const worr_snapshot_entity_v2 *entity,
                                   uint32_t max_entities)
{
    uint64_t mask;
    unsigned int i;

    if (!entity || entity->struct_size != sizeof(*entity) ||
        entity->schema_version != WORR_SNAPSHOT_ABI_VERSION ||
        entity->model_revision != WORR_SNAPSHOT_MODEL_REVISION ||
        entity->flags != 0 || max_entities == 0 ||
        !Worr_SnapshotGenerationValidV2(entity->generation, max_entities,
                                        false) ||
        !bytes_zero(entity->reserved0, sizeof(entity->reserved0)) ||
        !floats_finite(entity->origin, 3) ||
        !floats_finite(entity->angles, 3) ||
        !floats_finite(entity->old_origin, 3) || !isfinite(entity->alpha) ||
        !isfinite(entity->scale) || !isfinite(entity->loop_volume) ||
        !isfinite(entity->loop_attenuation)) {
        return false;
    }

    mask = entity->component_mask;
    if ((mask & ~WORR_SNAPSHOT_ENTITY_COMPONENTS_V2) != 0 ||
        (mask & WORR_SNAPSHOT_ENTITY_TRANSFORM) == 0) {
        return false;
    }
    if ((mask & WORR_SNAPSHOT_ENTITY_INTERPOLATION) == 0 &&
        (!floats_zero(entity->old_origin, 3) || entity->old_frame != 0)) {
        return false;
    }
    if ((mask & WORR_SNAPSHOT_ENTITY_MODELS) == 0) {
        for (i = 0; i < 4; ++i) {
            if (entity->model_index[i] != 0)
                return false;
        }
    }
    if ((mask & WORR_SNAPSHOT_ENTITY_ANIMATION) == 0 && entity->frame != 0)
        return false;
    if ((mask & WORR_SNAPSHOT_ENTITY_APPEARANCE) == 0 &&
        (entity->skin != 0 || entity->alpha != 0.0f ||
         entity->scale != 0.0f)) {
        return false;
    }
    if ((mask & WORR_SNAPSHOT_ENTITY_EFFECTS) == 0 &&
        (entity->effects != 0 || entity->renderfx != 0)) {
        return false;
    }
    if ((mask & WORR_SNAPSHOT_ENTITY_COLLISION) == 0 && entity->solid != 0)
        return false;
    if ((mask & WORR_SNAPSHOT_ENTITY_LOOP_SOUND) == 0 &&
        (entity->sound != 0 || entity->loop_volume != 0.0f ||
         entity->loop_attenuation != 0.0f)) {
        return false;
    }
    if ((mask & WORR_SNAPSHOT_ENTITY_OWNER) != 0) {
        if (!Worr_EventEntityRefValidV1(entity->owner, max_entities, false))
            return false;
    } else if (!absent_entity_ref(entity->owner)) {
        return false;
    }
    if ((mask & WORR_SNAPSHOT_ENTITY_INSTANCE) == 0 &&
        entity->instance_bits != 0) {
        return false;
    }
    return true;
}

bool Worr_SnapshotPlayerValidateV2(const worr_snapshot_player_v2 *player,
                                   uint32_t max_entities)
{
    uint64_t mask;
    unsigned int i;

    if (!player || player->struct_size != sizeof(*player) ||
        player->schema_version != WORR_SNAPSHOT_ABI_VERSION ||
        player->model_revision != WORR_SNAPSHOT_MODEL_REVISION ||
        player->flags != 0 || max_entities == 0 ||
        !Worr_SnapshotGenerationValidV2(player->controlled_entity,
                                        max_entities, false) ||
        !floats_finite(player->view_angles, 3) ||
        !floats_finite(player->view_offset, 3) ||
        !floats_finite(player->kick_angles, 3) ||
        !floats_finite(player->gun_angles, 3) ||
        !floats_finite(player->gun_offset, 3) ||
        !floats_finite(player->screen_blend, 4) ||
        !floats_finite(player->damage_blend, 4) || !isfinite(player->fov)) {
        return false;
    }

    mask = player->component_mask;
    if ((mask & ~WORR_SNAPSHOT_PLAYER_COMPONENTS_V2) != 0 ||
        (mask & WORR_SNAPSHOT_PLAYER_MOVEMENT) == 0 ||
        !prediction_state_valid(&player->movement)) {
        return false;
    }
    if ((mask & WORR_SNAPSHOT_PLAYER_VIEW) == 0 &&
        (!floats_zero(player->view_angles, 3) ||
         !floats_zero(player->view_offset, 3) ||
         !floats_zero(player->kick_angles, 3))) {
        return false;
    }
    if ((mask & WORR_SNAPSHOT_PLAYER_WEAPON) == 0 &&
        (player->gun_index != 0 || player->gun_frame != 0 ||
         player->gun_skin != 0 || player->gun_rate != 0 ||
         !floats_zero(player->gun_angles, 3) ||
         !floats_zero(player->gun_offset, 3))) {
        return false;
    }
    if ((mask & WORR_SNAPSHOT_PLAYER_BLEND) == 0 &&
        (!floats_zero(player->screen_blend, 4) ||
         !floats_zero(player->damage_blend, 4))) {
        return false;
    }
    if ((mask & WORR_SNAPSHOT_PLAYER_PRESENTATION) == 0 &&
        (player->rdflags != 0 || player->team_id != 0 ||
         player->fov != 0.0f)) {
        return false;
    }
    if ((mask & WORR_SNAPSHOT_PLAYER_STATS) == 0) {
        for (i = 0; i < WORR_SNAPSHOT_STATS_CAPACITY; ++i) {
            if (player->stats[i] != 0)
                return false;
        }
    }
    return true;
}

bool Worr_SnapshotEventRefsValidateV2(
    const worr_snapshot_event_ref_v2 *event_refs,
    uint32_t count)
{
    uint32_t i;

    if (count == 0)
        return true;
    if (!event_refs)
        return false;
    for (i = 0; i < count; ++i) {
        const worr_snapshot_event_ref_v2 *event_ref = &event_refs[i];
        const bool authoritative =
            event_ref->provenance ==
            WORR_SNAPSHOT_EVENT_PROVENANCE_AUTHORITY;
        const bool inferred =
            event_ref->provenance ==
            WORR_SNAPSHOT_EVENT_PROVENANCE_LEGACY_INFERRED;

        if (event_ref->struct_size != sizeof(*event_ref) ||
            event_ref->schema_version != WORR_SNAPSHOT_ABI_VERSION ||
            (!authoritative && !inferred) ||
            event_ref->carrier_ordinal != i ||
            event_ref->semantic_version != WORR_EVENT_MODEL_REVISION ||
            (authoritative &&
             !event_id_valid(event_ref->authority_id, false)) ||
            (inferred &&
             (!event_id_valid(event_ref->authority_id, true) ||
              event_ref->authority_id.stream_epoch != 0))) {
            return false;
        }
        if (i != 0 &&
            event_refs[i - 1].provenance != event_ref->provenance) {
            return false;
        }
        if (authoritative && i != 0 &&
            event_id_compare(event_refs[i - 1].authority_id,
                             event_ref->authority_id) >= 0) {
            return false;
        }
    }
    return true;
}

static uint64_t hash_prediction_state(uint64_t hash,
                                      const worr_prediction_state_v1 *state)
{
    hash = hash_u32(hash, state->struct_size);
    hash = hash_u32(hash, state->schema_version);
    hash = hash_u32(hash, (uint32_t)state->movement_type);
    hash = hash_floats(hash, state->origin, 3);
    hash = hash_floats(hash, state->velocity, 3);
    hash = hash_u16(hash, state->movement_flags);
    hash = hash_u16(hash, state->movement_time_ms);
    hash = hash_u16(hash, (uint16_t)state->gravity);
    hash = hash_u8(hash, (uint8_t)state->view_height);
    return hash_floats(hash, state->delta_angles, 3);
}

bool Worr_SnapshotPlayerHashV2(const worr_snapshot_player_v2 *player,
                               uint32_t max_entities,
                               uint64_t *hash_out)
{
    uint64_t hash;
    unsigned int i;

    if (!hash_out || !Worr_SnapshotPlayerValidateV2(player, max_entities))
        return false;

    hash = begin_hash(UINT32_C(0x504c5932)); /* PLY2 */
    hash = hash_u32(hash, player->struct_size);
    hash = hash_u32(hash, player->schema_version);
    hash = hash_u32(hash, player->model_revision);
    hash = hash_u32(hash, player->flags);
    hash = hash_generation(hash, player->controlled_entity);
    hash = hash_u64(hash, player->component_mask);
    hash = hash_prediction_state(hash, &player->movement);
    if ((player->component_mask & WORR_SNAPSHOT_PLAYER_VIEW) != 0) {
        hash = hash_floats(hash, player->view_angles, 3);
        hash = hash_floats(hash, player->view_offset, 3);
        hash = hash_floats(hash, player->kick_angles, 3);
    }
    if ((player->component_mask & WORR_SNAPSHOT_PLAYER_WEAPON) != 0) {
        hash = hash_floats(hash, player->gun_angles, 3);
        hash = hash_floats(hash, player->gun_offset, 3);
        hash = hash_u16(hash, player->gun_index);
        hash = hash_u16(hash, player->gun_frame);
        hash = hash_u8(hash, player->gun_skin);
        hash = hash_u8(hash, player->gun_rate);
    }
    if ((player->component_mask & WORR_SNAPSHOT_PLAYER_BLEND) != 0) {
        hash = hash_floats(hash, player->screen_blend, 4);
        hash = hash_floats(hash, player->damage_blend, 4);
    }
    if ((player->component_mask & WORR_SNAPSHOT_PLAYER_PRESENTATION) != 0) {
        hash = hash_u8(hash, player->rdflags);
        hash = hash_u8(hash, player->team_id);
        hash = hash_float(hash, player->fov);
    }
    if ((player->component_mask & WORR_SNAPSHOT_PLAYER_STATS) != 0) {
        for (i = 0; i < WORR_SNAPSHOT_STATS_CAPACITY; ++i)
            hash = hash_u16(hash, (uint16_t)player->stats[i]);
    }
    *hash_out = hash;
    return true;
}

bool Worr_SnapshotEntityHashV2(const worr_snapshot_entity_v2 *entity,
                               uint32_t max_entities,
                               uint64_t *hash_out)
{
    uint64_t hash;
    unsigned int i;

    if (!hash_out || !Worr_SnapshotEntityValidateV2(entity, max_entities))
        return false;

    hash = begin_hash(UINT32_C(0x454e5432)); /* ENT2 */
    hash = hash_u32(hash, entity->struct_size);
    hash = hash_u32(hash, entity->schema_version);
    hash = hash_u32(hash, entity->model_revision);
    hash = hash_u32(hash, entity->flags);
    hash = hash_generation(hash, entity->generation);
    hash = hash_u64(hash, entity->component_mask);
    if ((entity->component_mask & WORR_SNAPSHOT_ENTITY_TRANSFORM) != 0) {
        hash = hash_floats(hash, entity->origin, 3);
        hash = hash_floats(hash, entity->angles, 3);
    }
    if ((entity->component_mask & WORR_SNAPSHOT_ENTITY_INTERPOLATION) != 0) {
        hash = hash_floats(hash, entity->old_origin, 3);
        hash = hash_u32(hash, (uint32_t)entity->old_frame);
    }
    if ((entity->component_mask & WORR_SNAPSHOT_ENTITY_MODELS) != 0) {
        for (i = 0; i < 4; ++i)
            hash = hash_u16(hash, entity->model_index[i]);
    }
    if ((entity->component_mask & WORR_SNAPSHOT_ENTITY_ANIMATION) != 0)
        hash = hash_u16(hash, entity->frame);
    if ((entity->component_mask & WORR_SNAPSHOT_ENTITY_APPEARANCE) != 0) {
        hash = hash_u32(hash, entity->skin);
        hash = hash_float(hash, entity->alpha);
        hash = hash_float(hash, entity->scale);
    }
    if ((entity->component_mask & WORR_SNAPSHOT_ENTITY_EFFECTS) != 0) {
        hash = hash_u64(hash, entity->effects);
        hash = hash_u32(hash, entity->renderfx);
    }
    if ((entity->component_mask & WORR_SNAPSHOT_ENTITY_COLLISION) != 0)
        hash = hash_u32(hash, entity->solid);
    if ((entity->component_mask & WORR_SNAPSHOT_ENTITY_LOOP_SOUND) != 0) {
        hash = hash_u16(hash, entity->sound);
        hash = hash_float(hash, entity->loop_volume);
        hash = hash_float(hash, entity->loop_attenuation);
    }
    if ((entity->component_mask & WORR_SNAPSHOT_ENTITY_OWNER) != 0)
        hash = hash_entity_ref(hash, entity->owner);
    if ((entity->component_mask & WORR_SNAPSHOT_ENTITY_INSTANCE) != 0)
        hash = hash_u8(hash, entity->instance_bits);
    *hash_out = hash;
    return true;
}

bool Worr_SnapshotEntityListHashV2(
    const worr_snapshot_entity_v2 *entities,
    uint32_t count,
    uint32_t max_entities,
    uint64_t *hash_out)
{
    uint64_t hash;
    uint32_t i;

    if (!hash_out || max_entities == 0 || (count != 0 && !entities))
        return false;
    hash = begin_hash(UINT32_C(0x454c5332)); /* ELS2 */
    hash = hash_u32(hash, count);
    for (i = 0; i < count; ++i) {
        uint64_t entity_hash;
        if (!Worr_SnapshotEntityHashV2(&entities[i], max_entities,
                                       &entity_hash) ||
            (i != 0 &&
             entities[i - 1].generation.identity.index >=
                 entities[i].generation.identity.index)) {
            return false;
        }
        hash = hash_u64(hash, entity_hash);
    }
    *hash_out = hash;
    return true;
}

bool Worr_SnapshotAreaHashV2(const uint8_t *area_bytes,
                             uint32_t count,
                             uint64_t *hash_out)
{
    uint64_t hash;
    uint32_t i;

    if (!hash_out || (count != 0 && !area_bytes))
        return false;
    hash = begin_hash(UINT32_C(0x41524532)); /* ARE2 */
    hash = hash_u32(hash, count);
    for (i = 0; i < count; ++i)
        hash = hash_u8(hash, area_bytes[i]);
    *hash_out = hash;
    return true;
}

bool Worr_SnapshotEventRefsHashV2(
    const worr_snapshot_event_ref_v2 *event_refs,
    uint32_t count,
    uint64_t *hash_out)
{
    uint64_t hash;
    uint32_t i;

    if (!hash_out || !Worr_SnapshotEventRefsValidateV2(event_refs, count))
        return false;
    hash = begin_hash(UINT32_C(0x45565332)); /* EVS2 */
    hash = hash_u32(hash, count);
    for (i = 0; i < count; ++i) {
        hash = hash_u32(hash, event_refs[i].struct_size);
        hash = hash_u16(hash, event_refs[i].schema_version);
        hash = hash_u16(hash, event_refs[i].provenance);
        hash = hash_u32(hash, event_refs[i].carrier_ordinal);
        hash = hash_u32(hash, event_refs[i].semantic_version);
        hash = hash_event_id(hash, event_refs[i].authority_id);
        hash = hash_u64(hash, event_refs[i].semantic_hash);
    }
    *hash_out = hash;
    return true;
}

static bool serial_range_valid(worr_snapshot_serial_range_v2 range)
{
    return range.reserved0 == 0 &&
           ((range.count == 0 && range.first_serial == 0) ||
            (range.count != 0 && range.first_serial != 0));
}

static bool consumed_command_valid(
    worr_snapshot_consumed_command_v2 consumed_command)
{
    if (consumed_command.reserved0 != 0)
        return false;
    if (consumed_command.provenance ==
        WORR_SNAPSHOT_CONSUMED_COMMAND_NONE) {
        return consumed_command.cursor.epoch == 0 &&
               consumed_command.cursor.contiguous_sequence == 0;
    }
    if (consumed_command.provenance ==
        WORR_SNAPSHOT_CONSUMED_COMMAND_SERVER_CONSUMED) {
        return consumed_command.cursor.epoch != 0;
    }
    return false;
}

static bool event_range_valid(worr_snapshot_event_range_v2 range)
{
    worr_event_id_v1 cursor;
    const bool authoritative =
        range.provenance == WORR_SNAPSHOT_EVENT_PROVENANCE_AUTHORITY;
    const bool inferred =
        range.provenance ==
        WORR_SNAPSHOT_EVENT_PROVENANCE_LEGACY_INFERRED;

    if ((range.flags &
         ~(WORR_SNAPSHOT_EVENT_RANGE_CONTIGUOUS_AUTHORITY |
           WORR_SNAPSHOT_EVENT_RANGE_CONTIGUOUS_CARRIER)) != 0 ||
        range.reserved0 != 0) {
        return false;
    }
    if (range.count == 0) {
        return range.first_ref_serial == 0 && range.flags == 0 &&
               range.provenance == WORR_SNAPSHOT_EVENT_PROVENANCE_NONE &&
               range.first_carrier_ordinal == 0 &&
               event_id_valid(range.first_authority_id, true) &&
               range.first_authority_id.stream_epoch == 0 &&
               event_id_valid(range.one_past_authority_id, true) &&
               range.one_past_authority_id.stream_epoch == 0;
    }
    if (range.first_ref_serial == 0 || (!authoritative && !inferred) ||
        range.first_carrier_ordinal != 0 ||
        (range.flags & WORR_SNAPSHOT_EVENT_RANGE_CONTIGUOUS_CARRIER) == 0) {
        return false;
    }
    if (inferred) {
        return range.flags ==
                   WORR_SNAPSHOT_EVENT_RANGE_CONTIGUOUS_CARRIER &&
               event_id_valid(range.first_authority_id, true) &&
               range.first_authority_id.stream_epoch == 0 &&
               event_id_valid(range.one_past_authority_id, true) &&
               range.one_past_authority_id.stream_epoch == 0;
    }
    if (!event_id_valid(range.first_authority_id, false) ||
        !event_id_valid(range.one_past_authority_id, false) ||
        event_id_compare(range.first_authority_id,
                         range.one_past_authority_id) >= 0) {
        return false;
    }
    if ((range.flags &
         WORR_SNAPSHOT_EVENT_RANGE_CONTIGUOUS_AUTHORITY) == 0) {
        return true;
    }

    if (!event_id_advance(range.first_authority_id, range.count, &cursor))
        return false;
    return cursor.stream_epoch == range.one_past_authority_id.stream_epoch &&
           cursor.sequence == range.one_past_authority_id.sequence;
}

static bool discontinuity_valid(
    const worr_snapshot_discontinuity_v2 *discontinuity)
{
    uint32_t required_flag = 0;

    if (!discontinuity || discontinuity->reserved0 != 0 ||
        (discontinuity->flags & ~DISCONTINUITY_KNOWN_FLAGS) != 0 ||
        discontinuity->reason >
            WORR_SNAPSHOT_DISCONTINUITY_REASON_OBSERVER_ATTACH ||
        !Worr_SnapshotIdValidV2(discontinuity->previous, true)) {
        return false;
    }
    if ((discontinuity->flags == 0) !=
        (discontinuity->reason ==
         WORR_SNAPSHOT_DISCONTINUITY_REASON_NONE)) {
        return false;
    }
    if ((discontinuity->flags &
         WORR_SNAPSHOT_DISCONTINUITY_SEQUENCE_GAP) != 0) {
        if (discontinuity->skipped_sequences == 0)
            return false;
    } else if (discontinuity->skipped_sequences != 0) {
        return false;
    }

    switch (discontinuity->reason) {
    case WORR_SNAPSHOT_DISCONTINUITY_REASON_NONE:
        break;
    case WORR_SNAPSHOT_DISCONTINUITY_REASON_INITIAL:
        required_flag = WORR_SNAPSHOT_DISCONTINUITY_INITIAL;
        break;
    case WORR_SNAPSHOT_DISCONTINUITY_REASON_FULL_SNAPSHOT:
        required_flag = WORR_SNAPSHOT_DISCONTINUITY_FULL_SNAPSHOT;
        break;
    case WORR_SNAPSHOT_DISCONTINUITY_REASON_SEQUENCE_GAP:
        required_flag = WORR_SNAPSHOT_DISCONTINUITY_SEQUENCE_GAP;
        break;
    case WORR_SNAPSHOT_DISCONTINUITY_REASON_BASE_JUMP:
        required_flag = WORR_SNAPSHOT_DISCONTINUITY_BASE_JUMP;
        break;
    case WORR_SNAPSHOT_DISCONTINUITY_REASON_RATE_SUPPRESSED:
        required_flag = WORR_SNAPSHOT_DISCONTINUITY_RATE_SUPPRESSED;
        break;
    case WORR_SNAPSHOT_DISCONTINUITY_REASON_FRAGMENT_STALL:
        required_flag = WORR_SNAPSHOT_DISCONTINUITY_FRAGMENT_STALL;
        break;
    case WORR_SNAPSHOT_DISCONTINUITY_REASON_MAP_RESET:
        required_flag = WORR_SNAPSHOT_DISCONTINUITY_MAP_RESET;
        break;
    case WORR_SNAPSHOT_DISCONTINUITY_REASON_DEMO_REWIND:
        required_flag = WORR_SNAPSHOT_DISCONTINUITY_DEMO_REWIND;
        break;
    case WORR_SNAPSHOT_DISCONTINUITY_REASON_TRANSPORT_TRUNCATED:
        required_flag = WORR_SNAPSHOT_DISCONTINUITY_TRANSPORT_TRUNCATED;
        break;
    case WORR_SNAPSHOT_DISCONTINUITY_REASON_HARD_RESYNC:
        required_flag = WORR_SNAPSHOT_DISCONTINUITY_HARD_RESYNC;
        break;
    case WORR_SNAPSHOT_DISCONTINUITY_REASON_OBSERVER_ATTACH:
        required_flag = WORR_SNAPSHOT_DISCONTINUITY_OBSERVER_ATTACH;
        break;
    default:
        return false;
    }
    return required_flag == 0 ||
           (discontinuity->flags & required_flag) != 0;
}

static bool snapshot_metadata_valid(const worr_snapshot_v2 *snapshot,
                                    uint32_t max_entities)
{
    const bool keyframe =
        snapshot && (snapshot->flags & WORR_SNAPSHOT_FLAG_KEYFRAME) != 0;
    const bool authoritative =
        snapshot &&
        (snapshot->flags &
         WORR_SNAPSHOT_FLAG_AUTHORITATIVE_GENERATIONS) != 0;
    const bool controlled_authoritative =
        snapshot &&
        (snapshot->controlled_entity.provenance_flags &
         WORR_SNAPSHOT_GENERATION_AUTHORITATIVE) != 0;
    const bool truncated =
        snapshot &&
        (snapshot->flags & WORR_SNAPSHOT_FLAG_TRANSPORT_TRUNCATED) != 0;
    const bool promotion_eligible =
        snapshot &&
        (snapshot->flags & WORR_SNAPSHOT_FLAG_PROMOTION_ELIGIBLE) != 0;
    const bool full =
        snapshot &&
        (snapshot->discontinuity.flags &
         WORR_SNAPSHOT_DISCONTINUITY_FULL_SNAPSHOT) != 0;
    const bool reset_boundary =
        snapshot &&
        (snapshot->discontinuity.flags &
         (WORR_SNAPSHOT_DISCONTINUITY_INITIAL |
          WORR_SNAPSHOT_DISCONTINUITY_MAP_RESET |
          WORR_SNAPSHOT_DISCONTINUITY_DEMO_REWIND)) != 0;
    const bool has_previous =
        snapshot && Worr_SnapshotIdValidV2(snapshot->discontinuity.previous,
                                           false);
    const bool observer_attach =
        snapshot &&
        (snapshot->discontinuity.flags &
         WORR_SNAPSHOT_DISCONTINUITY_OBSERVER_ATTACH) != 0;

    if (!snapshot || max_entities == 0 ||
        snapshot->struct_size != sizeof(*snapshot) ||
        snapshot->schema_version != WORR_SNAPSHOT_ABI_VERSION ||
        snapshot->model_revision != WORR_SNAPSHOT_MODEL_REVISION ||
        (snapshot->flags & ~SNAPSHOT_KNOWN_FLAGS) != 0 ||
        (snapshot->flags & WORR_SNAPSHOT_FLAG_COMPLETE) == 0 ||
        snapshot->reserved0 != 0 ||
        !Worr_SnapshotIdValidV2(snapshot->snapshot_id, false) ||
        !Worr_SnapshotGenerationValidV2(snapshot->controlled_entity,
                                        max_entities, false) ||
        !consumed_command_valid(snapshot->consumed_command) ||
        !discontinuity_valid(&snapshot->discontinuity) ||
        !serial_range_valid(snapshot->entity_range) ||
        !serial_range_valid(snapshot->area_range) ||
        !event_range_valid(snapshot->event_range) ||
        snapshot->entity_range.count > max_entities ||
        authoritative != controlled_authoritative || keyframe != full) {
        return false;
    }
    if (keyframe) {
        if (!Worr_SnapshotIdValidV2(snapshot->base_id, true) ||
            snapshot->base_id.epoch != 0) {
            return false;
        }
    } else if (!Worr_SnapshotIdValidV2(snapshot->base_id, false) ||
               snapshot->base_id.epoch != snapshot->snapshot_id.epoch ||
               snapshot->base_id.sequence >= snapshot->snapshot_id.sequence) {
        return false;
    }
    if ((reset_boundary ||
         (snapshot->discontinuity.flags &
          WORR_SNAPSHOT_DISCONTINUITY_HARD_RESYNC) != 0) &&
        !keyframe) {
        return false;
    }
    if (reset_boundary) {
        if (has_previous || snapshot->snapshot_id.sequence != 1)
            return false;
    } else if (observer_attach) {
        if (has_previous || snapshot->discontinuity.server_tick_delta != 0 ||
            snapshot->discontinuity.reason !=
                WORR_SNAPSHOT_DISCONTINUITY_REASON_OBSERVER_ATTACH ||
            (snapshot->discontinuity.flags &
             (WORR_SNAPSHOT_DISCONTINUITY_INITIAL |
              WORR_SNAPSHOT_DISCONTINUITY_MAP_RESET |
              WORR_SNAPSHOT_DISCONTINUITY_DEMO_REWIND)) != 0) {
            return false;
        }
    } else {
        uint32_t distance;
        if (!has_previous ||
            snapshot->discontinuity.previous.epoch !=
                snapshot->snapshot_id.epoch ||
            snapshot->discontinuity.previous.sequence >=
                snapshot->snapshot_id.sequence) {
            return false;
        }
        distance = snapshot->snapshot_id.sequence -
                   snapshot->discontinuity.previous.sequence;
        if ((snapshot->discontinuity.flags &
             WORR_SNAPSHOT_DISCONTINUITY_SEQUENCE_GAP) != 0) {
            if (distance - 1u !=
                snapshot->discontinuity.skipped_sequences)
                return false;
        } else if (distance != 1) {
            return false;
        }
        if (!keyframe && snapshot->base_id.sequence >
                                 snapshot->discontinuity.previous.sequence) {
            return false;
        }
    }
    if (truncated !=
        ((snapshot->discontinuity.flags &
          WORR_SNAPSHOT_DISCONTINUITY_TRANSPORT_TRUNCATED) != 0)) {
        return false;
    }
    if (promotion_eligible && truncated) {
        return false;
    }
    return true;
}

bool Worr_SnapshotCalculateHashV2(const worr_snapshot_v2 *snapshot,
                                  uint32_t max_entities,
                                  uint64_t *hash_out)
{
    uint64_t hash;
    const worr_snapshot_discontinuity_v2 *discontinuity;

    if (!hash_out || !snapshot_metadata_valid(snapshot, max_entities))
        return false;

    discontinuity = &snapshot->discontinuity;
    hash = begin_hash(UINT32_C(0x534e5032)); /* SNP2 */
    hash = hash_u32(hash, snapshot->struct_size);
    hash = hash_u32(hash, snapshot->schema_version);
    hash = hash_u32(hash, snapshot->model_revision);
    hash = hash_u32(hash, snapshot->flags);
    hash = hash_snapshot_id(hash, snapshot->snapshot_id);
    hash = hash_snapshot_id(hash, snapshot->base_id);
    hash = hash_u32(hash, snapshot->server_tick);
    hash = hash_u64(hash, snapshot->server_time_us);
    hash = hash_generation(hash, snapshot->controlled_entity);
    hash = hash_u32(hash, snapshot->consumed_command.cursor.epoch);
    hash = hash_u32(
        hash, snapshot->consumed_command.cursor.contiguous_sequence);
    hash = hash_u32(hash, snapshot->consumed_command.provenance);
    hash = hash_u32(hash, discontinuity->flags);
    hash = hash_u16(hash, discontinuity->reason);
    hash = hash_snapshot_id(hash, discontinuity->previous);
    hash = hash_u32(hash, discontinuity->server_tick_delta);
    hash = hash_u32(hash, discontinuity->skipped_sequences);
    /* Store-local first_serial values are lifetime guards, not semantics. */
    hash = hash_u32(hash, snapshot->entity_range.count);
    hash = hash_u32(hash, snapshot->area_range.count);
    hash = hash_u16(hash, snapshot->event_range.provenance);
    hash = hash_u16(hash, snapshot->event_range.flags);
    hash = hash_event_id(hash, snapshot->event_range.first_authority_id);
    hash = hash_event_id(hash,
                         snapshot->event_range.one_past_authority_id);
    hash = hash_u32(hash, snapshot->event_range.first_carrier_ordinal);
    hash = hash_u32(hash, snapshot->event_range.count);
    hash = hash_u64(hash, snapshot->player_hash);
    hash = hash_u64(hash, snapshot->entity_hash);
    hash = hash_u64(hash, snapshot->area_hash);
    hash = hash_u64(hash, snapshot->event_hash);
    *hash_out = hash;
    return true;
}

bool Worr_SnapshotValidateV2(const worr_snapshot_v2 *snapshot,
                             uint32_t max_entities)
{
    uint64_t calculated;
    return snapshot &&
           Worr_SnapshotCalculateHashV2(snapshot, max_entities,
                                        &calculated) &&
           calculated == snapshot->snapshot_hash;
}
