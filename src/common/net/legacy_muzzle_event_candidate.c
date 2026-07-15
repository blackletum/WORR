/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/legacy_muzzle_event_candidate.h"

#include "common/intreadwrite.h"
#include "shared/shared.h"
#include "common/protocol.h"

#include <string.h>

static uint16_t muzzle_event_type(uint32_t family, uint16_t flash_id)
{
    if (family == WORR_EVENT_MUZZLE_FAMILY_MONSTER)
        return WORR_EVENT_TYPE_WEAPON_FIRE;
    if (flash_id >= WORR_EVENT_PLAYER_MUZZLE_LOGIN &&
        flash_id <= WORR_EVENT_PLAYER_MUZZLE_RESPAWN) {
        return WORR_EVENT_TYPE_STATE_CHANGE;
    }
    if (flash_id == WORR_EVENT_PLAYER_MUZZLE_ITEM_RESPAWN ||
        (flash_id >= WORR_EVENT_PLAYER_MUZZLE_NUKE1 &&
         flash_id <= WORR_EVENT_PLAYER_MUZZLE_NUKE8)) {
        return WORR_EVENT_TYPE_VISUAL_EFFECT;
    }
    return WORR_EVENT_TYPE_WEAPON_FIRE;
}

static worr_legacy_muzzle_event_candidate_result_v1
decode_muzzle_one(const uint8_t *raw_message, size_t raw_message_size,
                  size_t *cursor, q2proto_svc_muzzleflash_t *muzzleflash_out,
                  uint32_t *family_out)
{
    q2proto_svc_muzzleflash_t muzzleflash = {0};
    size_t remaining;
    uint8_t opcode;
    uint32_t family;

    if (!raw_message || !cursor || !muzzleflash_out || !family_out ||
        *cursor > raw_message_size) {
        return WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_INVALID_ARGUMENT;
    }
    remaining = raw_message_size - *cursor;
    if (remaining == 0)
        return WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_INVALID_SHAPE;

    opcode = raw_message[*cursor];
    if (opcode == svc_muzzleflash && remaining >= 4u) {
        muzzleflash.entity = (int16_t)RL16(&raw_message[*cursor + 1u]);
        muzzleflash.weapon = raw_message[*cursor + 3u] & ~MZ_SILENCED;
        muzzleflash.silenced =
            (raw_message[*cursor + 3u] & MZ_SILENCED) != 0;
        family = WORR_EVENT_MUZZLE_FAMILY_PLAYER;
        *cursor += 4u;
    } else if (opcode == svc_muzzleflash2 && remaining >= 4u) {
        muzzleflash.entity = (int16_t)RL16(&raw_message[*cursor + 1u]);
        muzzleflash.weapon = raw_message[*cursor + 3u];
        family = WORR_EVENT_MUZZLE_FAMILY_MONSTER;
        *cursor += 4u;
    } else if (opcode == svc_muzzleflash3 && remaining >= 5u) {
        muzzleflash.entity = (int16_t)RL16(&raw_message[*cursor + 1u]);
        muzzleflash.weapon = RL16(&raw_message[*cursor + 3u]);
        family = WORR_EVENT_MUZZLE_FAMILY_MONSTER;
        *cursor += 5u;
    } else {
        return WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_INVALID_SHAPE;
    }

    *muzzleflash_out = muzzleflash;
    *family_out = family;
    return WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_OK;
}

worr_legacy_muzzle_event_candidate_result_v1
Worr_LegacyMuzzleEventDecodeRawPrefixV1(
    const uint8_t *raw_message, size_t raw_message_size,
    q2proto_svc_muzzleflash_t *muzzleflash_out, uint32_t *family_out,
    size_t *bytes_consumed_out)
{
    q2proto_svc_muzzleflash_t muzzleflash;
    uint32_t family;
    size_t cursor = 0;
    worr_legacy_muzzle_event_candidate_result_v1 result;

    if (!raw_message || !muzzleflash_out || !family_out ||
        !bytes_consumed_out) {
        return WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_INVALID_ARGUMENT;
    }
    result = decode_muzzle_one(raw_message, raw_message_size, &cursor,
                               &muzzleflash, &family);
    if (result != WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_OK)
        return result;

    *muzzleflash_out = muzzleflash;
    *family_out = family;
    *bytes_consumed_out = cursor;
    return WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_OK;
}

worr_legacy_muzzle_event_candidate_result_v1
Worr_LegacyMuzzleEventDecodeRawSequenceV1(
    const uint8_t *raw_message, size_t raw_message_size,
    q2proto_svc_muzzleflash_t *muzzleflashes_out, uint32_t *families_out,
    uint32_t capacity, uint32_t *count_out)
{
    q2proto_svc_muzzleflash_t
        muzzleflashes[WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_SEQUENCE_MAX];
    uint32_t families[WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_SEQUENCE_MAX];
    uint32_t count = 0;
    size_t cursor = 0;

    if (!raw_message || !muzzleflashes_out || !families_out || !count_out ||
        capacity == 0) {
        return WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_INVALID_ARGUMENT;
    }
    if (raw_message_size == 0)
        return WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_INVALID_SHAPE;

    while (cursor != raw_message_size) {
        worr_legacy_muzzle_event_candidate_result_v1 result;

        if (count == capacity ||
            count == WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_SEQUENCE_MAX) {
            return WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_CAPACITY;
        }
        result = decode_muzzle_one(raw_message, raw_message_size, &cursor,
                                   &muzzleflashes[count], &families[count]);
        if (result != WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_OK)
            return result;
        ++count;
    }

    memcpy(muzzleflashes_out, muzzleflashes, sizeof(muzzleflashes[0]) * count);
    memcpy(families_out, families, sizeof(families[0]) * count);
    *count_out = count;
    return WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_OK;
}

worr_legacy_muzzle_event_candidate_result_v1
Worr_LegacyMuzzleEventDecodeRawV1(
    const uint8_t *raw_message, size_t raw_message_size,
    q2proto_svc_muzzleflash_t *muzzleflash_out, uint32_t *family_out)
{
    q2proto_svc_muzzleflash_t muzzleflash;
    uint32_t family;
    uint32_t count;
    worr_legacy_muzzle_event_candidate_result_v1 result;

    if (!muzzleflash_out || !family_out)
        return WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_INVALID_ARGUMENT;
    result = Worr_LegacyMuzzleEventDecodeRawSequenceV1(
        raw_message, raw_message_size, &muzzleflash, &family, 1, &count);
    if (result == WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_CAPACITY)
        return WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_INVALID_SHAPE;
    if (result != WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_OK)
        return result;

    *muzzleflash_out = muzzleflash;
    *family_out = family;
    return WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_OK;
}

worr_legacy_muzzle_event_candidate_result_v1
Worr_LegacyMuzzleEventCandidateBuildV1(
    const q2proto_svc_muzzleflash_t *muzzleflash, uint32_t family,
    uint32_t source_tick, uint64_t source_time_us, uint32_t max_entities,
    worr_event_record_v1 *candidate_out, uint32_t *source_entity_index_out)
{
    worr_event_record_v1 candidate;
    worr_event_payload_muzzle_v1 payload;

    if (!muzzleflash || !candidate_out || !source_entity_index_out ||
        max_entities == 0) {
        return WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_INVALID_ARGUMENT;
    }
    if (!Worr_EventMuzzleCarrierValidV1(
            family, muzzleflash->entity, muzzleflash->weapon, max_entities,
            WORR_EVENT_MONSTER_MUZZLE_LAST + 1u)) {
        return muzzleflash->entity <= 0 ||
                       (uint32_t)muzzleflash->entity >= max_entities
                   ? WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_ENTITY_OUT_OF_RANGE
                   : WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_INVALID_SHAPE;
    }
    if (family == WORR_EVENT_MUZZLE_FAMILY_MONSTER &&
        muzzleflash->silenced) {
        return WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_INVALID_SHAPE;
    }

    memset(&candidate, 0, sizeof(candidate));
    memset(&payload, 0, sizeof(payload));
    payload.family = (uint16_t)family;
    payload.flash_id = muzzleflash->weapon;
    if (muzzleflash->silenced)
        payload.flags = WORR_EVENT_MUZZLE_FLAG_SILENCED;

    candidate.struct_size = sizeof(candidate);
    candidate.schema_version = WORR_EVENT_ABI_VERSION;
    candidate.model_revision = WORR_EVENT_MODEL_REVISION;
    candidate.flags = WORR_EVENT_FLAG_REPLAY_SAFE | WORR_EVENT_FLAG_PRESENT_ONCE;
    candidate.source_tick = source_tick;
    candidate.source_time_us = source_time_us;
    candidate.source_entity.index = WORR_EVENT_NO_ENTITY;
    candidate.subject_entity.index = WORR_EVENT_NO_ENTITY;
    candidate.event_type = muzzle_event_type(family, muzzleflash->weapon);
    candidate.delivery_class = WORR_EVENT_DELIVERY_TRANSIENT;
    candidate.prediction_class = WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY;
    candidate.expiry_tick = source_tick + 1u;
    candidate.payload_kind = WORR_EVENT_PAYLOAD_MUZZLE_V1;
    candidate.payload_size = sizeof(payload);
    memcpy(candidate.payload, &payload, sizeof(payload));

    /* This is an unresolved template. Validate every other ABI field with a
     * temporary valid source, then restore the caller-owned lineage shape. */
    candidate.source_entity.index = 0;
    candidate.source_entity.generation = 1;
    if (!Worr_EventRecordCandidateValidateV1(&candidate, max_entities)) {
        return WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_INVALID_RECORD;
    }
    candidate.source_entity.index = WORR_EVENT_NO_ENTITY;
    candidate.source_entity.generation = 0;

    *candidate_out = candidate;
    *source_entity_index_out = (uint32_t)muzzleflash->entity;
    return WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_OK;
}
