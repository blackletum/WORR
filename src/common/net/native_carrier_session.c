/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/native_carrier_session.h"

#include <limits.h>
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

static worr_native_carrier_session_result_v1 carrier_result(
    worr_native_carrier_result_v1 result)
{
    switch (result) {
    case WORR_NATIVE_CARRIER_OK:
        return WORR_NATIVE_CARRIER_SESSION_OK;
    case WORR_NATIVE_CARRIER_NO_CARRIER:
        return WORR_NATIVE_CARRIER_SESSION_NO_CARRIER;
    case WORR_NATIVE_CARRIER_INVALID_ARGUMENT:
        return WORR_NATIVE_CARRIER_SESSION_INVALID_ARGUMENT;
    case WORR_NATIVE_CARRIER_OUTPUT_TOO_SMALL:
        return WORR_NATIVE_CARRIER_SESSION_OUTPUT_TOO_SMALL;
    case WORR_NATIVE_CARRIER_LIMIT:
        return WORR_NATIVE_CARRIER_SESSION_LIMIT;
    case WORR_NATIVE_CARRIER_MALFORMED:
        return WORR_NATIVE_CARRIER_SESSION_MALFORMED;
    case WORR_NATIVE_CARRIER_UNSUPPORTED:
        return WORR_NATIVE_CARRIER_SESSION_UNSUPPORTED;
    case WORR_NATIVE_CARRIER_CORRUPT:
        return WORR_NATIVE_CARRIER_SESSION_CORRUPT;
    }
    return WORR_NATIVE_CARRIER_SESSION_INVALID_STATE;
}

bool Worr_NativeCarrierSessionDataBudgetV1(
    uint16_t application_packet_budget,
    uint16_t legacy_bytes_reserve,
    uint16_t ack_range_reserve,
    uint16_t *max_wne_datagram_bytes_out)
{
    uint32_t overhead;
    uint32_t available;

    if (max_wne_datagram_bytes_out == NULL ||
        application_packet_budget == 0 ||
        application_packet_budget > WORR_NATIVE_CARRIER_MAX_PACKET_BYTES ||
        ack_range_reserve >= WORR_NATIVE_CARRIER_MAX_ENTRIES) {
        return false;
    }
    overhead = (uint32_t)legacy_bytes_reserve +
               WORR_NATIVE_CARRIER_WIRE_FOOTER_BYTES +
               WORR_NATIVE_CARRIER_WIRE_ENTRY_HEADER_BYTES +
               (uint32_t)ack_range_reserve *
                   WORR_NATIVE_CARRIER_WIRE_ACK_ENTRY_BYTES;
    if (overhead >= application_packet_budget)
        return false;
    available = (uint32_t)application_packet_budget - overhead;
    if (available <= WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES ||
        available > WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES) {
        return false;
    }
    *max_wne_datagram_bytes_out = (uint16_t)available;
    return true;
}

static void counter_increment(uint64_t *counter)
{
    if (*counter != UINT64_MAX)
        ++*counter;
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

bool Worr_NativeCarrierTxGateValidateV1(
    const worr_native_carrier_tx_gate_v1 *gate)
{
    const uint16_t known =
        WORR_NATIVE_CARRIER_TX_GATE_INITIALIZED |
        WORR_NATIVE_CARRIER_TX_GATE_ACTIVE |
        WORR_NATIVE_CARRIER_TX_GATE_PACKET_PENDING |
        WORR_NATIVE_CARRIER_TX_GATE_TOKEN_EXHAUSTED |
        WORR_NATIVE_CARRIER_TX_GATE_PAYLOAD_BOUND;
    const bool active = gate != NULL &&
                        (gate->state_flags &
                         WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) != 0;

    if (gate == NULL || gate->struct_size != sizeof(*gate) ||
        gate->schema_version != WORR_NATIVE_CARRIER_SESSION_ABI_VERSION ||
        (gate->state_flags & ~known) != 0 ||
        (gate->state_flags &
         WORR_NATIVE_CARRIER_TX_GATE_INITIALIZED) == 0 ||
        gate->transport_epoch == 0 || gate->connection_owner_id == 0 ||
        gate->next_token_id == 0 ||
        ((gate->state_flags &
          WORR_NATIVE_CARRIER_TX_GATE_PACKET_PENDING) != 0 && !active) ||
        ((gate->state_flags &
          WORR_NATIVE_CARRIER_TX_GATE_PAYLOAD_BOUND) != 0 && !active) ||
        ((gate->state_flags &
          WORR_NATIVE_CARRIER_TX_GATE_TOKEN_EXHAUSTED) != 0 &&
         gate->next_token_id != UINT64_MAX)) {
        return false;
    }
    if (!active) {
        return gate->active_message_sequence == 0 &&
               gate->active_token_id == 0 &&
               gate->confirmed_fragments == 0 &&
               gate->active_fragment_count == 0 &&
               gate->active_payload_crc32 == 0;
    }
    if (gate->active_message_sequence == 0 ||
        gate->active_token_id == 0 ||
        gate->active_fragment_count == 0 ||
        gate->active_fragment_count >
            WORR_NATIVE_ENVELOPE_MAX_FRAGMENTS ||
        gate->confirmed_fragments >= gate->active_fragment_count) {
        return false;
    }
    return (gate->state_flags &
            WORR_NATIVE_CARRIER_TX_GATE_PAYLOAD_BOUND) != 0 ||
           gate->active_payload_crc32 == 0;
}

bool Worr_NativeCarrierTxGateInitV1(
    worr_native_carrier_tx_gate_v1 *gate_out,
    const worr_native_session_binding_v1 *binding)
{
    worr_native_carrier_tx_gate_v1 initialized;

    if (gate_out == NULL || binding == NULL ||
        ranges_overlap(gate_out, sizeof(*gate_out), binding,
                       sizeof(*binding)) ||
        !Worr_NativeSessionBindingValidateV1(binding)) {
        return false;
    }
    memset(&initialized, 0, sizeof(initialized));
    initialized.struct_size = sizeof(initialized);
    initialized.schema_version =
        WORR_NATIVE_CARRIER_SESSION_ABI_VERSION;
    initialized.state_flags =
        WORR_NATIVE_CARRIER_TX_GATE_INITIALIZED;
    initialized.transport_epoch = binding->transport_epoch;
    initialized.connection_owner_id = binding->connection_owner_id;
    initialized.next_token_id = 1;
    *gate_out = initialized;
    return true;
}

bool Worr_NativeCarrierTxGateAdvanceEpochV1(
    worr_native_carrier_tx_gate_v1 *gate,
    const worr_native_session_binding_v1 *binding)
{
    if (!Worr_NativeCarrierTxGateValidateV1(gate) || binding == NULL ||
        ranges_overlap(gate, sizeof(*gate), binding, sizeof(*binding)) ||
        !Worr_NativeSessionBindingValidateV1(binding) ||
        binding->connection_owner_id != gate->connection_owner_id ||
        binding->transport_epoch <= gate->transport_epoch) {
        return false;
    }
    return Worr_NativeCarrierTxGateInitV1(gate, binding);
}

static worr_native_carrier_session_result_v1 tx_result(
    worr_native_tx_result_v1 result)
{
    switch (result) {
    case WORR_NATIVE_TX_SELECTED:
        return WORR_NATIVE_CARRIER_SESSION_OK;
    case WORR_NATIVE_TX_NOT_DUE:
        return WORR_NATIVE_CARRIER_SESSION_NOT_DUE;
    case WORR_NATIVE_TX_CLOCK_REGRESSION:
        return WORR_NATIVE_CARRIER_SESSION_CLOCK_REGRESSION;
    case WORR_NATIVE_TX_INVALID_ARGUMENT:
        return WORR_NATIVE_CARRIER_SESSION_INVALID_ARGUMENT;
    case WORR_NATIVE_TX_INVALID_STATE:
        return WORR_NATIVE_CARRIER_SESSION_STALE_DISPATCH;
    default:
        return WORR_NATIVE_CARRIER_SESSION_NATIVE_REJECTED;
    }
}

static bool record_equal(worr_native_record_ref_v1 left,
                         worr_native_record_ref_v1 right)
{
    return left.record_class == right.record_class &&
           left.reserved0 == right.reserved0 &&
           left.record_schema_version == right.record_schema_version &&
           left.object_epoch == right.object_epoch &&
           left.object_sequence == right.object_sequence;
}

static bool fragmenter_static_valid(
    const worr_native_carrier_dispatch_v1 *dispatch,
    const worr_native_envelope_fragmenter_v1 *fragmenter)
{
    const worr_native_tx_slot_v1 *selected =
        &dispatch->send_ticket.pre_send_slot;
    const uint16_t known =
        WORR_NATIVE_FRAGMENTER_INITIALIZED |
        WORR_NATIVE_FRAGMENTER_EXHAUSTED;

    return fragmenter->struct_size == sizeof(*fragmenter) &&
           fragmenter->schema_version ==
               WORR_NATIVE_ENVELOPE_ABI_VERSION &&
           (fragmenter->state_flags & ~known) == 0 &&
           (fragmenter->state_flags &
            WORR_NATIVE_FRAGMENTER_INITIALIZED) != 0 &&
           record_equal(fragmenter->record, selected->record) &&
           fragmenter->transport_epoch == dispatch->transport_epoch &&
           fragmenter->message_sequence == selected->message_sequence &&
           fragmenter->total_payload_bytes == selected->payload_bytes &&
           fragmenter->payload_crc32 == dispatch->payload_crc32 &&
           fragmenter->max_datagram_bytes ==
               selected->fragment_stride +
                   WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES &&
           fragmenter->fragment_stride == selected->fragment_stride &&
           fragmenter->fragment_count == selected->fragment_count &&
           fragmenter->next_fragment <= fragmenter->fragment_count &&
           fragmenter->priority == selected->priority &&
           fragmenter->reserved0[0] == 0 &&
           fragmenter->reserved0[1] == 0 &&
           fragmenter->reserved0[2] == 0 &&
           ((fragmenter->state_flags &
             WORR_NATIVE_FRAGMENTER_EXHAUSTED) != 0) ==
               (fragmenter->next_fragment == fragmenter->fragment_count);
}

static bool pending_fragmenter_is_next(
    const worr_native_carrier_dispatch_v1 *dispatch)
{
    const worr_native_envelope_fragmenter_v1 *current =
        &dispatch->fragmenter;
    const worr_native_envelope_fragmenter_v1 *pending =
        &dispatch->pending_fragmenter;

    return fragmenter_static_valid(dispatch, current) &&
           fragmenter_static_valid(dispatch, pending) &&
           current->next_fragment < current->fragment_count &&
           pending->next_fragment == current->next_fragment + 1u &&
           pending->state_flags ==
               (uint16_t)(WORR_NATIVE_FRAGMENTER_INITIALIZED |
                          (pending->next_fragment == pending->fragment_count
                               ? WORR_NATIVE_FRAGMENTER_EXHAUSTED
                               : 0u));
}

static bool dispatch_header_valid(
    const worr_native_carrier_dispatch_v1 *dispatch)
{
    const uint16_t known =
        WORR_NATIVE_CARRIER_DISPATCH_ACTIVE |
        WORR_NATIVE_CARRIER_DISPATCH_PAYLOAD_BOUND |
        WORR_NATIVE_CARRIER_DISPATCH_PACKET_PENDING |
        WORR_NATIVE_CARRIER_DISPATCH_COMMITTED |
        WORR_NATIVE_CARRIER_DISPATCH_ABORTED |
        WORR_NATIVE_CARRIER_DISPATCH_RETIRED;
    const bool active = dispatch != NULL &&
                        (dispatch->state_flags &
                         WORR_NATIVE_CARRIER_DISPATCH_ACTIVE) != 0;

    if (dispatch == NULL || dispatch->struct_size != sizeof(*dispatch) ||
        dispatch->schema_version !=
            WORR_NATIVE_CARRIER_SESSION_ABI_VERSION ||
        (dispatch->state_flags & ~known) != 0 ||
        dispatch->transport_epoch == 0 || dispatch->token_id == 0 ||
        dispatch->reserved0 != 0 ||
        dispatch->send_ticket.transport_epoch !=
            dispatch->transport_epoch ||
        dispatch->send_ticket.connection_owner_id == 0) {
        return false;
    }
    if (!active) {
        return dispatch->state_flags ==
                   WORR_NATIVE_CARRIER_DISPATCH_COMMITTED ||
               dispatch->state_flags ==
                   WORR_NATIVE_CARRIER_DISPATCH_ABORTED ||
               dispatch->state_flags ==
                   WORR_NATIVE_CARRIER_DISPATCH_RETIRED;
    }
    if ((dispatch->state_flags &
         (WORR_NATIVE_CARRIER_DISPATCH_COMMITTED |
          WORR_NATIVE_CARRIER_DISPATCH_ABORTED |
          WORR_NATIVE_CARRIER_DISPATCH_RETIRED)) != 0 ||
        !Worr_NativeCarrierSessionDataBudgetV1(
            dispatch->application_packet_budget,
            dispatch->legacy_bytes_reserve, 0,
            &(uint16_t){ 0 })) {
        return false;
    }
    if ((dispatch->state_flags &
         WORR_NATIVE_CARRIER_DISPATCH_PAYLOAD_BOUND) == 0) {
        return dispatch->payload_handle == 0 &&
               dispatch->payload_bytes == 0 &&
               dispatch->payload_crc32 == 0 &&
               dispatch->submitted_fragments == 0 &&
               (dispatch->state_flags &
                WORR_NATIVE_CARRIER_DISPATCH_PACKET_PENDING) == 0;
    }
    if (dispatch->payload_handle !=
            dispatch->send_ticket.pre_send_slot.payload_handle ||
        dispatch->payload_bytes !=
            dispatch->send_ticket.pre_send_slot.payload_bytes ||
        !fragmenter_static_valid(dispatch, &dispatch->fragmenter) ||
        dispatch->submitted_fragments !=
            dispatch->fragmenter.next_fragment) {
        return false;
    }
    if ((dispatch->state_flags &
         WORR_NATIVE_CARRIER_DISPATCH_PACKET_PENDING) != 0) {
        return dispatch->pending_packet_bytes != 0 &&
               dispatch->pending_fragment_index ==
                   dispatch->fragmenter.next_fragment &&
               pending_fragmenter_is_next(dispatch);
    }
    return dispatch->pending_packet_crc32 == 0 &&
           dispatch->pending_fragment_index == 0 &&
           dispatch->pending_packet_bytes == 0 &&
           memcmp(&dispatch->pending_fragmenter,
                  &(worr_native_envelope_fragmenter_v1){ 0 },
                  sizeof(dispatch->pending_fragmenter)) == 0;
}

static bool gate_matches_dispatch(
    const worr_native_carrier_tx_gate_v1 *gate,
    const worr_native_carrier_dispatch_v1 *dispatch)
{
    return Worr_NativeCarrierTxGateValidateV1(gate) &&
           dispatch_header_valid(dispatch) &&
           (gate->state_flags & WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) != 0 &&
           gate->transport_epoch == dispatch->transport_epoch &&
           gate->connection_owner_id ==
               dispatch->send_ticket.connection_owner_id &&
           gate->active_token_id == dispatch->token_id &&
           gate->active_message_sequence ==
               dispatch->send_ticket.pre_send_slot.message_sequence &&
           gate->active_fragment_count ==
               dispatch->send_ticket.pre_send_slot.fragment_count &&
           gate->confirmed_fragments == dispatch->submitted_fragments &&
           ((gate->state_flags &
             WORR_NATIVE_CARRIER_TX_GATE_PAYLOAD_BOUND) != 0) ==
               ((dispatch->state_flags &
                 WORR_NATIVE_CARRIER_DISPATCH_PAYLOAD_BOUND) != 0) &&
           ((gate->state_flags &
             WORR_NATIVE_CARRIER_TX_GATE_PAYLOAD_BOUND) == 0 ||
            gate->active_payload_crc32 == dispatch->payload_crc32) &&
           ((gate->state_flags &
             WORR_NATIVE_CARRIER_TX_GATE_PACKET_PENDING) != 0) ==
               ((dispatch->state_flags &
                 WORR_NATIVE_CARRIER_DISPATCH_PACKET_PENDING) != 0);
}

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
    worr_native_carrier_dispatch_v1 *dispatch_out)
{
    const size_t slots_bytes = (size_t)slot_capacity * sizeof(*slots);
    worr_native_carrier_tx_gate_v1 staged_gate;
    worr_native_carrier_dispatch_v1 initialized;
    worr_native_tx_result_v1 prepared;
    uint16_t maximum_datagram;
    uint16_t selected_datagram;
    uint64_t token;

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
        return WORR_NATIVE_CARRIER_SESSION_INVALID_ARGUMENT;
    }
    if (!Worr_NativeCarrierTxGateValidateV1(gate) ||
        !Worr_NativeTxSessionValidateV1(session, slots, slot_capacity)) {
        return WORR_NATIVE_CARRIER_SESSION_INVALID_STATE;
    }
    if (gate->transport_epoch != session->transport_epoch)
        return WORR_NATIVE_CARRIER_SESSION_WRONG_EPOCH;
    if (gate->connection_owner_id != session->connection_owner_id)
        return WORR_NATIVE_CARRIER_SESSION_INVALID_STATE;
    if ((gate->state_flags & WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) != 0)
        return WORR_NATIVE_CARRIER_SESSION_INVALID_STATE;
    if ((gate->state_flags &
         WORR_NATIVE_CARRIER_TX_GATE_TOKEN_EXHAUSTED) != 0) {
        return WORR_NATIVE_CARRIER_SESSION_TOKEN_EXHAUSTED;
    }
    if (selection_tick < gate->last_handoff_tick)
        return WORR_NATIVE_CARRIER_SESSION_CLOCK_REGRESSION;
    if (!Worr_NativeCarrierSessionDataBudgetV1(
            application_packet_budget, legacy_bytes_reserve, 0,
            &maximum_datagram)) {
        return WORR_NATIVE_CARRIER_SESSION_LIMIT;
    }

    memset(&initialized, 0, sizeof(initialized));
    initialized.struct_size = sizeof(initialized);
    initialized.schema_version =
        WORR_NATIVE_CARRIER_SESSION_ABI_VERSION;
    prepared = Worr_NativeTxSessionPrepareDueV1(
        session, slots, slot_capacity, selection_tick,
        resend_interval_ticks, &initialized.send_ticket);
    if (prepared != WORR_NATIVE_TX_SELECTED)
        return tx_result(prepared);
    if (initialized.send_ticket.pre_send_slot.fragment_stride >
        UINT16_MAX - WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES) {
        return WORR_NATIVE_CARRIER_SESSION_INVALID_STATE;
    }
    selected_datagram = (uint16_t)(
        initialized.send_ticket.pre_send_slot.fragment_stride +
        WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES);
    if (selected_datagram > maximum_datagram)
        return WORR_NATIVE_CARRIER_SESSION_LIMIT;

    token = gate->next_token_id;
    initialized.state_flags = WORR_NATIVE_CARRIER_DISPATCH_ACTIVE;
    initialized.transport_epoch = session->transport_epoch;
    initialized.token_id = token;
    initialized.application_packet_budget = application_packet_budget;
    initialized.legacy_bytes_reserve = legacy_bytes_reserve;

    staged_gate = *gate;
    staged_gate.state_flags |= WORR_NATIVE_CARRIER_TX_GATE_ACTIVE;
    staged_gate.active_token_id = token;
    staged_gate.active_message_sequence =
        initialized.send_ticket.pre_send_slot.message_sequence;
    staged_gate.active_fragment_count =
        initialized.send_ticket.pre_send_slot.fragment_count;
    if (token == UINT64_MAX) {
        staged_gate.state_flags |=
            WORR_NATIVE_CARRIER_TX_GATE_TOKEN_EXHAUSTED;
    } else {
        staged_gate.next_token_id = token + 1u;
    }
    *gate = staged_gate;
    *dispatch_out = initialized;
    return WORR_NATIVE_CARRIER_SESSION_OK;
}

static bool payload_matches_dispatch(
    const worr_native_carrier_dispatch_v1 *dispatch,
    uint32_t payload_handle,
    const void *payload,
    uint32_t payload_bytes)
{
    const worr_native_tx_slot_v1 *selected =
        &dispatch->send_ticket.pre_send_slot;
    worr_native_envelope_fragmenter_v1 verification;
    const uint16_t max_datagram = (uint16_t)(
        selected->fragment_stride +
        WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES);

    return payload_handle == dispatch->payload_handle &&
           payload_handle == selected->payload_handle &&
           payload != NULL && payload_bytes == dispatch->payload_bytes &&
           payload_bytes == selected->payload_bytes &&
           Worr_NativeEnvelopeFragmenterInitV1(
               &verification, dispatch->transport_epoch,
               selected->message_sequence, selected->record,
               selected->priority, payload, payload_bytes, max_datagram) &&
           verification.payload_crc32 == dispatch->fragmenter.payload_crc32 &&
           verification.fragment_stride ==
               dispatch->fragmenter.fragment_stride &&
           verification.fragment_count ==
               dispatch->fragmenter.fragment_count;
}

worr_native_carrier_session_result_v1
Worr_NativeCarrierSessionDispatchBindPayloadV1(
    worr_native_carrier_tx_gate_v1 *gate,
    worr_native_carrier_dispatch_v1 *dispatch,
    uint32_t payload_handle,
    const void *payload,
    uint32_t payload_bytes)
{
    worr_native_carrier_tx_gate_v1 staged_gate;
    worr_native_carrier_dispatch_v1 bound;
    const worr_native_tx_slot_v1 *selected;
    uint16_t max_datagram;

    if (gate == NULL || dispatch == NULL || payload_handle == 0 ||
        payload == NULL || payload_bytes == 0 ||
        ranges_overlap(dispatch, sizeof(*dispatch), gate, sizeof(*gate)) ||
        ranges_overlap(payload, payload_bytes, gate, sizeof(*gate)) ||
        ranges_overlap(payload, payload_bytes, dispatch,
                       sizeof(*dispatch))) {
        return WORR_NATIVE_CARRIER_SESSION_INVALID_ARGUMENT;
    }
    if (!gate_matches_dispatch(gate, dispatch) ||
        (dispatch->state_flags &
         WORR_NATIVE_CARRIER_DISPATCH_PAYLOAD_BOUND) != 0) {
        return WORR_NATIVE_CARRIER_SESSION_INVALID_STATE;
    }
    selected = &dispatch->send_ticket.pre_send_slot;
    if (payload_handle != selected->payload_handle ||
        payload_bytes != selected->payload_bytes ||
        selected->fragment_stride >
            UINT16_MAX - WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES) {
        return WORR_NATIVE_CARRIER_SESSION_INVALID_ARGUMENT;
    }
    max_datagram = (uint16_t)(
        selected->fragment_stride +
        WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES);
    bound = *dispatch;
    if (!Worr_NativeEnvelopeFragmenterInitV1(
            &bound.fragmenter, dispatch->transport_epoch,
            selected->message_sequence, selected->record,
            selected->priority, payload, payload_bytes, max_datagram) ||
        bound.fragmenter.fragment_stride != selected->fragment_stride ||
        bound.fragmenter.fragment_count != selected->fragment_count) {
        return WORR_NATIVE_CARRIER_SESSION_INVALID_STATE;
    }
    bound.payload_handle = payload_handle;
    bound.payload_bytes = payload_bytes;
    bound.payload_crc32 = bound.fragmenter.payload_crc32;
    bound.state_flags |= WORR_NATIVE_CARRIER_DISPATCH_PAYLOAD_BOUND;
    staged_gate = *gate;
    staged_gate.state_flags |=
        WORR_NATIVE_CARRIER_TX_GATE_PAYLOAD_BOUND;
    staged_gate.active_payload_crc32 = bound.payload_crc32;
    *gate = staged_gate;
    *dispatch = bound;
    return WORR_NATIVE_CARRIER_SESSION_OK;
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
    size_t *packet_bytes_out)
{
    uint8_t datagram[WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES];
    uint8_t packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    worr_native_carrier_entry_v1 entry;
    worr_native_carrier_tx_gate_v1 staged_gate;
    worr_native_carrier_dispatch_v1 staged_dispatch;
    worr_native_envelope_fragmenter_v1 advanced;
    worr_native_envelope_emit_result_v1 emitted;
    worr_native_carrier_result_v1 encoded;
    const size_t slots_bytes = (size_t)slot_capacity * sizeof(*slots);
    size_t datagram_bytes = 0;
    size_t encoded_bytes = 0;

    if (gate == NULL || session == NULL || slots == NULL ||
        dispatch == NULL || payload_handle == 0 || payload == NULL ||
        payload_bytes == 0 || packet_out == NULL ||
        packet_bytes_out == NULL ||
        (legacy_bytes != 0 && legacy_packet == NULL) ||
        ranges_overlap(gate, sizeof(*gate), session, sizeof(*session)) ||
        ranges_overlap(gate, sizeof(*gate), slots, slots_bytes) ||
        ranges_overlap(dispatch, sizeof(*dispatch), gate, sizeof(*gate)) ||
        ranges_overlap(dispatch, sizeof(*dispatch), session,
                       sizeof(*session)) ||
        ranges_overlap(dispatch, sizeof(*dispatch), slots, slots_bytes) ||
        ranges_overlap(payload, payload_bytes, gate, sizeof(*gate)) ||
        ranges_overlap(payload, payload_bytes, session, sizeof(*session)) ||
        ranges_overlap(payload, payload_bytes, slots, slots_bytes) ||
        ranges_overlap(payload, payload_bytes, dispatch,
                       sizeof(*dispatch)) ||
        ranges_overlap(legacy_packet, legacy_bytes, gate, sizeof(*gate)) ||
        ranges_overlap(legacy_packet, legacy_bytes, session,
                       sizeof(*session)) ||
        ranges_overlap(legacy_packet, legacy_bytes, slots, slots_bytes) ||
        ranges_overlap(legacy_packet, legacy_bytes, dispatch,
                       sizeof(*dispatch)) ||
        ranges_overlap(packet_bytes_out, sizeof(*packet_bytes_out), gate,
                       sizeof(*gate)) ||
        ranges_overlap(packet_bytes_out, sizeof(*packet_bytes_out), session,
                       sizeof(*session)) ||
        ranges_overlap(packet_bytes_out, sizeof(*packet_bytes_out), slots,
                       slots_bytes) ||
        ranges_overlap(packet_bytes_out, sizeof(*packet_bytes_out), dispatch,
                       sizeof(*dispatch)) ||
        ranges_overlap(packet_bytes_out, sizeof(*packet_bytes_out), payload,
                       payload_bytes) ||
        ranges_overlap(packet_bytes_out, sizeof(*packet_bytes_out),
                       legacy_packet, legacy_bytes)) {
        return WORR_NATIVE_CARRIER_SESSION_INVALID_ARGUMENT;
    }
    if (!gate_matches_dispatch(gate, dispatch))
        return WORR_NATIVE_CARRIER_SESSION_INVALID_STATE;
    if ((gate->state_flags &
         WORR_NATIVE_CARRIER_TX_GATE_PACKET_PENDING) != 0) {
        return WORR_NATIVE_CARRIER_SESSION_PACKET_PENDING;
    }
    if ((dispatch->state_flags &
         WORR_NATIVE_CARRIER_DISPATCH_PAYLOAD_BOUND) == 0 ||
        (dispatch->fragmenter.state_flags &
         WORR_NATIVE_FRAGMENTER_EXHAUSTED) != 0) {
        return WORR_NATIVE_CARRIER_SESSION_INVALID_STATE;
    }
    if (!Worr_NativeTxSessionPreparedValidateV1(
            session, slots, slot_capacity, &dispatch->send_ticket)) {
        return WORR_NATIVE_CARRIER_SESSION_STALE_DISPATCH;
    }
    if (!payload_matches_dispatch(dispatch, payload_handle, payload,
                                  payload_bytes)) {
        return WORR_NATIVE_CARRIER_SESSION_STALE_DISPATCH;
    }
    if (legacy_bytes > dispatch->legacy_bytes_reserve) {
        return WORR_NATIVE_CARRIER_SESSION_INVALID_ARGUMENT;
    }

    advanced = dispatch->fragmenter;
    emitted = Worr_NativeEnvelopeFragmentNextV1(
        &advanced, payload, payload_bytes, datagram,
        sizeof(datagram), &datagram_bytes);
    if (emitted == WORR_NATIVE_ENVELOPE_EMIT_EXHAUSTED ||
        emitted == WORR_NATIVE_ENVELOPE_EMIT_INVALID_STATE) {
        return WORR_NATIVE_CARRIER_SESSION_INVALID_STATE;
    }
    if (emitted == WORR_NATIVE_ENVELOPE_EMIT_OUTPUT_TOO_SMALL)
        return WORR_NATIVE_CARRIER_SESSION_OUTPUT_TOO_SMALL;
    if (emitted != WORR_NATIVE_ENVELOPE_EMIT_OK)
        return WORR_NATIVE_CARRIER_SESSION_INVALID_ARGUMENT;

    entry = data_entry((uint32_t)datagram_bytes);
    encoded = Worr_NativeCarrierEncodeV1(
        advanced.transport_epoch, legacy_packet, legacy_bytes,
        datagram, datagram_bytes, &entry, 1, packet,
        dispatch->application_packet_budget, &encoded_bytes);
    if (encoded != WORR_NATIVE_CARRIER_OK)
        return carrier_result(encoded);
    if (encoded_bytes > packet_capacity)
        return WORR_NATIVE_CARRIER_SESSION_OUTPUT_TOO_SMALL;
    if (ranges_overlap(packet_out, encoded_bytes, gate, sizeof(*gate)) ||
        ranges_overlap(packet_out, encoded_bytes, session,
                       sizeof(*session)) ||
        ranges_overlap(packet_out, encoded_bytes, slots, slots_bytes) ||
        ranges_overlap(packet_out, encoded_bytes, dispatch,
                       sizeof(*dispatch)) ||
        ranges_overlap(packet_out, encoded_bytes, payload, payload_bytes) ||
        ranges_overlap(packet_out, encoded_bytes, packet_bytes_out,
                       sizeof(*packet_bytes_out))) {
        return WORR_NATIVE_CARRIER_SESSION_INVALID_ARGUMENT;
    }

    staged_gate = *gate;
    staged_gate.state_flags |=
        WORR_NATIVE_CARRIER_TX_GATE_PACKET_PENDING;
    staged_dispatch = *dispatch;
    staged_dispatch.state_flags |=
        WORR_NATIVE_CARRIER_DISPATCH_PACKET_PENDING;
    staged_dispatch.pending_fragment_index =
        dispatch->fragmenter.next_fragment;
    staged_dispatch.pending_packet_bytes = (uint16_t)encoded_bytes;
    staged_dispatch.pending_packet_crc32 = crc32_bytes(packet, encoded_bytes);
    staged_dispatch.pending_fragmenter = advanced;
    memmove(packet_out, packet, encoded_bytes);
    *gate = staged_gate;
    *dispatch = staged_dispatch;
    *packet_bytes_out = encoded_bytes;
    return WORR_NATIVE_CARRIER_SESSION_OK;
}

static void gate_clear_active(worr_native_carrier_tx_gate_v1 *gate)
{
    gate->state_flags &=
        (uint16_t)(WORR_NATIVE_CARRIER_TX_GATE_INITIALIZED |
                   WORR_NATIVE_CARRIER_TX_GATE_TOKEN_EXHAUSTED);
    gate->active_message_sequence = 0;
    gate->active_token_id = 0;
    gate->confirmed_fragments = 0;
    gate->active_fragment_count = 0;
    gate->active_payload_crc32 = 0;
}

static void dispatch_clear_pending(
    worr_native_carrier_dispatch_v1 *dispatch)
{
    dispatch->state_flags &=
        (uint16_t)~WORR_NATIVE_CARRIER_DISPATCH_PACKET_PENDING;
    dispatch->pending_packet_crc32 = 0;
    dispatch->pending_fragment_index = 0;
    dispatch->pending_packet_bytes = 0;
    memset(&dispatch->pending_fragmenter, 0,
           sizeof(dispatch->pending_fragmenter));
}

static bool confirm_entry_shape_valid(
    const worr_native_carrier_view_v1 *view,
    bool allow_ack_tail)
{
    uint16_t index;

    if (view->entry_count == 0 ||
        view->entries[0].entry_type !=
            WORR_NATIVE_CARRIER_ENTRY_DATA_V1) {
        return false;
    }
    if (!allow_ack_tail)
        return view->entry_count == 1;
    for (index = 1; index < view->entry_count; ++index) {
        if (view->entries[index].entry_type !=
            WORR_NATIVE_CARRIER_ENTRY_ACK_V1) {
            return false;
        }
    }
    return true;
}

static worr_native_carrier_session_result_v1 dispatch_confirm_packet(
    worr_native_carrier_tx_gate_v1 *gate,
    worr_native_tx_session_v1 *session,
    worr_native_tx_slot_v1 *slots,
    uint16_t slot_capacity,
    worr_native_carrier_dispatch_v1 *dispatch,
    uint64_t handoff_tick,
    const void *packet,
    size_t packet_bytes,
    bool allow_ack_tail)
{
    const size_t slots_bytes = (size_t)slot_capacity * sizeof(*slots);
    worr_native_carrier_tx_gate_v1 staged_gate;
    worr_native_carrier_dispatch_v1 staged_dispatch;
    worr_native_tx_session_v1 staged_session;
    worr_native_tx_slot_v1 staged_slots[WORR_NATIVE_SESSION_MAX_TX_SLOTS];
    worr_native_carrier_view_v1 view;
    worr_native_envelope_frame_info_v1 info;
    worr_native_tx_result_v1 confirmed;
    bool ticket_current;
    bool final_fragment;

    if (gate == NULL || session == NULL || slots == NULL ||
        dispatch == NULL || packet == NULL || packet_bytes == 0 ||
        ranges_overlap(gate, sizeof(*gate), session, sizeof(*session)) ||
        ranges_overlap(gate, sizeof(*gate), slots, slots_bytes) ||
        ranges_overlap(dispatch, sizeof(*dispatch), gate, sizeof(*gate)) ||
        ranges_overlap(dispatch, sizeof(*dispatch), session,
                       sizeof(*session)) ||
        ranges_overlap(dispatch, sizeof(*dispatch), slots, slots_bytes) ||
        ranges_overlap(packet, packet_bytes, gate, sizeof(*gate)) ||
        ranges_overlap(packet, packet_bytes, session, sizeof(*session)) ||
        ranges_overlap(packet, packet_bytes, slots, slots_bytes) ||
        ranges_overlap(packet, packet_bytes, dispatch,
                       sizeof(*dispatch))) {
        return WORR_NATIVE_CARRIER_SESSION_INVALID_ARGUMENT;
    }
    if (!gate_matches_dispatch(gate, dispatch) ||
        (gate->state_flags &
         WORR_NATIVE_CARRIER_TX_GATE_PACKET_PENDING) == 0) {
        return WORR_NATIVE_CARRIER_SESSION_INVALID_STATE;
    }
    if (!Worr_NativeTxSessionValidateV1(session, slots, slot_capacity))
        return WORR_NATIVE_CARRIER_SESSION_INVALID_STATE;
    if (session->transport_epoch != dispatch->transport_epoch)
        return WORR_NATIVE_CARRIER_SESSION_WRONG_EPOCH;
    if (session->connection_owner_id != gate->connection_owner_id)
        return WORR_NATIVE_CARRIER_SESSION_INVALID_STATE;
    ticket_current = Worr_NativeTxSessionPreparedValidateV1(
        session, slots, slot_capacity, &dispatch->send_ticket);
    if (handoff_tick < gate->last_handoff_tick ||
        handoff_tick < dispatch->send_ticket.selection_tick) {
        return WORR_NATIVE_CARRIER_SESSION_CLOCK_REGRESSION;
    }
    if (packet_bytes != dispatch->pending_packet_bytes ||
        crc32_bytes(packet, packet_bytes) !=
            dispatch->pending_packet_crc32) {
        return WORR_NATIVE_CARRIER_SESSION_STALE_DISPATCH;
    }
    if (Worr_NativeCarrierDecodeV1(packet, packet_bytes, &view) !=
            WORR_NATIVE_CARRIER_OK ||
        view.transport_epoch != dispatch->transport_epoch ||
        !confirm_entry_shape_valid(&view, allow_ack_tail) ||
        Worr_NativeEnvelopeDecodeV1(
            (const uint8_t *)packet + view.entries[0].data_offset,
            view.entries[0].data_bytes, &info) !=
            WORR_NATIVE_ENVELOPE_DECODE_OK ||
        info.transport_epoch != dispatch->transport_epoch ||
        info.message_sequence !=
            dispatch->send_ticket.pre_send_slot.message_sequence ||
        info.record.record_class != dispatch->fragmenter.record.record_class ||
        info.record.record_schema_version !=
            dispatch->fragmenter.record.record_schema_version ||
        info.record.object_epoch !=
            dispatch->fragmenter.record.object_epoch ||
        info.record.object_sequence !=
            dispatch->fragmenter.record.object_sequence ||
        info.total_payload_bytes !=
            dispatch->fragmenter.total_payload_bytes ||
        info.fragment_index != dispatch->pending_fragment_index ||
        info.fragment_count != dispatch->fragmenter.fragment_count ||
        info.fragment_stride != dispatch->fragmenter.fragment_stride ||
        info.priority != dispatch->fragmenter.priority ||
        info.payload_crc32 != dispatch->fragmenter.payload_crc32 ||
        info.fragment_offset !=
            (uint32_t)dispatch->pending_fragment_index *
                dispatch->fragmenter.fragment_stride) {
        return WORR_NATIVE_CARRIER_SESSION_STALE_DISPATCH;
    }

    staged_gate = *gate;
    staged_dispatch = *dispatch;
    if (!ticket_current) {
        staged_gate.last_handoff_tick = handoff_tick;
        gate_clear_active(&staged_gate);
        counter_increment(&staged_gate.retired_bursts);
        dispatch_clear_pending(&staged_dispatch);
        staged_dispatch.state_flags =
            WORR_NATIVE_CARRIER_DISPATCH_RETIRED;
        *gate = staged_gate;
        *dispatch = staged_dispatch;
        return WORR_NATIVE_CARRIER_SESSION_DISPATCH_RETIRED;
    }
    staged_dispatch.fragmenter = staged_dispatch.pending_fragmenter;
    ++staged_dispatch.submitted_fragments;
    dispatch_clear_pending(&staged_dispatch);
    ++staged_gate.confirmed_fragments;
    staged_gate.state_flags &=
        (uint16_t)~WORR_NATIVE_CARRIER_TX_GATE_PACKET_PENDING;
    staged_gate.last_handoff_tick = handoff_tick;
    final_fragment =
        (staged_dispatch.fragmenter.state_flags &
         WORR_NATIVE_FRAGMENTER_EXHAUSTED) != 0;
    if (!final_fragment) {
        *gate = staged_gate;
        *dispatch = staged_dispatch;
        return WORR_NATIVE_CARRIER_SESSION_OK;
    }
    if (staged_dispatch.submitted_fragments !=
            staged_dispatch.fragmenter.fragment_count ||
        staged_gate.confirmed_fragments !=
            staged_gate.active_fragment_count) {
        return WORR_NATIVE_CARRIER_SESSION_INVALID_STATE;
    }

    staged_session = *session;
    memset(staged_slots, 0, sizeof(staged_slots));
    memcpy(staged_slots, slots, slots_bytes);
    confirmed = Worr_NativeTxSessionConfirmPreparedV1(
        &staged_session, staged_slots, slot_capacity,
        &staged_dispatch.send_ticket, handoff_tick);
    if (confirmed != WORR_NATIVE_TX_SELECTED)
        return tx_result(confirmed);

    staged_dispatch.state_flags =
        WORR_NATIVE_CARRIER_DISPATCH_COMMITTED;
    gate_clear_active(&staged_gate);
    counter_increment(&staged_gate.committed_bursts);
    *session = staged_session;
    memcpy(slots, staged_slots, slots_bytes);
    *gate = staged_gate;
    *dispatch = staged_dispatch;
    return WORR_NATIVE_CARRIER_SESSION_DISPATCH_COMMITTED;
}

worr_native_carrier_session_result_v1
Worr_NativeCarrierSessionDispatchConfirmPacketV1(
    worr_native_carrier_tx_gate_v1 *gate,
    worr_native_tx_session_v1 *session,
    worr_native_tx_slot_v1 *slots,
    uint16_t slot_capacity,
    worr_native_carrier_dispatch_v1 *dispatch,
    uint64_t handoff_tick,
    const void *packet,
    size_t packet_bytes)
{
    return dispatch_confirm_packet(
        gate, session, slots, slot_capacity, dispatch, handoff_tick,
        packet, packet_bytes, false);
}

worr_native_carrier_session_result_v1
Worr_NativeCarrierSessionDispatchConfirmMixedPacketV1(
    worr_native_carrier_tx_gate_v1 *gate,
    worr_native_tx_session_v1 *session,
    worr_native_tx_slot_v1 *slots,
    uint16_t slot_capacity,
    worr_native_carrier_dispatch_v1 *dispatch,
    uint64_t handoff_tick,
    const void *packet,
    size_t packet_bytes)
{
    return dispatch_confirm_packet(
        gate, session, slots, slot_capacity, dispatch, handoff_tick,
        packet, packet_bytes, true);
}

worr_native_carrier_session_result_v1
Worr_NativeCarrierSessionDispatchRejectPacketV1(
    worr_native_carrier_tx_gate_v1 *gate,
    worr_native_carrier_dispatch_v1 *dispatch)
{
    worr_native_carrier_tx_gate_v1 staged_gate;
    worr_native_carrier_dispatch_v1 staged_dispatch;

    if (gate == NULL || dispatch == NULL ||
        ranges_overlap(gate, sizeof(*gate), dispatch,
                       sizeof(*dispatch))) {
        return WORR_NATIVE_CARRIER_SESSION_INVALID_ARGUMENT;
    }
    if (!gate_matches_dispatch(gate, dispatch) ||
        (gate->state_flags &
         WORR_NATIVE_CARRIER_TX_GATE_PACKET_PENDING) == 0) {
        return WORR_NATIVE_CARRIER_SESSION_INVALID_STATE;
    }
    staged_gate = *gate;
    staged_gate.state_flags &=
        (uint16_t)~WORR_NATIVE_CARRIER_TX_GATE_PACKET_PENDING;
    staged_dispatch = *dispatch;
    dispatch_clear_pending(&staged_dispatch);
    *gate = staged_gate;
    *dispatch = staged_dispatch;
    return WORR_NATIVE_CARRIER_SESSION_OK;
}

worr_native_carrier_session_result_v1
Worr_NativeCarrierSessionDispatchAbortV1(
    worr_native_carrier_tx_gate_v1 *gate,
    worr_native_carrier_dispatch_v1 *dispatch)
{
    worr_native_carrier_tx_gate_v1 staged_gate;
    worr_native_carrier_dispatch_v1 staged_dispatch;

    if (gate == NULL || dispatch == NULL ||
        ranges_overlap(gate, sizeof(*gate), dispatch,
                       sizeof(*dispatch))) {
        return WORR_NATIVE_CARRIER_SESSION_INVALID_ARGUMENT;
    }
    if (!gate_matches_dispatch(gate, dispatch))
        return WORR_NATIVE_CARRIER_SESSION_INVALID_STATE;
    if ((gate->state_flags &
         WORR_NATIVE_CARRIER_TX_GATE_PACKET_PENDING) != 0) {
        return WORR_NATIVE_CARRIER_SESSION_PACKET_PENDING;
    }
    staged_gate = *gate;
    gate_clear_active(&staged_gate);
    counter_increment(&staged_gate.aborted_bursts);
    staged_dispatch = *dispatch;
    staged_dispatch.state_flags =
        WORR_NATIVE_CARRIER_DISPATCH_ABORTED;
    *gate = staged_gate;
    *dispatch = staged_dispatch;
    return WORR_NATIVE_CARRIER_SESSION_OK;
}

static uint32_t tx_sequence_highwater(
    const worr_native_tx_session_v1 *session)
{
    return (session->state_flags & WORR_NATIVE_TX_SEQUENCE_EXHAUSTED) != 0
               ? UINT32_MAX
               : session->next_message_sequence - 1u;
}

static worr_native_ack_range_v1 decoded_ack(
    uint32_t transport_epoch,
    uint64_t connection_owner_id,
    const worr_native_carrier_entry_v1 *entry)
{
    worr_native_ack_range_v1 acknowledgement;

    memset(&acknowledgement, 0, sizeof(acknowledgement));
    acknowledgement.struct_size = sizeof(acknowledgement);
    acknowledgement.schema_version = WORR_NATIVE_SESSION_ABI_VERSION;
    acknowledgement.transport_epoch = transport_epoch;
    acknowledgement.first_message_sequence =
        entry->first_message_sequence;
    acknowledgement.last_message_sequence =
        entry->last_message_sequence;
    acknowledgement.connection_owner_id = connection_owner_id;
    return acknowledgement;
}

worr_native_carrier_session_result_v1
Worr_NativeCarrierSessionApplyAcksV1(
    worr_native_tx_session_v1 *session,
    worr_native_tx_slot_v1 *slots,
    uint16_t slot_capacity,
    const void *packet,
    size_t packet_bytes,
    uint32_t *acked_count_out)
{
    worr_native_tx_session_v1 staged_session;
    worr_native_tx_slot_v1 staged_slots[WORR_NATIVE_SESSION_MAX_TX_SLOTS];
    worr_native_carrier_view_v1 view;
    worr_native_carrier_result_v1 decoded;
    const size_t slots_bytes = (size_t)slot_capacity * sizeof(*slots);
    uint32_t acknowledged_total = 0;
    uint16_t acknowledgement_entries = 0;
    uint16_t entry_index;

    if (session == NULL || slots == NULL || packet == NULL ||
        packet_bytes == 0 || acked_count_out == NULL || slot_capacity == 0 ||
        slot_capacity > WORR_NATIVE_SESSION_MAX_TX_SLOTS ||
        ranges_overlap(packet, packet_bytes, session, sizeof(*session)) ||
        ranges_overlap(packet, packet_bytes, slots, slots_bytes) ||
        ranges_overlap(acked_count_out, sizeof(*acked_count_out), session,
                       sizeof(*session)) ||
        ranges_overlap(acked_count_out, sizeof(*acked_count_out), slots,
                       slots_bytes) ||
        ranges_overlap(acked_count_out, sizeof(*acked_count_out), packet,
                       packet_bytes)) {
        return WORR_NATIVE_CARRIER_SESSION_INVALID_ARGUMENT;
    }
    if (!Worr_NativeTxSessionValidateV1(session, slots, slot_capacity))
        return WORR_NATIVE_CARRIER_SESSION_INVALID_STATE;
    decoded = Worr_NativeCarrierDecodeV1(packet, packet_bytes, &view);
    if (decoded != WORR_NATIVE_CARRIER_OK)
        return carrier_result(decoded);
    if (view.transport_epoch != session->transport_epoch)
        return WORR_NATIVE_CARRIER_SESSION_WRONG_EPOCH;

    staged_session = *session;
    memset(staged_slots, 0, sizeof(staged_slots));
    memcpy(staged_slots, slots, slots_bytes);
    for (entry_index = 0; entry_index < view.entry_count; ++entry_index) {
        const worr_native_carrier_entry_v1 *entry =
            &view.entries[entry_index];
        worr_native_ack_range_v1 acknowledgement;
        worr_native_tx_result_v1 applied;
        uint32_t acknowledged = 0;
        uint16_t slot_index;

        if (entry->entry_type != WORR_NATIVE_CARRIER_ENTRY_ACK_V1)
            continue;
        ++acknowledgement_entries;
        acknowledgement = decoded_ack(
            view.transport_epoch, staged_session.connection_owner_id,
            entry);
        if (!Worr_NativeAckRangeValidateV1(&acknowledgement))
            return WORR_NATIVE_CARRIER_SESSION_MALFORMED;
        if (acknowledgement.last_message_sequence >
            tx_sequence_highwater(&staged_session)) {
            return WORR_NATIVE_CARRIER_SESSION_FUTURE_ACK;
        }
        for (slot_index = 0; slot_index < slot_capacity; ++slot_index) {
            if (staged_slots[slot_index].state_flags == 0 ||
                staged_slots[slot_index].message_sequence <
                    acknowledgement.first_message_sequence ||
                staged_slots[slot_index].message_sequence >
                    acknowledgement.last_message_sequence) {
                continue;
            }
            if (staged_slots[slot_index].send_attempts == 0)
                return WORR_NATIVE_CARRIER_SESSION_UNSENT_ACK;
        }
        applied = Worr_NativeTxSessionApplyAckV1(
            &staged_session, staged_slots, slot_capacity,
            &acknowledgement, &acknowledged);
        if (applied != WORR_NATIVE_TX_ACKNOWLEDGED &&
            applied != WORR_NATIVE_TX_ACKNOWLEDGEMENT_EMPTY) {
            return WORR_NATIVE_CARRIER_SESSION_NATIVE_REJECTED;
        }
        acknowledged_total += acknowledged;
    }
    if (acknowledgement_entries == 0)
        return WORR_NATIVE_CARRIER_SESSION_ENTRY_NOT_FOUND;

    *session = staged_session;
    memcpy(slots, staged_slots, slots_bytes);
    *acked_count_out = acknowledged_total;
    return WORR_NATIVE_CARRIER_SESSION_OK;
}

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
    worr_native_ack_range_v1 *repeat_acknowledgement_out)
{
    worr_native_carrier_view_v1 view;
    worr_native_carrier_result_v1 decoded;
    worr_native_rx_result_v1 accepted;
    worr_native_rx_message_v1 message;
    worr_native_ack_range_v1 repeat;
    const worr_native_carrier_entry_v1 *entry;
    const size_t slots_bytes = (size_t)slot_capacity * sizeof(*slots);

    if (session == NULL || slots == NULL || payload_arena == NULL ||
        packet == NULL || packet_bytes == 0 || rx_result_out == NULL ||
        message_out == NULL || repeat_acknowledgement_out == NULL ||
        ranges_overlap(rx_result_out, sizeof(*rx_result_out), message_out,
                       sizeof(*message_out)) ||
        ranges_overlap(rx_result_out, sizeof(*rx_result_out),
                       repeat_acknowledgement_out, sizeof(repeat)) ||
        ranges_overlap(message_out, sizeof(*message_out),
                       repeat_acknowledgement_out, sizeof(repeat)) ||
        ranges_overlap(rx_result_out, sizeof(*rx_result_out), session,
                       sizeof(*session)) ||
        ranges_overlap(message_out, sizeof(*message_out), session,
                       sizeof(*session)) ||
        ranges_overlap(repeat_acknowledgement_out, sizeof(repeat), session,
                       sizeof(*session)) ||
        ranges_overlap(rx_result_out, sizeof(*rx_result_out), slots,
                       slots_bytes) ||
        ranges_overlap(message_out, sizeof(*message_out), slots,
                       slots_bytes) ||
        ranges_overlap(repeat_acknowledgement_out, sizeof(repeat), slots,
                       slots_bytes) ||
        ranges_overlap(rx_result_out, sizeof(*rx_result_out), payload_arena,
                       payload_arena_bytes) ||
        ranges_overlap(message_out, sizeof(*message_out), payload_arena,
                       payload_arena_bytes) ||
        ranges_overlap(repeat_acknowledgement_out, sizeof(repeat),
                       payload_arena, payload_arena_bytes) ||
        ranges_overlap(rx_result_out, sizeof(*rx_result_out), packet,
                       packet_bytes) ||
        ranges_overlap(message_out, sizeof(*message_out), packet,
                       packet_bytes) ||
        ranges_overlap(repeat_acknowledgement_out, sizeof(repeat), packet,
                       packet_bytes)) {
        return WORR_NATIVE_CARRIER_SESSION_INVALID_ARGUMENT;
    }

    decoded = Worr_NativeCarrierDecodeV1(packet, packet_bytes, &view);
    if (decoded != WORR_NATIVE_CARRIER_OK)
        return carrier_result(decoded);
    if (entry_index >= view.entry_count)
        return WORR_NATIVE_CARRIER_SESSION_ENTRY_NOT_FOUND;
    entry = &view.entries[entry_index];
    if (entry->entry_type != WORR_NATIVE_CARRIER_ENTRY_DATA_V1)
        return WORR_NATIVE_CARRIER_SESSION_WRONG_ENTRY_TYPE;

    accepted = Worr_NativeRxSessionAcceptV1(
        session, slots, slot_capacity, payload_arena, payload_arena_bytes,
        now_tick, (const uint8_t *)packet + entry->data_offset,
        entry->data_bytes, &message, &repeat);
    if (accepted == WORR_NATIVE_RX_MESSAGE_COMPLETE)
        *message_out = message;
    if (accepted == WORR_NATIVE_RX_ALREADY_COMMITTED)
        *repeat_acknowledgement_out = repeat;
    *rx_result_out = accepted;
    return WORR_NATIVE_CARRIER_SESSION_OK;
}
