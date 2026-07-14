/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "shared/shared.h"

#include "common/net/chan.h"
#include "common/net/command_stream.h"
#include "common/net/native_carrier_ack.h"
#include "common/net/native_command_shadow.h"
#include "common/net/native_readiness.h"
#include "common/net/native_readiness_sideband.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Default-off FR-10-T04 server pilot.  Legacy MOVE/BATCH_MOVE remains the sole
 * simulation authority.  The current native slice admits one observational
 * command per private transport epoch and emits only exact commit receipts.
 */
#define SV_NATIVE_SHADOW_VERSION 1u
/* CHALLENGE is the first post-bootstrap reliable.  This deadline therefore
 * covers only the private readiness exchange and its bounded queueing delay. */
#define SV_NATIVE_SHADOW_TIMEOUT_MS UINT64_C(10000)
#define SV_NATIVE_SHADOW_CHALLENGE_QUEUE_TIMEOUT_MS UINT32_C(60000)
#define SV_NATIVE_SHADOW_SVC_WIRE_BYTES \
    (WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT * 9u)
#define SV_NATIVE_SHADOW_RX_SLOT_CAPACITY 1u
#define SV_NATIVE_SHADOW_PAYLOAD_BYTES \
    WORR_NATIVE_COMMAND_SHADOW_ENCODED_BYTES
#define SV_NATIVE_SHADOW_WNE_BYTES \
    (WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES + \
     SV_NATIVE_SHADOW_PAYLOAD_BYTES)
#define SV_NATIVE_SHADOW_ACK_RETRY_MS 100u
#define SV_NATIVE_SHADOW_ACK_PROACTIVE_HANDOFFS 3u
#define SV_NATIVE_SHADOW_JOIN_EXPIRY_MS UINT64_C(500)
#define SV_NATIVE_SHADOW_RX_FRAGMENT_TIMEOUT_MS 1000u
#define SV_NATIVE_SHADOW_RX_COMPLETE_TIMEOUT_MS 1000u
#define SV_NATIVE_SHADOW_STATUS_VERSION 1u

typedef enum sv_native_shadow_lifecycle_v1_e {
    SV_NATIVE_SHADOW_LIFECYCLE_WAIT_READINESS = 1,
    SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE = 2,
    SV_NATIVE_SHADOW_LIFECYCLE_DRAIN = 3,
    SV_NATIVE_SHADOW_LIFECYCLE_DETACHED = 4,
} sv_native_shadow_lifecycle_v1;

typedef enum sv_native_shadow_transport_bank_v1_e {
    SV_NATIVE_SHADOW_TRANSPORT_BANK_NONE = 0,
    SV_NATIVE_SHADOW_TRANSPORT_BANK_CURRENT = 1,
    SV_NATIVE_SHADOW_TRANSPORT_BANK_RETIRED = 2,
} sv_native_shadow_transport_bank_v1;

typedef enum sv_native_shadow_failure_v1_e {
    SV_NATIVE_SHADOW_FAILURE_NONE = 0,
    SV_NATIVE_SHADOW_FAILURE_ARGUMENT = 1,
    SV_NATIVE_SHADOW_FAILURE_HOOK_INSTALL = 2,
    SV_NATIVE_SHADOW_FAILURE_CLOCK = 3,
    SV_NATIVE_SHADOW_FAILURE_OFFICIAL_BINDING = 4,
    SV_NATIVE_SHADOW_FAILURE_IDENTITY_EXHAUSTED = 5,
    SV_NATIVE_SHADOW_FAILURE_READINESS = 6,
    SV_NATIVE_SHADOW_FAILURE_SIDEBAND = 7,
    SV_NATIVE_SHADOW_FAILURE_QUEUE = 8,
    SV_NATIVE_SHADOW_FAILURE_SESSION = 9,
    SV_NATIVE_SHADOW_FAILURE_CARRIER = 10,
    SV_NATIVE_SHADOW_FAILURE_CODEC = 11,
    SV_NATIVE_SHADOW_FAILURE_COMMAND_SHADOW = 12,
    SV_NATIVE_SHADOW_FAILURE_ACK = 13,
    SV_NATIVE_SHADOW_FAILURE_ONE_SHOT_LIMIT = 14,
} sv_native_shadow_failure_v1;

typedef enum sv_native_shadow_observe_result_v1_e {
    SV_NATIVE_SHADOW_OBSERVE_NOT_SIDEBAND = 0,
    SV_NATIVE_SHADOW_OBSERVE_FIELD_CONSUMED = 1,
    SV_NATIVE_SHADOW_OBSERVE_SERVER_ACTIVE_READY = 2,
    SV_NATIVE_SHADOW_OBSERVE_PILOT_DISABLED = 3,
} sv_native_shadow_observe_result_v1;

typedef struct sv_native_shadow_transport_v1_s {
    worr_native_session_binding_v1 binding;
    uint32_t official_connection_epoch;
    uint32_t reserved0;
    worr_native_rx_session_v1 rx_session;
    worr_native_rx_slot_v1 rx_slots[SV_NATIVE_SHADOW_RX_SLOT_CAPACITY];
    uint8_t payload_arena[SV_NATIVE_SHADOW_PAYLOAD_BYTES];
    worr_native_carrier_ack_ledger_v1 ack_ledger;
    worr_native_command_shadow_join_v1 command_join;
} sv_native_shadow_transport_v1;

/*
 * Pointer-free, value-copy diagnostic surface for the staged production
 * proof.  It is deliberately versioned separately from the private peer
 * adapter so tooling never needs to inspect live engine storage.
 */
typedef struct sv_native_shadow_status_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t reserved0;
    uint32_t enabled;
    uint32_t lifecycle;
    uint32_t hooks_attached;
    uint32_t readiness_phase;
    uint32_t official_connection_epoch;
    uint32_t transport_epoch;
    uint32_t public_capabilities;
    uint32_t private_capabilities;
    uint32_t wire_committed;
    uint32_t ack_eligible;
    uint32_t reserved1;
    uint32_t reserved_counter_alignment;
    uint64_t challenges_queued;
    uint64_t client_ready_records;
    uint64_t server_active_records;
    uint64_t rx_carriers;
    uint64_t rx_commits;
    uint64_t rx_repeat_refreshes;
    uint64_t legacy_join_observations;
    uint64_t command_matches;
    uint64_t command_mismatches;
    uint64_t sample_offset_mismatches;
    uint64_t tx_ack_prepares;
    uint64_t tx_ack_handoffs;
    uint64_t async_rate_deferrals;
    uint64_t async_fragment_deferrals;
    uint64_t async_wake_attempts;
    uint64_t async_ack_handoffs;
    uint64_t async_wake_no_handoff;
    uint64_t rx_rejections;
    uint64_t tx_ack_rejections;
    uint64_t rx_drained;
    uint64_t drain_entries;
    uint64_t failures;
    uint32_t last_failure;
    uint32_t reserved2;
} sv_native_shadow_status_v1;

#if defined(__cplusplus)
static_assert(sizeof(sv_native_shadow_status_v1) == 240,
              "server native shadow status V1 layout changed");
static_assert(offsetof(sv_native_shadow_status_v1, challenges_queued) == 56,
              "server native shadow status V1 counter offset changed");
static_assert(offsetof(sv_native_shadow_status_v1, last_failure) == 232,
              "server native shadow status V1 tail offset changed");
#else
_Static_assert(sizeof(sv_native_shadow_status_v1) == 240,
               "server native shadow status V1 layout changed");
_Static_assert(offsetof(sv_native_shadow_status_v1, challenges_queued) == 56,
               "server native shadow status V1 counter offset changed");
_Static_assert(offsetof(sv_native_shadow_status_v1, last_failure) == 232,
               "server native shadow status V1 tail offset changed");
#endif

/*
 * Server-private, address-stable connection adapter.  Production allocates
 * this object separately for eligible clients so the disabled path adds only
 * one null pointer to client_t.  It is not a serialized or module ABI.
 */
typedef struct sv_native_shadow_peer_v1_s {
    uint32_t version;
    uint32_t initialized;
    netchan_t *netchan;
    uint64_t connection_owner_id;

    uint32_t enabled;
    uint32_t hooks_attached;
    uint32_t readiness_initialized;
    uint32_t packet_open;
    uint32_t clock_initialized;
    uint32_t clock_last_raw;
    uint64_t clock_ticks;

    uint32_t official_connection_epoch;
    uint32_t private_transport_epoch;
    uint64_t readiness_nonce;
    uint32_t last_failure;
    uint32_t reserved0;

    uint32_t lifecycle;
    uint32_t transport_initialized;
    uint32_t retired_transport_initialized;
    uint32_t activation_pending;
    uint32_t carrier_traffic_seen;
    uint32_t native_wire_committed;
    uint32_t wire_committed_transport_epoch;
    uint32_t native_commands_accepted;
    uint32_t pending_native_valid;
    uint32_t ack_emit_active;
    uint32_t ack_emit_bank;
    uint32_t ack_next_bank;
    uint32_t async_wake_active;
    uint32_t async_wake_handoff_seen;
    worr_command_id_v1 pending_native_id;

    uint64_t challenges_queued;
    uint64_t client_ready_records;
    uint64_t duplicate_client_ready_records;
    uint64_t server_active_records;
    uint64_t failures;
    uint64_t tx_bypass_calls;
    uint64_t rx_bypass_calls;
    uint64_t rx_carriers;
    uint64_t rx_commits;
    uint64_t rx_repeat_refreshes;
    uint64_t rx_rejections;
    uint64_t rx_drained;
    uint64_t one_shot_limits;
    uint64_t legacy_join_observations;
    uint64_t command_matches;
    uint64_t command_mismatches;
    uint64_t sample_offset_mismatches;
    uint64_t join_expiries;
    uint64_t tx_ack_prepares;
    uint64_t tx_ack_handoffs;
    uint64_t tx_ack_rejections;
    uint64_t drain_entries;
    uint64_t async_rate_deferrals;
    uint64_t async_fragment_deferrals;
    uint64_t async_wake_attempts;
    uint64_t async_ack_handoffs;
    uint64_t async_wake_no_handoff;
    uint64_t next_completion_token;
    uint64_t active_completion_token;

    worr_native_readiness_state_v1 readiness;
    worr_native_readiness_sideband_parser_v1 sideband;
    worr_native_carrier_ack_emit_token_v1 ack_emit_token;
    sv_native_shadow_transport_v1 transport;
    sv_native_shadow_transport_v1 retired_transport;
} sv_native_shadow_peer_v1;

/* Hook registration is the final successful initialization action. */
bool SV_NativeShadowPeerInitV1(sv_native_shadow_peer_v1 *peer,
                               netchan_t *netchan,
                               uint32_t raw_time_ms);

/* Clearing is idempotent and must precede final messages, close, and free. */
void SV_NativeShadowPeerDetachV1(sv_native_shadow_peer_v1 *peer);
void SV_NativeShadowPeerDestroyV1(sv_native_shadow_peer_v1 *peer);
void SV_NativeShadowPeerDisableV1(sv_native_shadow_peer_v1 *peer,
                                  sv_native_shadow_failure_v1 failure);

bool SV_NativeShadowPeerEnabledV1(
    const sv_native_shadow_peer_v1 *peer);
/* A pending post-bootstrap CHALLENGE may start only when no queued,
 * in-flight, or fragmented reliable generation can precede it. */
bool SV_NativeShadowPostBootstrapQueueIdleV1(
    const sv_native_shadow_peer_v1 *peer);
bool SV_NativeShadowSettingIndexV1(int16_t index);

/* Stamp the connection clock immediately before Netchan_ProcessEx can invoke
 * the RX hook.  This makes retained DATA and the legacy stream reconciliation
 * that follows share the admitted packet's time even after a long idle gap. */
bool SV_NativeShadowAdvanceAdmissionClockV1(
    sv_native_shadow_peer_v1 *peer,
    uint32_t raw_time_ms);

/*
 * Fixed-width SVC setting carrier.  Capacity covers both the current message
 * and the NEW-netchan reliable queue that will subsequently receive that
 * entire message.  Append success changes only message bytes/cursize and uses
 * one final copy; every failure leaves both buffers byte-identical.
 */
bool SV_NativeShadowCanAppendSvcReadinessV1(
    const sizebuf_t *message,
    const sizebuf_t *reliable_queue);
bool SV_NativeShadowAppendSvcReadinessV1(
    sizebuf_t *message,
    const sizebuf_t *reliable_queue,
    int setting_opcode,
    const worr_native_readiness_record_v1 *record);

/*
 * Called only after an accepted post-bootstrap SV_Begin has requested a new
 * epoch and the send scheduler reaches a clean reliable boundary.  The public
 * legacy capability tuple must already be selected and delivered.  Its
 * supported and negotiated masks must both remain exactly
 * WORR_NET_CAP_LEGACY_STAGE_MASK.  The private readiness binding adds only
 * WORR_NET_CAP_NATIVE_ENVELOPE_V1.
 */
bool SV_NativeShadowBeginEpochV1(
    sv_native_shadow_peer_v1 *peer,
    uint32_t official_connection_epoch,
    uint32_t official_supported,
    uint32_t official_negotiated,
    uint32_t raw_time_ms,
    worr_native_readiness_record_v1 *challenge_out);

/*
 * Called only after the SERVER_ACTIVE readiness record has been appended and
 * transferred into the NEW-netchan reliable queue.  This is the sole live
 * activation/epoch-advance boundary for WTC1.
 */
bool SV_NativeShadowServerActiveQueuedV1(
    sv_native_shadow_peer_v1 *peer);

/* Diagnostic-only legacy half.  A legacy record is never inserted unless an
 * exact native command ID is already resident in the bounded join. */
bool SV_NativeShadowObserveLegacyCommandV1(
    sv_native_shadow_peer_v1 *peer,
    const worr_command_record_v1 *record,
    uint32_t raw_time_ms);

/* Reconciles the sole pending native ID against the authoritative retained
 * legacy stream.  This runs immediately after netchan RX admission and before
 * parsing the packet, covering a native retry that follows an earlier MOVE. */
bool SV_NativeShadowReconcileCommandStreamV1(
    sv_native_shadow_peer_v1 *peer,
    const worr_command_stream_v1 *stream,
    uint32_t raw_time_ms);

/* Extends the checked connection clock and reports whether an ACK-only WTC1
 * handoff is due.  It never transmits and is safe before an outer send call. */
bool SV_NativeShadowAckDueV1(
    sv_native_shadow_peer_v1 *peer,
    uint32_t raw_time_ms);

/*
 * Strictly non-mutating eligibility check used ahead of the outer rate and
 * fragment gates.  It projects, but never extends, the checked connection
 * clock and never reserves an ACK token.
 */
bool SV_NativeShadowAckEligiblePeekV1(
    const sv_native_shadow_peer_v1 *peer,
    uint32_t raw_time_ms);

/* Async-send telemetry is saturated and owned by this adapter. */
void SV_NativeShadowRecordAsyncRateDeferralV1(
    sv_native_shadow_peer_v1 *peer);
void SV_NativeShadowRecordAsyncFragmentDeferralV1(
    sv_native_shadow_peer_v1 *peer);
bool SV_NativeShadowBeginAsyncWakeV1(
    sv_native_shadow_peer_v1 *peer);
void SV_NativeShadowEndAsyncWakeV1(
    sv_native_shadow_peer_v1 *peer);

/* Copies a coherent scalar-only status row without changing peer state. */
bool SV_NativeShadowGetStatusV1(
    const sv_native_shadow_peer_v1 *peer,
    uint32_t raw_time_ms,
    sv_native_shadow_status_v1 *status_out);

/* Packet-scoped CLC sideband parser. */
bool SV_NativeShadowPacketBeginV1(sv_native_shadow_peer_v1 *peer,
                                  uint32_t raw_time_ms);
sv_native_shadow_observe_result_v1 SV_NativeShadowObserveSettingV1(
    sv_native_shadow_peer_v1 *peer,
    int16_t index,
    int16_t value,
    worr_native_readiness_record_v1 *server_active_out);
bool SV_NativeShadowObserveInterveningServiceV1(
    sv_native_shadow_peer_v1 *peer);
bool SV_NativeShadowPacketEndV1(sv_native_shadow_peer_v1 *peer);

#ifdef __cplusplus
}
#endif
