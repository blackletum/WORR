/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/legacy_spatial_audio_event_candidate.h"

#include <string.h>

static bool entity_index_valid(int entity, uint32_t max_entities)
{
    return entity >= 0 && max_entities != 0 &&
           (uint32_t)entity < max_entities;
}

worr_legacy_spatial_audio_event_candidate_result_v1
Worr_LegacySpatialAudioEventCandidateBuildV1(
    const q2proto_sound_t *sound, uint32_t source_tick,
    uint64_t source_time_us, uint32_t max_entities,
    worr_event_record_v1 *candidate_out, uint32_t *source_entity_index_out)
{
    worr_event_record_v1 candidate;
    worr_event_payload_spatial_audio_v1 payload;
    uint32_t source_entity_index = 0;

    if (!sound || !candidate_out || !source_entity_index_out ||
        max_entities == 0) {
        return WORR_LEGACY_SPATIAL_AUDIO_EVENT_CANDIDATE_INVALID_ARGUMENT;
    }
    if (sound->index <= 0) {
        return WORR_LEGACY_SPATIAL_AUDIO_EVENT_CANDIDATE_INVALID_SHAPE;
    }
    if (sound->has_entity_channel &&
        !entity_index_valid(sound->entity, max_entities)) {
        return WORR_LEGACY_SPATIAL_AUDIO_EVENT_CANDIDATE_ENTITY_OUT_OF_RANGE;
    }
    if (sound->has_entity_channel &&
        (sound->channel < 0 || sound->channel > UINT16_MAX)) {
        return WORR_LEGACY_SPATIAL_AUDIO_EVENT_CANDIDATE_INVALID_SHAPE;
    }

    memset(&candidate, 0, sizeof(candidate));
    memset(&payload, 0, sizeof(payload));
    payload.asset_id = (uint32_t)sound->index;
    payload.raw_entity = WORR_EVENT_NO_ENTITY;
    payload.volume = sound->volume;
    payload.attenuation = sound->attenuation;
    payload.time_offset = sound->timeofs;
    payload.pitch = 1.0f;
    if (sound->has_entity_channel) {
        payload.flags |= WORR_EVENT_SPATIAL_AUDIO_HAS_ENTITY_CHANNEL;
        payload.channel = (uint16_t)sound->channel;
        payload.raw_entity = (uint32_t)sound->entity;
        source_entity_index = (uint32_t)sound->entity;
    }
    if (sound->has_position) {
        payload.flags |= WORR_EVENT_SPATIAL_AUDIO_HAS_POSITION;
        memcpy(payload.origin, sound->pos, sizeof(payload.origin));
    }

    candidate.struct_size = sizeof(candidate);
    candidate.schema_version = WORR_EVENT_ABI_VERSION;
    candidate.model_revision = WORR_EVENT_MODEL_REVISION;
    candidate.flags = WORR_EVENT_FLAG_REPLAY_SAFE | WORR_EVENT_FLAG_PRESENT_ONCE;
    candidate.source_tick = source_tick;
    candidate.source_entity.index = WORR_EVENT_NO_ENTITY;
    candidate.subject_entity.index = WORR_EVENT_NO_ENTITY;
    candidate.source_time_us = source_time_us;
    candidate.event_type = WORR_EVENT_TYPE_AUDIO_CUE;
    candidate.delivery_class = WORR_EVENT_DELIVERY_TRANSIENT;
    candidate.prediction_class = WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY;
    candidate.expiry_tick = source_tick + 1u;
    candidate.payload_kind = WORR_EVENT_PAYLOAD_SPATIAL_AUDIO_V1;
    candidate.payload_size = sizeof(payload);
    memcpy(candidate.payload, &payload, sizeof(payload));

    /* Validate payload/record semantics without leaking a fabricated entity
     * reference into the unresolved cgame/server action template. */
    candidate.source_entity.index = 0;
    candidate.source_entity.generation = 1;
    if (!Worr_EventRecordCandidateValidateV1(&candidate, max_entities)) {
        return WORR_LEGACY_SPATIAL_AUDIO_EVENT_CANDIDATE_INVALID_RECORD;
    }
    candidate.source_entity.index = WORR_EVENT_NO_ENTITY;
    candidate.source_entity.generation = 0;

    *candidate_out = candidate;
    *source_entity_index_out = source_entity_index;
    return WORR_LEGACY_SPATIAL_AUDIO_EVENT_CANDIDATE_OK;
}
