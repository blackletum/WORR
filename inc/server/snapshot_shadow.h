/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/snapshot_q2proto.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SV_SNAPSHOT_SHADOW_VERSION 1u
#define SV_SNAPSHOT_SHADOW_NO_SLOT UINT32_MAX

/*
 * Final-emission shadow results are deliberately independent from packet
 * writer results.  A non-OK result disables or aborts only observation; the
 * legacy q2proto packet remains authoritative.
 */
typedef enum sv_snapshot_shadow_result_v1_e {
    SV_SNAPSHOT_SHADOW_OK = 0,
    SV_SNAPSHOT_SHADOW_INVALID_ARGUMENT = 1,
    SV_SNAPSHOT_SHADOW_ALLOCATION_FAILED = 2,
    SV_SNAPSHOT_SHADOW_NOT_ACTIVE = 3,
    SV_SNAPSHOT_SHADOW_NO_PENDING_FRAME = 4,
    SV_SNAPSHOT_SHADOW_CAPTURE_FAILED = 5,
    SV_SNAPSHOT_SHADOW_DELTA_CAPACITY = 6,
    SV_SNAPSHOT_SHADOW_PROJECT_FAILED = 7,
    SV_SNAPSHOT_SHADOW_BASE_REF_MISSING = 8,
    SV_SNAPSHOT_SHADOW_GENERATION_EXHAUSTED = 9,
    SV_SNAPSHOT_SHADOW_STALE_REF = 10,
} sv_snapshot_shadow_result_v1;

enum {
    SV_SNAPSHOT_SHADOW_SENT_TRANSPORT_TRUNCATED = 1u << 0,
    SV_SNAPSHOT_SHADOW_SENT_RATE_SUPPRESSED = 1u << 1,
    /* A successfully completed frame followed a packet-fragment stall.
     * This is an observational cause marker; it never changes packet output. */
    SV_SNAPSHOT_SHADOW_SENT_FRAGMENT_STALL = 1u << 2,
};

typedef struct sv_snapshot_shadow_ref_v1_s {
    uint32_t slot;
    uint32_t generation;
} sv_snapshot_shadow_ref_v1;

typedef struct sv_snapshot_shadow_config_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t snapshot_epoch;
    uint32_t max_entities;
    uint32_t max_models;
    uint32_t max_sounds;
    uint32_t slot_capacity;
    uint32_t entities_per_slot;
    uint32_t area_bytes_per_slot;
    uint32_t beam_renderfx_mask;
    uint32_t legacy_renderfx_allowed_mask;
    uint32_t legacy_beam_clear_mask;
    uint8_t extended_entity_state;
    uint8_t reserved0[3];
} sv_snapshot_shadow_config_v1;

/* Runtime frame input.  The q2proto area pointer is copied by BeginFrame. */
typedef struct sv_snapshot_shadow_frame_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    const q2proto_svc_frame_t *wire_frame;
    uint32_t authoritative_server_tick;
    uint32_t authoritative_tick_delta;
    uint64_t authoritative_server_time_us;
    uint32_t controlled_entity_index;
    int32_t canonical_movement_type;
    uint16_t canonical_movement_flags;
    uint8_t team_id;
    uint8_t reserved0;
    worr_snapshot_consumed_command_v2 consumed_command;
} sv_snapshot_shadow_frame_v1;

/*
 * An immutable record of what a peer was actually sent.  wire_snapshot_number
 * remains the per-peer transport identifier; snapshot.server_tick is the
 * authoritative simulation tick and is never derived from that identifier.
 */
typedef struct sv_snapshot_shadow_sent_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    sv_snapshot_shadow_ref_v1 ref;
    sv_snapshot_shadow_ref_v1 base_ref;
    worr_snapshot_ref_v2 projection_ref;
    uint32_t wire_snapshot_number;
    int32_t wire_base_snapshot_number;
    uint32_t authoritative_server_tick;
    uint32_t authoritative_tick_delta;
    uint32_t flags;
    uint32_t entity_delta_count;
    uint64_t commit_serial;
    worr_snapshot_v2 snapshot;
    worr_snapshot_projection_hashes_v2 hashes;
} sv_snapshot_shadow_sent_v1;

typedef struct sv_snapshot_shadow_status_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t active;
    uint32_t pending;
    uint32_t last_result;
    uint32_t last_project_result;
    uint32_t pending_delta_count;
    uint32_t pending_delta_capacity;
    uint32_t retained_count;
    uint32_t slot_capacity;
    uint32_t snapshot_epoch;
    uint32_t reserved0;
    uint64_t allocation_bytes;
    uint64_t baseline_attempts;
    uint64_t baseline_failures;
    uint64_t frame_attempts;
    uint64_t delta_captures;
    uint64_t pending_aborts;
    uint64_t transport_truncations;
    uint64_t project_failures;
    uint64_t base_ref_failures;
    uint64_t frames_committed;
    uint64_t stale_ref_queries;
    uint64_t last_endpoint_hash;
    uint64_t last_legacy_parity_hash;
} sv_snapshot_shadow_status_v1;

typedef struct sv_snapshot_shadow_peer_v1_s sv_snapshot_shadow_peer_v1;

sv_snapshot_shadow_peer_v1 *SV_SnapshotShadowCreateV1(
    const sv_snapshot_shadow_config_v1 *config);
void SV_SnapshotShadowDestroyV1(sv_snapshot_shadow_peer_v1 *peer);

sv_snapshot_shadow_result_v1 SV_SnapshotShadowSetBaselineV1(
    sv_snapshot_shadow_peer_v1 *peer,
    uint32_t entity_index,
    const q2proto_entity_state_delta_t *baseline_delta);

/* Begin/capture/commit is one transaction at the packet writer boundary. */
sv_snapshot_shadow_result_v1 SV_SnapshotShadowBeginFrameV1(
    sv_snapshot_shadow_peer_v1 *peer,
    const sv_snapshot_shadow_frame_v1 *frame);
sv_snapshot_shadow_result_v1 SV_SnapshotShadowCaptureEntityDeltaV1(
    sv_snapshot_shadow_peer_v1 *peer,
    const q2proto_svc_frame_entity_delta_t *delta);
void SV_SnapshotShadowMarkTransportTruncatedV1(
    sv_snapshot_shadow_peer_v1 *peer);
void SV_SnapshotShadowMarkFragmentStallV1(
    sv_snapshot_shadow_peer_v1 *peer);
void SV_SnapshotShadowAbortFrameV1(sv_snapshot_shadow_peer_v1 *peer);
sv_snapshot_shadow_result_v1 SV_SnapshotShadowCommitFrameV1(
    sv_snapshot_shadow_peer_v1 *peer,
    sv_snapshot_shadow_ref_v1 *ref_out);

sv_snapshot_shadow_result_v1 SV_SnapshotShadowFindWireV1(
    sv_snapshot_shadow_peer_v1 *peer,
    int32_t wire_snapshot_number,
    sv_snapshot_shadow_ref_v1 *ref_out);
sv_snapshot_shadow_result_v1 SV_SnapshotShadowGetSentV1(
    sv_snapshot_shadow_peer_v1 *peer,
    sv_snapshot_shadow_ref_v1 ref,
    sv_snapshot_shadow_sent_v1 *sent_out);
sv_snapshot_shadow_result_v1 SV_SnapshotShadowViewV1(
    sv_snapshot_shadow_peer_v1 *peer,
    sv_snapshot_shadow_ref_v1 ref,
    worr_snapshot_projection_view_v2 *view_out,
    worr_snapshot_projection_hashes_v2 *hashes_out);
bool SV_SnapshotShadowGetStatusV1(
    const sv_snapshot_shadow_peer_v1 *peer,
    sv_snapshot_shadow_status_v1 *status_out);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
static_assert(sizeof(sv_snapshot_shadow_ref_v1) == 8,
              "server snapshot shadow ref layout changed");
static_assert(sizeof(sv_snapshot_shadow_config_v1) == 52,
              "server snapshot shadow config layout changed");
static_assert(sizeof(sv_snapshot_shadow_sent_v1) == 336,
              "server snapshot shadow sent record layout changed");
static_assert(offsetof(sv_snapshot_shadow_status_v1, allocation_bytes) == 48,
              "server snapshot shadow status header changed");
#else
_Static_assert(sizeof(sv_snapshot_shadow_ref_v1) == 8,
               "server snapshot shadow ref layout changed");
_Static_assert(sizeof(sv_snapshot_shadow_config_v1) == 52,
               "server snapshot shadow config layout changed");
_Static_assert(sizeof(sv_snapshot_shadow_sent_v1) == 336,
               "server snapshot shadow sent record layout changed");
_Static_assert(offsetof(sv_snapshot_shadow_status_v1, allocation_bytes) == 48,
               "server snapshot shadow status header changed");
#endif
