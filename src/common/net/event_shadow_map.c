/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "../../../inc/shared/event_shadow.h"

#include <string.h>

worr_event_shadow_map_result_v1 Worr_EventShadowMapLegacyEntityV1(
    worr_event_shadow_source_state_v1 *source_state,
    const worr_event_shadow_legacy_input_v1 *input,
    worr_event_record_v1 *candidate_out)
{
    worr_event_shadow_source_state_v1 next_state;
    worr_event_record_v1 candidate;
    worr_event_payload_u32x4_v1 payload;
    uint64_t event_bit;
    uint32_t ordinal;

    if (!source_state || !input || !candidate_out ||
        input->struct_size != sizeof(*input) ||
        input->schema_version != WORR_EVENT_SHADOW_API_VERSION ||
        input->reserved0 != 0 || input->raw_event == 0 ||
        input->raw_event > WORR_EVENT_SHADOW_MAX_LEGACY_EVENT ||
        input->max_entities == 0 ||
        input->source_entity.index >= input->max_entities ||
        input->source_entity.generation == 0) {
        return WORR_EVENT_SHADOW_MAP_INVALID;
    }

    event_bit = UINT64_C(1) << input->raw_event;
    next_state = *source_state;
    if (!next_state.active || next_state.source_tick != input->source_tick ||
        next_state.entity_generation != input->source_entity.generation) {
        memset(&next_state, 0, sizeof(next_state));
        next_state.active = 1;
        next_state.source_tick = input->source_tick;
        next_state.entity_generation = input->source_entity.generation;
    } else if ((next_state.seen_event_mask & event_bit) != 0) {
        return WORR_EVENT_SHADOW_MAP_DUPLICATE;
    }

    ordinal = next_state.next_ordinal;
    if (ordinal == UINT32_MAX)
        return WORR_EVENT_SHADOW_MAP_INVALID;

    memset(&candidate, 0, sizeof(candidate));
    candidate.struct_size = sizeof(candidate);
    candidate.schema_version = WORR_EVENT_ABI_VERSION;
    candidate.model_revision = WORR_EVENT_MODEL_REVISION;
    candidate.flags = WORR_EVENT_FLAG_REPLAY_SAFE |
                      WORR_EVENT_FLAG_PRESENT_ONCE;
    candidate.source_tick = input->source_tick;
    candidate.source_ordinal = ordinal;
    candidate.source_time_us = input->source_time_us;
    candidate.source_entity = input->source_entity;
    candidate.subject_entity.index = WORR_EVENT_NO_ENTITY;
    candidate.event_type = WORR_EVENT_TYPE_LEGACY_BRIDGE;
    candidate.delivery_class = WORR_EVENT_DELIVERY_TRANSIENT;
    candidate.prediction_class =
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY;
    candidate.expiry_tick = input->source_tick + 1u;
    candidate.payload_kind = WORR_EVENT_PAYLOAD_U32X4;
    candidate.payload_size = sizeof(payload);

    payload.value[0] = input->raw_event;
    payload.value[1] = input->source_entity.index;
    payload.value[2] = input->source_entity.generation;
    payload.value[3] = ordinal;
    memcpy(candidate.payload, &payload, sizeof(payload));

    next_state.seen_event_mask |= event_bit;
    next_state.next_ordinal = ordinal + 1u;
    *source_state = next_state;
    *candidate_out = candidate;
    return WORR_EVENT_SHADOW_MAP_MAPPED;
}
