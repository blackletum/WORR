/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/native_carrier.h"
#include "common/net/native_session.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Transport-neutral bridge between the isolated native session and WTC1
 * carrier cores.  This API does not advertise a capability or own a live
 * netchan boundary.  A live caller must create the native-session binding
 * from a confirmed capability and may admit carrier packets only after the
 * enclosing sequenced netchan packet has passed normal admission checks.
 * Carrier CRCs detect accidental corruption; they are not authentication.
 *
 * legacy_packet always means the complete, final application prefix after
 * reliable-data assembly.  It is never merely the caller's unreliable
 * message fragment.
 */
#define WORR_NATIVE_CARRIER_SESSION_ABI_VERSION 1u

enum {
    WORR_NATIVE_CARRIER_TX_GATE_INITIALIZED = 1u << 0,
    WORR_NATIVE_CARRIER_TX_GATE_ACTIVE = 1u << 1,
    WORR_NATIVE_CARRIER_TX_GATE_PACKET_PENDING = 1u << 2,
    WORR_NATIVE_CARRIER_TX_GATE_TOKEN_EXHAUSTED = 1u << 3,
    WORR_NATIVE_CARRIER_TX_GATE_PAYLOAD_BOUND = 1u << 4,
};

/*
 * One gate is owned by one connection/transport epoch.  It is deliberately
 * separate from the native TX session so enqueue and inbound-ACK processing
 * may continue while a fragmented message is in flight.  Exactly one native
 * message dispatch may be active.  The monotonically consumed token and the
 * gate's confirmed-fragment cursor reject copied/replayed dispatch objects.
 */
typedef struct worr_native_carrier_tx_gate_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t state_flags;
    uint32_t transport_epoch;
    uint32_t active_message_sequence;
    uint64_t next_token_id;
    uint64_t active_token_id;
    uint64_t last_handoff_tick;
    uint64_t committed_bursts;
    uint64_t aborted_bursts;
    uint64_t retired_bursts;
    uint16_t confirmed_fragments;
    uint16_t active_fragment_count;
    uint32_t active_payload_crc32;
    uint64_t connection_owner_id;
} worr_native_carrier_tx_gate_v1;

enum {
    WORR_NATIVE_CARRIER_DISPATCH_ACTIVE = 1u << 0,
    WORR_NATIVE_CARRIER_DISPATCH_PAYLOAD_BOUND = 1u << 1,
    WORR_NATIVE_CARRIER_DISPATCH_PACKET_PENDING = 1u << 2,
    WORR_NATIVE_CARRIER_DISPATCH_COMMITTED = 1u << 3,
    WORR_NATIVE_CARRIER_DISPATCH_ABORTED = 1u << 4,
    WORR_NATIVE_CARRIER_DISPATCH_RETIRED = 1u << 5,
};

/*
 * Caller-owned state for one whole-message dispatch.  No payload pointer is
 * retained.  Packet preparation advances only pending_fragmenter; the
 * confirmed fragmenter advances only after the caller reports that the exact
 * prepared packet was accepted synchronously by the transport.  Native send
 * attempts/timestamps are committed only with the final confirmed fragment.
 */
typedef struct worr_native_carrier_dispatch_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t state_flags;
    uint32_t transport_epoch;
    uint32_t payload_handle;
    uint64_t token_id;
    uint32_t payload_bytes;
    uint32_t pending_packet_crc32;
    uint16_t application_packet_budget;
    uint16_t legacy_bytes_reserve;
    uint16_t submitted_fragments;
    uint16_t pending_fragment_index;
    uint16_t pending_packet_bytes;
    uint16_t reserved0;
    uint32_t payload_crc32;
    worr_native_tx_send_ticket_v1 send_ticket;
    worr_native_envelope_fragmenter_v1 fragmenter;
    worr_native_envelope_fragmenter_v1 pending_fragmenter;
} worr_native_carrier_dispatch_v1;

typedef enum worr_native_carrier_session_result_v1_e {
    WORR_NATIVE_CARRIER_SESSION_OK = 0,
    WORR_NATIVE_CARRIER_SESSION_DISPATCH_COMMITTED = 1,
    WORR_NATIVE_CARRIER_SESSION_DISPATCH_RETIRED = 2,
    WORR_NATIVE_CARRIER_SESSION_NOT_DUE = 3,
    WORR_NATIVE_CARRIER_SESSION_NO_CARRIER = 4,
    WORR_NATIVE_CARRIER_SESSION_ENTRY_NOT_FOUND = 5,
    WORR_NATIVE_CARRIER_SESSION_WRONG_ENTRY_TYPE = 6,
    WORR_NATIVE_CARRIER_SESSION_INVALID_ARGUMENT = 7,
    WORR_NATIVE_CARRIER_SESSION_INVALID_STATE = 8,
    WORR_NATIVE_CARRIER_SESSION_LIMIT = 9,
    WORR_NATIVE_CARRIER_SESSION_OUTPUT_TOO_SMALL = 10,
    WORR_NATIVE_CARRIER_SESSION_MALFORMED = 11,
    WORR_NATIVE_CARRIER_SESSION_UNSUPPORTED = 12,
    WORR_NATIVE_CARRIER_SESSION_CORRUPT = 13,
    WORR_NATIVE_CARRIER_SESSION_WRONG_EPOCH = 14,
    WORR_NATIVE_CARRIER_SESSION_FUTURE_ACK = 15,
    WORR_NATIVE_CARRIER_SESSION_UNSENT_ACK = 16,
    WORR_NATIVE_CARRIER_SESSION_NATIVE_REJECTED = 17,
    WORR_NATIVE_CARRIER_SESSION_PACKET_PENDING = 18,
    WORR_NATIVE_CARRIER_SESSION_STALE_DISPATCH = 19,
    WORR_NATIVE_CARRIER_SESSION_CLOCK_REGRESSION = 20,
    WORR_NATIVE_CARRIER_SESSION_TOKEN_EXHAUSTED = 21,
    WORR_NATIVE_CARRIER_SESSION_SEMANTIC_REVALIDATION_REQUIRED = 22,
} worr_native_carrier_session_result_v1;

/*
 * Computes the largest complete WNE1 datagram available to one DATA entry:
 *
 *   application budget - complete legacy reserve - WTC1 footer
 *   - one DATA entry header - 16 bytes per reserved ACK entry.
 *
 * application_packet_budget is the direction's actual application ceiling
 * (for example min(1200, netchan.maxpacketlen)), never merely the WTC1 ceiling.
 * At least a WNE1 header plus one payload byte must remain.  The complete
 * application budget is capped at 1,200 bytes and ack_range_reserve at seven
 * because the DATA entry consumes the eighth WTC1 entry.  A frozen fragment
 * plan must fit this reserve for every packet in its burst.  For example, a
 * 1,024-byte application ceiling with no prefix/ACK reserve permits a
 * 984-byte WNE1 datagram, a 928-byte stride, and at most 59,392 payload bytes
 * across WNE1's 64-fragment ceiling.  The output is untouched on failure.
 */
bool Worr_NativeCarrierSessionDataBudgetV1(
    uint16_t application_packet_budget,
    uint16_t legacy_bytes_reserve,
    uint16_t ack_range_reserve,
    uint16_t *max_wne_datagram_bytes_out);

bool Worr_NativeCarrierTxGateInitV1(
    worr_native_carrier_tx_gate_v1 *gate_out,
    const worr_native_session_binding_v1 *binding);

bool Worr_NativeCarrierTxGateAdvanceEpochV1(
    worr_native_carrier_tx_gate_v1 *gate,
    const worr_native_session_binding_v1 *binding);

bool Worr_NativeCarrierTxGateValidateV1(
    const worr_native_carrier_tx_gate_v1 *gate);

/*
 * Reserves the gate and selects the same due record as the native TX
 * scheduler without mutating the session or slots.  The selected payload
 * handle is available in dispatch_out->send_ticket.pre_send_slot so the
 * caller can resolve it before BindPayload.  The slot's already-frozen WNE1
 * stride/count must fit the complete prefix reserve for the whole burst.
 * This first safe bridge emits DATA separately from the retained ACK ledger;
 * use the ACK-only scheduler when no DATA-independent receipt opportunity
 * exists.  Gate and output are unchanged for NOT_DUE or any failure.
 */
worr_native_carrier_session_result_v1
Worr_NativeCarrierSessionDispatchBeginV1(
    worr_native_carrier_tx_gate_v1 *gate,
    const worr_native_tx_session_v1 *session,
    const worr_native_tx_slot_v1 *slots,
    uint16_t slot_capacity,
    uint64_t selection_tick,
    uint32_t resend_interval_ticks,
    uint16_t application_packet_budget,
    uint16_t legacy_bytes_reserve,
    worr_native_carrier_dispatch_v1 *dispatch_out);

/*
 * Binds caller-owned immutable canonical bytes to the active dispatch.  The
 * payload checksum is rederived before every packet preparation; the
 * process-local handle must not be recycled while retained.  Dispatch is
 * untouched on failure.
 */
worr_native_carrier_session_result_v1
Worr_NativeCarrierSessionDispatchBindPayloadV1(
    worr_native_carrier_tx_gate_v1 *gate,
    worr_native_carrier_dispatch_v1 *dispatch,
    uint32_t payload_handle,
    const void *payload,
    uint32_t payload_bytes);

/*
 * Prepares exactly one WNE1 fragment in one WTC1 DATA entry.  It records a
 * pending fragment and full-packet CRC but does not advance the confirmed
 * cursor or native send accounting.  Actual legacy bytes must fit the reserve
 * frozen at Begin.  Gate, dispatch, packet bytes, and byte count are all
 * untouched on failure. Mutable state and outputs must be disjoint from all
 * inputs and each other. The read-only payload and legacy prefix may overlap;
 * packet_out may overlap the complete legacy_packet input but not payload.
 */
worr_native_carrier_session_result_v1
Worr_NativeCarrierSessionDispatchPreparePacketV1(
    worr_native_carrier_tx_gate_v1 *gate,
    const worr_native_tx_session_v1 *session,
    const worr_native_tx_slot_v1 *slots,
    uint16_t slot_capacity,
    worr_native_carrier_dispatch_v1 *dispatch,
    uint32_t payload_handle,
    const void *payload,
    uint32_t payload_bytes,
    const void *legacy_packet,
    uint16_t legacy_bytes,
    void *packet_out,
    size_t packet_capacity,
    size_t *packet_bytes_out);

/*
 * Call ConfirmPacket only after synchronous transport acceptance of the
 * prepared application packet. Confirmation rechecks its length, full-packet
 * CRC, and decoded native identity; CRC is not adversarial byte identity or
 * authentication. Non-final confirmation advances only the
 * gate/dispatch cursors.  Final confirmation atomically consumes the native
 * prepared-send ticket, records one complete send attempt, clears the gate,
 * and returns DISPATCH_COMMITTED.  If the admitted packet's retained identity
 * was concurrently acknowledged or superseded after preparation, confirmation
 * consumes the pending outcome, retires the dispatch without send accounting,
 * and returns DISPATCH_RETIRED. RejectPacket is the only legal response to a
 * definite transport rejection and leaves the same fragment ready to build
 * again.  A copied dispatch cannot confirm or rebuild against an advanced
 * gate.  All mutable state and packet ranges must be disjoint.
 */
worr_native_carrier_session_result_v1
Worr_NativeCarrierSessionDispatchConfirmPacketV1(
    worr_native_carrier_tx_gate_v1 *gate,
    worr_native_tx_session_v1 *session,
    worr_native_tx_slot_v1 *slots,
    uint16_t slot_capacity,
    worr_native_carrier_dispatch_v1 *dispatch,
    uint64_t handoff_tick,
    const void *packet,
    size_t packet_bytes);

/*
 * Mixed-packet counterpart used by the bounded DATA+ACK coordinator.  It
 * preserves the exact DATA identity and full-packet checks of ConfirmPacket,
 * but requires entry zero to be the sole DATA entry and every remaining
 * entry to be ACK.  ACK authority and exact range identity remain the ACK
 * ledger's responsibility.  The original ConfirmPacket API continues to
 * require exactly one DATA entry.
 */
worr_native_carrier_session_result_v1
Worr_NativeCarrierSessionDispatchConfirmMixedPacketV1(
    worr_native_carrier_tx_gate_v1 *gate,
    worr_native_tx_session_v1 *session,
    worr_native_tx_slot_v1 *slots,
    uint16_t slot_capacity,
    worr_native_carrier_dispatch_v1 *dispatch,
    uint64_t handoff_tick,
    const void *packet,
    size_t packet_bytes);

worr_native_carrier_session_result_v1
Worr_NativeCarrierSessionDispatchRejectPacketV1(
    worr_native_carrier_tx_gate_v1 *gate,
    worr_native_carrier_dispatch_v1 *dispatch);

/*
 * Abandons an active dispatch only when no prepared packet has an unknown
 * transport outcome.  Confirmed partial fragments remain harmless receiver
 * reassembly state and will expire; no native send attempt is recorded.  The
 * gate token is consumed, so copied plans remain invalid.
 */
worr_native_carrier_session_result_v1
Worr_NativeCarrierSessionDispatchAbortV1(
    worr_native_carrier_tx_gate_v1 *gate,
    worr_native_carrier_dispatch_v1 *dispatch);

/*
 * Decodes the complete admitted packet internally and applies every ACK entry
 * to a copied TX session/slot set. The batch commits all-or-none. Footer epoch is
 * converted into each fully initialized native exact range.  Local-future
 * ranges and ranges naming any retained slot that has never been selected are
 * rejected before commit.  DATA entries are ignored.  On success the returned
 * count is the exact sum written by the existing ApplyAck API; it is zero when
 * all exact ranges were already empty.  State and output are untouched on any
 * failure, including a packet with no ACK entry.  This function is legal only
 * after confirmed capability/epoch binding and successful sequenced-netchan
 * admission; WTC1 CRC alone is not receipt authority.
 */
worr_native_carrier_session_result_v1
Worr_NativeCarrierSessionApplyAcksV1(
    worr_native_tx_session_v1 *session,
    worr_native_tx_slot_v1 *slots,
    uint16_t slot_capacity,
    const void *packet,
    size_t packet_bytes,
    uint32_t *acked_count_out);

/*
 * Decodes an admitted packet+entry_index internally, preventing an untrusted constructed
 * view from naming arbitrary memory, then routes one DATA entry through the
 * existing RX session API.  rx_result_out is written iff RX Accept was
 * invoked.  message_out and repeat_acknowledgement_out retain the exact
 * conditional-write semantics of RX Accept (MESSAGE_COMPLETE and
 * ALREADY_COMMITTED respectively).  Session mutation is per entry; no
 * whole-mixed-packet atomicity is claimed.  The wrapper returns OK whenever
 * RX Accept was invoked, including for an underlying rejection; callers must
 * inspect rx_result_out.  NATIVE_REJECTED is reserved for bridge operations
 * that require an underlying success and is not returned for an RX result.
 * The three outputs are pairwise disjoint and may not overlap any state,
 * storage, or packet input.
 */
worr_native_carrier_session_result_v1
Worr_NativeCarrierSessionAcceptDataV1(
    worr_native_rx_session_v1 *session,
    worr_native_rx_slot_v1 *slots,
    uint16_t slot_capacity,
    void *payload_arena,
    size_t payload_arena_bytes,
    uint64_t now_tick,
    const void *packet,
    size_t packet_bytes,
    uint16_t entry_index,
    worr_native_rx_result_v1 *rx_result_out,
    worr_native_rx_message_v1 *message_out,
    worr_native_ack_range_v1 *repeat_acknowledgement_out);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_NATIVE_CARRIER_SESSION_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define WORR_NATIVE_CARRIER_SESSION_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

WORR_NATIVE_CARRIER_SESSION_STATIC_ASSERT(
    sizeof(worr_native_carrier_tx_gate_v1) == 80,
    "native carrier TX gate v1 layout changed");
WORR_NATIVE_CARRIER_SESSION_STATIC_ASSERT(
    offsetof(worr_native_carrier_tx_gate_v1, connection_owner_id) == 72,
    "native carrier TX gate owner offset changed");
WORR_NATIVE_CARRIER_SESSION_STATIC_ASSERT(
    offsetof(worr_native_carrier_tx_gate_v1, next_token_id) == 16,
    "native carrier TX gate token offset changed");
WORR_NATIVE_CARRIER_SESSION_STATIC_ASSERT(
    sizeof(worr_native_carrier_dispatch_v1) == 248,
    "native carrier dispatch v1 layout changed");
WORR_NATIVE_CARRIER_SESSION_STATIC_ASSERT(
    offsetof(worr_native_carrier_dispatch_v1, send_ticket) == 48,
    "native carrier dispatch ticket offset changed");
WORR_NATIVE_CARRIER_SESSION_STATIC_ASSERT(
    offsetof(worr_native_carrier_dispatch_v1, pending_fragmenter) ==
        offsetof(worr_native_carrier_dispatch_v1, fragmenter) +
            sizeof(worr_native_envelope_fragmenter_v1),
    "native carrier pending fragmenter layout changed");

#undef WORR_NATIVE_CARRIER_SESSION_STATIC_ASSERT
