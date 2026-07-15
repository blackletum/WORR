/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/legacy_temp_event_candidate.h"

#include "common/intreadwrite.h"
#include "shared/shared.h"
#include "common/math.h"
#include "common/protocol.h"

#include <string.h>

typedef struct legacy_temp_reader_v1_s {
    const uint8_t *data;
    size_t size;
    size_t cursor;
} legacy_temp_reader_v1;

static bool legacy_temp_read_u8(legacy_temp_reader_v1 *reader,
                                uint8_t *value_out)
{
    if (!reader || !value_out || reader->cursor >= reader->size)
        return false;
    *value_out = reader->data[reader->cursor++];
    return true;
}

static bool legacy_temp_read_i16(legacy_temp_reader_v1 *reader,
                                 int16_t *value_out)
{
    if (!reader || !value_out || reader->size - reader->cursor < 2u)
        return false;
    *value_out = (int16_t)RL16(&reader->data[reader->cursor]);
    reader->cursor += 2u;
    return true;
}

static bool legacy_temp_read_i32(legacy_temp_reader_v1 *reader,
                                 int32_t *value_out)
{
    if (!reader || !value_out || reader->size - reader->cursor < 4u)
        return false;
    *value_out = (int32_t)RL32(&reader->data[reader->cursor]);
    reader->cursor += 4u;
    return true;
}

static bool legacy_temp_read_float(legacy_temp_reader_v1 *reader,
                                   float *value_out)
{
    uint32_t bits;

    if (!reader || !value_out || reader->size - reader->cursor < 4u)
        return false;
    bits = RL32(&reader->data[reader->cursor]);
    memcpy(value_out, &bits, sizeof(*value_out));
    reader->cursor += 4u;
    return true;
}

static bool legacy_temp_read_position(legacy_temp_reader_v1 *reader,
                                      q2proto_vec3_t position_out)
{
    return legacy_temp_read_float(reader, &position_out[0]) &&
           legacy_temp_read_float(reader, &position_out[1]) &&
           legacy_temp_read_float(reader, &position_out[2]);
}

static bool legacy_temp_read_direction(legacy_temp_reader_v1 *reader,
                                       q2proto_vec3_t direction_out)
{
    uint8_t direction_index;

    if (!legacy_temp_read_u8(reader, &direction_index) ||
        direction_index >= NUMVERTEXNORMALS) {
        return false;
    }
    memcpy(direction_out, bytedirs[direction_index], sizeof(bytedirs[0]));
    return true;
}

static bool legacy_temp_read_position_direction(
    legacy_temp_reader_v1 *reader, q2proto_svc_temp_entity_t *temp_entity)
{
    return legacy_temp_read_position(reader, temp_entity->position1) &&
           legacy_temp_read_direction(reader, temp_entity->direction);
}

static bool legacy_temp_read_count_position_direction_color(
    legacy_temp_reader_v1 *reader, q2proto_svc_temp_entity_t *temp_entity)
{
    return legacy_temp_read_u8(reader, &temp_entity->count) &&
           legacy_temp_read_position(reader, temp_entity->position1) &&
           legacy_temp_read_direction(reader, temp_entity->direction) &&
           legacy_temp_read_u8(reader, &temp_entity->color);
}

static bool legacy_temp_read_position_pair(legacy_temp_reader_v1 *reader,
                                           q2proto_svc_temp_entity_t *temp_entity)
{
    return legacy_temp_read_position(reader, temp_entity->position1) &&
           legacy_temp_read_position(reader, temp_entity->position2);
}

static bool legacy_temp_read_source_position_pair(
    legacy_temp_reader_v1 *reader, q2proto_svc_temp_entity_t *temp_entity)
{
    return legacy_temp_read_i16(reader, &temp_entity->entity1) &&
           legacy_temp_read_position_pair(reader, temp_entity);
}

static bool legacy_temp_read_payload(legacy_temp_reader_v1 *reader,
                                     q2proto_svc_temp_entity_t *temp_entity)
{
    switch (temp_entity->type) {
    case WORR_EVENT_LEGACY_TEMP_GUNSHOT:
    case WORR_EVENT_LEGACY_TEMP_BLOOD:
    case WORR_EVENT_LEGACY_TEMP_BLASTER:
    case WORR_EVENT_LEGACY_TEMP_SHOTGUN:
    case WORR_EVENT_LEGACY_TEMP_SPARKS:
    case WORR_EVENT_LEGACY_TEMP_SCREEN_SPARKS:
    case WORR_EVENT_LEGACY_TEMP_SHIELD_SPARKS:
    case WORR_EVENT_LEGACY_TEMP_BULLET_SPARKS:
    case WORR_EVENT_LEGACY_TEMP_GREENBLOOD:
    case WORR_EVENT_LEGACY_TEMP_BLASTER2:
    case WORR_EVENT_LEGACY_TEMP_MOREBLOOD:
    case WORR_EVENT_LEGACY_TEMP_HEATBEAM_SPARKS:
    case WORR_EVENT_LEGACY_TEMP_HEATBEAM_STEAM:
    case WORR_EVENT_LEGACY_TEMP_ELECTRIC_SPARKS:
    case WORR_EVENT_LEGACY_TEMP_FLECHETTE:
    case WORR_EVENT_LEGACY_TEMP_BLUEHYPERBLASTER_FIXED:
    case WORR_EVENT_LEGACY_TEMP_BERSERK_SLAM:
        return legacy_temp_read_position_direction(reader, temp_entity);

    case WORR_EVENT_LEGACY_TEMP_SPLASH:
    case WORR_EVENT_LEGACY_TEMP_LASER_SPARKS:
    case WORR_EVENT_LEGACY_TEMP_WELDING_SPARKS:
    case WORR_EVENT_LEGACY_TEMP_TUNNEL_SPARKS:
        return legacy_temp_read_count_position_direction_color(reader,
                                                                temp_entity);

    case WORR_EVENT_LEGACY_TEMP_RAILTRAIL:
    case WORR_EVENT_LEGACY_TEMP_BUBBLETRAIL:
    case WORR_EVENT_LEGACY_TEMP_BFG_LASER:
    case WORR_EVENT_LEGACY_TEMP_BLUEHYPERBLASTER_BROKEN:
    case WORR_EVENT_LEGACY_TEMP_RAILTRAIL2:
    case WORR_EVENT_LEGACY_TEMP_DEBUGTRAIL:
    case WORR_EVENT_LEGACY_TEMP_BUBBLETRAIL2:
    case WORR_EVENT_LEGACY_TEMP_BFG_ZAP:
        return legacy_temp_read_position_pair(reader, temp_entity);

    case WORR_EVENT_LEGACY_TEMP_EXPLOSION1:
    case WORR_EVENT_LEGACY_TEMP_EXPLOSION2:
    case WORR_EVENT_LEGACY_TEMP_ROCKET_EXPLOSION:
    case WORR_EVENT_LEGACY_TEMP_GRENADE_EXPLOSION:
    case WORR_EVENT_LEGACY_TEMP_ROCKET_EXPLOSION_WATER:
    case WORR_EVENT_LEGACY_TEMP_GRENADE_EXPLOSION_WATER:
    case WORR_EVENT_LEGACY_TEMP_BFG_EXPLOSION:
    case WORR_EVENT_LEGACY_TEMP_BFG_BIGEXPLOSION:
    case WORR_EVENT_LEGACY_TEMP_BOSSTPORT:
    case WORR_EVENT_LEGACY_TEMP_PLASMA_EXPLOSION:
    case WORR_EVENT_LEGACY_TEMP_PLAIN_EXPLOSION:
    case WORR_EVENT_LEGACY_TEMP_CHAINFIST_SMOKE:
    case WORR_EVENT_LEGACY_TEMP_TRACKER_EXPLOSION:
    case WORR_EVENT_LEGACY_TEMP_TELEPORT_EFFECT:
    case WORR_EVENT_LEGACY_TEMP_DBALL_GOAL:
    case WORR_EVENT_LEGACY_TEMP_NUKEBLAST:
    case WORR_EVENT_LEGACY_TEMP_WIDOWSPLASH:
    case WORR_EVENT_LEGACY_TEMP_EXPLOSION1_BIG:
    case WORR_EVENT_LEGACY_TEMP_EXPLOSION1_NP:
    case WORR_EVENT_LEGACY_TEMP_EXPLOSION1_NL:
    case WORR_EVENT_LEGACY_TEMP_EXPLOSION2_NL:
        return legacy_temp_read_position(reader, temp_entity->position1);

    case WORR_EVENT_LEGACY_TEMP_PARASITE_ATTACK:
    case WORR_EVENT_LEGACY_TEMP_MEDIC_CABLE_ATTACK:
    case WORR_EVENT_LEGACY_TEMP_HEATBEAM:
    case WORR_EVENT_LEGACY_TEMP_MONSTER_HEATBEAM:
    case WORR_EVENT_LEGACY_TEMP_GRAPPLE_CABLE2:
    case WORR_EVENT_LEGACY_TEMP_LIGHTNING_BEAM:
        return legacy_temp_read_source_position_pair(reader, temp_entity);

    case WORR_EVENT_LEGACY_TEMP_GRAPPLE_CABLE:
        return legacy_temp_read_source_position_pair(reader, temp_entity) &&
               legacy_temp_read_position(reader, temp_entity->offset);

    case WORR_EVENT_LEGACY_TEMP_LIGHTNING:
        return legacy_temp_read_i16(reader, &temp_entity->entity1) &&
               legacy_temp_read_i16(reader, &temp_entity->entity2) &&
               legacy_temp_read_position_pair(reader, temp_entity);

    case WORR_EVENT_LEGACY_TEMP_FLASHLIGHT:
        return legacy_temp_read_position(reader, temp_entity->position1) &&
               legacy_temp_read_i16(reader, &temp_entity->entity1);

    case WORR_EVENT_LEGACY_TEMP_FORCEWALL:
        return legacy_temp_read_position_pair(reader, temp_entity) &&
               legacy_temp_read_u8(reader, &temp_entity->color);

    case WORR_EVENT_LEGACY_TEMP_STEAM:
        if (!legacy_temp_read_i16(reader, &temp_entity->entity1) ||
            !legacy_temp_read_u8(reader, &temp_entity->count) ||
            !legacy_temp_read_position(reader, temp_entity->position1) ||
            !legacy_temp_read_direction(reader, temp_entity->direction) ||
            !legacy_temp_read_u8(reader, &temp_entity->color) ||
            !legacy_temp_read_i16(reader, &temp_entity->entity2)) {
            return false;
        }
        return temp_entity->entity1 == -1 ||
               legacy_temp_read_i32(reader, &temp_entity->time);

    case WORR_EVENT_LEGACY_TEMP_WIDOWBEAMOUT:
        return legacy_temp_read_i16(reader, &temp_entity->entity1) &&
               legacy_temp_read_position(reader, temp_entity->position1);

    case WORR_EVENT_LEGACY_TEMP_POWER_SPLASH:
        return legacy_temp_read_i16(reader, &temp_entity->entity1) &&
               legacy_temp_read_u8(reader, &temp_entity->count);

    case WORR_EVENT_LEGACY_TEMP_DAMAGE_DEALT:
        return legacy_temp_read_u8(reader, &temp_entity->count);

    default:
        return false;
    }
}

static worr_legacy_temp_event_candidate_result_v1
legacy_temp_decode_one(legacy_temp_reader_v1 *reader,
                       q2proto_svc_temp_entity_t *temp_entity_out)
{
    q2proto_svc_temp_entity_t temp_entity;
    uint8_t opcode;

    if (!reader || !temp_entity_out ||
        !legacy_temp_read_u8(reader, &opcode) ||
        opcode != svc_temp_entity) {
        return WORR_LEGACY_TEMP_EVENT_CANDIDATE_INVALID_SHAPE;
    }

    memset(&temp_entity, 0, sizeof(temp_entity));
    if (!legacy_temp_read_u8(reader, &temp_entity.type))
        return WORR_LEGACY_TEMP_EVENT_CANDIDATE_INVALID_SHAPE;
    if (!legacy_temp_read_payload(reader, &temp_entity)) {
        uint16_t ignored_mask;
        const int16_t shape_probe_entity =
            temp_entity.type == WORR_EVENT_LEGACY_TEMP_STEAM ? -1 : 0;
        return Worr_EventLegacyTempFieldMaskV1(temp_entity.type,
                                               shape_probe_entity,
                                               &ignored_mask)
                   ? WORR_LEGACY_TEMP_EVENT_CANDIDATE_INVALID_SHAPE
                   : WORR_LEGACY_TEMP_EVENT_CANDIDATE_UNSUPPORTED_SUBTYPE;
    }

    *temp_entity_out = temp_entity;
    return WORR_LEGACY_TEMP_EVENT_CANDIDATE_OK;
}

worr_legacy_temp_event_candidate_result_v1
Worr_LegacyTempEventDecodeRawPrefixV1(
    const uint8_t *raw_message, size_t raw_message_size,
    q2proto_svc_temp_entity_t *temp_entity_out, size_t *bytes_consumed_out)
{
    legacy_temp_reader_v1 reader;
    q2proto_svc_temp_entity_t temp_entity;
    worr_legacy_temp_event_candidate_result_v1 result;

    if (!raw_message || !temp_entity_out || !bytes_consumed_out)
        return WORR_LEGACY_TEMP_EVENT_CANDIDATE_INVALID_ARGUMENT;
    if (raw_message_size == 0)
        return WORR_LEGACY_TEMP_EVENT_CANDIDATE_INVALID_SHAPE;

    reader.data = raw_message;
    reader.size = raw_message_size;
    reader.cursor = 0;
    result = legacy_temp_decode_one(&reader, &temp_entity);
    if (result != WORR_LEGACY_TEMP_EVENT_CANDIDATE_OK)
        return result;

    *temp_entity_out = temp_entity;
    *bytes_consumed_out = reader.cursor;
    return WORR_LEGACY_TEMP_EVENT_CANDIDATE_OK;
}

worr_legacy_temp_event_candidate_result_v1
Worr_LegacyTempEventDecodeRawSequenceV1(
    const uint8_t *raw_message, size_t raw_message_size,
    q2proto_svc_temp_entity_t *temp_entities_out, uint32_t capacity,
    uint32_t *count_out)
{
    legacy_temp_reader_v1 reader;
    q2proto_svc_temp_entity_t
        decoded[WORR_LEGACY_TEMP_EVENT_CANDIDATE_SEQUENCE_MAX];
    uint32_t count = 0;

    if (!raw_message || !temp_entities_out || !count_out || capacity == 0)
        return WORR_LEGACY_TEMP_EVENT_CANDIDATE_INVALID_ARGUMENT;
    if (raw_message_size == 0)
        return WORR_LEGACY_TEMP_EVENT_CANDIDATE_INVALID_SHAPE;

    reader.data = raw_message;
    reader.size = raw_message_size;
    reader.cursor = 0;
    while (reader.cursor != reader.size) {
        worr_legacy_temp_event_candidate_result_v1 result;

        if (count == capacity ||
            count == WORR_LEGACY_TEMP_EVENT_CANDIDATE_SEQUENCE_MAX) {
            return WORR_LEGACY_TEMP_EVENT_CANDIDATE_CAPACITY;
        }
        result = legacy_temp_decode_one(&reader, &decoded[count]);
        if (result != WORR_LEGACY_TEMP_EVENT_CANDIDATE_OK)
            return result;
        ++count;
    }

    memcpy(temp_entities_out, decoded, sizeof(decoded[0]) * count);
    *count_out = count;
    return WORR_LEGACY_TEMP_EVENT_CANDIDATE_OK;
}

worr_legacy_temp_event_candidate_result_v1
Worr_LegacyTempEventDecodeRawV1(const uint8_t *raw_message,
                                size_t raw_message_size,
                                q2proto_svc_temp_entity_t *temp_entity_out)
{
    q2proto_svc_temp_entity_t temp_entity;
    uint32_t count;
    worr_legacy_temp_event_candidate_result_v1 result;

    if (!temp_entity_out)
        return WORR_LEGACY_TEMP_EVENT_CANDIDATE_INVALID_ARGUMENT;
    result = Worr_LegacyTempEventDecodeRawSequenceV1(
        raw_message, raw_message_size, &temp_entity, 1, &count);
    if (result == WORR_LEGACY_TEMP_EVENT_CANDIDATE_CAPACITY)
        return WORR_LEGACY_TEMP_EVENT_CANDIDATE_INVALID_SHAPE;
    if (result != WORR_LEGACY_TEMP_EVENT_CANDIDATE_OK)
        return result;
    if (count != 1u)
        return WORR_LEGACY_TEMP_EVENT_CANDIDATE_INVALID_SHAPE;

    *temp_entity_out = temp_entity;
    return WORR_LEGACY_TEMP_EVENT_CANDIDATE_OK;
}

static bool temp_entity1_is_source(uint16_t subtype)
{
    switch (subtype) {
    case WORR_EVENT_LEGACY_TEMP_PARASITE_ATTACK:
    case WORR_EVENT_LEGACY_TEMP_MEDIC_CABLE_ATTACK:
    case WORR_EVENT_LEGACY_TEMP_GRAPPLE_CABLE:
    case WORR_EVENT_LEGACY_TEMP_LIGHTNING:
    case WORR_EVENT_LEGACY_TEMP_FLASHLIGHT:
    case WORR_EVENT_LEGACY_TEMP_HEATBEAM:
    case WORR_EVENT_LEGACY_TEMP_MONSTER_HEATBEAM:
    case WORR_EVENT_LEGACY_TEMP_GRAPPLE_CABLE2:
    case WORR_EVENT_LEGACY_TEMP_POWER_SPLASH:
    case WORR_EVENT_LEGACY_TEMP_LIGHTNING_BEAM:
        return true;
    default:
        return false;
    }
}

static bool entity_index_valid(int16_t entity, uint32_t max_entities)
{
    return entity >= 0 && max_entities != 0 &&
           (uint32_t)entity < max_entities;
}

worr_legacy_temp_event_candidate_result_v1
Worr_LegacyTempEventCandidateBuildV1(
    const q2proto_svc_temp_entity_t *temp_entity, uint32_t source_tick,
    uint64_t source_time_us, uint32_t max_entities,
    worr_event_record_v1 *candidate_out, uint32_t *source_entity_index_out,
    uint32_t *subject_entity_index_out)
{
    worr_event_record_v1 candidate;
    worr_event_payload_legacy_temp_v1 payload;
    uint32_t source_entity_index = 0;
    uint32_t subject_entity_index = WORR_EVENT_NO_ENTITY;
    uint16_t fields;
    const uint16_t subtype = temp_entity ? temp_entity->type : 0;

    if (!temp_entity || !candidate_out || !source_entity_index_out ||
        !subject_entity_index_out || max_entities == 0) {
        return WORR_LEGACY_TEMP_EVENT_CANDIDATE_INVALID_ARGUMENT;
    }
    if (!Worr_EventLegacyTempFieldMaskV1(
            subtype, temp_entity->entity1, &fields)) {
        return subtype == WORR_EVENT_LEGACY_TEMP_STEAM
                   ? WORR_LEGACY_TEMP_EVENT_CANDIDATE_INVALID_SHAPE
                   : WORR_LEGACY_TEMP_EVENT_CANDIDATE_UNSUPPORTED_SUBTYPE;
    }
    if (temp_entity1_is_source(subtype) &&
        !entity_index_valid(temp_entity->entity1, max_entities)) {
        return WORR_LEGACY_TEMP_EVENT_CANDIDATE_ENTITY_OUT_OF_RANGE;
    }
    if (subtype == WORR_EVENT_LEGACY_TEMP_LIGHTNING &&
        !entity_index_valid(temp_entity->entity2, max_entities)) {
        return WORR_LEGACY_TEMP_EVENT_CANDIDATE_ENTITY_OUT_OF_RANGE;
    }

    memset(&candidate, 0, sizeof(candidate));
    memset(&payload, 0, sizeof(payload));
    payload.subtype = subtype;
    payload.valid_fields = fields;
    if ((fields & WORR_EVENT_LEGACY_TEMP_FIELD_ENTITY1) != 0)
        payload.raw_entity1 = temp_entity->entity1;
    if ((fields & WORR_EVENT_LEGACY_TEMP_FIELD_ENTITY2) != 0)
        payload.raw_entity2 = temp_entity->entity2;
    if ((fields & WORR_EVENT_LEGACY_TEMP_FIELD_TIME) != 0)
        payload.time_ms = temp_entity->time;
    if ((fields & WORR_EVENT_LEGACY_TEMP_FIELD_COUNT) != 0)
        payload.count_or_amount = temp_entity->count;
    if ((fields & WORR_EVENT_LEGACY_TEMP_FIELD_COLOR) != 0)
        payload.color = temp_entity->color;
    if ((fields & WORR_EVENT_LEGACY_TEMP_FIELD_POSITION1) != 0)
        memcpy(payload.position1, temp_entity->position1,
               sizeof(payload.position1));
    if ((fields & WORR_EVENT_LEGACY_TEMP_FIELD_POSITION2) != 0)
        memcpy(payload.position2, temp_entity->position2,
               sizeof(payload.position2));
    if ((fields & WORR_EVENT_LEGACY_TEMP_FIELD_OFFSET) != 0)
        memcpy(payload.offset, temp_entity->offset, sizeof(payload.offset));
    if ((fields & WORR_EVENT_LEGACY_TEMP_FIELD_DIRECTION) != 0)
        memcpy(payload.direction, temp_entity->direction,
               sizeof(payload.direction));

    candidate.struct_size = sizeof(candidate);
    candidate.schema_version = WORR_EVENT_ABI_VERSION;
    candidate.model_revision = WORR_EVENT_MODEL_REVISION;
    candidate.flags = WORR_EVENT_FLAG_REPLAY_SAFE | WORR_EVENT_FLAG_PRESENT_ONCE;
    candidate.source_tick = source_tick;
    candidate.source_entity.index = WORR_EVENT_NO_ENTITY;
    candidate.subject_entity.index = WORR_EVENT_NO_ENTITY;
    candidate.source_time_us = source_time_us;
    candidate.event_type = subtype == WORR_EVENT_LEGACY_TEMP_DAMAGE_DEALT
                               ? WORR_EVENT_TYPE_GAMEPLAY_CUE
                               : WORR_EVENT_TYPE_VISUAL_EFFECT;
    candidate.delivery_class = WORR_EVENT_DELIVERY_TRANSIENT;
    candidate.prediction_class = WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY;
    candidate.expiry_tick = source_tick + 1u;
    candidate.payload_kind = WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1;
    candidate.payload_size = sizeof(payload);
    memcpy(candidate.payload, &payload, sizeof(payload));

    if (temp_entity1_is_source(subtype))
        source_entity_index = (uint32_t)temp_entity->entity1;
    if (subtype == WORR_EVENT_LEGACY_TEMP_LIGHTNING)
        subject_entity_index = (uint32_t)temp_entity->entity2;
    /*
     * cgame action templates intentionally leave both record references
     * unresolved.  Validate every other ABI property with a temporary valid
     * world source, then restore the template shape for the caller's lineage
     * resolver.
     */
    candidate.source_entity.index = 0;
    candidate.source_entity.generation = 1;
    if (!Worr_EventRecordCandidateValidateV1(&candidate, max_entities))
        return WORR_LEGACY_TEMP_EVENT_CANDIDATE_INVALID_RECORD;
    candidate.source_entity.index = WORR_EVENT_NO_ENTITY;
    candidate.source_entity.generation = 0;

    *candidate_out = candidate;
    *source_entity_index_out = source_entity_index;
    *subject_entity_index_out = subject_entity_index;
    return WORR_LEGACY_TEMP_EVENT_CANDIDATE_OK;
}
