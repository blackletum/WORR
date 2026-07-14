/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "server/snapshot_shadow.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct sv_snapshot_shadow_peer_v1_s {
    sv_snapshot_shadow_config_v1 config;
    worr_snapshot_q2proto_context_v2 context;
    worr_snapshot_q2proto_slot_v2 *slots;
    worr_snapshot_entity_v2 *entities;
    uint8_t *area_bytes;
    worr_snapshot_event_ref_v2 *event_refs;
    worr_snapshot_q2proto_lineage_v2 *lineages;
    worr_snapshot_entity_v2 *baselines;
    uint8_t *baseline_present;
    worr_snapshot_entity_v2 *scratch_entities;
    uint8_t *scratch_area_bytes;
    worr_snapshot_event_ref_v2 *scratch_event_refs;
    worr_snapshot_q2proto_lineage_v2 *scratch_lineage;
    q2proto_svc_frame_entity_delta_t *pending_deltas;
    uint8_t *pending_area_bytes;
    sv_snapshot_shadow_sent_v1 *sent;

    q2proto_svc_frame_t pending_frame;
    sv_snapshot_shadow_frame_v1 pending_input;
    uint32_t pending_delta_count;
    uint32_t pending_delta_capacity;
    uint32_t pending_flags;
    uint32_t capture_failed;
    uint32_t active;
    uint32_t pending;
    uint64_t next_commit_serial;
    sv_snapshot_shadow_status_v1 status;
};

static void increment_saturating(uint64_t *value)
{
    if (*value != UINT64_MAX)
        ++*value;
}

static void set_result(sv_snapshot_shadow_peer_v1 *peer,
                       sv_snapshot_shadow_result_v1 result)
{
    if (peer)
        peer->status.last_result = (uint32_t)result;
}

static bool checked_product_u32(uint32_t left, uint32_t right,
                                uint32_t *product_out)
{
    const uint64_t product = (uint64_t)left * (uint64_t)right;
    if (!product_out || product == 0 || product > UINT32_MAX)
        return false;
    *product_out = (uint32_t)product;
    return true;
}

static void *allocate_array(uint32_t count, size_t element_size,
                            uint64_t *bytes)
{
    size_t allocation_size;
    void *allocation;

    if (!count || !element_size || count > SIZE_MAX / element_size)
        return NULL;
    allocation_size = (size_t)count * element_size;
    if (!bytes || *bytes > UINT64_MAX - allocation_size)
        return NULL;
    allocation = calloc(1, allocation_size);
    if (allocation)
        *bytes += allocation_size;
    return allocation;
}

static void destroy_storage(sv_snapshot_shadow_peer_v1 *peer)
{
    if (!peer)
        return;
    free(peer->slots);
    free(peer->entities);
    free(peer->area_bytes);
    free(peer->event_refs);
    free(peer->lineages);
    free(peer->baselines);
    free(peer->baseline_present);
    free(peer->scratch_entities);
    free(peer->scratch_area_bytes);
    free(peer->scratch_event_refs);
    free(peer->scratch_lineage);
    free(peer->pending_deltas);
    free(peer->pending_area_bytes);
    free(peer->sent);
}

static bool config_valid(const sv_snapshot_shadow_config_v1 *config)
{
    uint64_t delta_capacity;

    if (!config || config->struct_size != sizeof(*config) ||
        config->schema_version != SV_SNAPSHOT_SHADOW_VERSION ||
        config->snapshot_epoch == 0 || config->max_entities < 2 ||
        config->max_models == 0 || config->max_sounds == 0 ||
        config->slot_capacity < 2 || config->entities_per_slot == 0 ||
        config->entities_per_slot >= config->max_entities ||
        config->area_bytes_per_slot == 0 || config->reserved0[0] != 0 ||
        config->reserved0[1] != 0 || config->reserved0[2] != 0 ||
        config->extended_entity_state > 1) {
        return false;
    }
    delta_capacity = (uint64_t)config->entities_per_slot * 2u + 1u;
    return delta_capacity <= UINT32_MAX;
}

sv_snapshot_shadow_peer_v1 *SV_SnapshotShadowCreateV1(
    const sv_snapshot_shadow_config_v1 *config)
{
    sv_snapshot_shadow_peer_v1 *peer;
    worr_snapshot_q2proto_profile_v2 profile;
    worr_snapshot_q2proto_storage_v2 storage;
    uint32_t entity_total;
    uint32_t area_total;
    uint32_t event_total;
    uint32_t lineage_total;
    uint64_t bytes = 0;

    if (!config_valid(config) ||
        !checked_product_u32(config->slot_capacity,
                             config->entities_per_slot, &entity_total) ||
        !checked_product_u32(config->slot_capacity,
                             config->area_bytes_per_slot, &area_total) ||
        !checked_product_u32(config->slot_capacity,
                             config->entities_per_slot, &event_total) ||
        !checked_product_u32(config->slot_capacity, config->max_entities,
                             &lineage_total)) {
        return NULL;
    }

    peer = calloc(1, sizeof(*peer));
    if (!peer)
        return NULL;
    bytes = sizeof(*peer);
    peer->config = *config;
    peer->pending_delta_capacity = config->entities_per_slot * 2u + 1u;

#define ALLOCATE(member, type, count)                                        \
    do {                                                                      \
        peer->member = allocate_array((count), sizeof(type), &bytes);         \
        if (!peer->member)                                                    \
            goto allocation_failed;                                          \
    } while (0)

    ALLOCATE(slots, worr_snapshot_q2proto_slot_v2,
             config->slot_capacity);
    ALLOCATE(entities, worr_snapshot_entity_v2, entity_total);
    ALLOCATE(area_bytes, uint8_t, area_total);
    ALLOCATE(event_refs, worr_snapshot_event_ref_v2, event_total);
    ALLOCATE(lineages, worr_snapshot_q2proto_lineage_v2, lineage_total);
    ALLOCATE(baselines, worr_snapshot_entity_v2, config->max_entities);
    ALLOCATE(baseline_present, uint8_t, config->max_entities);
    ALLOCATE(scratch_entities, worr_snapshot_entity_v2,
             config->entities_per_slot);
    ALLOCATE(scratch_area_bytes, uint8_t, config->area_bytes_per_slot);
    ALLOCATE(scratch_event_refs, worr_snapshot_event_ref_v2,
             config->entities_per_slot);
    ALLOCATE(scratch_lineage, worr_snapshot_q2proto_lineage_v2,
             config->max_entities);
    ALLOCATE(pending_deltas, q2proto_svc_frame_entity_delta_t,
             peer->pending_delta_capacity);
    ALLOCATE(pending_area_bytes, uint8_t, config->area_bytes_per_slot);
    ALLOCATE(sent, sv_snapshot_shadow_sent_v1, config->slot_capacity);
#undef ALLOCATE

    memset(&profile, 0, sizeof(profile));
    profile.struct_size = sizeof(profile);
    profile.schema_version = WORR_SNAPSHOT_Q2PROTO_VERSION;
    profile.snapshot_epoch = config->snapshot_epoch;
    profile.max_entities = config->max_entities;
    profile.max_models = config->max_models;
    profile.max_sounds = config->max_sounds;
    profile.beam_renderfx_mask = config->beam_renderfx_mask;
    profile.legacy_renderfx_allowed_mask =
        config->legacy_renderfx_allowed_mask;
    profile.legacy_beam_clear_mask = config->legacy_beam_clear_mask;
    profile.extended_entity_state = config->extended_entity_state;

    memset(&storage, 0, sizeof(storage));
    storage.struct_size = sizeof(storage);
    storage.schema_version = WORR_SNAPSHOT_Q2PROTO_VERSION;
    storage.slots = peer->slots;
    storage.entities = peer->entities;
    storage.area_bytes = peer->area_bytes;
    storage.event_refs = peer->event_refs;
    storage.lineages = peer->lineages;
    storage.baselines = peer->baselines;
    storage.baseline_present = peer->baseline_present;
    storage.scratch_entities = peer->scratch_entities;
    storage.scratch_area_bytes = peer->scratch_area_bytes;
    storage.scratch_event_refs = peer->scratch_event_refs;
    storage.scratch_lineage = peer->scratch_lineage;
    storage.slot_capacity = config->slot_capacity;
    storage.entities_per_slot = config->entities_per_slot;
    storage.area_bytes_per_slot = config->area_bytes_per_slot;
    storage.event_refs_per_slot = config->entities_per_slot;
    storage.entity_storage_capacity = entity_total;
    storage.area_storage_capacity = area_total;
    storage.event_storage_capacity = event_total;
    storage.lineage_storage_capacity = lineage_total;
    storage.scratch_entity_capacity = config->entities_per_slot;
    storage.scratch_area_capacity = config->area_bytes_per_slot;
    storage.scratch_event_capacity = config->entities_per_slot;
    storage.scratch_lineage_capacity = config->max_entities;
    if (Worr_SnapshotQ2ProtoInitV2(&peer->context, &profile, &storage) !=
        WORR_SNAPSHOT_Q2PROTO_OK) {
        goto allocation_failed;
    }

    peer->active = 1;
    peer->next_commit_serial = 1;
    peer->status.struct_size = sizeof(peer->status);
    peer->status.schema_version = SV_SNAPSHOT_SHADOW_VERSION;
    peer->status.active = 1;
    peer->status.pending_delta_capacity = peer->pending_delta_capacity;
    peer->status.slot_capacity = config->slot_capacity;
    peer->status.snapshot_epoch = config->snapshot_epoch;
    peer->status.allocation_bytes = bytes;
    return peer;

allocation_failed:
    destroy_storage(peer);
    free(peer);
    return NULL;
}

void SV_SnapshotShadowDestroyV1(sv_snapshot_shadow_peer_v1 *peer)
{
    if (!peer)
        return;
    destroy_storage(peer);
    memset(peer, 0, sizeof(*peer));
    free(peer);
}

sv_snapshot_shadow_result_v1 SV_SnapshotShadowSetBaselineV1(
    sv_snapshot_shadow_peer_v1 *peer, uint32_t entity_index,
    const q2proto_entity_state_delta_t *baseline_delta)
{
    worr_snapshot_q2proto_result_v2 result;

    if (!peer || !baseline_delta) {
        set_result(peer, SV_SNAPSHOT_SHADOW_INVALID_ARGUMENT);
        return SV_SNAPSHOT_SHADOW_INVALID_ARGUMENT;
    }
    if (!peer->active) {
        set_result(peer, SV_SNAPSHOT_SHADOW_NOT_ACTIVE);
        return SV_SNAPSHOT_SHADOW_NOT_ACTIVE;
    }
    increment_saturating(&peer->status.baseline_attempts);
    result = Worr_SnapshotQ2ProtoSetBaselineV2(
        &peer->context, entity_index, baseline_delta);
    peer->status.last_project_result = (uint32_t)result;
    if (result != WORR_SNAPSHOT_Q2PROTO_OK) {
        increment_saturating(&peer->status.baseline_failures);
        set_result(peer, SV_SNAPSHOT_SHADOW_PROJECT_FAILED);
        return SV_SNAPSHOT_SHADOW_PROJECT_FAILED;
    }
    set_result(peer, SV_SNAPSHOT_SHADOW_OK);
    return SV_SNAPSHOT_SHADOW_OK;
}

static void abort_frame(sv_snapshot_shadow_peer_v1 *peer, bool count)
{
    if (!peer)
        return;
    if (count && peer->pending)
        increment_saturating(&peer->status.pending_aborts);
    peer->pending = 0;
    peer->capture_failed = 0;
    peer->pending_flags = 0;
    peer->pending_delta_count = 0;
    peer->status.pending = 0;
    peer->status.pending_delta_count = 0;
    memset(&peer->pending_frame, 0, sizeof(peer->pending_frame));
    memset(&peer->pending_input, 0, sizeof(peer->pending_input));
}

sv_snapshot_shadow_result_v1 SV_SnapshotShadowBeginFrameV1(
    sv_snapshot_shadow_peer_v1 *peer,
    const sv_snapshot_shadow_frame_v1 *frame)
{
    if (!peer || !frame || frame->struct_size != sizeof(*frame) ||
        frame->schema_version != SV_SNAPSHOT_SHADOW_VERSION ||
        !frame->wire_frame || frame->wire_frame->serverframe < 0 ||
        frame->wire_frame->deltaframe >= frame->wire_frame->serverframe ||
        frame->wire_frame->areabits_len >
            peer->config.area_bytes_per_slot ||
        (frame->wire_frame->areabits_len != 0 &&
         !frame->wire_frame->areabits) ||
        frame->controlled_entity_index == 0 ||
        frame->controlled_entity_index >= peer->config.max_entities) {
        set_result(peer, SV_SNAPSHOT_SHADOW_INVALID_ARGUMENT);
        return SV_SNAPSHOT_SHADOW_INVALID_ARGUMENT;
    }
    if (!peer->active) {
        set_result(peer, SV_SNAPSHOT_SHADOW_NOT_ACTIVE);
        return SV_SNAPSHOT_SHADOW_NOT_ACTIVE;
    }
    abort_frame(peer, true);
    increment_saturating(&peer->status.frame_attempts);

    peer->pending_input = *frame;
    peer->pending_frame = *frame->wire_frame;
    if (peer->pending_frame.areabits_len != 0) {
        memcpy(peer->pending_area_bytes, peer->pending_frame.areabits,
               peer->pending_frame.areabits_len);
        peer->pending_frame.areabits = peer->pending_area_bytes;
    } else {
        peer->pending_frame.areabits = NULL;
    }
    peer->pending_input.wire_frame = &peer->pending_frame;
    peer->pending = 1;
    peer->status.pending = 1;
    set_result(peer, SV_SNAPSHOT_SHADOW_OK);
    return SV_SNAPSHOT_SHADOW_OK;
}

sv_snapshot_shadow_result_v1 SV_SnapshotShadowCaptureEntityDeltaV1(
    sv_snapshot_shadow_peer_v1 *peer,
    const q2proto_svc_frame_entity_delta_t *delta)
{
    if (!peer || !delta) {
        set_result(peer, SV_SNAPSHOT_SHADOW_INVALID_ARGUMENT);
        return SV_SNAPSHOT_SHADOW_INVALID_ARGUMENT;
    }
    if (!peer->pending) {
        set_result(peer, SV_SNAPSHOT_SHADOW_NO_PENDING_FRAME);
        return SV_SNAPSHOT_SHADOW_NO_PENDING_FRAME;
    }
    if (peer->capture_failed) {
        set_result(peer, SV_SNAPSHOT_SHADOW_CAPTURE_FAILED);
        return SV_SNAPSHOT_SHADOW_CAPTURE_FAILED;
    }
    if (peer->pending_delta_count >= peer->pending_delta_capacity) {
        peer->capture_failed = 1;
        set_result(peer, SV_SNAPSHOT_SHADOW_DELTA_CAPACITY);
        return SV_SNAPSHOT_SHADOW_DELTA_CAPACITY;
    }
    peer->pending_deltas[peer->pending_delta_count++] = *delta;
    peer->status.pending_delta_count = peer->pending_delta_count;
    increment_saturating(&peer->status.delta_captures);
    set_result(peer, SV_SNAPSHOT_SHADOW_OK);
    return SV_SNAPSHOT_SHADOW_OK;
}

void SV_SnapshotShadowMarkTransportTruncatedV1(
    sv_snapshot_shadow_peer_v1 *peer)
{
    if (!peer || !peer->pending)
        return;
    if ((peer->pending_flags &
         SV_SNAPSHOT_SHADOW_SENT_TRANSPORT_TRUNCATED) == 0) {
        increment_saturating(&peer->status.transport_truncations);
    }
    peer->pending_flags |= SV_SNAPSHOT_SHADOW_SENT_TRANSPORT_TRUNCATED;
}

void SV_SnapshotShadowMarkFragmentStallV1(
    sv_snapshot_shadow_peer_v1 *peer)
{
    if (!peer || !peer->pending)
        return;
    peer->pending_flags |= SV_SNAPSHOT_SHADOW_SENT_FRAGMENT_STALL;
}

void SV_SnapshotShadowAbortFrameV1(sv_snapshot_shadow_peer_v1 *peer)
{
    abort_frame(peer, true);
}

static sv_snapshot_shadow_result_v1 find_wire_internal(
    const sv_snapshot_shadow_peer_v1 *peer, int32_t wire_snapshot_number,
    sv_snapshot_shadow_ref_v1 *ref_out)
{
    uint32_t index;

    if (!peer || wire_snapshot_number < 0 || !ref_out)
        return SV_SNAPSHOT_SHADOW_INVALID_ARGUMENT;
    for (index = 0; index < peer->config.slot_capacity; ++index) {
        const sv_snapshot_shadow_sent_v1 *sent = &peer->sent[index];
        if (sent->ref.generation != 0 &&
            sent->wire_snapshot_number == (uint32_t)wire_snapshot_number) {
            *ref_out = sent->ref;
            return SV_SNAPSHOT_SHADOW_OK;
        }
    }
    return SV_SNAPSHOT_SHADOW_STALE_REF;
}

sv_snapshot_shadow_result_v1 SV_SnapshotShadowCommitFrameV1(
    sv_snapshot_shadow_peer_v1 *peer, sv_snapshot_shadow_ref_v1 *ref_out)
{
    worr_snapshot_q2proto_frame_input_v2 input;
    worr_snapshot_projection_view_v2 projected_view;
    worr_snapshot_projection_view_v2 adjusted_view;
    worr_snapshot_projection_hashes_v2 projected_hashes;
    worr_snapshot_projection_hashes_v2 adjusted_hashes;
    worr_snapshot_ref_v2 projection_ref;
    sv_snapshot_shadow_ref_v1 base_ref = {
        SV_SNAPSHOT_SHADOW_NO_SLOT, 0};
    sv_snapshot_shadow_ref_v1 committed_ref;
    sv_snapshot_shadow_sent_v1 record;
    worr_snapshot_q2proto_result_v2 project_result;
    uint32_t target_slot;
    uint32_t target_generation;
    uint32_t input_flags;

    if (!peer) {
        return SV_SNAPSHOT_SHADOW_INVALID_ARGUMENT;
    }
    if (!peer->pending) {
        set_result(peer, SV_SNAPSHOT_SHADOW_NO_PENDING_FRAME);
        return SV_SNAPSHOT_SHADOW_NO_PENDING_FRAME;
    }
    if (peer->capture_failed || peer->pending_delta_count == 0 ||
        peer->pending_deltas[peer->pending_delta_count - 1u].newnum != 0) {
        abort_frame(peer, true);
        set_result(peer, SV_SNAPSHOT_SHADOW_CAPTURE_FAILED);
        return SV_SNAPSHOT_SHADOW_CAPTURE_FAILED;
    }
    if (peer->pending_frame.deltaframe > 0 &&
        find_wire_internal(peer, peer->pending_frame.deltaframe,
                           &base_ref) != SV_SNAPSHOT_SHADOW_OK) {
        increment_saturating(&peer->status.base_ref_failures);
        abort_frame(peer, true);
        set_result(peer, SV_SNAPSHOT_SHADOW_BASE_REF_MISSING);
        return SV_SNAPSHOT_SHADOW_BASE_REF_MISSING;
    }

    target_slot = peer->context.next_slot;
    if (target_slot >= peer->config.slot_capacity) {
        abort_frame(peer, true);
        set_result(peer, SV_SNAPSHOT_SHADOW_PROJECT_FAILED);
        return SV_SNAPSHOT_SHADOW_PROJECT_FAILED;
    }
    target_generation = peer->sent[target_slot].ref.generation;
    if (target_generation == UINT32_MAX) {
        abort_frame(peer, true);
        set_result(peer, SV_SNAPSHOT_SHADOW_GENERATION_EXHAUSTED);
        return SV_SNAPSHOT_SHADOW_GENERATION_EXHAUSTED;
    }
    ++target_generation;

    input_flags = WORR_SNAPSHOT_Q2PROTO_FRAME_MOVEMENT_TYPE_VALID |
                  WORR_SNAPSHOT_Q2PROTO_FRAME_MOVEMENT_FLAGS_VALID |
                  WORR_SNAPSHOT_Q2PROTO_FRAME_TEAM_ID_VALID;
    if (peer->context.publish_count == 0 &&
        peer->pending_frame.serverframe > 0) {
        input_flags |= WORR_SNAPSHOT_Q2PROTO_FRAME_OBSERVER_ATTACH;
    } else if (peer->context.publish_count != 0 &&
               peer->pending_frame.deltaframe <= 0) {
        input_flags |= WORR_SNAPSHOT_Q2PROTO_FRAME_LINEAGE_PARENT_VALID;
    }
    if ((peer->pending_flags &
         SV_SNAPSHOT_SHADOW_SENT_TRANSPORT_TRUNCATED) != 0) {
        input_flags |= WORR_SNAPSHOT_Q2PROTO_FRAME_TRANSPORT_TRUNCATED;
    }
    if ((peer->pending_flags &
         SV_SNAPSHOT_SHADOW_SENT_FRAGMENT_STALL) != 0) {
        input_flags |= WORR_SNAPSHOT_Q2PROTO_FRAME_FRAGMENT_STALL;
    }

    memset(&input, 0, sizeof(input));
    input.struct_size = sizeof(input);
    input.schema_version = WORR_SNAPSHOT_Q2PROTO_VERSION;
    input.frame = &peer->pending_frame;
    input.entity_deltas = peer->pending_deltas;
    input.entity_delta_count = peer->pending_delta_count;
    input.flags = input_flags;
    input.controlled_entity_index =
        peer->pending_input.controlled_entity_index;
    input.canonical_movement_type =
        peer->pending_input.canonical_movement_type;
    input.canonical_movement_flags =
        peer->pending_input.canonical_movement_flags;
    input.team_id = peer->pending_input.team_id;
    if ((input_flags &
         WORR_SNAPSHOT_Q2PROTO_FRAME_LINEAGE_PARENT_VALID) != 0) {
        /* Full wire frames have no delta base, but retaining the exact last
         * observed wire frame as a generation-only parent avoids inventing
         * entity reuse at an ordinary no-delta refresh. */
        input.lineage_parent_serverframe = (int32_t)(
            peer->context.last_observed.sequence - 1u);
    }
    input.server_time_us =
        peer->pending_input.authoritative_server_time_us;
    input.consumed_command = peer->pending_input.consumed_command;

    project_result = Worr_SnapshotQ2ProtoPublishV2(
        &peer->context, &input, &projection_ref);
    peer->status.last_project_result = (uint32_t)project_result;
    if (project_result != WORR_SNAPSHOT_Q2PROTO_OK) {
        increment_saturating(&peer->status.project_failures);
        abort_frame(peer, true);
        set_result(peer, SV_SNAPSHOT_SHADOW_PROJECT_FAILED);
        return SV_SNAPSHOT_SHADOW_PROJECT_FAILED;
    }
    memset(&projected_view, 0, sizeof(projected_view));
    memset(&projected_hashes, 0, sizeof(projected_hashes));
    project_result = Worr_SnapshotQ2ProtoViewV2(
        &peer->context, projection_ref, &projected_view, &projected_hashes);
    if (project_result != WORR_SNAPSHOT_Q2PROTO_OK ||
        projection_ref.slot != target_slot) {
        increment_saturating(&peer->status.project_failures);
        abort_frame(peer, true);
        set_result(peer, SV_SNAPSHOT_SHADOW_PROJECT_FAILED);
        return SV_SNAPSHOT_SHADOW_PROJECT_FAILED;
    }

    memset(&record, 0, sizeof(record));
    record.struct_size = sizeof(record);
    record.schema_version = SV_SNAPSHOT_SHADOW_VERSION;
    record.ref.slot = target_slot;
    record.ref.generation = target_generation;
    record.base_ref = base_ref;
    record.projection_ref = projection_ref;
    record.wire_snapshot_number =
        (uint32_t)peer->pending_frame.serverframe;
    record.wire_base_snapshot_number = peer->pending_frame.deltaframe;
    record.authoritative_server_tick =
        peer->pending_input.authoritative_server_tick;
    record.authoritative_tick_delta =
        peer->pending_input.authoritative_tick_delta;
    record.flags = peer->pending_flags;
    record.entity_delta_count = peer->pending_delta_count - 1u;
    record.commit_serial = peer->next_commit_serial;
    record.snapshot = *projected_view.snapshot;
    record.snapshot.server_tick =
        peer->pending_input.authoritative_server_tick;
    record.snapshot.server_time_us =
        peer->pending_input.authoritative_server_time_us;
    record.snapshot.discontinuity.server_tick_delta =
        peer->context.publish_count == 1
            ? 0
            : peer->pending_input.authoritative_tick_delta;
    if (peer->context.publish_count > 1 &&
        peer->pending_input.authoritative_tick_delta > 1) {
        record.flags |= SV_SNAPSHOT_SHADOW_SENT_RATE_SUPPRESSED;
        record.snapshot.discontinuity.flags |=
            WORR_SNAPSHOT_DISCONTINUITY_RATE_SUPPRESSED;
        if (record.snapshot.discontinuity.reason ==
            WORR_SNAPSHOT_DISCONTINUITY_REASON_NONE) {
            record.snapshot.discontinuity.reason =
                WORR_SNAPSHOT_DISCONTINUITY_REASON_RATE_SUPPRESSED;
        }
    }
    if (!Worr_SnapshotCalculateHashV2(
            &record.snapshot, peer->config.max_entities,
            &record.snapshot.snapshot_hash)) {
        increment_saturating(&peer->status.project_failures);
        abort_frame(peer, true);
        set_result(peer, SV_SNAPSHOT_SHADOW_PROJECT_FAILED);
        return SV_SNAPSHOT_SHADOW_PROJECT_FAILED;
    }
    adjusted_view = projected_view;
    adjusted_view.snapshot = &record.snapshot;
    memset(&adjusted_hashes, 0, sizeof(adjusted_hashes));
    if (!Worr_SnapshotProjectionHashesV2(
            &adjusted_view, peer->config.max_entities, &adjusted_hashes) ||
        adjusted_hashes.legacy_parity_hash !=
            projected_hashes.legacy_parity_hash) {
        increment_saturating(&peer->status.project_failures);
        abort_frame(peer, true);
        set_result(peer, SV_SNAPSHOT_SHADOW_PROJECT_FAILED);
        return SV_SNAPSHOT_SHADOW_PROJECT_FAILED;
    }
    record.hashes = adjusted_hashes;

    peer->sent[target_slot] = record;
    if (peer->status.retained_count < peer->config.slot_capacity)
        ++peer->status.retained_count;
    increment_saturating(&peer->status.frames_committed);
    peer->status.last_endpoint_hash = adjusted_hashes.endpoint_hash;
    peer->status.last_legacy_parity_hash =
        adjusted_hashes.legacy_parity_hash;
    if (peer->next_commit_serial != UINT64_MAX)
        ++peer->next_commit_serial;
    committed_ref = record.ref;
    abort_frame(peer, false);
    set_result(peer, SV_SNAPSHOT_SHADOW_OK);
    if (ref_out)
        *ref_out = committed_ref;
    return SV_SNAPSHOT_SHADOW_OK;
}

sv_snapshot_shadow_result_v1 SV_SnapshotShadowFindWireV1(
    sv_snapshot_shadow_peer_v1 *peer, int32_t wire_snapshot_number,
    sv_snapshot_shadow_ref_v1 *ref_out)
{
    const sv_snapshot_shadow_result_v1 result =
        find_wire_internal(peer, wire_snapshot_number, ref_out);
    if (peer && result == SV_SNAPSHOT_SHADOW_STALE_REF)
        increment_saturating(&peer->status.stale_ref_queries);
    set_result(peer, result);
    return result;
}

static const sv_snapshot_shadow_sent_v1 *resolve_sent(
    const sv_snapshot_shadow_peer_v1 *peer, sv_snapshot_shadow_ref_v1 ref)
{
    const sv_snapshot_shadow_sent_v1 *sent;

    if (!peer || ref.slot >= peer->config.slot_capacity ||
        ref.generation == 0)
        return NULL;
    sent = &peer->sent[ref.slot];
    if (sent->ref.generation != ref.generation)
        return NULL;
    return sent;
}

sv_snapshot_shadow_result_v1 SV_SnapshotShadowGetSentV1(
    sv_snapshot_shadow_peer_v1 *peer, sv_snapshot_shadow_ref_v1 ref,
    sv_snapshot_shadow_sent_v1 *sent_out)
{
    const sv_snapshot_shadow_sent_v1 *sent = resolve_sent(peer, ref);
    if (!peer || !sent_out) {
        set_result(peer, SV_SNAPSHOT_SHADOW_INVALID_ARGUMENT);
        return SV_SNAPSHOT_SHADOW_INVALID_ARGUMENT;
    }
    if (!sent) {
        increment_saturating(&peer->status.stale_ref_queries);
        set_result(peer, SV_SNAPSHOT_SHADOW_STALE_REF);
        return SV_SNAPSHOT_SHADOW_STALE_REF;
    }
    *sent_out = *sent;
    set_result(peer, SV_SNAPSHOT_SHADOW_OK);
    return SV_SNAPSHOT_SHADOW_OK;
}

sv_snapshot_shadow_result_v1 SV_SnapshotShadowViewV1(
    sv_snapshot_shadow_peer_v1 *peer, sv_snapshot_shadow_ref_v1 ref,
    worr_snapshot_projection_view_v2 *view_out,
    worr_snapshot_projection_hashes_v2 *hashes_out)
{
    const sv_snapshot_shadow_sent_v1 *sent = resolve_sent(peer, ref);
    worr_snapshot_projection_view_v2 view;
    worr_snapshot_projection_hashes_v2 ignored_hashes;

    if (!peer || !view_out || !hashes_out) {
        set_result(peer, SV_SNAPSHOT_SHADOW_INVALID_ARGUMENT);
        return SV_SNAPSHOT_SHADOW_INVALID_ARGUMENT;
    }
    if (!sent) {
        increment_saturating(&peer->status.stale_ref_queries);
        set_result(peer, SV_SNAPSHOT_SHADOW_STALE_REF);
        return SV_SNAPSHOT_SHADOW_STALE_REF;
    }
    memset(&view, 0, sizeof(view));
    memset(&ignored_hashes, 0, sizeof(ignored_hashes));
    if (Worr_SnapshotQ2ProtoViewV2(
            &peer->context, sent->projection_ref, &view,
            &ignored_hashes) != WORR_SNAPSHOT_Q2PROTO_OK) {
        increment_saturating(&peer->status.stale_ref_queries);
        set_result(peer, SV_SNAPSHOT_SHADOW_STALE_REF);
        return SV_SNAPSHOT_SHADOW_STALE_REF;
    }
    view.snapshot = &sent->snapshot;
    *view_out = view;
    *hashes_out = sent->hashes;
    set_result(peer, SV_SNAPSHOT_SHADOW_OK);
    return SV_SNAPSHOT_SHADOW_OK;
}

bool SV_SnapshotShadowGetStatusV1(
    const sv_snapshot_shadow_peer_v1 *peer,
    sv_snapshot_shadow_status_v1 *status_out)
{
    if (!peer || !status_out)
        return false;
    *status_out = peer->status;
    return true;
}
