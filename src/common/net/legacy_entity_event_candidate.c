/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/legacy_entity_event_candidate.h"

#include <string.h>

bool Worr_LegacyEntityEventCandidateBuildV1(
    uint32_t source_tick, uint64_t source_time_us, uint32_t source_ordinal,
    worr_event_entity_ref_v1 source_entity, uint16_t raw_event,
    uint32_t max_entities, worr_event_record_v1 *candidate_out,
    uint64_t *semantic_hash_out) {
  worr_event_payload_legacy_entity_v1 payload;
  worr_event_record_v1 candidate;
  uint64_t semantic_hash;
  uint16_t event_type;
  uint16_t payload_flags;

  if (!candidate_out || !semantic_hash_out)
    return false;

  switch (raw_event) {
  case WORR_EVENT_LEGACY_ENTITY_ITEM_RESPAWN:
    event_type = WORR_EVENT_TYPE_VISUAL_EFFECT;
    payload_flags = WORR_EVENT_LEGACY_ENTITY_FLAG_PRESENTATION;
    break;
  case WORR_EVENT_LEGACY_ENTITY_FOOTSTEP:
  case WORR_EVENT_LEGACY_ENTITY_FALL_SHORT:
  case WORR_EVENT_LEGACY_ENTITY_FALL_MEDIUM:
  case WORR_EVENT_LEGACY_ENTITY_FALL_FAR:
  case WORR_EVENT_LEGACY_ENTITY_OTHER_FOOTSTEP:
  case WORR_EVENT_LEGACY_ENTITY_LADDER_STEP:
    event_type = WORR_EVENT_TYPE_MOVEMENT_CUE;
    payload_flags = WORR_EVENT_LEGACY_ENTITY_FLAG_PRESENTATION;
    break;
  case WORR_EVENT_LEGACY_ENTITY_PLAYER_TELEPORT:
    event_type = WORR_EVENT_TYPE_STATE_CHANGE;
    payload_flags = WORR_EVENT_LEGACY_ENTITY_FLAG_PRESENTATION |
                    WORR_EVENT_LEGACY_ENTITY_FLAG_DISCONTINUITY;
    break;
  case WORR_EVENT_LEGACY_ENTITY_OTHER_TELEPORT:
    event_type = WORR_EVENT_TYPE_STATE_CHANGE;
    payload_flags = WORR_EVENT_LEGACY_ENTITY_FLAG_DISCONTINUITY;
    break;
  default:
    return false;
  }

  memset(&candidate, 0, sizeof(candidate));
  memset(&payload, 0, sizeof(payload));
  payload.raw_event = raw_event;
  payload.flags = payload_flags;
  candidate.struct_size = sizeof(candidate);
  candidate.schema_version = WORR_EVENT_ABI_VERSION;
  candidate.model_revision = WORR_EVENT_MODEL_REVISION;
  candidate.flags = WORR_EVENT_FLAG_REPLAY_SAFE | WORR_EVENT_FLAG_PRESENT_ONCE;
  candidate.source_tick = source_tick;
  candidate.source_ordinal = source_ordinal;
  candidate.source_time_us = source_time_us;
  candidate.source_entity = source_entity;
  candidate.subject_entity.index = WORR_EVENT_NO_ENTITY;
  candidate.event_type = event_type;
  candidate.delivery_class = WORR_EVENT_DELIVERY_TRANSIENT;
  candidate.prediction_class = WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY;
  candidate.expiry_tick = source_tick + 1u;
  candidate.payload_kind = WORR_EVENT_PAYLOAD_LEGACY_ENTITY_V1;
  candidate.payload_size = sizeof(payload);
  memcpy(candidate.payload, &payload, sizeof(payload));

  if (!Worr_EventRecordCandidateValidateV1(&candidate, max_entities) ||
      !Worr_EventRecordSemanticHashV1(&candidate, max_entities,
                                      &semantic_hash)) {
    return false;
  }

  *candidate_out = candidate;
  *semantic_hash_out = semantic_hash;
  return true;
}
