/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/native_carrier_ack.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Pointer-free, transport-neutral transaction joining one WTC1 DATA fragment
 * with zero to seven exact ACK ranges.  It does not advertise capability or
 * call transport.  All live mutable objects remain caller-owned and every
 * operation stages them before committing an all-or-none state transition.
 */
#define WORR_NATIVE_CARRIER_MIXED_ABI_VERSION 1u
#define WORR_NATIVE_CARRIER_MIXED_ACK_RESERVE 7u

enum {
    WORR_NATIVE_CARRIER_MIXED_TOKEN_INITIALIZED = 1u << 0,
    WORR_NATIVE_CARRIER_MIXED_TOKEN_PACKET_BOUND = 1u << 1,
    WORR_NATIVE_CARRIER_MIXED_TOKEN_ACK_BOUND = 1u << 2,
};

typedef struct worr_native_carrier_mixed_token_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t state_flags;
    uint32_t transport_epoch;
    uint32_t packet_crc32;
    uint16_t packet_bytes;
    uint16_t ack_range_count;
    uint32_t reserved0;
    uint64_t connection_owner_id;
    uint64_t dispatch_token_id;
    uint32_t message_sequence;
    uint16_t fragment_index;
    uint16_t reserved1;
    worr_native_carrier_ack_emit_token_v1 ack_token;
} worr_native_carrier_mixed_token_v1;

typedef enum worr_native_carrier_mixed_result_v1_e {
    WORR_NATIVE_CARRIER_MIXED_OK = 0,
    WORR_NATIVE_CARRIER_MIXED_DISPATCH_COMMITTED = 1,
    WORR_NATIVE_CARRIER_MIXED_DISPATCH_RETIRED = 2,
    WORR_NATIVE_CARRIER_MIXED_NOT_DUE = 3,
    WORR_NATIVE_CARRIER_MIXED_INVALID_ARGUMENT = 4,
    WORR_NATIVE_CARRIER_MIXED_INVALID_STATE = 5,
    WORR_NATIVE_CARRIER_MIXED_LIMIT = 6,
    WORR_NATIVE_CARRIER_MIXED_OUTPUT_TOO_SMALL = 7,
    WORR_NATIVE_CARRIER_MIXED_WRONG_EPOCH = 8,
    WORR_NATIVE_CARRIER_MIXED_CLOCK_REGRESSION = 9,
    WORR_NATIVE_CARRIER_MIXED_PACKET_PENDING = 10,
    WORR_NATIVE_CARRIER_MIXED_STALE_TRANSACTION = 11,
    WORR_NATIVE_CARRIER_MIXED_TOKEN_EXHAUSTED = 12,
    WORR_NATIVE_CARRIER_MIXED_PACKET_MISMATCH = 13,
    WORR_NATIVE_CARRIER_MIXED_DATA_REJECTED = 14,
    WORR_NATIVE_CARRIER_MIXED_ACK_REJECTED = 15,
} worr_native_carrier_mixed_result_v1;

/*
 * Begins the same due DATA dispatch as the existing session API, but only
 * if its already-frozen WNE1 fragment stride fits the complete application
 * budget after permanently reserving seven ACK entries.  Gate and output
 * are untouched on every failure.
 */
worr_native_carrier_mixed_result_v1 Worr_NativeCarrierMixedBeginV1(
    worr_native_carrier_tx_gate_v1 *gate,
    const worr_native_tx_session_v1 *session,
    const worr_native_tx_slot_v1 *slots, uint16_t slot_capacity,
    uint64_t selection_tick, uint32_t resend_interval_ticks,
    uint16_t application_packet_budget, uint16_t legacy_bytes_reserve,
    worr_native_carrier_dispatch_v1 *dispatch_out);

/*
 * Prepares one DATA fragment first, then appends the exact due ACK ranges.
 * NOT_DUE from the ACK ledger is a successful DATA-only fallback.  Every
 * other ACK error fails the whole operation.  Success returns a token
 * bound to the exact full packet; retries must terminate it with
 * RejectPacket and prepare again.  No output or mutable state changes on
 * failure.
 */
worr_native_carrier_mixed_result_v1 Worr_NativeCarrierMixedPreparePacketV1(
    worr_native_carrier_tx_gate_v1 *gate,
    const worr_native_tx_session_v1 *session,
    const worr_native_tx_slot_v1 *slots, uint16_t slot_capacity,
    worr_native_carrier_dispatch_v1 *dispatch,
    worr_native_carrier_ack_ledger_v1 *ack_ledger, uint64_t now_tick,
    uint32_t ack_retry_interval_ticks, uint32_t payload_handle,
    const void *payload, uint32_t payload_bytes, const void *legacy_packet,
    uint16_t legacy_bytes, void *packet_out, size_t packet_capacity,
    size_t *packet_bytes_out, worr_native_carrier_mixed_token_v1 *token_out);

/* Synchronous accepted/rejected transport outcomes. */
worr_native_carrier_mixed_result_v1 Worr_NativeCarrierMixedConfirmPacketV1(
    worr_native_carrier_tx_gate_v1 *gate, worr_native_tx_session_v1 *session,
    worr_native_tx_slot_v1 *slots, uint16_t slot_capacity,
    worr_native_carrier_dispatch_v1 *dispatch,
    worr_native_carrier_ack_ledger_v1 *ack_ledger,
    const worr_native_carrier_mixed_token_v1 *token, uint64_t handoff_tick,
    const void *packet, size_t packet_bytes);

worr_native_carrier_mixed_result_v1 Worr_NativeCarrierMixedRejectPacketV1(
    worr_native_carrier_tx_gate_v1 *gate,
    worr_native_carrier_dispatch_v1 *dispatch,
    worr_native_carrier_ack_ledger_v1 *ack_ledger,
    const worr_native_carrier_mixed_token_v1 *token, const void *packet,
    size_t packet_bytes);

/*
 * Discards a bound packet that provably never reached transport, then aborts
 * the whole DATA dispatch.  No ACK handoff credit or DATA send attempt is
 * spent.  RejectPacket instead keeps the same DATA fragment retryable.
 */
worr_native_carrier_mixed_result_v1 Worr_NativeCarrierMixedAbortPacketV1(
    worr_native_carrier_tx_gate_v1 *gate,
    worr_native_carrier_dispatch_v1 *dispatch,
    worr_native_carrier_ack_ledger_v1 *ack_ledger,
    const worr_native_carrier_mixed_token_v1 *token, const void *packet,
    size_t packet_bytes);

/* Aborts an active DATA dispatch before any packet preparation. */
worr_native_carrier_mixed_result_v1
Worr_NativeCarrierMixedAbortV1(worr_native_carrier_tx_gate_v1 *gate,
                               worr_native_carrier_dispatch_v1 *dispatch,
                               worr_native_carrier_ack_ledger_v1 *ack_ledger);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_NATIVE_CARRIER_MIXED_STATIC_ASSERT(condition, message)           \
    static_assert((condition), message)
#else
#define WORR_NATIVE_CARRIER_MIXED_STATIC_ASSERT(condition, message)           \
    _Static_assert((condition), message)
#endif

WORR_NATIVE_CARRIER_MIXED_STATIC_ASSERT(
    sizeof(worr_native_carrier_mixed_token_v1) == 168,
    "native carrier mixed token v1 layout changed");
WORR_NATIVE_CARRIER_MIXED_STATIC_ASSERT(
    offsetof(worr_native_carrier_mixed_token_v1, connection_owner_id) == 24,
    "native carrier mixed owner offset changed");
WORR_NATIVE_CARRIER_MIXED_STATIC_ASSERT(
    offsetof(worr_native_carrier_mixed_token_v1, ack_token) == 48,
    "native carrier mixed ACK token offset changed");

#undef WORR_NATIVE_CARRIER_MIXED_STATIC_ASSERT
