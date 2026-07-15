/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/native_carrier_mixed.h"

#include <limits.h>
#include <stdbool.h>
#include <string.h>

static bool ranges_overlap(const void *left, size_t left_bytes,
                           const void *right, size_t right_bytes)
{
    const uintptr_t left_begin = (uintptr_t)left;
    const uintptr_t right_begin = (uintptr_t)right;
    uintptr_t left_end;
    uintptr_t right_end;

    if (left == NULL || right == NULL || left_bytes == 0 || right_bytes == 0)
        return false;
    if (left_bytes > UINTPTR_MAX - left_begin ||
        right_bytes > UINTPTR_MAX - right_begin) {
        return true;
    }
    left_end = left_begin + left_bytes;
    right_end = right_begin + right_bytes;
    return left_begin < right_end && right_begin < left_end;
}

static bool bytes_are_zero(const void *data, size_t count)
{
    const uint8_t *bytes = (const uint8_t *)data;
    size_t index;

    for (index = 0; index < count; ++index) {
        if (bytes[index] != 0)
            return false;
    }
    return true;
}

static uint32_t crc32_bytes(const void *data, size_t count)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = UINT32_MAX;
    size_t index;

    for (index = 0; index < count; ++index) {
        unsigned bit;

        crc ^= bytes[index];
        for (bit = 0; bit < 8; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return ~crc;
}

static worr_native_carrier_mixed_result_v1
session_result(worr_native_carrier_session_result_v1 result)
{
    switch (result) {
    case WORR_NATIVE_CARRIER_SESSION_OK:
        return WORR_NATIVE_CARRIER_MIXED_OK;
    case WORR_NATIVE_CARRIER_SESSION_DISPATCH_COMMITTED:
        return WORR_NATIVE_CARRIER_MIXED_DISPATCH_COMMITTED;
    case WORR_NATIVE_CARRIER_SESSION_DISPATCH_RETIRED:
        return WORR_NATIVE_CARRIER_MIXED_DISPATCH_RETIRED;
    case WORR_NATIVE_CARRIER_SESSION_NOT_DUE:
        return WORR_NATIVE_CARRIER_MIXED_NOT_DUE;
    case WORR_NATIVE_CARRIER_SESSION_INVALID_ARGUMENT:
        return WORR_NATIVE_CARRIER_MIXED_INVALID_ARGUMENT;
    case WORR_NATIVE_CARRIER_SESSION_INVALID_STATE:
        return WORR_NATIVE_CARRIER_MIXED_INVALID_STATE;
    case WORR_NATIVE_CARRIER_SESSION_LIMIT:
        return WORR_NATIVE_CARRIER_MIXED_LIMIT;
    case WORR_NATIVE_CARRIER_SESSION_OUTPUT_TOO_SMALL:
        return WORR_NATIVE_CARRIER_MIXED_OUTPUT_TOO_SMALL;
    case WORR_NATIVE_CARRIER_SESSION_WRONG_EPOCH:
        return WORR_NATIVE_CARRIER_MIXED_WRONG_EPOCH;
    case WORR_NATIVE_CARRIER_SESSION_CLOCK_REGRESSION:
        return WORR_NATIVE_CARRIER_MIXED_CLOCK_REGRESSION;
    case WORR_NATIVE_CARRIER_SESSION_PACKET_PENDING:
        return WORR_NATIVE_CARRIER_MIXED_PACKET_PENDING;
    case WORR_NATIVE_CARRIER_SESSION_STALE_DISPATCH:
        return WORR_NATIVE_CARRIER_MIXED_STALE_TRANSACTION;
    case WORR_NATIVE_CARRIER_SESSION_TOKEN_EXHAUSTED:
        return WORR_NATIVE_CARRIER_MIXED_TOKEN_EXHAUSTED;
    default:
        return WORR_NATIVE_CARRIER_MIXED_DATA_REJECTED;
    }
}

static worr_native_carrier_mixed_result_v1
ack_result(worr_native_carrier_ack_result_v1 result)
{
    switch (result) {
    case WORR_NATIVE_CARRIER_ACK_OK:
        return WORR_NATIVE_CARRIER_MIXED_OK;
    case WORR_NATIVE_CARRIER_ACK_NOT_DUE:
        return WORR_NATIVE_CARRIER_MIXED_NOT_DUE;
    case WORR_NATIVE_CARRIER_ACK_INVALID_ARGUMENT:
        return WORR_NATIVE_CARRIER_MIXED_INVALID_ARGUMENT;
    case WORR_NATIVE_CARRIER_ACK_INVALID_STATE:
        return WORR_NATIVE_CARRIER_MIXED_INVALID_STATE;
    case WORR_NATIVE_CARRIER_ACK_LIMIT:
        return WORR_NATIVE_CARRIER_MIXED_LIMIT;
    case WORR_NATIVE_CARRIER_ACK_OUTPUT_TOO_SMALL:
        return WORR_NATIVE_CARRIER_MIXED_OUTPUT_TOO_SMALL;
    case WORR_NATIVE_CARRIER_ACK_WRONG_EPOCH:
        return WORR_NATIVE_CARRIER_MIXED_WRONG_EPOCH;
    case WORR_NATIVE_CARRIER_ACK_CLOCK_REGRESSION:
        return WORR_NATIVE_CARRIER_MIXED_CLOCK_REGRESSION;
    case WORR_NATIVE_CARRIER_ACK_STALE_PREPARATION:
        return WORR_NATIVE_CARRIER_MIXED_STALE_TRANSACTION;
    case WORR_NATIVE_CARRIER_ACK_PACKET_MISMATCH:
        return WORR_NATIVE_CARRIER_MIXED_PACKET_MISMATCH;
    case WORR_NATIVE_CARRIER_ACK_TOKEN_EXHAUSTED:
        return WORR_NATIVE_CARRIER_MIXED_TOKEN_EXHAUSTED;
    default:
        return WORR_NATIVE_CARRIER_MIXED_ACK_REJECTED;
    }
}

static bool token_validate(const worr_native_carrier_mixed_token_v1 *token)
{
    const uint16_t known = WORR_NATIVE_CARRIER_MIXED_TOKEN_INITIALIZED |
                           WORR_NATIVE_CARRIER_MIXED_TOKEN_PACKET_BOUND |
                           WORR_NATIVE_CARRIER_MIXED_TOKEN_ACK_BOUND;
    const bool ack_bound =
        token != NULL &&
        (token->state_flags & WORR_NATIVE_CARRIER_MIXED_TOKEN_ACK_BOUND) != 0;

    if (token == NULL || token->struct_size != sizeof(*token) ||
        token->schema_version != WORR_NATIVE_CARRIER_MIXED_ABI_VERSION ||
        (token->state_flags & ~known) != 0 ||
        (token->state_flags &
         (WORR_NATIVE_CARRIER_MIXED_TOKEN_INITIALIZED |
          WORR_NATIVE_CARRIER_MIXED_TOKEN_PACKET_BOUND)) !=
            (WORR_NATIVE_CARRIER_MIXED_TOKEN_INITIALIZED |
             WORR_NATIVE_CARRIER_MIXED_TOKEN_PACKET_BOUND) ||
        token->transport_epoch == 0 || token->packet_bytes == 0 ||
        token->packet_bytes > WORR_NATIVE_CARRIER_MAX_PACKET_BYTES ||
        token->connection_owner_id == 0 || token->dispatch_token_id == 0 ||
        token->message_sequence == 0 || token->reserved0 != 0 ||
        token->reserved1 != 0 ||
        token->ack_range_count > WORR_NATIVE_CARRIER_MIXED_ACK_RESERVE) {
        return false;
    }
    if (!ack_bound) {
        return token->ack_range_count == 0 &&
               bytes_are_zero(&token->ack_token, sizeof(token->ack_token));
    }
    return token->ack_range_count != 0 &&
           token->ack_token.struct_size == sizeof(token->ack_token) &&
           token->ack_token.schema_version ==
               WORR_NATIVE_CARRIER_ACK_ABI_VERSION &&
           token->ack_token.range_count == token->ack_range_count &&
           token->ack_token.transport_epoch == token->transport_epoch &&
           token->ack_token.connection_owner_id ==
               token->connection_owner_id &&
           token->ack_token.packet_crc32 == token->packet_crc32 &&
           token->ack_token.packet_bytes == token->packet_bytes &&
           (token->ack_token.state_flags &
            (WORR_NATIVE_CARRIER_ACK_TOKEN_INITIALIZED |
             WORR_NATIVE_CARRIER_ACK_TOKEN_PACKET_BOUND)) ==
               (WORR_NATIVE_CARRIER_ACK_TOKEN_INITIALIZED |
                WORR_NATIVE_CARRIER_ACK_TOKEN_PACKET_BOUND);
}

static bool
packet_matches_token(const worr_native_carrier_mixed_token_v1 *token,
                     const void *packet, size_t packet_bytes)
{
    worr_native_carrier_view_v1 view;
    worr_native_envelope_frame_info_v1 info;
    uint16_t index;

    if (!token_validate(token) || packet == NULL ||
        packet_bytes != token->packet_bytes ||
        crc32_bytes(packet, packet_bytes) != token->packet_crc32 ||
        Worr_NativeCarrierDecodeV1(packet, packet_bytes, &view) !=
            WORR_NATIVE_CARRIER_OK ||
        view.transport_epoch != token->transport_epoch ||
        view.entry_count != (uint16_t)(1u + token->ack_range_count) ||
        view.entries[0].entry_type != WORR_NATIVE_CARRIER_ENTRY_DATA_V1 ||
        Worr_NativeEnvelopeDecodeV1((const uint8_t *)packet +
                                        view.entries[0].data_offset,
                                    view.entries[0].data_bytes,
                                    &info) != WORR_NATIVE_ENVELOPE_DECODE_OK ||
        info.message_sequence != token->message_sequence ||
        info.fragment_index != token->fragment_index) {
        return false;
    }
    for (index = 0; index < token->ack_range_count; ++index) {
        const worr_native_carrier_entry_v1 *entry =
            &view.entries[(uint16_t)(index + 1u)];

        if (entry->entry_type != WORR_NATIVE_CARRIER_ENTRY_ACK_V1 ||
            entry->first_message_sequence !=
                token->ack_token.ranges[index].first_message_sequence ||
            entry->last_message_sequence !=
                token->ack_token.ranges[index].last_message_sequence) {
            return false;
        }
    }
    return true;
}

static bool owner_epoch_match(const worr_native_carrier_tx_gate_v1 *gate,
                              const worr_native_carrier_ack_ledger_v1 *ledger)
{
    return Worr_NativeCarrierTxGateValidateV1(gate) &&
           Worr_NativeCarrierAckLedgerValidateV1(ledger) &&
           gate->transport_epoch == ledger->transport_epoch &&
           gate->connection_owner_id == ledger->connection_owner_id;
}

worr_native_carrier_mixed_result_v1 Worr_NativeCarrierMixedBeginV1(
    worr_native_carrier_tx_gate_v1 *gate,
    const worr_native_tx_session_v1 *session,
    const worr_native_tx_slot_v1 *slots, uint16_t slot_capacity,
    uint64_t selection_tick, uint32_t resend_interval_ticks,
    uint16_t application_packet_budget, uint16_t legacy_bytes_reserve,
    worr_native_carrier_dispatch_v1 *dispatch_out)
{
    const size_t slots_bytes = (size_t)slot_capacity * sizeof(*slots);
    worr_native_carrier_tx_gate_v1 staged_gate;
    worr_native_carrier_dispatch_v1 staged_dispatch;
    worr_native_carrier_session_result_v1 begun;
    uint16_t reserved_datagram;
    uint32_t selected_datagram;

    if (gate == NULL || session == NULL || slots == NULL ||
        dispatch_out == NULL ||
        ranges_overlap(gate, sizeof(*gate), session, sizeof(*session)) ||
        ranges_overlap(gate, sizeof(*gate), slots, slots_bytes) ||
        ranges_overlap(dispatch_out, sizeof(*dispatch_out), gate,
                       sizeof(*gate)) ||
        ranges_overlap(dispatch_out, sizeof(*dispatch_out), session,
                       sizeof(*session)) ||
        ranges_overlap(dispatch_out, sizeof(*dispatch_out), slots,
                       slots_bytes)) {
        return WORR_NATIVE_CARRIER_MIXED_INVALID_ARGUMENT;
    }
    if (!Worr_NativeCarrierSessionDataBudgetV1(
            application_packet_budget, legacy_bytes_reserve,
            WORR_NATIVE_CARRIER_MIXED_ACK_RESERVE, &reserved_datagram)) {
        return WORR_NATIVE_CARRIER_MIXED_LIMIT;
    }
    staged_gate = *gate;
    begun = Worr_NativeCarrierSessionDispatchBeginV1(
        &staged_gate, session, slots, slot_capacity, selection_tick,
        resend_interval_ticks, application_packet_budget, legacy_bytes_reserve,
        &staged_dispatch);
    if (begun != WORR_NATIVE_CARRIER_SESSION_OK)
        return session_result(begun);
    selected_datagram =
        (uint32_t)staged_dispatch.send_ticket.pre_send_slot.fragment_stride +
        WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES;
    if (selected_datagram > reserved_datagram)
        return WORR_NATIVE_CARRIER_MIXED_LIMIT;
    *gate = staged_gate;
    *dispatch_out = staged_dispatch;
    return WORR_NATIVE_CARRIER_MIXED_OK;
}

static worr_native_carrier_entry_v1 data_entry(uint32_t data_bytes)
{
    worr_native_carrier_entry_v1 entry;

    memset(&entry, 0, sizeof(entry));
    entry.struct_size = sizeof(entry);
    entry.schema_version = WORR_NATIVE_CARRIER_ABI_VERSION;
    entry.entry_type = WORR_NATIVE_CARRIER_ENTRY_DATA_V1;
    entry.data_bytes = data_bytes;
    return entry;
}

static worr_native_carrier_entry_v1
ack_entry(const worr_native_ack_range_v1 *range)
{
    worr_native_carrier_entry_v1 entry;

    memset(&entry, 0, sizeof(entry));
    entry.struct_size = sizeof(entry);
    entry.schema_version = WORR_NATIVE_CARRIER_ABI_VERSION;
    entry.entry_type = WORR_NATIVE_CARRIER_ENTRY_ACK_V1;
    entry.first_message_sequence = range->first_message_sequence;
    entry.last_message_sequence = range->last_message_sequence;
    return entry;
}

worr_native_carrier_mixed_result_v1 Worr_NativeCarrierMixedPreparePacketV1(
    worr_native_carrier_tx_gate_v1 *gate,
    const worr_native_tx_session_v1 *session,
    const worr_native_tx_slot_v1 *slots, uint16_t slot_capacity,
    worr_native_carrier_dispatch_v1 *dispatch,
    worr_native_carrier_ack_ledger_v1 *ack_ledger, uint64_t now_tick,
    uint32_t ack_retry_interval_ticks, uint32_t payload_handle,
    const void *payload, uint32_t payload_bytes, const void *legacy_packet,
    uint16_t legacy_bytes, void *packet_out, size_t packet_capacity,
    size_t *packet_bytes_out, worr_native_carrier_mixed_token_v1 *token_out)
{
    const size_t slots_bytes = (size_t)slot_capacity * sizeof(*slots);
    uint8_t data_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    uint8_t packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    worr_native_ack_range_v1 ranges[WORR_NATIVE_CARRIER_MIXED_ACK_RESERVE];
    worr_native_carrier_entry_v1 entries[WORR_NATIVE_CARRIER_MAX_ENTRIES];
    worr_native_carrier_view_v1 data_view;
    worr_native_carrier_tx_gate_v1 staged_gate;
    worr_native_carrier_dispatch_v1 staged_dispatch;
    worr_native_carrier_ack_ledger_v1 staged_ledger;
    worr_native_carrier_ack_emit_token_v1 ack_token;
    worr_native_carrier_mixed_token_v1 token;
    worr_native_carrier_session_result_v1 data_prepared;
    worr_native_carrier_ack_result_v1 ack_prepared;
    worr_native_carrier_result_v1 encoded;
    uint16_t range_count = 0;
    uint16_t index;
    size_t data_packet_bytes = 0;
    size_t encoded_bytes = 0;
    bool ack_due = false;

    if (gate == NULL || session == NULL || slots == NULL || dispatch == NULL ||
        ack_ledger == NULL || payload == NULL || payload_handle == 0 ||
        payload_bytes == 0 || packet_out == NULL || packet_bytes_out == NULL ||
        token_out == NULL || (legacy_bytes != 0 && legacy_packet == NULL) ||
        ranges_overlap(gate, sizeof(*gate), session, sizeof(*session)) ||
        ranges_overlap(gate, sizeof(*gate), slots, slots_bytes) ||
        ranges_overlap(gate, sizeof(*gate), dispatch, sizeof(*dispatch)) ||
        ranges_overlap(gate, sizeof(*gate), ack_ledger, sizeof(*ack_ledger)) ||
        ranges_overlap(session, sizeof(*session), slots, slots_bytes) ||
        ranges_overlap(dispatch, sizeof(*dispatch), session,
                       sizeof(*session)) ||
        ranges_overlap(dispatch, sizeof(*dispatch), slots, slots_bytes) ||
        ranges_overlap(dispatch, sizeof(*dispatch), ack_ledger,
                       sizeof(*ack_ledger)) ||
        ranges_overlap(ack_ledger, sizeof(*ack_ledger), session,
                       sizeof(*session)) ||
        ranges_overlap(ack_ledger, sizeof(*ack_ledger), slots, slots_bytes) ||
        ranges_overlap(payload, payload_bytes, gate, sizeof(*gate)) ||
        ranges_overlap(payload, payload_bytes, session, sizeof(*session)) ||
        ranges_overlap(payload, payload_bytes, slots, slots_bytes) ||
        ranges_overlap(payload, payload_bytes, dispatch, sizeof(*dispatch)) ||
        ranges_overlap(payload, payload_bytes, ack_ledger,
                       sizeof(*ack_ledger)) ||
        ranges_overlap(legacy_packet, legacy_bytes, gate, sizeof(*gate)) ||
        ranges_overlap(legacy_packet, legacy_bytes, session,
                       sizeof(*session)) ||
        ranges_overlap(legacy_packet, legacy_bytes, slots, slots_bytes) ||
        ranges_overlap(legacy_packet, legacy_bytes, dispatch,
                       sizeof(*dispatch)) ||
        ranges_overlap(legacy_packet, legacy_bytes, ack_ledger,
                       sizeof(*ack_ledger)) ||
        ranges_overlap(packet_out, packet_capacity, gate, sizeof(*gate)) ||
        ranges_overlap(packet_out, packet_capacity, session,
                       sizeof(*session)) ||
        ranges_overlap(packet_out, packet_capacity, slots, slots_bytes) ||
        ranges_overlap(packet_out, packet_capacity, dispatch,
                       sizeof(*dispatch)) ||
        ranges_overlap(packet_out, packet_capacity, ack_ledger,
                       sizeof(*ack_ledger)) ||
        ranges_overlap(packet_out, packet_capacity, payload, payload_bytes) ||
        ranges_overlap(packet_out, packet_capacity, packet_bytes_out,
                       sizeof(*packet_bytes_out)) ||
        ranges_overlap(packet_out, packet_capacity, token_out,
                       sizeof(*token_out)) ||
        ranges_overlap(packet_bytes_out, sizeof(*packet_bytes_out), token_out,
                       sizeof(*token_out)) ||
        ranges_overlap(packet_bytes_out, sizeof(*packet_bytes_out), gate,
                       sizeof(*gate)) ||
        ranges_overlap(packet_bytes_out, sizeof(*packet_bytes_out), session,
                       sizeof(*session)) ||
        ranges_overlap(packet_bytes_out, sizeof(*packet_bytes_out), slots,
                       slots_bytes) ||
        ranges_overlap(packet_bytes_out, sizeof(*packet_bytes_out), dispatch,
                       sizeof(*dispatch)) ||
        ranges_overlap(packet_bytes_out, sizeof(*packet_bytes_out), ack_ledger,
                       sizeof(*ack_ledger)) ||
        ranges_overlap(packet_bytes_out, sizeof(*packet_bytes_out), payload,
                       payload_bytes) ||
        ranges_overlap(packet_bytes_out, sizeof(*packet_bytes_out),
                       legacy_packet, legacy_bytes) ||
        ranges_overlap(token_out, sizeof(*token_out), gate, sizeof(*gate)) ||
        ranges_overlap(token_out, sizeof(*token_out), session,
                       sizeof(*session)) ||
        ranges_overlap(token_out, sizeof(*token_out), slots, slots_bytes) ||
        ranges_overlap(token_out, sizeof(*token_out), dispatch,
                       sizeof(*dispatch)) ||
        ranges_overlap(token_out, sizeof(*token_out), ack_ledger,
                       sizeof(*ack_ledger)) ||
        ranges_overlap(token_out, sizeof(*token_out), payload,
                       payload_bytes) ||
        ranges_overlap(token_out, sizeof(*token_out), legacy_packet,
                       legacy_bytes)) {
        return WORR_NATIVE_CARRIER_MIXED_INVALID_ARGUMENT;
    }
    if (!owner_epoch_match(gate, ack_ledger))
        return WORR_NATIVE_CARRIER_MIXED_INVALID_STATE;

    staged_gate = *gate;
    staged_dispatch = *dispatch;
    staged_ledger = *ack_ledger;
    data_prepared = Worr_NativeCarrierSessionDispatchPreparePacketV1(
        &staged_gate, session, slots, slot_capacity, &staged_dispatch,
        payload_handle, payload, payload_bytes, legacy_packet, legacy_bytes,
        data_packet, sizeof(data_packet), &data_packet_bytes);
    if (data_prepared != WORR_NATIVE_CARRIER_SESSION_OK)
        return session_result(data_prepared);
    if (Worr_NativeCarrierDecodeV1(data_packet, data_packet_bytes,
                                   &data_view) != WORR_NATIVE_CARRIER_OK ||
        data_view.entry_count != 1 ||
        data_view.entries[0].entry_type != WORR_NATIVE_CARRIER_ENTRY_DATA_V1) {
        return WORR_NATIVE_CARRIER_MIXED_DATA_REJECTED;
    }

    memset(ranges, 0, sizeof(ranges));
    memset(&ack_token, 0, sizeof(ack_token));
    ack_prepared = Worr_NativeCarrierAckPrepareRangesV1(
        &staged_ledger, now_tick, ack_retry_interval_ticks,
        WORR_NATIVE_CARRIER_MIXED_ACK_RESERVE, ranges,
        WORR_NATIVE_CARRIER_MIXED_ACK_RESERVE, &range_count, &ack_token);
    if (ack_prepared == WORR_NATIVE_CARRIER_ACK_OK) {
        ack_due = true;
    } else if (ack_prepared != WORR_NATIVE_CARRIER_ACK_NOT_DUE) {
        return ack_result(ack_prepared);
    }

    memset(entries, 0, sizeof(entries));
    entries[0] = data_entry(data_view.entries[0].data_bytes);
    for (index = 0; index < range_count; ++index)
        entries[index + 1u] = ack_entry(&ranges[index]);
    encoded = Worr_NativeCarrierEncodeV1(
        staged_dispatch.transport_epoch, legacy_packet, legacy_bytes,
        data_packet + data_view.entries[0].data_offset,
        data_view.entries[0].data_bytes, entries, (uint16_t)(1u + range_count),
        packet, staged_dispatch.application_packet_budget, &encoded_bytes);
    if (encoded != WORR_NATIVE_CARRIER_OK) {
        return encoded == WORR_NATIVE_CARRIER_OUTPUT_TOO_SMALL
                   ? WORR_NATIVE_CARRIER_MIXED_OUTPUT_TOO_SMALL
               : encoded == WORR_NATIVE_CARRIER_LIMIT
                   ? WORR_NATIVE_CARRIER_MIXED_LIMIT
                   : WORR_NATIVE_CARRIER_MIXED_DATA_REJECTED;
    }
    if (encoded_bytes > packet_capacity)
        return WORR_NATIVE_CARRIER_MIXED_OUTPUT_TOO_SMALL;
    if (ack_due) {
        ack_prepared = Worr_NativeCarrierAckBindPacketV1(
            &staged_ledger, &ack_token, packet, encoded_bytes);
        if (ack_prepared != WORR_NATIVE_CARRIER_ACK_OK)
            return ack_result(ack_prepared);
    }

    staged_dispatch.pending_packet_bytes = (uint16_t)encoded_bytes;
    staged_dispatch.pending_packet_crc32 = crc32_bytes(packet, encoded_bytes);
    memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    token.schema_version = WORR_NATIVE_CARRIER_MIXED_ABI_VERSION;
    token.state_flags = WORR_NATIVE_CARRIER_MIXED_TOKEN_INITIALIZED |
                        WORR_NATIVE_CARRIER_MIXED_TOKEN_PACKET_BOUND;
    token.transport_epoch = staged_dispatch.transport_epoch;
    token.packet_crc32 = staged_dispatch.pending_packet_crc32;
    token.packet_bytes = (uint16_t)encoded_bytes;
    token.ack_range_count = range_count;
    token.connection_owner_id = staged_gate.connection_owner_id;
    token.dispatch_token_id = staged_dispatch.token_id;
    token.message_sequence =
        staged_dispatch.send_ticket.pre_send_slot.message_sequence;
    token.fragment_index = staged_dispatch.pending_fragment_index;
    if (ack_due) {
        token.state_flags |= WORR_NATIVE_CARRIER_MIXED_TOKEN_ACK_BOUND;
        token.ack_token = ack_token;
    }
    if (!token_validate(&token) ||
        !packet_matches_token(&token, packet, encoded_bytes)) {
        return WORR_NATIVE_CARRIER_MIXED_INVALID_STATE;
    }

    memmove(packet_out, packet, encoded_bytes);
    *packet_bytes_out = encoded_bytes;
    *token_out = token;
    *gate = staged_gate;
    *dispatch = staged_dispatch;
    *ack_ledger = staged_ledger;
    return WORR_NATIVE_CARRIER_MIXED_OK;
}

static bool token_matches_live(const worr_native_carrier_tx_gate_v1 *gate,
                               const worr_native_carrier_dispatch_v1 *dispatch,
                               const worr_native_carrier_ack_ledger_v1 *ledger,
                               const worr_native_carrier_mixed_token_v1 *token)
{
    const bool ack_bound =
        token != NULL &&
        (token->state_flags & WORR_NATIVE_CARRIER_MIXED_TOKEN_ACK_BOUND) != 0;

    return token_validate(token) && owner_epoch_match(gate, ledger) &&
           token->transport_epoch == gate->transport_epoch &&
           token->connection_owner_id == gate->connection_owner_id &&
           token->dispatch_token_id == gate->active_token_id &&
           token->dispatch_token_id == dispatch->token_id &&
           token->message_sequence == gate->active_message_sequence &&
           token->message_sequence ==
               dispatch->send_ticket.pre_send_slot.message_sequence &&
           token->fragment_index == dispatch->pending_fragment_index &&
           token->packet_bytes == dispatch->pending_packet_bytes &&
           token->packet_crc32 == dispatch->pending_packet_crc32 &&
           ((ledger->state_flags &
             WORR_NATIVE_CARRIER_ACK_LEDGER_EMIT_ACTIVE) != 0) == ack_bound &&
           (!ack_bound || memcmp(&ledger->active_token, &token->ack_token,
                                 sizeof(token->ack_token)) == 0);
}

worr_native_carrier_mixed_result_v1 Worr_NativeCarrierMixedConfirmPacketV1(
    worr_native_carrier_tx_gate_v1 *gate, worr_native_tx_session_v1 *session,
    worr_native_tx_slot_v1 *slots, uint16_t slot_capacity,
    worr_native_carrier_dispatch_v1 *dispatch,
    worr_native_carrier_ack_ledger_v1 *ack_ledger,
    const worr_native_carrier_mixed_token_v1 *token, uint64_t handoff_tick,
    const void *packet, size_t packet_bytes)
{
    const size_t slots_bytes = (size_t)slot_capacity * sizeof(*slots);
    worr_native_carrier_tx_gate_v1 staged_gate;
    worr_native_tx_session_v1 staged_session;
    worr_native_tx_slot_v1 staged_slots[WORR_NATIVE_SESSION_MAX_TX_SLOTS];
    worr_native_carrier_dispatch_v1 staged_dispatch;
    worr_native_carrier_ack_ledger_v1 staged_ledger;
    worr_native_carrier_session_result_v1 data_confirmed;
    worr_native_carrier_ack_result_v1 ack_confirmed;
    const bool ack_bound =
        token != NULL &&
        (token->state_flags & WORR_NATIVE_CARRIER_MIXED_TOKEN_ACK_BOUND) != 0;

    if (gate == NULL || session == NULL || slots == NULL || dispatch == NULL ||
        ack_ledger == NULL || token == NULL || packet == NULL ||
        packet_bytes == 0 ||
        slot_capacity > WORR_NATIVE_SESSION_MAX_TX_SLOTS ||
        ranges_overlap(gate, sizeof(*gate), session, sizeof(*session)) ||
        ranges_overlap(gate, sizeof(*gate), slots, slots_bytes) ||
        ranges_overlap(gate, sizeof(*gate), dispatch, sizeof(*dispatch)) ||
        ranges_overlap(gate, sizeof(*gate), ack_ledger, sizeof(*ack_ledger)) ||
        ranges_overlap(session, sizeof(*session), slots, slots_bytes) ||
        ranges_overlap(session, sizeof(*session), dispatch,
                       sizeof(*dispatch)) ||
        ranges_overlap(session, sizeof(*session), ack_ledger,
                       sizeof(*ack_ledger)) ||
        ranges_overlap(slots, slots_bytes, dispatch, sizeof(*dispatch)) ||
        ranges_overlap(slots, slots_bytes, ack_ledger, sizeof(*ack_ledger)) ||
        ranges_overlap(dispatch, sizeof(*dispatch), ack_ledger,
                       sizeof(*ack_ledger)) ||
        ranges_overlap(token, sizeof(*token), gate, sizeof(*gate)) ||
        ranges_overlap(token, sizeof(*token), session, sizeof(*session)) ||
        ranges_overlap(token, sizeof(*token), slots, slots_bytes) ||
        ranges_overlap(token, sizeof(*token), dispatch, sizeof(*dispatch)) ||
        ranges_overlap(token, sizeof(*token), ack_ledger,
                       sizeof(*ack_ledger)) ||
        ranges_overlap(packet, packet_bytes, gate, sizeof(*gate)) ||
        ranges_overlap(packet, packet_bytes, session, sizeof(*session)) ||
        ranges_overlap(packet, packet_bytes, slots, slots_bytes) ||
        ranges_overlap(packet, packet_bytes, dispatch, sizeof(*dispatch)) ||
        ranges_overlap(packet, packet_bytes, ack_ledger,
                       sizeof(*ack_ledger))) {
        return WORR_NATIVE_CARRIER_MIXED_INVALID_ARGUMENT;
    }
    if (!token_matches_live(gate, dispatch, ack_ledger, token))
        return WORR_NATIVE_CARRIER_MIXED_STALE_TRANSACTION;
    if (!packet_matches_token(token, packet, packet_bytes))
        return WORR_NATIVE_CARRIER_MIXED_PACKET_MISMATCH;

    staged_gate = *gate;
    staged_session = *session;
    memset(staged_slots, 0, sizeof(staged_slots));
    memcpy(staged_slots, slots, slots_bytes);
    staged_dispatch = *dispatch;
    staged_ledger = *ack_ledger;
    data_confirmed = Worr_NativeCarrierSessionDispatchConfirmMixedPacketV1(
        &staged_gate, &staged_session, staged_slots, slot_capacity,
        &staged_dispatch, handoff_tick, packet, packet_bytes);
    if (data_confirmed != WORR_NATIVE_CARRIER_SESSION_OK &&
        data_confirmed != WORR_NATIVE_CARRIER_SESSION_DISPATCH_COMMITTED &&
        data_confirmed != WORR_NATIVE_CARRIER_SESSION_DISPATCH_RETIRED) {
        return session_result(data_confirmed);
    }
    if (ack_bound) {
        ack_confirmed = Worr_NativeCarrierAckCommitHandoffV1(
            &staged_ledger, &token->ack_token, handoff_tick, packet,
            packet_bytes);
        if (ack_confirmed != WORR_NATIVE_CARRIER_ACK_OK)
            return ack_result(ack_confirmed);
    }

    *gate = staged_gate;
    *session = staged_session;
    memcpy(slots, staged_slots, slots_bytes);
    *dispatch = staged_dispatch;
    *ack_ledger = staged_ledger;
    return session_result(data_confirmed);
}

worr_native_carrier_mixed_result_v1 Worr_NativeCarrierMixedRejectPacketV1(
    worr_native_carrier_tx_gate_v1 *gate,
    worr_native_carrier_dispatch_v1 *dispatch,
    worr_native_carrier_ack_ledger_v1 *ack_ledger,
    const worr_native_carrier_mixed_token_v1 *token, const void *packet,
    size_t packet_bytes)
{
    worr_native_carrier_tx_gate_v1 staged_gate;
    worr_native_carrier_dispatch_v1 staged_dispatch;
    worr_native_carrier_ack_ledger_v1 staged_ledger;
    worr_native_carrier_session_result_v1 data_rejected;
    worr_native_carrier_ack_result_v1 ack_rejected;
    const bool ack_bound =
        token != NULL &&
        (token->state_flags & WORR_NATIVE_CARRIER_MIXED_TOKEN_ACK_BOUND) != 0;

    if (gate == NULL || dispatch == NULL || ack_ledger == NULL ||
        token == NULL || packet == NULL || packet_bytes == 0 ||
        ranges_overlap(gate, sizeof(*gate), dispatch, sizeof(*dispatch)) ||
        ranges_overlap(gate, sizeof(*gate), ack_ledger, sizeof(*ack_ledger)) ||
        ranges_overlap(dispatch, sizeof(*dispatch), ack_ledger,
                       sizeof(*ack_ledger)) ||
        ranges_overlap(token, sizeof(*token), gate, sizeof(*gate)) ||
        ranges_overlap(token, sizeof(*token), dispatch, sizeof(*dispatch)) ||
        ranges_overlap(token, sizeof(*token), ack_ledger,
                       sizeof(*ack_ledger)) ||
        ranges_overlap(packet, packet_bytes, gate, sizeof(*gate)) ||
        ranges_overlap(packet, packet_bytes, dispatch, sizeof(*dispatch)) ||
        ranges_overlap(packet, packet_bytes, ack_ledger,
                       sizeof(*ack_ledger))) {
        return WORR_NATIVE_CARRIER_MIXED_INVALID_ARGUMENT;
    }
    if (!token_matches_live(gate, dispatch, ack_ledger, token))
        return WORR_NATIVE_CARRIER_MIXED_STALE_TRANSACTION;
    if (!packet_matches_token(token, packet, packet_bytes))
        return WORR_NATIVE_CARRIER_MIXED_PACKET_MISMATCH;

    staged_gate = *gate;
    staged_dispatch = *dispatch;
    staged_ledger = *ack_ledger;
    data_rejected = Worr_NativeCarrierSessionDispatchRejectPacketV1(
        &staged_gate, &staged_dispatch);
    if (data_rejected != WORR_NATIVE_CARRIER_SESSION_OK)
        return session_result(data_rejected);
    if (ack_bound) {
        ack_rejected = Worr_NativeCarrierAckRejectHandoffV1(&staged_ledger,
                                                            &token->ack_token);
        if (ack_rejected != WORR_NATIVE_CARRIER_ACK_OK)
            return ack_result(ack_rejected);
    }
    *gate = staged_gate;
    *dispatch = staged_dispatch;
    *ack_ledger = staged_ledger;
    return WORR_NATIVE_CARRIER_MIXED_OK;
}

worr_native_carrier_mixed_result_v1 Worr_NativeCarrierMixedAbortPacketV1(
    worr_native_carrier_tx_gate_v1 *gate,
    worr_native_carrier_dispatch_v1 *dispatch,
    worr_native_carrier_ack_ledger_v1 *ack_ledger,
    const worr_native_carrier_mixed_token_v1 *token, const void *packet,
    size_t packet_bytes)
{
    worr_native_carrier_tx_gate_v1 staged_gate;
    worr_native_carrier_dispatch_v1 staged_dispatch;
    worr_native_carrier_ack_ledger_v1 staged_ledger;
    worr_native_carrier_session_result_v1 data_result;
    worr_native_carrier_ack_result_v1 ack_rejected;
    const bool ack_bound =
        token != NULL &&
        (token->state_flags & WORR_NATIVE_CARRIER_MIXED_TOKEN_ACK_BOUND) != 0;

    if (gate == NULL || dispatch == NULL || ack_ledger == NULL ||
        token == NULL || packet == NULL || packet_bytes == 0 ||
        ranges_overlap(gate, sizeof(*gate), dispatch, sizeof(*dispatch)) ||
        ranges_overlap(gate, sizeof(*gate), ack_ledger, sizeof(*ack_ledger)) ||
        ranges_overlap(dispatch, sizeof(*dispatch), ack_ledger,
                       sizeof(*ack_ledger)) ||
        ranges_overlap(token, sizeof(*token), gate, sizeof(*gate)) ||
        ranges_overlap(token, sizeof(*token), dispatch, sizeof(*dispatch)) ||
        ranges_overlap(token, sizeof(*token), ack_ledger,
                       sizeof(*ack_ledger)) ||
        ranges_overlap(packet, packet_bytes, gate, sizeof(*gate)) ||
        ranges_overlap(packet, packet_bytes, dispatch, sizeof(*dispatch)) ||
        ranges_overlap(packet, packet_bytes, ack_ledger,
                       sizeof(*ack_ledger))) {
        return WORR_NATIVE_CARRIER_MIXED_INVALID_ARGUMENT;
    }
    if (!token_matches_live(gate, dispatch, ack_ledger, token))
        return WORR_NATIVE_CARRIER_MIXED_STALE_TRANSACTION;
    if (!packet_matches_token(token, packet, packet_bytes))
        return WORR_NATIVE_CARRIER_MIXED_PACKET_MISMATCH;

    staged_gate = *gate;
    staged_dispatch = *dispatch;
    staged_ledger = *ack_ledger;
    data_result = Worr_NativeCarrierSessionDispatchRejectPacketV1(
        &staged_gate, &staged_dispatch);
    if (data_result != WORR_NATIVE_CARRIER_SESSION_OK)
        return session_result(data_result);
    if (ack_bound) {
        ack_rejected = Worr_NativeCarrierAckRejectHandoffV1(&staged_ledger,
                                                            &token->ack_token);
        if (ack_rejected != WORR_NATIVE_CARRIER_ACK_OK)
            return ack_result(ack_rejected);
    }
    data_result = Worr_NativeCarrierSessionDispatchAbortV1(&staged_gate,
                                                           &staged_dispatch);
    if (data_result != WORR_NATIVE_CARRIER_SESSION_OK)
        return session_result(data_result);
    *gate = staged_gate;
    *dispatch = staged_dispatch;
    *ack_ledger = staged_ledger;
    return WORR_NATIVE_CARRIER_MIXED_OK;
}

worr_native_carrier_mixed_result_v1
Worr_NativeCarrierMixedAbortV1(worr_native_carrier_tx_gate_v1 *gate,
                               worr_native_carrier_dispatch_v1 *dispatch,
                               worr_native_carrier_ack_ledger_v1 *ack_ledger)
{
    worr_native_carrier_tx_gate_v1 staged_gate;
    worr_native_carrier_dispatch_v1 staged_dispatch;
    worr_native_carrier_session_result_v1 aborted;

    if (gate == NULL || dispatch == NULL || ack_ledger == NULL ||
        ranges_overlap(gate, sizeof(*gate), dispatch, sizeof(*dispatch)) ||
        ranges_overlap(gate, sizeof(*gate), ack_ledger, sizeof(*ack_ledger)) ||
        ranges_overlap(dispatch, sizeof(*dispatch), ack_ledger,
                       sizeof(*ack_ledger))) {
        return WORR_NATIVE_CARRIER_MIXED_INVALID_ARGUMENT;
    }
    if (!owner_epoch_match(gate, ack_ledger) ||
        (ack_ledger->state_flags &
         WORR_NATIVE_CARRIER_ACK_LEDGER_EMIT_ACTIVE) != 0) {
        return WORR_NATIVE_CARRIER_MIXED_INVALID_STATE;
    }
    staged_gate = *gate;
    staged_dispatch = *dispatch;
    aborted = Worr_NativeCarrierSessionDispatchAbortV1(&staged_gate,
                                                       &staged_dispatch);
    if (aborted != WORR_NATIVE_CARRIER_SESSION_OK)
        return session_result(aborted);
    *gate = staged_gate;
    *dispatch = staged_dispatch;
    return WORR_NATIVE_CARRIER_MIXED_OK;
}
