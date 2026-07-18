/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/snapshot_projection.h"

#ifdef __cplusplus
extern "C" {
#endif

/* q2proto_sound.h lacks its own C++ linkage guard; include the complete public
 * surface under C linkage so its declarations match the C library. */
#include "q2proto/q2proto.h"

#define WORR_SNAPSHOT_Q2PROTO_VERSION 2u

typedef enum worr_snapshot_q2proto_result_v2_e {
    WORR_SNAPSHOT_Q2PROTO_OK = 0,
    WORR_SNAPSHOT_Q2PROTO_INVALID_ARGUMENT = 1,
    WORR_SNAPSHOT_Q2PROTO_INVALID_CONTEXT = 2,
    WORR_SNAPSHOT_Q2PROTO_INVALID_FRAME = 3,
    WORR_SNAPSHOT_Q2PROTO_INVALID_BASE = 4,
    WORR_SNAPSHOT_Q2PROTO_INVALID_PLAYER = 5,
    WORR_SNAPSHOT_Q2PROTO_INVALID_ENTITY = 6,
    WORR_SNAPSHOT_Q2PROTO_INVALID_ENTITY_ORDER = 7,
    WORR_SNAPSHOT_Q2PROTO_INVALID_EVENT = 8,
    WORR_SNAPSHOT_Q2PROTO_CAPACITY = 9,
    WORR_SNAPSHOT_Q2PROTO_SERIAL_EXHAUSTED = 10,
    WORR_SNAPSHOT_Q2PROTO_GENERATION_EXHAUSTED = 11,
    WORR_SNAPSHOT_Q2PROTO_STALE_REF = 12,
    WORR_SNAPSHOT_Q2PROTO_BUFFER_TOO_SMALL = 13,
} worr_snapshot_q2proto_result_v2;

enum {
    WORR_SNAPSHOT_Q2PROTO_FRAME_OBSERVER_ATTACH = 1u << 0,
    WORR_SNAPSHOT_Q2PROTO_FRAME_TRANSPORT_TRUNCATED = 1u << 1,
    WORR_SNAPSHOT_Q2PROTO_FRAME_MOVEMENT_TYPE_VALID = 1u << 2,
    WORR_SNAPSHOT_Q2PROTO_FRAME_MOVEMENT_FLAGS_VALID = 1u << 3,
    WORR_SNAPSHOT_Q2PROTO_FRAME_TEAM_ID_VALID = 1u << 4,
    /* Generation-only parent for a full wire frame; never a delta base. */
    WORR_SNAPSHOT_Q2PROTO_FRAME_LINEAGE_PARENT_VALID = 1u << 5,
    /* Server-only cause for a visible sequence gap; not legacy-parity data. */
    WORR_SNAPSHOT_Q2PROTO_FRAME_FRAGMENT_STALL = 1u << 6,
};

/*
 * Protocol-independent client reconstruction policy. The render masks are
 * explicit engine semantics, not copied q2proto private codec constants.
 */
typedef struct worr_snapshot_q2proto_profile_v2_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t snapshot_epoch;
    uint32_t max_entities;
    uint32_t max_models;
    uint32_t max_sounds;
    uint32_t beam_renderfx_mask;
    uint32_t legacy_renderfx_allowed_mask;
    uint32_t legacy_beam_clear_mask;
    uint8_t extended_entity_state;
    uint8_t reserved0[7];
} worr_snapshot_q2proto_profile_v2;

typedef struct worr_snapshot_q2proto_lineage_v2_s {
    uint32_t generation;
    uint8_t present;
    uint8_t reserved0[3];
} worr_snapshot_q2proto_lineage_v2;

typedef struct worr_snapshot_q2proto_slot_v2_s {
    worr_snapshot_v2 snapshot;
    worr_snapshot_player_v2 player;
    worr_snapshot_projection_hashes_v2 hashes;
    uint64_t entity_first_serial;
    uint64_t area_first_serial;
    uint64_t event_first_serial;
    uint32_t entity_count;
    uint32_t area_byte_count;
    uint32_t event_ref_count;
    uint32_t generation;
    uint32_t committed;
    uint32_t reserved0;
} worr_snapshot_q2proto_slot_v2;

/* Every pointer names caller-owned fixed storage. Distinct arenas may not overlap. */
typedef struct worr_snapshot_q2proto_storage_v2_s {
    uint32_t struct_size;
    uint32_t schema_version;
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
    uint32_t slot_capacity;
    uint32_t entities_per_slot;
    uint32_t area_bytes_per_slot;
    uint32_t event_refs_per_slot;
    uint32_t entity_storage_capacity;
    uint32_t area_storage_capacity;
    uint32_t event_storage_capacity;
    uint32_t lineage_storage_capacity;
    uint32_t scratch_entity_capacity;
    uint32_t scratch_area_capacity;
    uint32_t scratch_event_capacity;
    uint32_t scratch_lineage_capacity;
} worr_snapshot_q2proto_storage_v2;

typedef struct worr_snapshot_q2proto_context_v2_s {
    uint32_t struct_size;
    uint32_t schema_version;
    worr_snapshot_q2proto_profile_v2 profile;
    worr_snapshot_q2proto_storage_v2 storage;
    worr_snapshot_id_v2 last_observed;
    uint32_t next_slot;
    uint32_t occupied;
    uint64_t next_entity_serial;
    uint64_t next_area_serial;
    uint64_t next_event_serial;
    uint64_t publish_count;
} worr_snapshot_q2proto_context_v2;

/*
 * q2proto does not expose the engine's legacy pmtype/pmflags conversion or a
 * reliable first-person controlled entity identity. Those exact adapter
 * results are explicit here rather than guessed from protocol numbers.
 */
typedef struct worr_snapshot_q2proto_frame_input_v2_s {
    uint32_t struct_size;
    uint32_t schema_version;
    const q2proto_svc_frame_t *frame;
    const q2proto_svc_frame_entity_delta_t *entity_deltas;
    uint32_t entity_delta_count;
    uint32_t flags;
    uint32_t controlled_entity_index;
    int32_t canonical_movement_type;
    uint16_t canonical_movement_flags;
    uint8_t team_id;
    uint8_t reserved0;
    int32_t lineage_parent_serverframe;
    uint32_t reserved1;
    uint64_t server_time_us;
    /* Optional exact server-consumed cursor supplied by a negotiated adapter;
     * legacy frames without that sideband use the canonical absent value. */
    worr_snapshot_consumed_command_v2 consumed_command;
} worr_snapshot_q2proto_frame_input_v2;

worr_snapshot_q2proto_result_v2 Worr_SnapshotQ2ProtoInitV2(
    worr_snapshot_q2proto_context_v2 *context,
    const worr_snapshot_q2proto_profile_v2 *profile,
    const worr_snapshot_q2proto_storage_v2 *storage);

worr_snapshot_q2proto_result_v2 Worr_SnapshotQ2ProtoSetBaselineV2(
    worr_snapshot_q2proto_context_v2 *context,
    uint32_t entity_index,
    const q2proto_entity_state_delta_t *baseline_delta);

worr_snapshot_q2proto_result_v2 Worr_SnapshotQ2ProtoPublishV2(
    worr_snapshot_q2proto_context_v2 *context,
    const worr_snapshot_q2proto_frame_input_v2 *input,
    worr_snapshot_ref_v2 *ref_out);

worr_snapshot_q2proto_result_v2 Worr_SnapshotQ2ProtoViewV2(
    const worr_snapshot_q2proto_context_v2 *context,
    worr_snapshot_ref_v2 ref,
    worr_snapshot_projection_view_v2 *view_out,
    worr_snapshot_projection_hashes_v2 *hashes_out);

/* Rekey every retained projection to a negotiated epoch without invalidating
 * process-local refs or discarding q2proto delta bases.  The operation is
 * transactional: invalid retained state leaves the context unchanged. */
worr_snapshot_q2proto_result_v2 Worr_SnapshotQ2ProtoRebindEpochV2(
    worr_snapshot_q2proto_context_v2 *context,
    uint32_t new_snapshot_epoch);

worr_snapshot_q2proto_result_v2 Worr_SnapshotQ2ProtoResetV2(
    worr_snapshot_q2proto_context_v2 *context,
    uint32_t new_snapshot_epoch);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
static_assert(sizeof(worr_snapshot_q2proto_lineage_v2) == 8,
              "q2proto lineage v2 layout changed");
#else
_Static_assert(sizeof(worr_snapshot_q2proto_lineage_v2) == 8,
               "q2proto lineage v2 layout changed");
#endif
