/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "snapshot_event_candidates_internal.h"

#include "common/net/legacy_entity_event_candidate.h"
#include "common/net/legacy_muzzle_event_candidate.h"
#include "common/net/legacy_spatial_audio_event_candidate.h"
#include "common/net/legacy_temp_event_candidate.h"

static bool final_projection_valid(const worr_snapshot_projection_view_v2 *view,
                                   uint32_t max_entities) {
  uint64_t entity_hash;
  uint64_t event_hash;

  if (!view || max_entities < 2 || view->struct_size != sizeof(*view) ||
      view->schema_version != WORR_SNAPSHOT_PROJECTION_VERSION ||
      view->reserved0 != 0 || !view->snapshot ||
      (view->entity_count != 0 && !view->entities) ||
      (view->event_ref_count != 0 && !view->event_refs) ||
      view->entity_count >= max_entities ||
      view->snapshot->entity_range.count != view->entity_count ||
      view->snapshot->event_range.count != view->event_ref_count ||
      !Worr_SnapshotValidateV2(view->snapshot, max_entities) ||
      !Worr_SnapshotEntityListHashV2(view->entities, view->entity_count,
                                     max_entities, &entity_hash) ||
      !Worr_SnapshotEventRefsHashV2(view->event_refs, view->event_ref_count,
                                    &event_hash) ||
      entity_hash != view->snapshot->entity_hash ||
      event_hash != view->snapshot->event_hash) {
    return false;
  }

  if (view->event_ref_count == 0) {
    return view->snapshot->event_range.provenance ==
           WORR_SNAPSHOT_EVENT_PROVENANCE_NONE;
  }
  return view->snapshot->event_range.provenance ==
             WORR_SNAPSHOT_EVENT_PROVENANCE_LEGACY_INFERRED &&
         view->event_refs[0].provenance ==
             WORR_SNAPSHOT_EVENT_PROVENANCE_LEGACY_INFERRED;
}

static sv_snapshot_event_candidates_result_v1 load_final_emission_view(
    sv_snapshot_shadow_peer_v1 *peer, sv_snapshot_shadow_ref_v1 ref,
    uint32_t max_entities, sv_snapshot_shadow_sent_v1 *sent_out,
    worr_snapshot_projection_view_v2 *view_out)
{
  worr_snapshot_projection_hashes_v2 hashes;
  sv_snapshot_shadow_result_v1 sent_result;
  sv_snapshot_shadow_result_v1 view_result;

  sent_result = SV_SnapshotShadowGetSentV1(peer, ref, sent_out);
  if (sent_result == SV_SNAPSHOT_SHADOW_STALE_REF)
    return SV_SNAPSHOT_EVENT_CANDIDATES_STALE_REF;
  if (sent_result != SV_SNAPSHOT_SHADOW_OK)
    return SV_SNAPSHOT_EVENT_CANDIDATES_INVALID_PROJECTION;

  view_result = SV_SnapshotShadowViewV1(peer, ref, view_out, &hashes);
  if (view_result == SV_SNAPSHOT_SHADOW_STALE_REF)
    return SV_SNAPSHOT_EVENT_CANDIDATES_STALE_REF;
  if (view_result != SV_SNAPSHOT_SHADOW_OK ||
      !final_projection_valid(view_out, max_entities)) {
    return SV_SNAPSHOT_EVENT_CANDIDATES_INVALID_PROJECTION;
  }
  return SV_SNAPSHOT_EVENT_CANDIDATES_OK;
}

static sv_snapshot_event_candidates_result_v1 resolve_snapshot_entity(
    const worr_snapshot_projection_view_v2 *view,
    uint32_t entity_index, worr_event_entity_ref_v1 *entity_out)
{
    uint32_t index;

    if (!view || !entity_out)
      return SV_SNAPSHOT_EVENT_CANDIDATES_INVALID_ARGUMENT;
    if (entity_index == 0) {
      entity_out->index = 0;
      entity_out->generation = 1;
      return SV_SNAPSHOT_EVENT_CANDIDATES_OK;
    }
    for (index = 0; index < view->entity_count; ++index) {
      if (view->entities[index].generation.identity.index == entity_index)
        break;
    }
    if (index == view->entity_count)
      return SV_SNAPSHOT_EVENT_CANDIDATES_SOURCE_NOT_VISIBLE;

    *entity_out = view->entities[index].generation.identity;
    return SV_SNAPSHOT_EVENT_CANDIDATES_OK;
}

static sv_snapshot_event_candidates_result_v1 bind_visible_source_entity(
    const worr_snapshot_projection_view_v2 *view,
    uint32_t source_entity_index, uint32_t max_entities,
    worr_event_record_v1 *candidate)
{
  sv_snapshot_event_candidates_result_v1 result;

  if (!candidate)
    return SV_SNAPSHOT_EVENT_CANDIDATES_INVALID_ARGUMENT;
  result = resolve_snapshot_entity(view, source_entity_index,
                                   &candidate->source_entity);
  if (result != SV_SNAPSHOT_EVENT_CANDIDATES_OK)
    return result;

  if (!Worr_EventRecordCandidateValidateV1(candidate, max_entities))
    return SV_SNAPSHOT_EVENT_CANDIDATES_INVALID_CANDIDATE;
  return SV_SNAPSHOT_EVENT_CANDIDATES_OK;
}

static sv_snapshot_event_candidates_result_v1 bind_visible_subject_entity(
    const worr_snapshot_projection_view_v2 *view,
    uint32_t subject_entity_index, uint32_t max_entities,
    worr_event_record_v1 *candidate)
{
  sv_snapshot_event_candidates_result_v1 result;

  if (!candidate)
    return SV_SNAPSHOT_EVENT_CANDIDATES_INVALID_ARGUMENT;
  result = resolve_snapshot_entity(view, subject_entity_index,
                                   &candidate->subject_entity);
  if (result != SV_SNAPSHOT_EVENT_CANDIDATES_OK)
    return result;

  if (!Worr_EventRecordCandidateValidateV1(candidate, max_entities))
    return SV_SNAPSHOT_EVENT_CANDIDATES_INVALID_CANDIDATE;
  return SV_SNAPSHOT_EVENT_CANDIDATES_OK;
}

static sv_snapshot_event_candidates_result_v1
scan_candidates(const worr_snapshot_projection_view_v2 *view,
                const q2proto_svc_frame_entity_delta_t *entity_deltas,
                uint32_t entity_delta_count, uint32_t max_entities,
                sv_snapshot_event_candidate_source_v1 *sources_out,
                uint32_t *source_count_out) {
  uint32_t entity_cursor = 0;
  uint32_t event_index = 0;
  uint32_t previous_entity = 0;
  uint32_t delta_index;

  for (delta_index = 0; delta_index + 1u < entity_delta_count; ++delta_index) {
    const q2proto_svc_frame_entity_delta_t *carrier =
        &entity_deltas[delta_index];
    const uint32_t entity_index = carrier->newnum;
    uint16_t raw_event;
    worr_event_record_v1 candidate;
    uint64_t semantic_hash;

    if (entity_index == 0 || entity_index >= max_entities ||
        entity_index <= previous_entity) {
      return SV_SNAPSHOT_EVENT_CANDIDATES_INVALID_PROJECTION;
    }
    previous_entity = entity_index;
    if (carrier->remove)
      continue;

    raw_event = (carrier->entity_delta.delta_bits & Q2P_ESD_EVENT) != 0
                    ? carrier->entity_delta.event
                    : 0;
    if (raw_event == 0)
      continue;

    while (entity_cursor < view->entity_count &&
           view->entities[entity_cursor].generation.identity.index <
               entity_index) {
      ++entity_cursor;
    }
    if (entity_cursor >= view->entity_count ||
        view->entities[entity_cursor].generation.identity.index !=
            entity_index ||
        event_index >= view->event_ref_count) {
      return SV_SNAPSHOT_EVENT_CANDIDATES_SEMANTIC_MISMATCH;
    }
    if (!Worr_LegacyEntityEventCandidateBuildV1(
            view->snapshot->server_tick, view->snapshot->server_time_us,
            entity_cursor, view->entities[entity_cursor].generation.identity,
            raw_event, max_entities, &candidate, &semantic_hash) ||
        semantic_hash != view->event_refs[event_index].semantic_hash) {
      return SV_SNAPSHOT_EVENT_CANDIDATES_SEMANTIC_MISMATCH;
    }
    if (sources_out) {
      sources_out[event_index].source_ordinal = entity_cursor;
      sources_out[event_index].source_entity =
          view->entities[entity_cursor].generation.identity;
      sources_out[event_index].raw_event = raw_event;
      sources_out[event_index].reserved0 = 0;
    }
    ++event_index;
  }

  if (event_index != view->event_ref_count)
    return SV_SNAPSHOT_EVENT_CANDIDATES_SEMANTIC_MISMATCH;
  *source_count_out = event_index;
  return SV_SNAPSHOT_EVENT_CANDIDATES_OK;
}

sv_snapshot_event_candidates_result_v1
SV_SnapshotEventCandidatesBuildFinalEmissionInternalV1(
    const worr_snapshot_projection_view_v2 *view,
    const q2proto_svc_frame_entity_delta_t *entity_deltas,
    uint32_t entity_delta_count, uint32_t max_entities,
    sv_snapshot_event_candidate_source_v1 *sources_out,
    uint32_t source_capacity, uint32_t *source_count_out) {
  sv_snapshot_event_candidates_result_v1 result;
  uint32_t source_count;

  if (!entity_deltas || entity_delta_count == 0 || !source_count_out ||
      entity_deltas[entity_delta_count - 1u].newnum != 0 ||
      entity_deltas[entity_delta_count - 1u].remove ||
      (source_capacity != 0 && !sources_out)) {
    return SV_SNAPSHOT_EVENT_CANDIDATES_INVALID_ARGUMENT;
  }
  if (!final_projection_valid(view, max_entities))
    return SV_SNAPSHOT_EVENT_CANDIDATES_INVALID_PROJECTION;

  result = scan_candidates(view, entity_deltas, entity_delta_count,
                           max_entities, NULL, &source_count);
  if (result != SV_SNAPSHOT_EVENT_CANDIDATES_OK)
    return result;
  if (source_count > source_capacity) {
    *source_count_out = source_count;
    return SV_SNAPSHOT_EVENT_CANDIDATES_CAPACITY;
  }
  if (source_count != 0 && !sources_out)
    return SV_SNAPSHOT_EVENT_CANDIDATES_INVALID_ARGUMENT;

  result = scan_candidates(view, entity_deltas, entity_delta_count,
                           max_entities, sources_out, &source_count);
  if (result != SV_SNAPSHOT_EVENT_CANDIDATES_OK)
    return result;
  *source_count_out = source_count;
  return SV_SNAPSHOT_EVENT_CANDIDATES_OK;
}

sv_snapshot_event_candidates_result_v1
SV_SnapshotShadowBuildSpatialAudioCandidateV1(
    sv_snapshot_shadow_peer_v1 *peer, sv_snapshot_shadow_ref_v1 ref,
    uint32_t max_entities, const q2proto_sound_t *sound,
    worr_event_record_v1 *candidate_out) {
  worr_snapshot_projection_view_v2 view;
  sv_snapshot_shadow_sent_v1 sent;
  worr_event_record_v1 candidate;
  uint32_t source_entity_index;
  sv_snapshot_event_candidates_result_v1 bind_result;
  worr_legacy_spatial_audio_event_candidate_result_v1 result;

  if (!peer || !sound || !candidate_out || max_entities == 0)
    return SV_SNAPSHOT_EVENT_CANDIDATES_INVALID_ARGUMENT;
  bind_result = load_final_emission_view(peer, ref, max_entities, &sent, &view);
  if (bind_result != SV_SNAPSHOT_EVENT_CANDIDATES_OK)
    return bind_result;

  result = Worr_LegacySpatialAudioEventCandidateBuildV1(
      sound, sent.wire_snapshot_number, sent.snapshot.server_time_us,
      max_entities, &candidate, &source_entity_index);
  if (result == WORR_LEGACY_SPATIAL_AUDIO_EVENT_CANDIDATE_ENTITY_OUT_OF_RANGE)
    return SV_SNAPSHOT_EVENT_CANDIDATES_SOURCE_NOT_VISIBLE;
  if (result != WORR_LEGACY_SPATIAL_AUDIO_EVENT_CANDIDATE_OK)
    return SV_SNAPSHOT_EVENT_CANDIDATES_INVALID_CANDIDATE;

  if (sound->has_entity_channel) {
    bind_result = bind_visible_source_entity(
        &view, source_entity_index, max_entities, &candidate);
    if (bind_result == SV_SNAPSHOT_EVENT_CANDIDATES_OK) {
      *candidate_out = candidate;
      return SV_SNAPSHOT_EVENT_CANDIDATES_OK;
    }
    if (bind_result != SV_SNAPSHOT_EVENT_CANDIDATES_SOURCE_NOT_VISIBLE ||
        !sound->has_position) {
      return bind_result;
    }
  } else if (!sound->has_position) {
    return SV_SNAPSHOT_EVENT_CANDIDATES_INVALID_ARGUMENT;
  }

  {
    worr_event_payload_spatial_audio_v1 payload;

    memcpy(&payload, candidate.payload, sizeof(payload));
    payload.flags |= WORR_EVENT_SPATIAL_AUDIO_POSITION_FORCED;
    memcpy(candidate.payload, &payload, sizeof(payload));
  }
  bind_result = bind_visible_source_entity(&view, 0, max_entities, &candidate);
  if (bind_result != SV_SNAPSHOT_EVENT_CANDIDATES_OK)
    return bind_result;
  *candidate_out = candidate;
  return SV_SNAPSHOT_EVENT_CANDIDATES_OK;
}

sv_snapshot_event_candidates_result_v1
SV_SnapshotShadowBuildMuzzleCandidateV1(
    sv_snapshot_shadow_peer_v1 *peer, sv_snapshot_shadow_ref_v1 ref,
    uint32_t max_entities, const q2proto_svc_muzzleflash_t *muzzleflash,
    uint32_t family, worr_event_record_v1 *candidate_out)
{
  worr_snapshot_projection_view_v2 view;
  sv_snapshot_shadow_sent_v1 sent;
  worr_event_record_v1 candidate;
  uint32_t source_entity_index;
  sv_snapshot_event_candidates_result_v1 bind_result;
  worr_legacy_muzzle_event_candidate_result_v1 result;

  if (!peer || !muzzleflash || !candidate_out || max_entities == 0)
    return SV_SNAPSHOT_EVENT_CANDIDATES_INVALID_ARGUMENT;
  bind_result = load_final_emission_view(peer, ref, max_entities, &sent, &view);
  if (bind_result != SV_SNAPSHOT_EVENT_CANDIDATES_OK)
    return bind_result;

  result = Worr_LegacyMuzzleEventCandidateBuildV1(
      muzzleflash, family, sent.wire_snapshot_number,
      sent.snapshot.server_time_us, max_entities, &candidate,
      &source_entity_index);
  if (result == WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_ENTITY_OUT_OF_RANGE)
    return SV_SNAPSHOT_EVENT_CANDIDATES_SOURCE_NOT_VISIBLE;
  if (result != WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_OK)
    return SV_SNAPSHOT_EVENT_CANDIDATES_INVALID_CANDIDATE;

  bind_result = bind_visible_source_entity(
      &view, source_entity_index, max_entities, &candidate);
  if (bind_result != SV_SNAPSHOT_EVENT_CANDIDATES_OK)
    return bind_result;
  *candidate_out = candidate;
  return SV_SNAPSHOT_EVENT_CANDIDATES_OK;
}

sv_snapshot_event_candidates_result_v1
SV_SnapshotShadowBuildMuzzleCandidatesV1(
    sv_snapshot_shadow_peer_v1 *peer, sv_snapshot_shadow_ref_v1 ref,
    uint32_t max_entities, const q2proto_svc_muzzleflash_t *muzzleflashes,
    const uint32_t *families, uint32_t muzzle_count,
    worr_event_record_v1 *candidates_out)
{
  worr_event_record_v1
      candidates[WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_SEQUENCE_MAX];
  uint32_t index;

  if (!peer || !muzzleflashes || !families || !candidates_out ||
      muzzle_count == 0 || max_entities == 0) {
    return SV_SNAPSHOT_EVENT_CANDIDATES_INVALID_ARGUMENT;
  }
  if (muzzle_count > WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_SEQUENCE_MAX)
    return SV_SNAPSHOT_EVENT_CANDIDATES_CAPACITY;

  for (index = 0; index < muzzle_count; ++index) {
    const sv_snapshot_event_candidates_result_v1 result =
        SV_SnapshotShadowBuildMuzzleCandidateV1(
            peer, ref, max_entities, &muzzleflashes[index], families[index],
            &candidates[index]);
    if (result != SV_SNAPSHOT_EVENT_CANDIDATES_OK)
      return result;
  }
  for (index = 0; index < muzzle_count; ++index)
    candidates_out[index] = candidates[index];
  return SV_SNAPSHOT_EVENT_CANDIDATES_OK;
}

sv_snapshot_event_candidates_result_v1
SV_SnapshotShadowBuildGameEventCandidatesV1(
    sv_snapshot_shadow_peer_v1 *peer, sv_snapshot_shadow_ref_v1 ref,
    uint32_t max_entities,
    const worr_legacy_game_event_candidate_carrier_v1 *carriers,
    uint32_t carrier_count, worr_event_record_v1 *candidates_out)
{
  worr_event_record_v1
      candidates[WORR_LEGACY_GAME_EVENT_CANDIDATE_SEQUENCE_MAX];
  uint32_t index;

  if (!peer || !carriers || !candidates_out || carrier_count == 0 ||
      max_entities == 0) {
    return SV_SNAPSHOT_EVENT_CANDIDATES_INVALID_ARGUMENT;
  }
  if (carrier_count > WORR_LEGACY_GAME_EVENT_CANDIDATE_SEQUENCE_MAX)
    return SV_SNAPSHOT_EVENT_CANDIDATES_CAPACITY;

  for (index = 0; index < carrier_count; ++index) {
    sv_snapshot_event_candidates_result_v1 result;

    if (carriers[index].reserved0 != 0)
      return SV_SNAPSHOT_EVENT_CANDIDATES_INVALID_CANDIDATE;
    switch (carriers[index].kind) {
    case WORR_LEGACY_GAME_EVENT_CANDIDATE_TEMP_ENTITY:
      if (carriers[index].muzzle_family != 0)
        return SV_SNAPSHOT_EVENT_CANDIDATES_INVALID_CANDIDATE;
      result = SV_SnapshotShadowBuildTempCandidateV1(
          peer, ref, max_entities, &carriers[index].temp_entity,
          &candidates[index]);
      break;
    case WORR_LEGACY_GAME_EVENT_CANDIDATE_MUZZLEFLASH:
      result = SV_SnapshotShadowBuildMuzzleCandidateV1(
          peer, ref, max_entities, &carriers[index].muzzleflash,
          carriers[index].muzzle_family, &candidates[index]);
      break;
    default:
      return SV_SNAPSHOT_EVENT_CANDIDATES_INVALID_CANDIDATE;
    }
    if (result != SV_SNAPSHOT_EVENT_CANDIDATES_OK)
      return result;
  }
  for (index = 0; index < carrier_count; ++index)
    candidates_out[index] = candidates[index];
  return SV_SNAPSHOT_EVENT_CANDIDATES_OK;
}

sv_snapshot_event_candidates_result_v1
SV_SnapshotShadowBuildTempCandidateV1(
    sv_snapshot_shadow_peer_v1 *peer, sv_snapshot_shadow_ref_v1 ref,
    uint32_t max_entities, const q2proto_svc_temp_entity_t *temp_entity,
    worr_event_record_v1 *candidate_out)
{
  worr_snapshot_projection_view_v2 view;
  sv_snapshot_shadow_sent_v1 sent;
  worr_event_record_v1 candidate;
  uint32_t source_entity_index;
  uint32_t subject_entity_index;
  sv_snapshot_event_candidates_result_v1 bind_result;
  worr_legacy_temp_event_candidate_result_v1 result;

  if (!peer || !temp_entity || !candidate_out || max_entities == 0)
    return SV_SNAPSHOT_EVENT_CANDIDATES_INVALID_ARGUMENT;
  bind_result = load_final_emission_view(peer, ref, max_entities, &sent, &view);
  if (bind_result != SV_SNAPSHOT_EVENT_CANDIDATES_OK)
    return bind_result;

  result = Worr_LegacyTempEventCandidateBuildV1(
      temp_entity, sent.wire_snapshot_number, sent.snapshot.server_time_us,
      max_entities, &candidate, &source_entity_index, &subject_entity_index);
  if (result == WORR_LEGACY_TEMP_EVENT_CANDIDATE_ENTITY_OUT_OF_RANGE)
    return SV_SNAPSHOT_EVENT_CANDIDATES_SOURCE_NOT_VISIBLE;
  if (result != WORR_LEGACY_TEMP_EVENT_CANDIDATE_OK)
    return SV_SNAPSHOT_EVENT_CANDIDATES_INVALID_CANDIDATE;

  bind_result = bind_visible_source_entity(
      &view, source_entity_index, max_entities, &candidate);
  if (bind_result != SV_SNAPSHOT_EVENT_CANDIDATES_OK)
    return bind_result;
  if (subject_entity_index != WORR_EVENT_NO_ENTITY) {
    bind_result = bind_visible_subject_entity(
        &view, subject_entity_index, max_entities, &candidate);
    if (bind_result != SV_SNAPSHOT_EVENT_CANDIDATES_OK)
      return bind_result;
  }
  if (!Worr_EventRecordCandidateValidateV1(&candidate, max_entities))
    return SV_SNAPSHOT_EVENT_CANDIDATES_INVALID_CANDIDATE;

  *candidate_out = candidate;
  return SV_SNAPSHOT_EVENT_CANDIDATES_OK;
}

sv_snapshot_event_candidates_result_v1
SV_SnapshotShadowBuildTempCandidatesV1(
    sv_snapshot_shadow_peer_v1 *peer, sv_snapshot_shadow_ref_v1 ref,
    uint32_t max_entities, const q2proto_svc_temp_entity_t *temp_entities,
    uint32_t temp_count, worr_event_record_v1 *candidates_out)
{
  worr_event_record_v1
      candidates[WORR_LEGACY_TEMP_EVENT_CANDIDATE_SEQUENCE_MAX];
  uint32_t index;

  if (!peer || !temp_entities || !candidates_out || temp_count == 0 ||
      max_entities == 0) {
    return SV_SNAPSHOT_EVENT_CANDIDATES_INVALID_ARGUMENT;
  }
  if (temp_count > WORR_LEGACY_TEMP_EVENT_CANDIDATE_SEQUENCE_MAX)
    return SV_SNAPSHOT_EVENT_CANDIDATES_CAPACITY;

  for (index = 0; index < temp_count; ++index) {
    const sv_snapshot_event_candidates_result_v1 result =
        SV_SnapshotShadowBuildTempCandidateV1(
            peer, ref, max_entities, &temp_entities[index],
            &candidates[index]);
    if (result != SV_SNAPSHOT_EVENT_CANDIDATES_OK)
      return result;
  }
  for (index = 0; index < temp_count; ++index)
    candidates_out[index] = candidates[index];
  return SV_SNAPSHOT_EVENT_CANDIDATES_OK;
}
