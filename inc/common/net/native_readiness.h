/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/capability.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Isolated FR-10-T04 endpoint-readiness state machine.  This core owns no
 * pointers, transport, storage, clock, or random-number generator and does
 * not advertise WORR_NET_CAP_NATIVE_ENVELOPE_V1.  The caller supplies every
 * nonzero readiness nonce and is responsible for carrying records reliably.
 */
#define WORR_NATIVE_READINESS_ABI_VERSION 1u

typedef enum worr_native_readiness_role_v1_e {
    WORR_NATIVE_READINESS_ROLE_SERVER = 1,
    WORR_NATIVE_READINESS_ROLE_CLIENT = 2,
} worr_native_readiness_role_v1;

typedef enum worr_native_readiness_phase_v1_e {
    WORR_NATIVE_READINESS_PHASE_RESET = 0,
    WORR_NATIVE_READINESS_PHASE_SERVER_WAIT_CLIENT_READY = 1,
    WORR_NATIVE_READINESS_PHASE_CLIENT_WAIT_CHALLENGE = 2,
    WORR_NATIVE_READINESS_PHASE_CLIENT_WAIT_SERVER_ACTIVE = 3,
    WORR_NATIVE_READINESS_PHASE_SERVER_ACTIVE = 4,
    WORR_NATIVE_READINESS_PHASE_CLIENT_ACTIVE = 5,
    WORR_NATIVE_READINESS_PHASE_FAILED = 6,
} worr_native_readiness_phase_v1;

typedef enum worr_native_readiness_record_kind_v1_e {
    WORR_NATIVE_READINESS_RECORD_CHALLENGE = 1,
    WORR_NATIVE_READINESS_RECORD_CLIENT_READY = 2,
    WORR_NATIVE_READINESS_RECORD_SERVER_ACTIVE = 3,
} worr_native_readiness_record_kind_v1;

typedef enum worr_native_readiness_result_v1_e {
    WORR_NATIVE_READINESS_OK = 0,
    WORR_NATIVE_READINESS_EXACT_DUPLICATE = 1,
    WORR_NATIVE_READINESS_INVALID_ARGUMENT = 2,
    WORR_NATIVE_READINESS_INVALID_STATE = 3,
    WORR_NATIVE_READINESS_INVALID_RECORD = 4,
    WORR_NATIVE_READINESS_WRONG_ROLE = 5,
    WORR_NATIVE_READINESS_WRONG_ORDER = 6,
    WORR_NATIVE_READINESS_BINDING_MISMATCH = 7,
    WORR_NATIVE_READINESS_CLOCK_REGRESSION = 8,
    WORR_NATIVE_READINESS_DEADLINE_EXPIRED = 9,
    WORR_NATIVE_READINESS_DEADLINE_OVERFLOW = 10,
    WORR_NATIVE_READINESS_EPOCH_NOT_NEWER = 11,
    WORR_NATIVE_READINESS_NONCE_NOT_NEWER = 12,
    WORR_NATIVE_READINESS_NONCE_EXHAUSTED = 13,
    WORR_NATIVE_READINESS_GENERATION_EXHAUSTED = 14,
    WORR_NATIVE_READINESS_EPOCH_EXHAUSTED = 15,
} worr_native_readiness_result_v1;

typedef struct worr_native_readiness_record_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t record_kind;
    uint32_t transport_epoch;
    uint32_t negotiated_capabilities;
    uint64_t readiness_nonce;
    uint32_t record_checksum;
    uint32_t reserved0;
} worr_native_readiness_record_v1;

/* Every counter saturates at UINT64_MAX. */
typedef struct worr_native_readiness_telemetry_v1_s {
    uint64_t challenges_emitted;
    uint64_t challenges_accepted;
    uint64_t client_ready_emitted;
    uint64_t client_ready_accepted;
    uint64_t server_active_emitted;
    uint64_t server_active_accepted;
    uint64_t exact_duplicates;
    uint64_t binding_mismatches;
    uint64_t order_failures;
    uint64_t invalid_records;
    uint64_t deadline_checks;
    uint64_t deadline_expirations;
    uint64_t clock_regressions;
    uint64_t epoch_advances;
    uint64_t failures;
} worr_native_readiness_telemetry_v1;

enum {
    WORR_NATIVE_READINESS_STATE_INITIALIZED = 1u << 0,
};

typedef struct worr_native_readiness_state_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t role;
    uint16_t phase;
    uint16_t state_flags;
    uint32_t transport_epoch;
    uint32_t negotiated_capabilities;
    uint32_t reserved0;
    uint64_t readiness_nonce;
    uint64_t nonce_floor;
    uint64_t generation;
    uint64_t timeout_ticks;
    uint64_t phase_start_tick;
    uint64_t deadline_tick;
    uint64_t last_tick;
    worr_native_readiness_telemetry_v1 telemetry;
} worr_native_readiness_state_v1;

/*
 * Records use a canonical little-endian CRC32 over their domain tag and every
 * field except record_checksum.  The checksum detects accidental corruption;
 * it is not authentication.  Invalid initialization leaves output untouched.
 */
bool Worr_NativeReadinessRecordInitV1(
    worr_native_readiness_record_v1 *record_out,
    uint16_t record_kind,
    uint32_t transport_epoch,
    uint32_t negotiated_capabilities,
    uint64_t readiness_nonce);
bool Worr_NativeReadinessRecordValidateV1(
    const worr_native_readiness_record_v1 *record);
bool Worr_NativeReadinessStateValidateV1(
    const worr_native_readiness_state_v1 *state);

/*
 * Server initialization is legal only after local receive resources and a
 * BYPASS-only transmit hook are ready.  Success atomically initializes state
 * and emits CHALLENGE.  State and output must be disjoint and remain untouched
 * on failure.  timeout_ticks is nonzero and now_tick + timeout_ticks must fit.
 * For both Init entry points, transport_epoch identifies the fresh connection
 * incarnation and must be globally non-reused by the caller.
 */
worr_native_readiness_result_v1 Worr_NativeReadinessServerInitV1(
    worr_native_readiness_state_v1 *state_out,
    uint32_t transport_epoch,
    uint32_t negotiated_capabilities,
    uint64_t readiness_nonce,
    uint64_t now_tick,
    uint64_t timeout_ticks,
    worr_native_readiness_record_v1 *challenge_out);

/* Client initialization waits for the exact server CHALLENGE. */
worr_native_readiness_result_v1 Worr_NativeReadinessClientInitV1(
    worr_native_readiness_state_v1 *state_out,
    uint32_t transport_epoch,
    uint32_t negotiated_capabilities,
    uint64_t now_tick,
    uint64_t timeout_ticks);

/*
 * In-incarnation advancement requires a strictly newer epoch and generation.
 * Server nonces must be strictly increasing; client advancement retains the
 * previous accepted nonce as a floor for the next CHALLENGE.  UINT64_MAX is
 * terminal nonce exhaustion, and UINT32_MAX is terminal transport-epoch
 * exhaustion.  Fresh reconnect objects use Init instead.  Every fresh
 * connection incarnation must receive a globally non-reused transport_epoch;
 * that caller-owned invariant prevents an old valid CHALLENGE from binding to
 * a newly initialized client and cannot be enforced by this pointer-free core.
 */
worr_native_readiness_result_v1 Worr_NativeReadinessServerAdvanceEpochV1(
    worr_native_readiness_state_v1 *state,
    uint32_t transport_epoch,
    uint32_t negotiated_capabilities,
    uint64_t readiness_nonce,
    uint64_t now_tick,
    uint64_t timeout_ticks,
    worr_native_readiness_record_v1 *challenge_out);
worr_native_readiness_result_v1 Worr_NativeReadinessClientAdvanceEpochV1(
    worr_native_readiness_state_v1 *state,
    uint32_t transport_epoch,
    uint32_t negotiated_capabilities,
    uint64_t now_tick,
    uint64_t timeout_ticks);

/*
 * Exact transition chain:
 *   CHALLENGE -> CLIENT_READY -> SERVER_ACTIVE.
 *
 * Every mutable object, input record, and output record must be pairwise
 * disjoint.  Invalid arguments/aliasing leave all objects untouched.  A valid
 * but corrupt, out-of-order, or binding-mismatched peer record makes failure
 * sticky and leaves outputs untouched.  Exact duplicates are idempotent;
 * duplicate CHALLENGE/CLIENT_READY observations reproduce the exact response
 * so a lost response can be retried without reopening the state transition.
 */
worr_native_readiness_result_v1 Worr_NativeReadinessClientObserveChallengeV1(
    worr_native_readiness_state_v1 *state,
    const worr_native_readiness_record_v1 *challenge,
    uint64_t now_tick,
    worr_native_readiness_record_v1 *client_ready_out);
worr_native_readiness_result_v1 Worr_NativeReadinessServerObserveClientReadyV1(
    worr_native_readiness_state_v1 *state,
    const worr_native_readiness_record_v1 *client_ready,
    uint64_t now_tick,
    worr_native_readiness_record_v1 *server_active_out);
worr_native_readiness_result_v1 Worr_NativeReadinessClientObserveServerActiveV1(
    worr_native_readiness_state_v1 *state,
    const worr_native_readiness_record_v1 *server_active,
    uint64_t now_tick);

/* Waiting states fail closed at now_tick >= deadline_tick. */
worr_native_readiness_result_v1 Worr_NativeReadinessCheckDeadlineV1(
    worr_native_readiness_state_v1 *state,
    uint64_t now_tick);

/*
 * Client receive becomes safe after CHALLENGE was accepted and local adapter
 * setup completed before CLIENT_READY emission.  Server receive/transmit and
 * client transmit become safe only in their respective ACTIVE phases.  Live
 * adapters must use these mutating gates immediately before every native RX/TX
 * admission; each gate applies the same sticky clock/deadline check as
 * Worr_NativeReadinessCheckDeadlineV1 before testing the phase.
 */
bool Worr_NativeReadinessCanReceiveNativeV1(
    worr_native_readiness_state_v1 *state,
    uint64_t now_tick);
bool Worr_NativeReadinessCanTransmitNativeV1(
    worr_native_readiness_state_v1 *state,
    uint64_t now_tick);

/* Teardown clears the complete pointer-free object, including telemetry. */
bool Worr_NativeReadinessResetV1(
    worr_native_readiness_state_v1 *state);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_NATIVE_READINESS_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define WORR_NATIVE_READINESS_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

WORR_NATIVE_READINESS_STATIC_ASSERT(
    sizeof(worr_native_readiness_record_v1) == 32,
    "native readiness record V1 layout changed");
WORR_NATIVE_READINESS_STATIC_ASSERT(
    offsetof(worr_native_readiness_record_v1, readiness_nonce) == 16,
    "native readiness record nonce offset changed");
WORR_NATIVE_READINESS_STATIC_ASSERT(
    offsetof(worr_native_readiness_record_v1, record_checksum) == 24,
    "native readiness record checksum offset changed");
WORR_NATIVE_READINESS_STATIC_ASSERT(
    sizeof(worr_native_readiness_telemetry_v1) == 120,
    "native readiness telemetry V1 layout changed");
WORR_NATIVE_READINESS_STATIC_ASSERT(
    sizeof(worr_native_readiness_state_v1) == 200,
    "native readiness state V1 layout changed");
WORR_NATIVE_READINESS_STATIC_ASSERT(
    offsetof(worr_native_readiness_state_v1, readiness_nonce) == 24,
    "native readiness state nonce offset changed");
WORR_NATIVE_READINESS_STATIC_ASSERT(
    offsetof(worr_native_readiness_state_v1, telemetry) == 80,
    "native readiness state telemetry offset changed");

#undef WORR_NATIVE_READINESS_STATIC_ASSERT
