/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "server/native_shadow.h"

#include <limits.h>
#include <string.h>

/*
 * Server connection setup and parsing are single-threaded.  These counters
 * deliberately live outside svs so SV_Shutdown and slot reuse cannot recycle
 * local provenance during this process lifetime.
 */
static uint64_t next_connection_owner_id;
static uint32_t next_private_transport_epoch;
static uint64_t next_readiness_nonce;

static void saturating_increment(uint64_t *value)
{
    if (*value != UINT64_MAX)
        ++*value;
}

static bool allocate_owner(uint64_t *owner_out)
{
    if (owner_out == NULL ||
        next_connection_owner_id == UINT64_MAX) {
        return false;
    }
    *owner_out = ++next_connection_owner_id;
    return true;
}

static bool allocate_epoch_and_nonce(uint32_t *epoch_out,
                                     uint64_t *nonce_out)
{
    if (epoch_out == NULL || nonce_out == NULL ||
        next_private_transport_epoch == UINT32_MAX ||
        next_readiness_nonce == UINT64_MAX) {
        return false;
    }
    *epoch_out = ++next_private_transport_epoch;
    *nonce_out = ++next_readiness_nonce;
    return true;
}

static bool peer_structural_valid(const sv_native_shadow_peer_v1 *peer)
{
    return peer != NULL &&
           peer->version == SV_NATIVE_SHADOW_VERSION &&
           peer->initialized == 1 && peer->netchan != NULL &&
           peer->connection_owner_id != 0 &&
           peer->clock_initialized == 1 && peer->reserved0 == 0 &&
           peer->lifecycle >=
               SV_NATIVE_SHADOW_LIFECYCLE_WAIT_READINESS &&
           peer->lifecycle <= SV_NATIVE_SHADOW_LIFECYCLE_DETACHED &&
           peer->transport_initialized <= 1 &&
           peer->retired_transport_initialized <= 1 &&
           peer->activation_pending <= 1 &&
           peer->carrier_traffic_seen <= 1 &&
           peer->native_wire_committed <= 1 &&
           ((peer->native_wire_committed == 0 &&
             peer->wire_committed_transport_epoch == 0) ||
            (peer->native_wire_committed == 1 &&
             peer->wire_committed_transport_epoch != 0)) &&
           peer->native_commands_accepted <= 1 &&
           peer->pending_native_valid <= 1 &&
           (peer->pending_native_valid == 0 ||
            (peer->pending_native_id.epoch != 0 &&
             peer->pending_native_id.sequence != 0)) &&
           peer->ack_emit_active <= 1 &&
           peer->ack_emit_bank <=
               SV_NATIVE_SHADOW_TRANSPORT_BANK_RETIRED &&
           peer->async_wake_active <= 1 &&
           peer->async_wake_handoff_seen <= 1 &&
           (peer->async_wake_active != 0 ||
            peer->async_wake_handoff_seen == 0) &&
           (peer->ack_next_bank ==
                SV_NATIVE_SHADOW_TRANSPORT_BANK_CURRENT ||
            peer->ack_next_bank ==
                SV_NATIVE_SHADOW_TRANSPORT_BANK_RETIRED);
}

static bool extend_clock(sv_native_shadow_peer_v1 *peer,
                         uint32_t raw_time_ms,
                         uint64_t *extended_out)
{
    uint32_t delta;

    if (!peer_structural_valid(peer) || extended_out == NULL)
        return false;
    delta = raw_time_ms - peer->clock_last_raw;
    /* A real forward gap can never be distinguished from a regression after
     * half the 32-bit domain, so fail closed instead of guessing. */
    if (delta > INT32_MAX || peer->clock_ticks > UINT64_MAX - delta)
        return false;
    peer->clock_last_raw = raw_time_ms;
    peer->clock_ticks += delta;
    *extended_out = peer->clock_ticks;
    return true;
}

static bool project_clock(const sv_native_shadow_peer_v1 *peer,
                          uint32_t raw_time_ms,
                          uint64_t *extended_out)
{
    uint32_t delta;

    if (!peer_structural_valid(peer) || extended_out == NULL)
        return false;
    delta = raw_time_ms - peer->clock_last_raw;
    if (delta > INT32_MAX || peer->clock_ticks > UINT64_MAX - delta)
        return false;
    *extended_out = peer->clock_ticks + delta;
    return true;
}

static bool ack_due_at_tick(
    const sv_native_shadow_peer_v1 *peer,
    uint64_t now_tick,
    bool *due_out)
{
    worr_native_carrier_ack_result_v1 current;
    worr_native_carrier_ack_result_v1 retired;

    if (!peer_structural_valid(peer) || due_out == NULL)
        return false;
    current = peer->transport_initialized
        ? Worr_NativeCarrierAckPeekDueV1(
              &peer->transport.ack_ledger, now_tick,
              SV_NATIVE_SHADOW_ACK_RETRY_MS)
        : WORR_NATIVE_CARRIER_ACK_NOT_DUE;
    retired = peer->retired_transport_initialized
        ? Worr_NativeCarrierAckPeekDueV1(
              &peer->retired_transport.ack_ledger, now_tick,
              SV_NATIVE_SHADOW_ACK_RETRY_MS)
        : WORR_NATIVE_CARRIER_ACK_NOT_DUE;
    if ((current != WORR_NATIVE_CARRIER_ACK_OK &&
         current != WORR_NATIVE_CARRIER_ACK_NOT_DUE) ||
        (retired != WORR_NATIVE_CARRIER_ACK_OK &&
         retired != WORR_NATIVE_CARRIER_ACK_NOT_DUE)) {
        return false;
    }
    *due_out = current == WORR_NATIVE_CARRIER_ACK_OK ||
               retired == WORR_NATIVE_CARRIER_ACK_OK;
    return true;
}

static bool transport_valid(
    const sv_native_shadow_transport_v1 *transport)
{
    return transport != NULL &&
           transport->official_connection_epoch != 0 &&
           transport->reserved0 == 0 &&
           Worr_NativeSessionBindingValidateV1(&transport->binding) &&
           Worr_NativeRxSessionValidateV1(
               &transport->rx_session, transport->rx_slots,
               SV_NATIVE_SHADOW_RX_SLOT_CAPACITY) &&
           Worr_NativeCarrierAckLedgerValidateV1(
               &transport->ack_ledger) &&
           Worr_NativeCommandShadowJoinValidateV1(
               &transport->command_join);
}

static bool record_ref_equal(worr_native_record_ref_v1 left,
                             worr_native_record_ref_v1 right)
{
    return left.record_class == right.record_class &&
           left.reserved0 == right.reserved0 &&
           left.record_schema_version == right.record_schema_version &&
           left.object_epoch == right.object_epoch &&
           left.object_sequence == right.object_sequence;
}

static void enter_drain(sv_native_shadow_peer_v1 *peer,
                        sv_native_shadow_failure_v1 failure,
                        bool disable_readiness)
{
    if (!peer_structural_valid(peer))
        return;
    if (peer->lifecycle != SV_NATIVE_SHADOW_LIFECYCLE_DRAIN)
        saturating_increment(&peer->drain_entries);
    peer->lifecycle = SV_NATIVE_SHADOW_LIFECYCLE_DRAIN;
    peer->last_failure = (uint32_t)failure;
    if (failure != SV_NATIVE_SHADOW_FAILURE_NONE)
        saturating_increment(&peer->failures);
    if (!disable_readiness)
        return;
    peer->enabled = 0;
    peer->readiness_initialized = 0;
    peer->activation_pending = 0;
    peer->packet_open = 0;
    memset(&peer->readiness, 0, sizeof(peer->readiness));
    memset(&peer->sideband, 0, sizeof(peer->sideband));
}

static void carrier_reject(sv_native_shadow_peer_v1 *peer,
                           sv_native_shadow_failure_v1 failure)
{
    peer->carrier_traffic_seen = 1;
    saturating_increment(&peer->rx_rejections);
    enter_drain(peer, failure, true);
}

static bool carrier_command_shape(
    const byte *packet,
    const worr_native_carrier_view_v1 *view,
    worr_native_envelope_frame_info_v1 *frame_out)
{
    worr_native_envelope_frame_info_v1 frame;
    const worr_native_carrier_entry_v1 *entry;

    if (packet == NULL || view == NULL || frame_out == NULL ||
        view->entry_count != 1)
        return false;
    entry = &view->entries[0];
    if (entry->entry_type != WORR_NATIVE_CARRIER_ENTRY_DATA_V1 ||
        entry->data_bytes != SV_NATIVE_SHADOW_WNE_BYTES ||
        entry->data_offset > view->packet_bytes ||
        entry->data_bytes > view->packet_bytes - entry->data_offset ||
        Worr_NativeEnvelopeDecodeV1(
            packet + entry->data_offset, entry->data_bytes, &frame) !=
            WORR_NATIVE_ENVELOPE_DECODE_OK) {
        return false;
    }
    if (frame.transport_epoch != view->transport_epoch ||
        frame.record.record_class != WORR_NATIVE_RECORD_COMMAND_V1 ||
        frame.record.reserved0 != 0 ||
        frame.record.record_schema_version != WORR_COMMAND_ABI_VERSION ||
        frame.total_payload_bytes != SV_NATIVE_SHADOW_PAYLOAD_BYTES ||
        frame.fragment_offset != 0 ||
        frame.fragment_payload_bytes != SV_NATIVE_SHADOW_PAYLOAD_BYTES ||
        frame.fragment_index != 0 || frame.fragment_count != 1 ||
        frame.fragment_stride != SV_NATIVE_SHADOW_PAYLOAD_BYTES ||
        frame.fragment_flags !=
            (WORR_NATIVE_ENVELOPE_FRAGMENT_FIRST |
             WORR_NATIVE_ENVELOPE_FRAGMENT_LAST) ||
        frame.payload_offset != WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES) {
        return false;
    }
    *frame_out = frame;
    return true;
}

static sv_native_shadow_transport_v1 *transport_for_epoch(
    sv_native_shadow_peer_v1 *peer, uint32_t transport_epoch,
    sv_native_shadow_transport_bank_v1 *bank_out)
{
    if (peer->transport_initialized &&
        peer->transport.binding.transport_epoch == transport_epoch) {
        *bank_out = SV_NATIVE_SHADOW_TRANSPORT_BANK_CURRENT;
        return &peer->transport;
    }
    if (peer->retired_transport_initialized &&
        peer->retired_transport.binding.transport_epoch == transport_epoch) {
        *bank_out = SV_NATIVE_SHADOW_TRANSPORT_BANK_RETIRED;
        return &peer->retired_transport;
    }
    *bank_out = SV_NATIVE_SHADOW_TRANSPORT_BANK_NONE;
    return NULL;
}

static bool decode_completed_command(
    const sv_native_shadow_transport_v1 *transport,
    const worr_native_rx_message_v1 *message,
    worr_command_record_v1 *record_out)
{
    worr_native_codec_info_v1 codec_info;
    worr_native_record_ref_v1 codec_record;
    const byte *payload;

    if (transport == NULL || message == NULL || record_out == NULL ||
        message->payload_bytes != SV_NATIVE_SHADOW_PAYLOAD_BYTES ||
        message->payload_offset > sizeof(transport->payload_arena) ||
        message->payload_bytes >
            sizeof(transport->payload_arena) - message->payload_offset) {
        return false;
    }
    payload = transport->payload_arena + message->payload_offset;
    if (Worr_NativeCodecInspectV1(
            payload, message->payload_bytes, &codec_info) !=
            WORR_NATIVE_CODEC_OK ||
        !Worr_NativeCodecInfoRecordRefV1(
            &codec_info, &codec_record) ||
        !record_ref_equal(codec_record, message->record) ||
        Worr_NativeCodecCommandDecodeV1(
            payload, message->payload_bytes,
            WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS,
            record_out) != WORR_NATIVE_CODEC_OK ||
        record_out->command_id.epoch != message->record.object_epoch ||
        record_out->command_id.sequence !=
            message->record.object_sequence) {
        return false;
    }
    return true;
}

static void expose_legacy(netchan_app_rx_output_v1_t *output,
                          uint32_t legacy_bytes)
{
    memset(output, 0, sizeof(*output));
    output->abi_version = NETCHAN_APP_RX_HOOK_ABI_V1;
    output->struct_size = sizeof(*output);
    output->legacy_bytes = legacy_bytes;
}

static netchan_app_tx_prepare_result_t native_shadow_tx_prepare(
    void *opaque,
    const netchan_app_tx_prepare_info_v1_t *info,
    const byte *legacy_application,
    byte *candidate_application,
    netchan_app_tx_prepare_output_v1_t *output)
{
    sv_native_shadow_peer_v1 *peer = opaque;
    sv_native_shadow_transport_v1 *selected;
    sv_native_shadow_transport_v1 staged;
    worr_native_carrier_ack_emit_token_v1 token;
    worr_native_carrier_ack_result_v1 current_due;
    worr_native_carrier_ack_result_v1 retired_due;
    worr_native_carrier_ack_result_v1 result;
    sv_native_shadow_transport_bank_v1 bank;
    uint16_t budget;
    size_t packet_bytes;

    if (!peer_structural_valid(peer) || !info || !output ||
        info->abi_version != NETCHAN_APP_TX_HOOK_ABI_V1 ||
        info->struct_size != sizeof(*info) ||
        info->legacy_application_bytes > info->max_application_bytes ||
        (info->legacy_application_bytes != 0 && !legacy_application) ||
        info->max_application_bytes == 0 || !candidate_application ||
        peer->ack_emit_active ||
        peer->lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_WAIT_READINESS ||
        peer->lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_DETACHED) {
        if (peer_structural_valid(peer))
            saturating_increment(&peer->tx_bypass_calls);
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }

    current_due = peer->transport_initialized
        ? Worr_NativeCarrierAckPeekDueV1(
              &peer->transport.ack_ledger, peer->clock_ticks,
              SV_NATIVE_SHADOW_ACK_RETRY_MS)
        : WORR_NATIVE_CARRIER_ACK_NOT_DUE;
    retired_due = peer->retired_transport_initialized
        ? Worr_NativeCarrierAckPeekDueV1(
              &peer->retired_transport.ack_ledger, peer->clock_ticks,
              SV_NATIVE_SHADOW_ACK_RETRY_MS)
        : WORR_NATIVE_CARRIER_ACK_NOT_DUE;
    if ((current_due != WORR_NATIVE_CARRIER_ACK_OK &&
         current_due != WORR_NATIVE_CARRIER_ACK_NOT_DUE) ||
        (retired_due != WORR_NATIVE_CARRIER_ACK_OK &&
         retired_due != WORR_NATIVE_CARRIER_ACK_NOT_DUE)) {
        saturating_increment(&peer->tx_bypass_calls);
        enter_drain(peer, SV_NATIVE_SHADOW_FAILURE_ACK, true);
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }
    if (current_due != WORR_NATIVE_CARRIER_ACK_OK &&
        retired_due != WORR_NATIVE_CARRIER_ACK_OK) {
        saturating_increment(&peer->tx_bypass_calls);
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }
    if (current_due == WORR_NATIVE_CARRIER_ACK_OK &&
        retired_due == WORR_NATIVE_CARRIER_ACK_OK) {
        bank = (sv_native_shadow_transport_bank_v1)peer->ack_next_bank;
    } else {
        bank = current_due == WORR_NATIVE_CARRIER_ACK_OK
            ? SV_NATIVE_SHADOW_TRANSPORT_BANK_CURRENT
            : SV_NATIVE_SHADOW_TRANSPORT_BANK_RETIRED;
    }
    selected = bank == SV_NATIVE_SHADOW_TRANSPORT_BANK_CURRENT
        ? &peer->transport : &peer->retired_transport;
    staged = *selected;
    budget = (uint16_t)min(
        info->max_application_bytes,
        (uint32_t)WORR_NATIVE_CARRIER_MAX_PACKET_BYTES);
    packet_bytes = 0;
    result = Worr_NativeCarrierAckPreparePacketV1(
        &staged.ack_ledger, peer->clock_ticks,
        SV_NATIVE_SHADOW_ACK_RETRY_MS, budget,
        legacy_application, info->legacy_application_bytes,
        candidate_application, budget, &packet_bytes, &token);
    if (result != WORR_NATIVE_CARRIER_ACK_OK) {
        saturating_increment(&peer->tx_bypass_calls);
        if (result != WORR_NATIVE_CARRIER_ACK_NOT_DUE &&
            result != WORR_NATIVE_CARRIER_ACK_OUTPUT_TOO_SMALL &&
            result != WORR_NATIVE_CARRIER_ACK_LIMIT) {
            enter_drain(peer, SV_NATIVE_SHADOW_FAILURE_ACK, true);
        }
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }
    if (packet_bytes > UINT32_MAX ||
        peer->next_completion_token == UINT64_MAX) {
        saturating_increment(&peer->tx_bypass_calls);
        enter_drain(peer, SV_NATIVE_SHADOW_FAILURE_ACK, true);
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }

    *selected = staged;
    peer->ack_emit_token = token;
    peer->ack_emit_active = 1;
    peer->ack_emit_bank = (uint32_t)bank;
    peer->active_completion_token = ++peer->next_completion_token;
    peer->ack_next_bank =
        bank == SV_NATIVE_SHADOW_TRANSPORT_BANK_CURRENT
            ? SV_NATIVE_SHADOW_TRANSPORT_BANK_RETIRED
            : SV_NATIVE_SHADOW_TRANSPORT_BANK_CURRENT;
    saturating_increment(&peer->tx_ack_prepares);

    memset(output, 0, sizeof(*output));
    output->abi_version = NETCHAN_APP_TX_HOOK_ABI_V1;
    output->struct_size = sizeof(*output);
    output->application_bytes = (uint32_t)packet_bytes;
    output->token = peer->active_completion_token;
    return NETCHAN_APP_TX_PREPARE_PREPARED;
}

static void native_shadow_tx_completion(
    void *opaque,
    const netchan_app_tx_completion_info_v1_t *info,
    const byte *application)
{
    sv_native_shadow_peer_v1 *peer = opaque;
    sv_native_shadow_transport_v1 *transport;
    worr_native_carrier_ack_result_v1 result;
    const bool info_valid = info != NULL &&
        info->abi_version == NETCHAN_APP_TX_HOOK_ABI_V1 &&
        info->struct_size == sizeof(*info);
    bool accepted_completion;
    bool definite_rejection;
    bool provenance_valid;
    bool completion_fault = false;

    if (!peer_structural_valid(peer) || !peer->ack_emit_active)
        return;
    transport = peer->ack_emit_bank ==
            SV_NATIVE_SHADOW_TRANSPORT_BANK_CURRENT
        ? &peer->transport : &peer->retired_transport;

    accepted_completion = info_valid &&
        info->result == NETCHAN_APP_TX_COMPLETION_ACCEPTED &&
        info->packet_copies != 0 && info->accepted_copies != 0 &&
        info->accepted_copies <= info->packet_copies;
    definite_rejection = info_valid &&
        (info->result == NETCHAN_APP_TX_COMPLETION_NOT_ACCEPTED ||
         info->result == NETCHAN_APP_TX_COMPLETION_PREPARE_INVALID);
    provenance_valid = info_valid &&
        info->token == peer->active_completion_token;

    if (provenance_valid && accepted_completion && application) {
        result = Worr_NativeCarrierAckCommitHandoffV1(
            &transport->ack_ledger, &peer->ack_emit_token,
            peer->clock_ticks, application, info->application_bytes);
        if (result == WORR_NATIVE_CARRIER_ACK_OK) {
            peer->carrier_traffic_seen = 1;
            saturating_increment(&peer->tx_ack_handoffs);
            if (peer->async_wake_active)
                peer->async_wake_handoff_seen = 1;
        } else {
            (void)Worr_NativeCarrierAckRejectHandoffV1(
                &transport->ack_ledger, &peer->ack_emit_token);
            completion_fault = true;
        }
    } else if (provenance_valid && definite_rejection) {
        result = Worr_NativeCarrierAckRejectHandoffV1(
            &transport->ack_ledger, &peer->ack_emit_token);
        saturating_increment(&peer->tx_ack_rejections);
        if (result != WORR_NATIVE_CARRIER_ACK_OK)
            completion_fault = true;
    } else {
        (void)Worr_NativeCarrierAckRejectHandoffV1(
            &transport->ack_ledger, &peer->ack_emit_token);
        saturating_increment(&peer->tx_ack_rejections);
        completion_fault = true;
    }
    peer->ack_emit_active = 0;
    peer->ack_emit_bank = SV_NATIVE_SHADOW_TRANSPORT_BANK_NONE;
    peer->active_completion_token = 0;
    memset(&peer->ack_emit_token, 0, sizeof(peer->ack_emit_token));
    if (completion_fault)
        enter_drain(peer, SV_NATIVE_SHADOW_FAILURE_ACK, true);
}

static netchan_app_rx_result_t native_shadow_rx(
    void *opaque,
    const netchan_app_rx_info_v1_t *info,
    const byte *application,
    netchan_app_rx_output_v1_t *output)
{
    sv_native_shadow_peer_v1 *peer = opaque;
    worr_native_carrier_view_v1 view;
    worr_native_envelope_frame_info_v1 frame;
    worr_native_carrier_result_v1 carrier_result;
    sv_native_shadow_transport_v1 *transport;
    sv_native_shadow_transport_v1 staged;
    sv_native_shadow_transport_bank_v1 bank;
    worr_native_carrier_session_result_v1 bridge_result;
    worr_native_rx_result_v1 rx_result;
    worr_native_rx_message_v1 message;
    worr_command_record_v1 record;
    worr_native_command_shadow_compare_report_v1 report;
    worr_native_command_shadow_join_result_v1 join_result;
    uint32_t pruned;

    if (!peer_structural_valid(peer) || !info || !output ||
        info->abi_version != NETCHAN_APP_RX_HOOK_ABI_V1 ||
        info->struct_size != sizeof(*info) ||
        info->application_bytes == 0 || !application) {
        if (peer_structural_valid(peer))
            saturating_increment(&peer->rx_bypass_calls);
        return NETCHAN_APP_RX_BYPASS;
    }
    carrier_result = Worr_NativeCarrierDecodeV1(
        application, info->application_bytes, &view);
    if (carrier_result == WORR_NATIVE_CARRIER_NO_CARRIER) {
        saturating_increment(&peer->rx_bypass_calls);
        return NETCHAN_APP_RX_BYPASS;
    }

    peer->carrier_traffic_seen = 1;
    saturating_increment(&peer->rx_carriers);
    if (carrier_result != WORR_NATIVE_CARRIER_OK ||
        !carrier_command_shape(application, &view, &frame)) {
        carrier_reject(peer, SV_NATIVE_SHADOW_FAILURE_CARRIER);
        return NETCHAN_APP_RX_REJECT;
    }
    transport = transport_for_epoch(
        peer, view.transport_epoch, &bank);
    if (!transport || !transport_valid(transport)) {
        /* SERVER_ACTIVE may already be on the wire when a later local
         * transport setup/validation fault is discovered.  The exact
         * committed epoch is still trustworthy connection provenance: strip
         * its structurally valid carrier and preserve the authoritative
         * legacy prefix, but never admit or acknowledge native DATA. */
        if (peer->native_wire_committed &&
            view.transport_epoch ==
                peer->wire_committed_transport_epoch) {
            if (peer->lifecycle != SV_NATIVE_SHADOW_LIFECYCLE_DRAIN) {
                enter_drain(
                    peer, SV_NATIVE_SHADOW_FAILURE_SESSION, true);
            }
            saturating_increment(&peer->rx_drained);
            expose_legacy(output, view.legacy_bytes);
            return NETCHAN_APP_RX_EXPOSE_LEGACY;
        }
        carrier_reject(peer, SV_NATIVE_SHADOW_FAILURE_CARRIER);
        return NETCHAN_APP_RX_REJECT;
    }

    staged = *transport;
    memset(&message, 0, sizeof(message));
    bridge_result = Worr_NativeCarrierSessionAcceptDataRetainedV1(
        &staged.rx_session, staged.rx_slots,
        SV_NATIVE_SHADOW_RX_SLOT_CAPACITY,
        staged.payload_arena, sizeof(staged.payload_arena),
        peer->clock_ticks, application, info->application_bytes, 0,
        &staged.ack_ledger, &rx_result, &message);
    if (bridge_result != WORR_NATIVE_CARRIER_SESSION_OK) {
        carrier_reject(peer, SV_NATIVE_SHADOW_FAILURE_SESSION);
        return NETCHAN_APP_RX_REJECT;
    }
    if (rx_result == WORR_NATIVE_RX_ALREADY_COMMITTED) {
        *transport = staged;
        saturating_increment(&peer->rx_repeat_refreshes);
        expose_legacy(output, view.legacy_bytes);
        return NETCHAN_APP_RX_EXPOSE_LEGACY;
    }
    if (rx_result != WORR_NATIVE_RX_MESSAGE_COMPLETE ||
        !decode_completed_command(&staged, &message, &record) ||
        !record_ref_equal(frame.record, message.record)) {
        carrier_reject(
            peer, rx_result == WORR_NATIVE_RX_MESSAGE_COMPLETE
                      ? SV_NATIVE_SHADOW_FAILURE_CODEC
                      : SV_NATIVE_SHADOW_FAILURE_SESSION);
        return NETCHAN_APP_RX_REJECT;
    }
    if (record.command_id.epoch !=
        transport->official_connection_epoch) {
        carrier_reject(
            peer, SV_NATIVE_SHADOW_FAILURE_COMMAND_SHADOW);
        return NETCHAN_APP_RX_REJECT;
    }

    if (bank != SV_NATIVE_SHADOW_TRANSPORT_BANK_CURRENT ||
        peer->lifecycle != SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE ||
        peer->native_commands_accepted != 0) {
        saturating_increment(&peer->one_shot_limits);
        peer->last_failure = SV_NATIVE_SHADOW_FAILURE_ONE_SHOT_LIMIT;
        if (bank == SV_NATIVE_SHADOW_TRANSPORT_BANK_CURRENT &&
            peer->lifecycle != SV_NATIVE_SHADOW_LIFECYCLE_DRAIN) {
            saturating_increment(&peer->drain_entries);
            peer->lifecycle = SV_NATIVE_SHADOW_LIFECYCLE_DRAIN;
        }
        saturating_increment(&peer->rx_drained);
        expose_legacy(output, view.legacy_bytes);
        return NETCHAN_APP_RX_EXPOSE_LEGACY;
    }

    if (Worr_NativeCommandShadowJoinPruneV1(
            &staged.command_join, peer->clock_ticks, &pruned) !=
            WORR_NATIVE_COMMAND_SHADOW_JOIN_PRUNE_COMPLETE) {
        carrier_reject(peer, SV_NATIVE_SHADOW_FAILURE_COMMAND_SHADOW);
        return NETCHAN_APP_RX_REJECT;
    }
    join_result = Worr_NativeCommandShadowJoinObserveV1(
        &staged.command_join, WORR_NATIVE_COMMAND_SHADOW_JOIN_NATIVE,
        &record, peer->clock_ticks, &report);
    if (join_result != WORR_NATIVE_COMMAND_SHADOW_JOIN_STORED_NATIVE ||
        Worr_NativeCarrierSessionCommitRetainedV1(
            &staged.rx_session, staged.rx_slots,
            SV_NATIVE_SHADOW_RX_SLOT_CAPACITY,
            message.slot_index, message.message_sequence,
            &staged.ack_ledger) != WORR_NATIVE_RX_COMMITTED) {
        carrier_reject(peer, SV_NATIVE_SHADOW_FAILURE_COMMAND_SHADOW);
        return NETCHAN_APP_RX_REJECT;
    }

    *transport = staged;
    peer->native_commands_accepted = 1;
    peer->pending_native_id = record.command_id;
    peer->pending_native_valid = 1;
    saturating_increment(&peer->rx_commits);
    expose_legacy(output, view.legacy_bytes);
    return NETCHAN_APP_RX_EXPOSE_LEGACY;
}

static bool native_shadow_hooks_exact(
    const sv_native_shadow_peer_v1 *peer)
{
    const netchan_t *netchan;

    if (!peer_structural_valid(peer))
        return false;
    netchan = peer->netchan;
    return netchan->type == NETCHAN_NEW &&
           netchan->app_tx_prepare == native_shadow_tx_prepare &&
           netchan->app_tx_completion == native_shadow_tx_completion &&
           netchan->app_tx_opaque == peer &&
           netchan->app_rx == native_shadow_rx &&
           netchan->app_rx_opaque == peer;
}

bool SV_NativeShadowPeerInitV1(sv_native_shadow_peer_v1 *peer,
                               netchan_t *netchan,
                               uint32_t raw_time_ms)
{
    uint64_t owner;

    if (peer == NULL || netchan == NULL || netchan->type != NETCHAN_NEW ||
        netchan->app_tx_prepare != NULL ||
        netchan->app_tx_completion != NULL ||
        netchan->app_tx_opaque != NULL || netchan->app_rx != NULL ||
        netchan->app_rx_opaque != NULL || !allocate_owner(&owner)) {
        return false;
    }

    memset(peer, 0, sizeof(*peer));
    peer->version = SV_NATIVE_SHADOW_VERSION;
    peer->initialized = 1;
    peer->netchan = netchan;
    peer->connection_owner_id = owner;
    peer->enabled = 1;
    peer->clock_initialized = 1;
    peer->clock_last_raw = raw_time_ms;
    peer->clock_ticks = raw_time_ms;
    peer->lifecycle = SV_NATIVE_SHADOW_LIFECYCLE_WAIT_READINESS;
    peer->ack_next_bank = SV_NATIVE_SHADOW_TRANSPORT_BANK_CURRENT;

    if (!Netchan_SetApplicationTxHook(
            netchan, native_shadow_tx_prepare,
            native_shadow_tx_completion, peer)) {
        memset(peer, 0, sizeof(*peer));
        return false;
    }
    if (!Netchan_SetApplicationRxHook(
            netchan, native_shadow_rx, peer)) {
        (void)Netchan_SetApplicationTxHook(
            netchan, NULL, NULL, NULL);
        memset(peer, 0, sizeof(*peer));
        return false;
    }
    peer->hooks_attached = 1;
    return true;
}

void SV_NativeShadowPeerDetachV1(sv_native_shadow_peer_v1 *peer)
{
    netchan_t *netchan;

    /* Teardown is a lifetime operation, not a semantic state transition.
     * Clear callbacks from the minimal initialized ownership tuple even when
     * a separate invariant has failed, so Destroy can never leave netchan
     * pointing at storage it is about to clear/free. */
    if (peer == NULL || peer->version != SV_NATIVE_SHADOW_VERSION ||
        peer->initialized != 1 || peer->netchan == NULL) {
        return;
    }
    netchan = peer->netchan;
    if (netchan->type == NETCHAN_NEW) {
        if (netchan->app_tx_opaque == peer) {
            (void)Netchan_SetApplicationTxHook(
                netchan, NULL, NULL, NULL);
        }
        if (netchan->app_rx_opaque == peer)
            (void)Netchan_SetApplicationRxHook(netchan, NULL, NULL);
    }
    peer->hooks_attached = 0;
    peer->async_wake_active = 0;
    peer->async_wake_handoff_seen = 0;
    peer->lifecycle = SV_NATIVE_SHADOW_LIFECYCLE_DETACHED;
}

void SV_NativeShadowPeerDisableV1(sv_native_shadow_peer_v1 *peer,
                                  sv_native_shadow_failure_v1 failure)
{
    if (!peer_structural_valid(peer))
        return;
    if (peer->native_wire_committed || peer->carrier_traffic_seen ||
        peer->lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_DRAIN) {
        enter_drain(peer, failure, true);
        return;
    }
    if (peer->enabled != 0)
        saturating_increment(&peer->failures);
    peer->enabled = 0;
    peer->readiness_initialized = 0;
    peer->activation_pending = 0;
    peer->packet_open = 0;
    peer->last_failure = (uint32_t)failure;
    memset(&peer->readiness, 0, sizeof(peer->readiness));
    memset(&peer->sideband, 0, sizeof(peer->sideband));
    SV_NativeShadowPeerDetachV1(peer);
}

void SV_NativeShadowPeerDestroyV1(sv_native_shadow_peer_v1 *peer)
{
    if (peer == NULL)
        return;
    SV_NativeShadowPeerDetachV1(peer);
    memset(peer, 0, sizeof(*peer));
}

bool SV_NativeShadowPeerEnabledV1(
    const sv_native_shadow_peer_v1 *peer)
{
    return peer_structural_valid(peer) && peer->enabled == 1 &&
           native_shadow_hooks_exact(peer) &&
           peer->lifecycle != SV_NATIVE_SHADOW_LIFECYCLE_DETACHED;
}

bool SV_NativeShadowPostBootstrapQueueIdleV1(
    const sv_native_shadow_peer_v1 *peer)
{
    const netchan_t *netchan;

    if (!SV_NativeShadowPeerEnabledV1(peer))
        return false;
    netchan = peer->netchan;
    return netchan->message.cursize == 0 &&
           !netchan->message.overflowed &&
           netchan->reliable_length == 0 &&
           !netchan->fragment_pending &&
           netchan->fragment_out.cursize == 0;
}

bool SV_NativeShadowSettingIndexV1(int16_t index)
{
    return index >= WORR_NATIVE_READINESS_SETTING_BEGIN &&
           index <= WORR_NATIVE_READINESS_SETTING_COMMIT;
}

bool SV_NativeShadowAdvanceAdmissionClockV1(
    sv_native_shadow_peer_v1 *peer,
    uint32_t raw_time_ms)
{
    uint64_t now_tick;

    if (!native_shadow_hooks_exact(peer))
        return false;
    if (extend_clock(peer, raw_time_ms, &now_tick))
        return true;
    SV_NativeShadowPeerDisableV1(
        peer, SV_NATIVE_SHADOW_FAILURE_CLOCK);
    return false;
}

static bool append_buffer_valid(const sizebuf_t *buffer)
{
    return buffer != NULL && buffer->data != NULL &&
           !buffer->overflowed && buffer->cursize <= buffer->maxsize;
}

static bool backing_ranges_overlap(const sizebuf_t *left,
                                   const sizebuf_t *right)
{
    const uintptr_t left_begin = (uintptr_t)left->data;
    const uintptr_t right_begin = (uintptr_t)right->data;
    uintptr_t left_end;
    uintptr_t right_end;

    if (left->maxsize == 0 || right->maxsize == 0)
        return false;
    if (left->maxsize > UINTPTR_MAX - left_begin ||
        right->maxsize > UINTPTR_MAX - right_begin) {
        return true;
    }
    left_end = left_begin + left->maxsize;
    right_end = right_begin + right->maxsize;
    return left_begin < right_end && right_begin < left_end;
}

bool SV_NativeShadowCanAppendSvcReadinessV1(
    const sizebuf_t *message,
    const sizebuf_t *reliable_queue)
{
    uint32_t eventual_message_bytes;

    if (!append_buffer_valid(message) ||
        !append_buffer_valid(reliable_queue) ||
        message == reliable_queue ||
        backing_ranges_overlap(message, reliable_queue) ||
        message->maxsize - message->cursize <
            SV_NATIVE_SHADOW_SVC_WIRE_BYTES) {
        return false;
    }
    eventual_message_bytes = message->cursize +
                             SV_NATIVE_SHADOW_SVC_WIRE_BYTES;
    return reliable_queue->maxsize - reliable_queue->cursize >=
           eventual_message_bytes;
}

static void write_i32_le(byte *destination, int32_t value)
{
    const uint32_t bits = (uint32_t)value;

    destination[0] = (byte)(bits & UINT32_C(0xff));
    destination[1] = (byte)((bits >> 8) & UINT32_C(0xff));
    destination[2] = (byte)((bits >> 16) & UINT32_C(0xff));
    destination[3] = (byte)((bits >> 24) & UINT32_C(0xff));
}

bool SV_NativeShadowAppendSvcReadinessV1(
    sizebuf_t *message,
    const sizebuf_t *reliable_queue,
    int setting_opcode,
    const worr_native_readiness_record_v1 *record)
{
    worr_native_readiness_setting_pair_v1 pairs[
        WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT];
    byte staging[SV_NATIVE_SHADOW_SVC_WIRE_BYTES];
    uint32_t pair_index;
    uint32_t staging_offset = 0;

    if (record == NULL || setting_opcode < 0 || setting_opcode > UINT8_MAX ||
        !SV_NativeShadowCanAppendSvcReadinessV1(
            message, reliable_queue) ||
        !Worr_NativeReadinessSidebandEncodeV1(
            record, pairs,
            WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT)) {
        return false;
    }

    for (pair_index = 0;
         pair_index < WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT;
         ++pair_index) {
        staging[staging_offset] = (byte)setting_opcode;
        write_i32_le(staging + staging_offset + 1,
                     (int32_t)pairs[pair_index].index);
        write_i32_le(staging + staging_offset + 5,
                     (int32_t)pairs[pair_index].value);
        staging_offset += 9;
    }
    if (staging_offset != SV_NATIVE_SHADOW_SVC_WIRE_BYTES)
        return false;

    memcpy(message->data + message->cursize, staging, sizeof(staging));
    message->cursize += SV_NATIVE_SHADOW_SVC_WIRE_BYTES;
    return true;
}

bool SV_NativeShadowBeginEpochV1(
    sv_native_shadow_peer_v1 *peer,
    uint32_t official_connection_epoch,
    uint32_t official_supported,
    uint32_t official_negotiated,
    uint32_t raw_time_ms,
    worr_native_readiness_record_v1 *challenge_out)
{
    uint32_t transport_epoch;
    uint64_t readiness_nonce;
    uint64_t now_tick;
    uint32_t private_capabilities;
    worr_native_readiness_result_v1 result;
    worr_native_readiness_state_v1 readiness;
    worr_native_readiness_sideband_parser_v1 sideband;
    worr_native_readiness_record_v1 challenge;
    bool reopen_packet;

    if (!SV_NativeShadowPeerEnabledV1(peer) || challenge_out == NULL)
        return false;
    if (official_connection_epoch == 0 ||
        official_supported != WORR_NET_CAP_LEGACY_STAGE_MASK ||
        official_negotiated != WORR_NET_CAP_LEGACY_STAGE_MASK) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_OFFICIAL_BINDING);
        return false;
    }
    if (!extend_clock(peer, raw_time_ms, &now_tick)) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_CLOCK);
        return false;
    }
    if (!allocate_epoch_and_nonce(&transport_epoch, &readiness_nonce)) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_IDENTITY_EXHAUSTED);
        return false;
    }

    private_capabilities = official_negotiated |
                           WORR_NET_CAP_NATIVE_ENVELOPE_V1;
    memset(&readiness, 0, sizeof(readiness));
    if (peer->readiness_initialized == 0) {
        result = Worr_NativeReadinessServerInitV1(
            &readiness, transport_epoch, private_capabilities,
            readiness_nonce, now_tick, SV_NATIVE_SHADOW_TIMEOUT_MS,
            &challenge);
    } else {
        readiness = peer->readiness;
        result = Worr_NativeReadinessServerAdvanceEpochV1(
            &readiness, transport_epoch, private_capabilities,
            readiness_nonce, now_tick, SV_NATIVE_SHADOW_TIMEOUT_MS,
            &challenge);
    }
    if (result != WORR_NATIVE_READINESS_OK) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_READINESS);
        return false;
    }

    reopen_packet = peer->packet_open != 0;
    if (!Worr_NativeReadinessSidebandParserInitV1(&sideband) ||
        (reopen_packet &&
         Worr_NativeReadinessSidebandPacketBeginV1(&sideband) !=
             WORR_NATIVE_READINESS_SIDEBAND_PACKET_STARTED)) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_SIDEBAND);
        return false;
    }

    /* One bounded retired bank preserves exact old-epoch receipt liveness.
     * A later map replaces it with the immediately preceding current bank. */
    if (peer->transport_initialized) {
        peer->retired_transport = peer->transport;
        peer->retired_transport_initialized = 1;
        memset(&peer->transport, 0, sizeof(peer->transport));
        peer->transport_initialized = 0;
        peer->native_commands_accepted = 0;
    }
    memset(&peer->pending_native_id, 0,
           sizeof(peer->pending_native_id));
    peer->pending_native_valid = 0;
    peer->readiness = readiness;
    peer->sideband = sideband;
    peer->readiness_initialized = 1;
    peer->packet_open = reopen_packet ? 1u : 0u;
    peer->activation_pending = 0;
    peer->lifecycle = peer->retired_transport_initialized
        ? SV_NATIVE_SHADOW_LIFECYCLE_DRAIN
        : SV_NATIVE_SHADOW_LIFECYCLE_WAIT_READINESS;
    peer->official_connection_epoch = official_connection_epoch;
    peer->private_transport_epoch = transport_epoch;
    peer->readiness_nonce = readiness_nonce;
    saturating_increment(&peer->challenges_queued);
    *challenge_out = challenge;
    return true;
}

static bool transport_init(
    sv_native_shadow_transport_v1 *transport_out,
    const worr_native_session_binding_v1 *binding,
    uint32_t official_connection_epoch)
{
    sv_native_shadow_transport_v1 transport;

    if (!transport_out || !binding || official_connection_epoch == 0)
        return false;
    memset(&transport, 0, sizeof(transport));
    transport.binding = *binding;
    transport.official_connection_epoch = official_connection_epoch;
    if (!Worr_NativeRxSessionInitV1(
            &transport.rx_session, transport.rx_slots,
            SV_NATIVE_SHADOW_RX_SLOT_CAPACITY,
            SV_NATIVE_SHADOW_PAYLOAD_BYTES,
            SV_NATIVE_SHADOW_RX_FRAGMENT_TIMEOUT_MS,
            SV_NATIVE_SHADOW_RX_COMPLETE_TIMEOUT_MS, binding) ||
        !Worr_NativeCarrierAckLedgerInitV1(
            &transport.ack_ledger, binding,
            SV_NATIVE_SHADOW_ACK_PROACTIVE_HANDOFFS) ||
        !Worr_NativeCommandShadowJoinInitV1(
            &transport.command_join,
            WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS,
            SV_NATIVE_SHADOW_JOIN_EXPIRY_MS) ||
        !transport_valid(&transport)) {
        return false;
    }
    *transport_out = transport;
    return true;
}

static bool transport_advance(
    sv_native_shadow_transport_v1 *transport_out,
    const sv_native_shadow_transport_v1 *previous,
    const worr_native_session_binding_v1 *binding,
    uint32_t official_connection_epoch)
{
    sv_native_shadow_transport_v1 transport;
    worr_native_command_shadow_join_v1 join;

    if (!transport_out || !previous || !binding ||
        official_connection_epoch == 0 ||
        !transport_valid(previous)) {
        return false;
    }
    transport = *previous;
    if (!Worr_NativeRxSessionAdvanceEpochV1(
            &transport.rx_session, transport.rx_slots,
            SV_NATIVE_SHADOW_RX_SLOT_CAPACITY, binding) ||
        !Worr_NativeCarrierAckLedgerAdvanceEpochV1(
            &transport.ack_ledger, binding) ||
        !Worr_NativeCommandShadowJoinInitV1(
            &join, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS,
            SV_NATIVE_SHADOW_JOIN_EXPIRY_MS)) {
        return false;
    }
    transport.binding = *binding;
    transport.official_connection_epoch = official_connection_epoch;
    transport.command_join = join;
    memset(transport.payload_arena, 0,
           sizeof(transport.payload_arena));
    if (!transport_valid(&transport))
        return false;
    *transport_out = transport;
    return true;
}

bool SV_NativeShadowServerActiveQueuedV1(
    sv_native_shadow_peer_v1 *peer)
{
    worr_native_session_binding_v1 binding;
    sv_native_shadow_transport_v1 transport;

    /* This API's contract says SERVER_ACTIVE is already resident in the
     * reliable queue.  From this point the client may legitimately observe
     * ACTIVE even if any local validation or transport initialization below
     * fails, so hook retention becomes irreversible before the first
     * fallible operation. */
    if (!native_shadow_hooks_exact(peer))
        return false;
    peer->native_wire_committed = 1;
    peer->wire_committed_transport_epoch =
        peer->private_transport_epoch;
    if (!SV_NativeShadowPeerEnabledV1(peer) ||
        peer->readiness_initialized == 0 ||
        peer->readiness.phase !=
            WORR_NATIVE_READINESS_PHASE_SERVER_ACTIVE) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_READINESS);
        return false;
    }
    if (peer->transport_initialized &&
        peer->transport.binding.transport_epoch ==
            peer->readiness.transport_epoch) {
        peer->activation_pending = 0;
        return true;
    }
    if (!peer->activation_pending ||
        !Worr_NativeSessionBindingInitFromReadinessV1(
            &binding, &peer->readiness,
            peer->connection_owner_id)) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_SESSION);
        return false;
    }
    if (peer->retired_transport_initialized) {
        if (!transport_advance(
                &transport, &peer->retired_transport, &binding,
                peer->official_connection_epoch)) {
            SV_NativeShadowPeerDisableV1(
                peer, SV_NATIVE_SHADOW_FAILURE_SESSION);
            return false;
        }
    } else if (!transport_init(
                   &transport, &binding,
                   peer->official_connection_epoch)) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_SESSION);
        return false;
    }

    peer->transport = transport;
    peer->transport_initialized = 1;
    peer->activation_pending = 0;
    peer->native_commands_accepted = 0;
    memset(&peer->pending_native_id, 0,
           sizeof(peer->pending_native_id));
    peer->pending_native_valid = 0;
    peer->lifecycle = SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE;
    return true;
}

bool SV_NativeShadowPacketBeginV1(sv_native_shadow_peer_v1 *peer,
                                  uint32_t raw_time_ms)
{
    uint64_t now_tick;
    worr_native_readiness_result_v1 readiness_result;

    if (!SV_NativeShadowPeerEnabledV1(peer) ||
        peer->readiness_initialized == 0)
        return false;
    if (!extend_clock(peer, raw_time_ms, &now_tick)) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_CLOCK);
        return false;
    }
    readiness_result = Worr_NativeReadinessCheckDeadlineV1(
        &peer->readiness, now_tick);
    if (readiness_result != WORR_NATIVE_READINESS_OK) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_READINESS);
        return false;
    }
    if (Worr_NativeReadinessSidebandPacketBeginV1(&peer->sideband) !=
        WORR_NATIVE_READINESS_SIDEBAND_PACKET_STARTED) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_SIDEBAND);
        return false;
    }
    peer->packet_open = 1;
    return true;
}

sv_native_shadow_observe_result_v1 SV_NativeShadowObserveSettingV1(
    sv_native_shadow_peer_v1 *peer,
    int16_t index,
    int16_t value,
    worr_native_readiness_record_v1 *server_active_out)
{
    worr_native_readiness_sideband_result_v1 sideband_result;
    worr_native_readiness_result_v1 readiness_result;
    worr_native_readiness_record_v1 client_ready;
    worr_native_readiness_record_v1 server_active;
    worr_native_readiness_state_v1 readiness;

    if (!SV_NativeShadowPeerEnabledV1(peer) ||
        peer->readiness_initialized == 0 || peer->packet_open == 0 ||
        server_active_out == NULL) {
        return SV_NATIVE_SHADOW_OBSERVE_PILOT_DISABLED;
    }

    sideband_result = Worr_NativeReadinessSidebandObservePairV1(
        &peer->sideband, index, value);
    if (sideband_result == WORR_NATIVE_READINESS_SIDEBAND_NOT_SIDEBAND)
        return SV_NATIVE_SHADOW_OBSERVE_NOT_SIDEBAND;
    if (sideband_result == WORR_NATIVE_READINESS_SIDEBAND_FIELD_ACCEPTED)
        return SV_NATIVE_SHADOW_OBSERVE_FIELD_CONSUMED;
    if (sideband_result !=
        WORR_NATIVE_READINESS_SIDEBAND_RECORD_COMMITTED) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_SIDEBAND);
        return SV_NATIVE_SHADOW_OBSERVE_PILOT_DISABLED;
    }
    if (Worr_NativeReadinessSidebandTakeRecordV1(
            &peer->sideband, &client_ready) !=
        WORR_NATIVE_READINESS_SIDEBAND_RECORD_TAKEN) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_SIDEBAND);
        return SV_NATIVE_SHADOW_OBSERVE_PILOT_DISABLED;
    }

    readiness = peer->readiness;
    readiness_result = Worr_NativeReadinessServerObserveClientReadyV1(
        &readiness, &client_ready, peer->clock_ticks,
        &server_active);
    if (readiness_result != WORR_NATIVE_READINESS_OK &&
        readiness_result != WORR_NATIVE_READINESS_EXACT_DUPLICATE) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_READINESS);
        return SV_NATIVE_SHADOW_OBSERVE_PILOT_DISABLED;
    }
    peer->readiness = readiness;
    peer->activation_pending =
        !peer->transport_initialized ||
        peer->transport.binding.transport_epoch !=
            readiness.transport_epoch;
    saturating_increment(&peer->client_ready_records);
    if (readiness_result == WORR_NATIVE_READINESS_EXACT_DUPLICATE)
        saturating_increment(&peer->duplicate_client_ready_records);
    saturating_increment(&peer->server_active_records);
    *server_active_out = server_active;
    return SV_NATIVE_SHADOW_OBSERVE_SERVER_ACTIVE_READY;
}

static bool prune_pending_native(
    sv_native_shadow_peer_v1 *peer, uint64_t now_tick)
{
    worr_native_command_shadow_join_v1 join;
    worr_native_command_shadow_join_slot_v1 slot;
    worr_native_command_shadow_join_result_v1 result;
    uint32_t pruned;

    if (!peer_structural_valid(peer) || !peer->transport_initialized)
        return false;
    join = peer->transport.command_join;
    if (Worr_NativeCommandShadowJoinPruneV1(
            &join, now_tick, &pruned) !=
            WORR_NATIVE_COMMAND_SHADOW_JOIN_PRUNE_COMPLETE) {
        enter_drain(
            peer, SV_NATIVE_SHADOW_FAILURE_COMMAND_SHADOW, true);
        return false;
    }
    peer->transport.command_join = join;
    if (!peer->pending_native_valid)
        return true;

    result = Worr_NativeCommandShadowJoinFindV1(
        &join, peer->pending_native_id, &slot);
    if (result == WORR_NATIVE_COMMAND_SHADOW_JOIN_NOT_FOUND) {
        memset(&peer->pending_native_id, 0,
               sizeof(peer->pending_native_id));
        peer->pending_native_valid = 0;
        saturating_increment(&peer->join_expiries);
        enter_drain(
            peer, SV_NATIVE_SHADOW_FAILURE_COMMAND_SHADOW, false);
        return true;
    }
    if (result != WORR_NATIVE_COMMAND_SHADOW_JOIN_FOUND ||
        (slot.state_flags &
         WORR_NATIVE_COMMAND_SHADOW_JOIN_SLOT_NATIVE) == 0) {
        enter_drain(
            peer, SV_NATIVE_SHADOW_FAILURE_COMMAND_SHADOW, true);
        return false;
    }
    return true;
}

bool SV_NativeShadowObserveLegacyCommandV1(
    sv_native_shadow_peer_v1 *peer,
    const worr_command_record_v1 *record,
    uint32_t raw_time_ms)
{
    worr_native_command_shadow_join_v1 join;
    worr_native_command_shadow_compare_report_v1 report;
    worr_native_command_shadow_join_result_v1 result;
    uint64_t now_tick;

    if (!peer_structural_valid(peer) || !record)
        return false;
    if (!peer->transport_initialized ||
        (peer->lifecycle != SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE &&
         peer->lifecycle != SV_NATIVE_SHADOW_LIFECYCLE_DRAIN)) {
        return true;
    }
    if (!extend_clock(peer, raw_time_ms, &now_tick)) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_CLOCK);
        return false;
    }
    if (!prune_pending_native(peer, now_tick))
        return false;
    if (!peer->pending_native_valid ||
        record->command_id.epoch != peer->pending_native_id.epoch ||
        record->command_id.sequence !=
            peer->pending_native_id.sequence) {
        return true;
    }

    join = peer->transport.command_join;
    result = Worr_NativeCommandShadowJoinObserveV1(
        &join, WORR_NATIVE_COMMAND_SHADOW_JOIN_LEGACY,
        record, now_tick, &report);
    if (result !=
            WORR_NATIVE_COMMAND_SHADOW_JOIN_MATCHED_WATERMARK_UNVERIFIED &&
        result != WORR_NATIVE_COMMAND_SHADOW_JOIN_COMMAND_MISMATCH &&
        result !=
            WORR_NATIVE_COMMAND_SHADOW_JOIN_SAMPLE_OFFSET_MISMATCH &&
        result != WORR_NATIVE_COMMAND_SHADOW_JOIN_DUPLICATE_LEGACY) {
        enter_drain(
            peer, SV_NATIVE_SHADOW_FAILURE_COMMAND_SHADOW, true);
        return false;
    }
    peer->transport.command_join = join;
    memset(&peer->pending_native_id, 0,
           sizeof(peer->pending_native_id));
    peer->pending_native_valid = 0;
    saturating_increment(&peer->legacy_join_observations);
    if (result ==
        WORR_NATIVE_COMMAND_SHADOW_JOIN_MATCHED_WATERMARK_UNVERIFIED) {
        saturating_increment(&peer->command_matches);
    } else if (result ==
               WORR_NATIVE_COMMAND_SHADOW_JOIN_COMMAND_MISMATCH) {
        saturating_increment(&peer->command_mismatches);
        enter_drain(
            peer, SV_NATIVE_SHADOW_FAILURE_COMMAND_SHADOW, false);
    } else if (result ==
               WORR_NATIVE_COMMAND_SHADOW_JOIN_SAMPLE_OFFSET_MISMATCH) {
        saturating_increment(&peer->sample_offset_mismatches);
        enter_drain(
            peer, SV_NATIVE_SHADOW_FAILURE_COMMAND_SHADOW, false);
    }
    return true;
}

bool SV_NativeShadowReconcileCommandStreamV1(
    sv_native_shadow_peer_v1 *peer,
    const worr_command_stream_v1 *stream,
    uint32_t raw_time_ms)
{
    worr_command_record_v1 record;
    uint64_t now_tick;

    if (!peer_structural_valid(peer))
        return false;
    if (!peer->transport_initialized || !peer->pending_native_valid ||
        (peer->lifecycle != SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE &&
         peer->lifecycle != SV_NATIVE_SHADOW_LIFECYCLE_DRAIN)) {
        return true;
    }
    if (!extend_clock(peer, raw_time_ms, &now_tick)) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_CLOCK);
        return false;
    }
    if (stream != NULL && !Worr_CommandStreamValidateV1(stream)) {
        enter_drain(
            peer, SV_NATIVE_SHADOW_FAILURE_COMMAND_SHADOW, true);
        return false;
    }
    if (stream != NULL && Worr_CommandStreamCopyRecordV1(
            stream, peer->pending_native_id, &record)) {
        return SV_NativeShadowObserveLegacyCommandV1(
            peer, &record, raw_time_ms);
    }
    return prune_pending_native(peer, now_tick);
}

bool SV_NativeShadowAckDueV1(
    sv_native_shadow_peer_v1 *peer,
    uint32_t raw_time_ms)
{
    worr_native_readiness_state_v1 readiness;
    bool due;
    uint64_t now_tick;

    if (!native_shadow_hooks_exact(peer) ||
        peer->lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_WAIT_READINESS ||
        peer->lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_DETACHED) {
        return false;
    }
    if (!extend_clock(peer, raw_time_ms, &now_tick)) {
        SV_NativeShadowPeerDisableV1(
            peer, SV_NATIVE_SHADOW_FAILURE_CLOCK);
        return false;
    }
    if (peer->lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE) {
        readiness = peer->readiness;
        if (!Worr_NativeReadinessCanTransmitNativeV1(
                &readiness, now_tick)) {
            SV_NativeShadowPeerDisableV1(
                peer, SV_NATIVE_SHADOW_FAILURE_READINESS);
            return false;
        }
        peer->readiness = readiness;
    }
    if (!ack_due_at_tick(peer, now_tick, &due)) {
        enter_drain(peer, SV_NATIVE_SHADOW_FAILURE_ACK, true);
        return false;
    }
    return due;
}

bool SV_NativeShadowAckEligiblePeekV1(
    const sv_native_shadow_peer_v1 *peer,
    uint32_t raw_time_ms)
{
    worr_native_readiness_state_v1 readiness;
    bool due;
    uint64_t now_tick;

    if (!native_shadow_hooks_exact(peer) ||
        peer->lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_WAIT_READINESS ||
        peer->lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_DETACHED ||
        !project_clock(peer, raw_time_ms, &now_tick)) {
        return false;
    }
    if (peer->lifecycle == SV_NATIVE_SHADOW_LIFECYCLE_ACTIVE) {
        readiness = peer->readiness;
        if (!Worr_NativeReadinessStateValidateV1(&readiness) ||
            !Worr_NativeReadinessCanTransmitNativeV1(
                &readiness, now_tick)) {
            return false;
        }
    }
    return ack_due_at_tick(peer, now_tick, &due) && due;
}

void SV_NativeShadowRecordAsyncRateDeferralV1(
    sv_native_shadow_peer_v1 *peer)
{
    if (peer_structural_valid(peer))
        saturating_increment(&peer->async_rate_deferrals);
}

void SV_NativeShadowRecordAsyncFragmentDeferralV1(
    sv_native_shadow_peer_v1 *peer)
{
    if (peer_structural_valid(peer))
        saturating_increment(&peer->async_fragment_deferrals);
}

bool SV_NativeShadowBeginAsyncWakeV1(
    sv_native_shadow_peer_v1 *peer)
{
    if (!peer_structural_valid(peer) || peer->async_wake_active)
        return false;
    saturating_increment(&peer->async_wake_attempts);
    peer->async_wake_active = 1;
    peer->async_wake_handoff_seen = 0;
    return true;
}

void SV_NativeShadowEndAsyncWakeV1(
    sv_native_shadow_peer_v1 *peer)
{
    if (!peer_structural_valid(peer) || !peer->async_wake_active)
        return;
    if (peer->async_wake_handoff_seen)
        saturating_increment(&peer->async_ack_handoffs);
    else
        saturating_increment(&peer->async_wake_no_handoff);
    peer->async_wake_active = 0;
    peer->async_wake_handoff_seen = 0;
}

bool SV_NativeShadowGetStatusV1(
    const sv_native_shadow_peer_v1 *peer,
    uint32_t raw_time_ms,
    sv_native_shadow_status_v1 *status_out)
{
    sv_native_shadow_status_v1 status;

    if (!peer_structural_valid(peer) || status_out == NULL)
        return false;
    memset(&status, 0, sizeof(status));
    status.struct_size = sizeof(status);
    status.schema_version = SV_NATIVE_SHADOW_STATUS_VERSION;
    status.enabled = peer->enabled;
    status.lifecycle = peer->lifecycle;
    status.hooks_attached = native_shadow_hooks_exact(peer) ? 1u : 0u;
    status.readiness_phase = peer->readiness_initialized
        ? peer->readiness.phase : WORR_NATIVE_READINESS_PHASE_RESET;
    status.official_connection_epoch = peer->official_connection_epoch;
    status.transport_epoch = peer->private_transport_epoch;
    status.public_capabilities = peer->official_connection_epoch != 0
        ? WORR_NET_CAP_LEGACY_STAGE_MASK : 0;
    status.private_capabilities = peer->readiness_initialized
        ? peer->readiness.negotiated_capabilities
        : (peer->transport_initialized
               ? peer->transport.binding.negotiated_capabilities : 0);
    status.wire_committed = peer->native_wire_committed;
    status.ack_eligible =
        SV_NativeShadowAckEligiblePeekV1(peer, raw_time_ms) ? 1u : 0u;
    status.challenges_queued = peer->challenges_queued;
    status.client_ready_records = peer->client_ready_records;
    status.server_active_records = peer->server_active_records;
    status.rx_carriers = peer->rx_carriers;
    status.rx_commits = peer->rx_commits;
    status.rx_repeat_refreshes = peer->rx_repeat_refreshes;
    status.legacy_join_observations = peer->legacy_join_observations;
    status.command_matches = peer->command_matches;
    status.command_mismatches = peer->command_mismatches;
    status.sample_offset_mismatches = peer->sample_offset_mismatches;
    status.tx_ack_prepares = peer->tx_ack_prepares;
    status.tx_ack_handoffs = peer->tx_ack_handoffs;
    status.async_rate_deferrals = peer->async_rate_deferrals;
    status.async_fragment_deferrals = peer->async_fragment_deferrals;
    status.async_wake_attempts = peer->async_wake_attempts;
    status.async_ack_handoffs = peer->async_ack_handoffs;
    status.async_wake_no_handoff = peer->async_wake_no_handoff;
    status.rx_rejections = peer->rx_rejections;
    status.tx_ack_rejections = peer->tx_ack_rejections;
    status.rx_drained = peer->rx_drained;
    status.drain_entries = peer->drain_entries;
    status.failures = peer->failures;
    status.last_failure = peer->last_failure;
    *status_out = status;
    return true;
}

bool SV_NativeShadowObserveInterveningServiceV1(
    sv_native_shadow_peer_v1 *peer)
{
    worr_native_readiness_sideband_result_v1 result;

    if (!SV_NativeShadowPeerEnabledV1(peer) ||
        peer->readiness_initialized == 0 || peer->packet_open == 0)
        return false;
    result = Worr_NativeReadinessSidebandObserveInterveningServiceV1(
        &peer->sideband);
    if (result == WORR_NATIVE_READINESS_SIDEBAND_NOT_SIDEBAND)
        return true;
    SV_NativeShadowPeerDisableV1(
        peer, SV_NATIVE_SHADOW_FAILURE_SIDEBAND);
    return false;
}

bool SV_NativeShadowPacketEndV1(sv_native_shadow_peer_v1 *peer)
{
    worr_native_readiness_sideband_result_v1 result;

    if (!SV_NativeShadowPeerEnabledV1(peer) ||
        peer->readiness_initialized == 0 || peer->packet_open == 0)
        return false;
    result = Worr_NativeReadinessSidebandPacketEndV1(&peer->sideband);
    peer->packet_open = 0;
    if (result == WORR_NATIVE_READINESS_SIDEBAND_PACKET_ENDED)
        return true;
    SV_NativeShadowPeerDisableV1(
        peer, SV_NATIVE_SHADOW_FAILURE_SIDEBAND);
    return false;
}
