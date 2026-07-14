/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "client.h"
#include "client/native_readiness_pilot.h"

#include "common/net/native_carrier.h"
#include "common/net/native_carrier_session.h"
#include "common/net/native_command_shadow.h"
#include "common/net/native_readiness.h"
#include "common/net/native_readiness_sideband.h"
#include "common/net/native_session.h"
#include "common/q2proto_shared.h"
#include "q2proto/q2proto.h"

#include <array>
#include <cstdint>
#include <cstring>

namespace {

constexpr uint32_t kPrivateCapabilities =
    WORR_NET_CAP_LEGACY_STAGE_MASK | WORR_NET_CAP_NATIVE_ENVELOPE_V1;
/* The server starts CHALLENGE after bootstrap.  This timeout covers the
 * bounded private readiness exchange only; authoritative legacy continues
 * independently if the default-off pilot fails closed. */
constexpr uint64_t kReadinessTimeoutMilliseconds = UINT64_C(10000);
constexpr uint32_t kResendIntervalMilliseconds = UINT32_C(100);
constexpr uint16_t kTxSlotCapacity = 1;
constexpr uint8_t kProofPriority = 0;
constexpr uint16_t kCommandPayloadBytes =
    WORR_NATIVE_COMMAND_SHADOW_ENCODED_BYTES;
constexpr uint16_t kCommandDatagramBytes =
    WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES + kCommandPayloadBytes;
constexpr uint16_t kCommandCarrierOverhead =
    WORR_NATIVE_CARRIER_WIRE_ENTRY_HEADER_BYTES +
    kCommandDatagramBytes + WORR_NATIVE_CARRIER_WIRE_FOOTER_BYTES;
constexpr size_t kEncodedClientReadyBytes =
    WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT * 5u;
constexpr size_t kScratchBytes = 128u;

static_assert(WORR_NET_CAP_LEGACY_STAGE_MASK == UINT32_C(3));
static_assert(kPrivateCapabilities == UINT32_C(0x13));
static_assert(kEncodedClientReadyBytes == 65u);
static_assert(kCommandPayloadBytes == 110u);
static_assert(kCommandDatagramBytes == 166u);
static_assert(kCommandCarrierOverhead == 206u);
static_assert(818u + kCommandCarrierOverhead == 1024u);
static_assert(819u + kCommandCarrierOverhead > 1024u);
static_assert((CMD_BACKUP & (CMD_BACKUP - 1u)) == 0u);

enum class pilot_mode_t : uint32_t {
    arming = 1,
    active = 2,
    drain = 3,
};

struct checked_tick_extender_t {
    bool initialized{};
    uint32_t last_raw{};
    uint64_t extended{};
};

struct command_ring_entry_t {
    bool valid{};
    uint32_t legacy_command_number{};
    worr_command_record_v1 record{};
};

struct client_native_readiness_telemetry_t {
    uint64_t challenges{};
    uint64_t client_ready_queued{};
    uint64_t server_active{};
    uint64_t proof_enqueued{};
    uint64_t retained_highwater{};
    uint64_t retained_releases{};
    uint64_t tx_first_sends{};
    uint64_t tx_retries{};
    uint64_t tx_handoffs{};
    uint64_t ack_carriers{};
    uint64_t acknowledged_reliable{};
    uint64_t drains{};
    uint64_t failures{};
    uint32_t last_failure{};
};

struct client_native_readiness_pilot_t {
    bool enabled{};
    bool tx_hook_registered{};
    bool rx_hook_registered{};
    bool capability_confirmed{};
    bool map_quiesced{};
    bool readiness_initialized{};
    bool session_initialized{};
    bool retired_session_initialized{};
    bool builder_initialized{};
    bool proof_enqueued_once{};
    bool readiness_committed{};
    bool carrier_traffic_seen{};
    pilot_mode_t mode{pilot_mode_t::arming};
    netchan_t *channel{};
    uint64_t connection_owner_id{};
    checked_tick_extender_t clock{};
    worr_native_readiness_sideband_parser_v1 parser{};
    worr_native_readiness_state_v1 readiness{};
    worr_native_session_binding_v1 binding{};
    worr_native_tx_session_v1 tx{};
    std::array<worr_native_tx_slot_v1, kTxSlotCapacity> tx_slots{};
    worr_native_carrier_tx_gate_v1 tx_gate{};
    worr_native_carrier_dispatch_v1 dispatch{};
    worr_native_command_shadow_payload_registry_v1 payload_registry{};
    worr_native_tx_session_v1 retired_tx{};
    std::array<worr_native_tx_slot_v1, kTxSlotCapacity> retired_tx_slots{};
    worr_native_command_shadow_payload_registry_v1 retired_payload_registry{};
    worr_native_command_shadow_builder_v1 builder{};
    std::array<command_ring_entry_t, CMD_BACKUP> command_ring{};
};

cvar_t *cl_worr_native_shadow{};
cvar_t *cl_worr_native_shadow_probe_hold{};
client_native_readiness_pilot_t pilot{};
client_native_readiness_telemetry_t telemetry{};
uint64_t next_connection_owner_id{1};

netchan_app_tx_prepare_result_t pilot_tx_prepare(
    void *, const netchan_app_tx_prepare_info_v1_t *, const byte *, byte *,
    netchan_app_tx_prepare_output_v1_t *);
void pilot_tx_completion(void *,
                         const netchan_app_tx_completion_info_v1_t *,
                         const byte *);
netchan_app_rx_result_t pilot_rx(
    void *, const netchan_app_rx_info_v1_t *, const byte *,
    netchan_app_rx_output_v1_t *);

bool extend_tick(checked_tick_extender_t &clock, uint32_t raw,
                 uint64_t &extended_out)
{
    if (!clock.initialized) {
        clock.initialized = true;
        clock.last_raw = raw;
        clock.extended = raw;
        extended_out = clock.extended;
        return true;
    }

    const uint32_t delta = raw - clock.last_raw;
    /* The half-range rule distinguishes a genuine 32-bit wrap from a clock
     * regression without guessing how many wraps elapsed. */
    if (delta > UINT32_MAX / 2u ||
        clock.extended > UINT64_MAX - delta) {
        return false;
    }

    clock.last_raw = raw;
    clock.extended += delta;
    extended_out = clock.extended;
    return true;
}

bool current_tick(uint64_t &tick_out)
{
    return extend_tick(pilot.clock, static_cast<uint32_t>(com_localTime),
                       tick_out);
}

bool allocate_connection_owner(uint64_t &owner_out)
{
    if (next_connection_owner_id == 0)
        return false;
    owner_out = next_connection_owner_id;
    if (next_connection_owner_id == UINT64_MAX)
        next_connection_owner_id = 0;
    else
        ++next_connection_owner_id;
    return true;
}

void counter_increment(uint64_t &counter)
{
    if (counter != UINT64_MAX)
        ++counter;
}

void disable_pilot()
{
    netchan_t *const channel = pilot.channel;
    if (channel && pilot.rx_hook_registered &&
        channel->app_rx == pilot_rx &&
        channel->app_rx_opaque == &pilot) {
        (void)Netchan_SetApplicationRxHook(channel, nullptr, nullptr);
    }
    if (channel && pilot.tx_hook_registered &&
        channel->app_tx_prepare == pilot_tx_prepare &&
        channel->app_tx_completion == pilot_tx_completion &&
        channel->app_tx_opaque == &pilot) {
        (void)Netchan_SetApplicationTxHook(
            channel, nullptr, nullptr, nullptr);
    }
    pilot = {};
}

bool hooks_attached_exact()
{
    return pilot.enabled && !cls.demo.playback && !cls.demo.seeking &&
           pilot.channel && pilot.channel == &cls.netchan &&
           pilot.channel->type == NETCHAN_NEW &&
           pilot.tx_hook_registered && pilot.rx_hook_registered &&
           pilot.channel->app_tx_prepare == pilot_tx_prepare &&
           pilot.channel->app_tx_completion == pilot_tx_completion &&
           pilot.channel->app_tx_opaque == &pilot &&
           pilot.channel->app_rx == pilot_rx &&
           pilot.channel->app_rx_opaque == &pilot;
}

bool live_pilot()
{
    if (!pilot.enabled)
        return false;
    if (cls.demo.playback || cls.demo.seeking) {
        /* Native carrier traffic is deliberately absent from demo paths. */
        disable_pilot();
        return false;
    }
    if (!hooks_attached_exact()) {
        disable_pilot();
        return false;
    }
    return true;
}

void enter_drain(bool diagnostic_failure = true)
{
    if (!pilot.enabled)
        return;
    if (diagnostic_failure && pilot.mode != pilot_mode_t::drain)
        counter_increment(telemetry.drains);
    pilot.mode = pilot_mode_t::drain;

    /* No callback is re-entrant on this netchan.  A non-pending dispatch can
     * therefore be abandoned immediately; a pending accepted outcome stays
     * frozen and DRAIN will never select further DATA. */
    if (pilot.session_initialized &&
        (pilot.tx_gate.state_flags &
         WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) != 0 &&
        (pilot.tx_gate.state_flags &
         WORR_NATIVE_CARRIER_TX_GATE_PACKET_PENDING) == 0) {
        (void)Worr_NativeCarrierSessionDispatchAbortV1(
            &pilot.tx_gate, &pilot.dispatch);
    }
}

void pilot_failure()
{
    counter_increment(telemetry.failures);
    telemetry.last_failure = 1;
    if (pilot.readiness_committed || pilot.carrier_traffic_seen)
        enter_drain(true);
    else
        disable_pilot();
}

bool capability_is_exact_legacy_confirmation(
    const worr_net_capability_state_v1 *state)
{
    return state && Worr_NetCapabilityStateValidateV1(state) &&
           state->phase == WORR_NET_CAPABILITY_CONFIRMED &&
           state->offered == WORR_NET_CAP_LEGACY_STAGE_MASK &&
           state->supported == WORR_NET_CAP_LEGACY_STAGE_MASK &&
           state->peer_supported == WORR_NET_CAP_LEGACY_STAGE_MASK &&
           state->negotiated == WORR_NET_CAP_LEGACY_STAGE_MASK;
}

bool queue_client_ready(
    const worr_native_readiness_record_v1 &client_ready)
{
    std::array<worr_native_readiness_setting_pair_v1,
               WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT> pairs{};
    if (!Worr_NativeReadinessSidebandEncodeV1(
            &client_ready, pairs.data(),
            static_cast<uint32_t>(pairs.size()))) {
        return false;
    }

    std::array<byte, kScratchBytes> bytes{};
    sizebuf_t scratch{};
    SZ_InitWrite(&scratch, bytes.data(), bytes.size());
    q2protoio_ioarg_t io{};
    io.sz_write = &scratch;
    io.max_msg_len = scratch.maxsize;

    q2proto_clc_message_t message{};
    message.type = Q2P_CLC_SETTING;
    for (const auto &pair : pairs) {
        message.setting.index = pair.index;
        message.setting.value = pair.value;
        if (q2proto_client_write(
                &cls.q2proto_ctx, reinterpret_cast<uintptr_t>(&io),
                &message) != Q2P_ERR_SUCCESS) {
            return false;
        }
    }
    if (scratch.overflowed || scratch.cursize != kEncodedClientReadyBytes)
        return false;

    netchan_t *const channel = pilot.channel;
    if (!channel || channel != &cls.netchan || !channel->message.data ||
        channel->message.overflowed ||
        channel->message.cursize > channel->message.maxsize ||
        scratch.cursize >
            channel->message.maxsize - channel->message.cursize) {
        return false;
    }

    /* One preflighted append is the atomic reliable-queue commit. */
    SZ_Write(&channel->message, scratch.data, scratch.cursize);
    pilot.readiness_committed = true;
    counter_increment(telemetry.client_ready_queued);
    return true;
}

bool observe_challenge(
    const worr_native_readiness_record_v1 &challenge, uint64_t now)
{
    const bool starts_fresh_map_epoch = pilot.map_quiesced;
    worr_native_readiness_state_v1 next{};
    if (!pilot.readiness_initialized) {
        if (Worr_NativeReadinessClientInitV1(
                &next, challenge.transport_epoch, kPrivateCapabilities,
                now, kReadinessTimeoutMilliseconds) !=
            WORR_NATIVE_READINESS_OK) {
            return false;
        }
    } else {
        next = pilot.readiness;
        if (pilot.map_quiesced) {
            if (Worr_NativeReadinessClientAdvanceEpochV1(
                    &next, challenge.transport_epoch,
                    kPrivateCapabilities, now,
                    kReadinessTimeoutMilliseconds) !=
                WORR_NATIVE_READINESS_OK) {
                return false;
            }
        } else if (challenge.transport_epoch != next.transport_epoch) {
            return false;
        }
    }

    worr_native_readiness_record_v1 client_ready{};
    const worr_native_readiness_result_v1 result =
        Worr_NativeReadinessClientObserveChallengeV1(
            &next, &challenge, now, &client_ready);
    if (result != WORR_NATIVE_READINESS_OK &&
        result != WORR_NATIVE_READINESS_EXACT_DUPLICATE) {
        return false;
    }
    if (!queue_client_ready(client_ready))
        return false;

    if (result == WORR_NATIVE_READINESS_OK)
        counter_increment(telemetry.challenges);

    pilot.readiness = next;
    pilot.readiness_initialized = true;
    pilot.map_quiesced = false;
    if (starts_fresh_map_epoch)
        pilot.mode = pilot_mode_t::arming;
    return true;
}

bool activate_native_session(
    const worr_native_readiness_state_v1 &readiness)
{
    worr_native_session_binding_v1 binding{};
    worr_native_tx_session_v1 next_tx{};
    std::array<worr_native_tx_slot_v1, kTxSlotCapacity> next_slots{};
    worr_native_carrier_tx_gate_v1 next_gate{};
    worr_native_command_shadow_payload_registry_v1 next_registry{};

    if (!Worr_NativeSessionBindingInitFromReadinessV1(
            &binding, &readiness, pilot.connection_owner_id) ||
        binding.negotiated_capabilities != kPrivateCapabilities ||
        !Worr_NativeCommandShadowPayloadRegistryInitV1(
            &next_registry, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS)) {
        return false;
    }

    if (!pilot.session_initialized) {
        if (!Worr_NativeTxSessionInitV1(
                &next_tx, next_slots.data(), kTxSlotCapacity, &binding) ||
            !Worr_NativeCarrierTxGateInitV1(&next_gate, &binding)) {
            return false;
        }
    } else {
        if (!Worr_NativeCommandShadowPayloadRegistryValidateV1(
                &pilot.payload_registry) ||
            pilot.payload_registry.occupied_count !=
                pilot.tx.retained_count) {
            return false;
        }
        next_tx = pilot.tx;
        next_slots = pilot.tx_slots;
        next_gate = pilot.tx_gate;
        if (!Worr_NativeTxSessionAdvanceEpochV1(
                &next_tx, next_slots.data(), kTxSlotCapacity, &binding) ||
            !Worr_NativeCarrierTxGateAdvanceEpochV1(
                &next_gate, &binding)) {
            return false;
        }
    }

    if (pilot.session_initialized) {
        /* Preserve exactly the immediately preceding transport bank so an
         * ACK already in flight may release its retained command handle after
         * the new epoch becomes active.  A later activation replaces it. */
        pilot.retired_tx = pilot.tx;
        pilot.retired_tx_slots = pilot.tx_slots;
        pilot.retired_payload_registry = pilot.payload_registry;
        pilot.retired_session_initialized = true;
    }
    pilot.binding = binding;
    pilot.tx = next_tx;
    pilot.tx_slots = next_slots;
    pilot.tx_gate = next_gate;
    pilot.dispatch = {};
    pilot.payload_registry = next_registry;
    pilot.session_initialized = true;
    pilot.proof_enqueued_once = false;
    pilot.mode = pilot_mode_t::active;
    return true;
}

bool observe_server_active(
    const worr_native_readiness_record_v1 &server_active, uint64_t now)
{
    /* DRAIN is fail-closed within one map epoch.  Only a successfully
     * validated fresh challenge after map quiesce may move it back to ARMING;
     * a later ACTIVE record from the failed epoch cannot resurrect DATA. */
    if (!pilot.readiness_initialized || pilot.mode == pilot_mode_t::drain)
        return false;
    worr_native_readiness_state_v1 next = pilot.readiness;
    const worr_native_readiness_result_v1 result =
        Worr_NativeReadinessClientObserveServerActiveV1(
            &next, &server_active, now);
    if (result != WORR_NATIVE_READINESS_OK &&
        result != WORR_NATIVE_READINESS_EXACT_DUPLICATE) {
        return false;
    }

    /* An old SERVER_ACTIVE duplicate must never resurrect a map-quiesced
     * epoch.  Only the state machine's fresh transition installs a session. */
    if (result == WORR_NATIVE_READINESS_OK &&
        !activate_native_session(next)) {
        return false;
    }
    if (result == WORR_NATIVE_READINESS_OK)
        counter_increment(telemetry.server_active);
    pilot.readiness = next;
    return true;
}

bool observe_record(const worr_native_readiness_record_v1 &record)
{
    if (!pilot.capability_confirmed ||
        record.negotiated_capabilities != kPrivateCapabilities) {
        return false;
    }

    uint64_t now;
    if (!current_tick(now))
        return false;
    switch (record.record_kind) {
    case WORR_NATIVE_READINESS_RECORD_CHALLENGE:
        return observe_challenge(record, now);
    case WORR_NATIVE_READINESS_RECORD_SERVER_ACTIVE:
        return observe_server_active(record, now);
    default:
        return false;
    }
}

bool accepted_observe_result(
    worr_native_readiness_sideband_result_v1 result)
{
    return result == WORR_NATIVE_READINESS_SIDEBAND_NOT_SIDEBAND ||
           result == WORR_NATIVE_READINESS_SIDEBAND_FIELD_ACCEPTED;
}

worr_native_record_ref_v1 command_record_ref(
    worr_command_id_v1 command_id)
{
    worr_native_record_ref_v1 ref{};
    ref.record_class = WORR_NATIVE_RECORD_COMMAND_V1;
    ref.record_schema_version = WORR_COMMAND_ABI_VERSION;
    ref.object_epoch = command_id.epoch;
    ref.object_sequence = command_id.sequence;
    return ref;
}

void abort_dispatch_if_active()
{
    if ((pilot.tx_gate.state_flags &
         WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) == 0) {
        return;
    }
    if ((pilot.tx_gate.state_flags &
         WORR_NATIVE_CARRIER_TX_GATE_PACKET_PENDING) != 0) {
        if (Worr_NativeCarrierSessionDispatchRejectPacketV1(
                &pilot.tx_gate, &pilot.dispatch) !=
            WORR_NATIVE_CARRIER_SESSION_OK) {
            return;
        }
    }
    (void)Worr_NativeCarrierSessionDispatchAbortV1(
        &pilot.tx_gate, &pilot.dispatch);
}

netchan_app_tx_prepare_result_t pilot_tx_prepare(
    void *opaque, const netchan_app_tx_prepare_info_v1_t *info,
    const byte *legacy_application, byte *candidate_application,
    netchan_app_tx_prepare_output_v1_t *output)
{
    if (opaque != &pilot || !live_pilot())
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    if (pilot.mode != pilot_mode_t::active ||
        !pilot.session_initialized || !pilot.proof_enqueued_once ||
        pilot.tx.retained_count == 0) {
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }
    if (!info || !output || !candidate_application ||
        info->abi_version != NETCHAN_APP_TX_HOOK_ABI_V1 ||
        info->struct_size != sizeof(*info) ||
        info->reliable_bytes > UINT32_MAX - info->unreliable_bytes ||
        info->reliable_bytes + info->unreliable_bytes !=
            info->legacy_application_bytes ||
        info->legacy_application_bytes > info->max_application_bytes ||
        info->legacy_application_bytes > UINT16_MAX ||
        (info->legacy_application_bytes != 0 && !legacy_application)) {
        pilot_failure();
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }

    const uint16_t application_budget = static_cast<uint16_t>(
        info->max_application_bytes > WORR_NATIVE_CARRIER_MAX_PACKET_BYTES
            ? WORR_NATIVE_CARRIER_MAX_PACKET_BYTES
            : info->max_application_bytes);
    const uint16_t legacy_bytes =
        static_cast<uint16_t>(info->legacy_application_bytes);
    if (application_budget == 0 || legacy_bytes > application_budget)
        return NETCHAN_APP_TX_PREPARE_BYPASS;

    uint64_t now;
    if (!current_tick(now) ||
        !Worr_NativeReadinessCanTransmitNativeV1(
            &pilot.readiness, now)) {
        pilot_failure();
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }

    if ((pilot.tx_gate.state_flags &
         WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) != 0) {
        pilot_failure();
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }

    const worr_native_carrier_session_result_v1 begun =
        Worr_NativeCarrierSessionDispatchBeginV1(
            &pilot.tx_gate, &pilot.tx, pilot.tx_slots.data(),
            kTxSlotCapacity, now, kResendIntervalMilliseconds,
            application_budget, legacy_bytes, &pilot.dispatch);
    if (begun == WORR_NATIVE_CARRIER_SESSION_NOT_DUE ||
        begun == WORR_NATIVE_CARRIER_SESSION_LIMIT) {
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }
    if (begun != WORR_NATIVE_CARRIER_SESSION_OK) {
        pilot_failure();
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }

    const uint32_t handle =
        pilot.dispatch.send_ticket.pre_send_slot.payload_handle;
    std::array<byte, kCommandPayloadBytes> payload{};
    size_t payload_bytes = 0;
    worr_command_id_v1 payload_command_id{};
    if (Worr_NativeCommandShadowPayloadCopyV1(
            &pilot.payload_registry, handle, payload.data(),
            payload.size(), &payload_bytes, &payload_command_id) !=
            WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_COPIED ||
        payload_bytes != kCommandPayloadBytes ||
        payload_command_id.epoch !=
            pilot.dispatch.send_ticket.pre_send_slot.record.object_epoch ||
        payload_command_id.sequence !=
            pilot.dispatch.send_ticket.pre_send_slot.record.object_sequence ||
        Worr_NativeCarrierSessionDispatchBindPayloadV1(
            &pilot.tx_gate, &pilot.dispatch, handle, payload.data(),
            static_cast<uint32_t>(payload_bytes)) !=
            WORR_NATIVE_CARRIER_SESSION_OK) {
        abort_dispatch_if_active();
        pilot_failure();
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }

    size_t packet_bytes = 0;
    const worr_native_carrier_session_result_v1 prepared =
        Worr_NativeCarrierSessionDispatchPreparePacketV1(
            &pilot.tx_gate, &pilot.tx, pilot.tx_slots.data(),
            kTxSlotCapacity, &pilot.dispatch, handle, payload.data(),
            static_cast<uint32_t>(payload_bytes), legacy_application,
            legacy_bytes, candidate_application,
            info->max_application_bytes, &packet_bytes);
    if (prepared != WORR_NATIVE_CARRIER_SESSION_OK) {
        abort_dispatch_if_active();
        if (prepared != WORR_NATIVE_CARRIER_SESSION_LIMIT &&
            prepared != WORR_NATIVE_CARRIER_SESSION_OUTPUT_TOO_SMALL) {
            pilot_failure();
        }
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }
    if (packet_bytes !=
            static_cast<size_t>(legacy_bytes) + kCommandCarrierOverhead ||
        packet_bytes > UINT32_MAX) {
        abort_dispatch_if_active();
        pilot_failure();
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    }

    output->abi_version = NETCHAN_APP_TX_HOOK_ABI_V1;
    output->struct_size = sizeof(*output);
    output->application_bytes = static_cast<uint32_t>(packet_bytes);
    output->reserved0 = 0;
    output->token = pilot.dispatch.token_id;
    return NETCHAN_APP_TX_PREPARE_PREPARED;
}

void pilot_tx_completion(
    void *opaque, const netchan_app_tx_completion_info_v1_t *info,
    const byte *application)
{
    if (opaque != &pilot || !live_pilot())
        return;
    if (!info || info->abi_version != NETCHAN_APP_TX_HOOK_ABI_V1 ||
        info->struct_size != sizeof(*info) ||
        info->token == 0 || info->token != pilot.dispatch.token_id ||
        (info->application_bytes != 0 && !application) ||
        (pilot.tx_gate.state_flags &
         WORR_NATIVE_CARRIER_TX_GATE_PACKET_PENDING) == 0) {
        pilot_failure();
        return;
    }

    if (info->result == NETCHAN_APP_TX_COMPLETION_ACCEPTED) {
        pilot.carrier_traffic_seen = true;
        const bool retry =
            pilot.dispatch.send_ticket.pre_send_slot.send_attempts != 0;
        uint64_t now;
        if (!current_tick(now) ||
            Worr_NativeCarrierSessionDispatchConfirmPacketV1(
                &pilot.tx_gate, &pilot.tx, pilot.tx_slots.data(),
                kTxSlotCapacity, &pilot.dispatch, now, application,
                info->application_bytes) !=
                WORR_NATIVE_CARRIER_SESSION_DISPATCH_COMMITTED) {
            enter_drain(true);
        } else {
            counter_increment(telemetry.tx_handoffs);
            counter_increment(retry ? telemetry.tx_retries
                                    : telemetry.tx_first_sends);
        }
        return;
    }

    if (info->result == NETCHAN_APP_TX_COMPLETION_NOT_ACCEPTED ||
        info->result == NETCHAN_APP_TX_COMPLETION_PREPARE_INVALID) {
        const bool rejected =
            Worr_NativeCarrierSessionDispatchRejectPacketV1(
                &pilot.tx_gate, &pilot.dispatch) ==
            WORR_NATIVE_CARRIER_SESSION_OK;
        const bool aborted = rejected &&
            Worr_NativeCarrierSessionDispatchAbortV1(
                &pilot.tx_gate, &pilot.dispatch) ==
                WORR_NATIVE_CARRIER_SESSION_OK;
        if (!aborted)
            pilot_failure();
        return;
    }

    pilot_failure();
}

bool apply_ack_to_bank(
    worr_native_tx_session_v1 &tx,
    std::array<worr_native_tx_slot_v1, kTxSlotCapacity> &slots,
    worr_native_command_shadow_payload_registry_v1 &registry,
    const byte *application, uint32_t application_bytes,
    uint32_t &acknowledged_out)
{
    worr_native_tx_session_v1 next_tx = tx;
    auto next_slots = slots;
    auto next_registry = registry;
    const bool retained_before = next_slots[0].state_flags != 0;
    const uint32_t retained_handle =
        retained_before ? next_slots[0].payload_handle : 0;
    uint32_t acknowledged = 0;
    if (Worr_NativeCarrierSessionApplyAcksV1(
            &next_tx, next_slots.data(), kTxSlotCapacity,
            application, application_bytes, &acknowledged) !=
            WORR_NATIVE_CARRIER_SESSION_OK ||
        acknowledged > 1 ||
        (acknowledged == 1 &&
         (!retained_before || next_slots[0].state_flags != 0)) ||
        (acknowledged == 0 && retained_before !=
            (next_slots[0].state_flags != 0))) {
        return false;
    }
    if (acknowledged == 1 &&
        Worr_NativeCommandShadowPayloadReleaseV1(
            &next_registry, retained_handle) !=
            WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_RELEASED) {
        return false;
    }
    if (!Worr_NativeTxSessionValidateV1(
            &next_tx, next_slots.data(), kTxSlotCapacity) ||
        !Worr_NativeCommandShadowPayloadRegistryValidateV1(
            &next_registry) ||
        next_registry.occupied_count != next_tx.retained_count) {
        return false;
    }
    tx = next_tx;
    slots = next_slots;
    registry = next_registry;
    acknowledged_out = acknowledged;
    return true;
}

netchan_app_rx_result_t pilot_rx(
    void *opaque, const netchan_app_rx_info_v1_t *info,
    const byte *application, netchan_app_rx_output_v1_t *output)
{
    if (opaque != &pilot || !live_pilot())
        return NETCHAN_APP_RX_BYPASS;
    if (!info || !output || !application ||
        info->abi_version != NETCHAN_APP_RX_HOOK_ABI_V1 ||
        info->struct_size != sizeof(*info)) {
        pilot_failure();
        return NETCHAN_APP_RX_REJECT;
    }

    worr_native_carrier_view_v1 view{};
    const worr_native_carrier_result_v1 decoded =
        Worr_NativeCarrierDecodeV1(
            application, info->application_bytes, &view);
    if (decoded == WORR_NATIVE_CARRIER_NO_CARRIER)
        return NETCHAN_APP_RX_BYPASS;

    /* A terminal WTC marker makes this native traffic even when corrupt. */
    pilot.carrier_traffic_seen = true;
    if (decoded != WORR_NATIVE_CARRIER_OK ||
        !pilot.session_initialized ||
        (pilot.mode != pilot_mode_t::active &&
         pilot.mode != pilot_mode_t::drain) ||
        view.entry_count != 1 ||
        view.entries[0].entry_type !=
            WORR_NATIVE_CARRIER_ENTRY_ACK_V1) {
        enter_drain(true);
        return NETCHAN_APP_RX_REJECT;
    }

    if (pilot.mode == pilot_mode_t::active) {
        uint64_t now;
        if (!current_tick(now) ||
            !Worr_NativeReadinessCanReceiveNativeV1(
                &pilot.readiness, now)) {
            enter_drain(true);
            return NETCHAN_APP_RX_REJECT;
        }
    }

    const bool current_epoch =
        view.transport_epoch == pilot.tx.transport_epoch;
    const bool retired_epoch = pilot.retired_session_initialized &&
        view.transport_epoch == pilot.retired_tx.transport_epoch;
    if (current_epoch == retired_epoch) {
        enter_drain(true);
        return NETCHAN_APP_RX_REJECT;
    }
    uint32_t acknowledged = 0;
    const bool applied = current_epoch
        ? apply_ack_to_bank(
              pilot.tx, pilot.tx_slots, pilot.payload_registry,
              application, info->application_bytes, acknowledged)
        : apply_ack_to_bank(
              pilot.retired_tx, pilot.retired_tx_slots,
              pilot.retired_payload_registry, application,
              info->application_bytes, acknowledged);
    if (!applied) {
        enter_drain(true);
        return NETCHAN_APP_RX_REJECT;
    }
    counter_increment(telemetry.ack_carriers);
    if (acknowledged != 0) {
        counter_increment(telemetry.retained_releases);
        counter_increment(telemetry.acknowledged_reliable);
    }
    output->abi_version = NETCHAN_APP_RX_HOOK_ABI_V1;
    output->struct_size = sizeof(*output);
    output->legacy_bytes = view.legacy_bytes;
    output->reserved0 = 0;
    return NETCHAN_APP_RX_EXPOSE_LEGACY;
}

} // namespace

extern "C" void CL_NativeReadinessPilotRegisterCvar(void)
{
    cl_worr_native_shadow = Cvar_Get("cl_worr_native_shadow", "0", 0);
    cl_worr_native_shadow_probe_hold = Cvar_Get(
        "cl_worr_native_shadow_probe_hold", "0", CVAR_NOARCHIVE);
}

extern "C" bool CL_NativeReadinessPilotBeginConnection(netchan_t *channel)
{
    disable_pilot();
    telemetry = {};
    if (cls.demo.playback || cls.demo.seeking ||
        !channel || channel != &cls.netchan ||
        channel->type != NETCHAN_NEW ||
        channel->app_tx_prepare || channel->app_tx_completion ||
        channel->app_tx_opaque || channel->app_rx ||
        channel->app_rx_opaque) {
        return false;
    }

    /* The opt-in is sampled exactly once per fresh NEW netchan. */
    if (!cl_worr_native_shadow || !cl_worr_native_shadow->integer)
        return false;

    uint64_t owner;
    if (!allocate_connection_owner(owner))
        return false;
    pilot.enabled = true;
    pilot.channel = channel;
    pilot.connection_owner_id = owner;
    pilot.map_quiesced = true;
    pilot.mode = pilot_mode_t::arming;
    uint64_t ignored_tick;
    if (!current_tick(ignored_tick) ||
        !Worr_NativeReadinessSidebandParserInitV1(&pilot.parser)) {
        disable_pilot();
        return false;
    }

    if (!Netchan_SetApplicationTxHook(
            channel, pilot_tx_prepare, pilot_tx_completion, &pilot)) {
        disable_pilot();
        return false;
    }
    pilot.tx_hook_registered = true;
    if (!Netchan_SetApplicationRxHook(channel, pilot_rx, &pilot)) {
        disable_pilot();
        return false;
    }
    pilot.rx_hook_registered = true;
    return true;
}

extern "C" void CL_NativeReadinessPilotBeforeNetchanClose(
    netchan_t *channel)
{
    if (channel && pilot.channel == channel)
        disable_pilot();
}

extern "C" void CL_NativeReadinessPilotQuiesceMap(void)
{
    if (live_pilot()) {
        pilot.map_quiesced = true;
        enter_drain(false);
    }
}

extern "C" void CL_NativeReadinessPilotServerDataReset(void)
{
    if (!live_pilot())
        return;
    pilot.map_quiesced = true;
    enter_drain(false);
    pilot.capability_confirmed = false;
    pilot.builder_initialized = false;
    pilot.builder = {};
    pilot.command_ring = {};
    if (!Worr_NativeReadinessSidebandParserInitV1(&pilot.parser))
        pilot_failure();
}

extern "C" void CL_NativeReadinessPilotPacketBegin(void)
{
    if (!live_pilot())
        return;
    uint64_t now;
    if (!current_tick(now)) {
        pilot_failure();
        return;
    }
    const auto result =
        Worr_NativeReadinessSidebandPacketBeginV1(&pilot.parser);
    if (result != WORR_NATIVE_READINESS_SIDEBAND_PACKET_STARTED) {
        pilot_failure();
        return;
    }

    if (pilot.readiness_initialized) {
        worr_native_readiness_state_v1 next = pilot.readiness;
        if (Worr_NativeReadinessCheckDeadlineV1(&next, now) !=
            WORR_NATIVE_READINESS_OK) {
            pilot_failure();
            return;
        }
        pilot.readiness = next;
    }
}

extern "C" void CL_NativeReadinessPilotPacketEnd(void)
{
    if (!live_pilot())
        return;
    const auto result =
        Worr_NativeReadinessSidebandPacketEndV1(&pilot.parser);
    if (result != WORR_NATIVE_READINESS_SIDEBAND_PACKET_ENDED)
        pilot_failure();
}

extern "C" bool CL_NativeReadinessPilotObserveSetting(
    int32_t index, int32_t value)
{
    const bool carrier = CL_NativeReadinessPilotIsCarrierSetting(index);
    if (!live_pilot())
        return carrier;

    const auto result = Worr_NativeReadinessSidebandObserveSvcSettingV1(
        &pilot.parser, index, value);
    if (result == WORR_NATIVE_READINESS_SIDEBAND_RECORD_COMMITTED) {
        worr_native_readiness_record_v1 record{};
        if (Worr_NativeReadinessSidebandTakeRecordV1(
                &pilot.parser, &record) !=
                WORR_NATIVE_READINESS_SIDEBAND_RECORD_TAKEN ||
            !observe_record(record)) {
            pilot_failure();
        }
        return carrier;
    }
    if (!accepted_observe_result(result))
        pilot_failure();
    return carrier;
}

extern "C" void CL_NativeReadinessPilotObserveInterveningService(void)
{
    if (!live_pilot())
        return;
    const auto result =
        Worr_NativeReadinessSidebandObserveInterveningServiceV1(
            &pilot.parser);
    if (result != WORR_NATIVE_READINESS_SIDEBAND_NOT_SIDEBAND)
        pilot_failure();
}

extern "C" void CL_NativeReadinessPilotCapabilityConfirmed(
    const worr_net_capability_state_v1 *capability_state)
{
    if (!live_pilot())
        return;
    if (!capability_is_exact_legacy_confirmation(capability_state)) {
        pilot_failure();
        return;
    }
    if (pilot.builder_initialized) {
        if (pilot.builder.command_epoch !=
            capability_state->connection_epoch) {
            pilot_failure();
            return;
        }
    } else {
        worr_native_command_shadow_builder_v1 builder{};
        if (!Worr_NativeCommandShadowBuilderInitV1(
                &builder, capability_state->connection_epoch,
                WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS)) {
            pilot_failure();
            return;
        }
        pilot.builder = builder;
        pilot.builder_initialized = true;
        pilot.command_ring = {};
    }
    pilot.capability_confirmed = true;
}

extern "C" void CL_NativeReadinessPilotObserveFinalizedCommand(
    uint32_t legacy_command_number,
    const worr_command_id_v1 *command_id,
    const worr_prediction_command_v1 *command)
{
    if (!live_pilot())
        return;
    if (!pilot.builder_initialized || !command_id || !command ||
        legacy_command_number == 0) {
        pilot_failure();
        return;
    }

    worr_command_record_v1 record{};
    if (Worr_NativeCommandShadowBuilderBuildV1(
            &pilot.builder, *command_id, command, &record) !=
        WORR_NATIVE_COMMAND_SHADOW_BUILD_BUILT) {
        pilot_failure();
        return;
    }
    command_ring_entry_t &entry =
        pilot.command_ring[legacy_command_number & CMD_MASK];
    entry.valid = true;
    entry.legacy_command_number = legacy_command_number;
    entry.record = record;
}

extern "C" void CL_NativeReadinessPilotObserveEncodedCommandRange(
    uint32_t first_legacy_command_number, uint32_t command_count)
{
    if (!live_pilot() || pilot.mode != pilot_mode_t::active ||
        !pilot.session_initialized || pilot.proof_enqueued_once)
        return;
    if (first_legacy_command_number == 0 || command_count == 0 ||
        command_count - 1u >
            UINT32_MAX - first_legacy_command_number) {
        pilot_failure();
        return;
    }
    const uint32_t latest =
        first_legacy_command_number + command_count - 1u;
    const command_ring_entry_t &entry =
        pilot.command_ring[latest & CMD_MASK];
    if (!entry.valid || entry.legacy_command_number != latest) {
        pilot_failure();
        return;
    }
    if (cl_worr_native_shadow_probe_hold &&
        cl_worr_native_shadow_probe_hold->integer) {
        return;
    }

    auto next_registry = pilot.payload_registry;
    worr_native_tx_session_v1 next_tx = pilot.tx;
    auto next_slots = pilot.tx_slots;
    uint32_t handle = 0;
    const worr_native_command_shadow_payload_result_v1 retained =
        Worr_NativeCommandShadowPayloadRetainV1(
            &next_registry, &entry.record, &handle);
    if (retained == WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_CAPACITY_STALL) {
        /* A bounded observational stall never perturbs legacy transmission. */
        return;
    }
    if (retained != WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_RETAINED) {
        pilot_failure();
        return;
    }

    uint64_t now;
    if (!current_tick(now)) {
        pilot_failure();
        return;
    }
    uint32_t message_sequence = 0;
    const worr_native_tx_result_v1 enqueued =
        Worr_NativeTxSessionEnqueueV1(
            &next_tx, next_slots.data(), kTxSlotCapacity,
            command_record_ref(entry.record.command_id), kProofPriority,
            handle, kCommandPayloadBytes, kCommandDatagramBytes, now,
            &message_sequence);
    if (enqueued == WORR_NATIVE_TX_CAPACITY ||
        enqueued == WORR_NATIVE_TX_RECEIPT_WINDOW) {
        return;
    }
    if (enqueued != WORR_NATIVE_TX_RETAINED || message_sequence != 1 ||
        next_tx.retained_count != 1 ||
        next_registry.occupied_count != 1) {
        pilot_failure();
        return;
    }

    pilot.payload_registry = next_registry;
    pilot.tx = next_tx;
    pilot.tx_slots = next_slots;
    pilot.proof_enqueued_once = true;
    counter_increment(telemetry.proof_enqueued);
    const uint64_t retained_total = next_tx.retained_count +
        (pilot.retired_session_initialized
             ? pilot.retired_tx.retained_count
             : 0u);
    if (telemetry.retained_highwater < retained_total)
        telemetry.retained_highwater = retained_total;
}

extern "C" bool CL_NativeReadinessPilotIsCarrierSetting(int32_t index)
{
    return index >= WORR_NATIVE_READINESS_SETTING_BEGIN &&
           index <= WORR_NATIVE_READINESS_SETTING_COMMIT;
}

extern "C" bool CL_NativeReadinessPilotGetStatusV1(
    cl_native_readiness_pilot_status_v1 *status_out)
{
    if (!status_out)
        return false;

    cl_native_readiness_pilot_status_v1 status{};
    status.struct_size = sizeof(status);
    status.schema_version = CL_NATIVE_READINESS_PILOT_STATUS_ABI_V1;
    status.enabled = pilot.enabled ? 1u : 0u;
    status.mode = pilot.enabled ? static_cast<uint32_t>(pilot.mode) : 0u;
    status.hooks = hooks_attached_exact() ? 1u : 0u;
    status.capability_confirmed = pilot.capability_confirmed ? 1u : 0u;
    status.readiness_phase = pilot.readiness_initialized
                                 ? pilot.readiness.phase
                                 : WORR_NATIVE_READINESS_PHASE_RESET;
    status.official_epoch =
        pilot.builder_initialized ? pilot.builder.command_epoch : 0u;
    status.transport_epoch = pilot.readiness_initialized
                                 ? pilot.readiness.transport_epoch
                                 : 0u;
    status.protocol = static_cast<uint32_t>(cls.serverProtocol);
    status.public_mask = WORR_NET_CAP_LEGACY_STAGE_MASK;
    status.private_mask = kPrivateCapabilities;
    status.probe_hold = cl_worr_native_shadow_probe_hold &&
                                cl_worr_native_shadow_probe_hold->integer
                            ? 1u
                            : 0u;
    status.challenges = telemetry.challenges;
    status.client_ready_queued = telemetry.client_ready_queued;
    status.server_active = telemetry.server_active;
    status.proof_enqueued = telemetry.proof_enqueued;
    status.retained =
        (pilot.session_initialized ? pilot.tx.retained_count : 0u) +
        (pilot.retired_session_initialized
             ? pilot.retired_tx.retained_count
             : 0u);
    status.retained_highwater = telemetry.retained_highwater;
    status.retained_releases = telemetry.retained_releases;
    status.tx_first_sends = telemetry.tx_first_sends;
    status.tx_retries = telemetry.tx_retries;
    status.tx_handoffs = telemetry.tx_handoffs;
    status.ack_carriers = telemetry.ack_carriers;
    status.acknowledged_reliable = telemetry.acknowledged_reliable;
    status.drains = telemetry.drains;
    status.failures = telemetry.failures;
    status.last_failure = telemetry.last_failure;
    *status_out = status;
    return true;
}

#if defined(WORR_NATIVE_READINESS_PILOT_TESTING)
extern "C" bool CL_NativeReadinessPilotGetTestState(
    cl_native_readiness_pilot_test_state_t *state_out)
{
    if (!state_out || !pilot.enabled)
        return false;
    cl_native_readiness_pilot_test_state_t state{};
    state.transport_epoch =
        pilot.session_initialized ? pilot.tx.transport_epoch : 0;
    state.retained_messages =
        pilot.session_initialized ? pilot.tx.retained_count : 0;
    state.retained_payloads =
        pilot.session_initialized
            ? pilot.payload_registry.occupied_count
            : 0;
    state.retired_transport_epoch =
        pilot.retired_session_initialized
            ? pilot.retired_tx.transport_epoch
            : 0;
    state.retired_messages =
        pilot.retired_session_initialized
            ? pilot.retired_tx.retained_count
            : 0;
    state.retired_payloads =
        pilot.retired_session_initialized
            ? pilot.retired_payload_registry.occupied_count
            : 0;
    state.message_sequence_highwater =
        pilot.session_initialized
            ? pilot.tx.next_message_sequence - 1u
            : 0;
    state.selected_send_attempts =
        pilot.session_initialized && pilot.tx_slots[0].state_flags != 0
            ? pilot.tx_slots[0].send_attempts
            : 0;
    state.mode = static_cast<uint32_t>(pilot.mode);
    state.hooks_installed =
        pilot.tx_hook_registered && pilot.rx_hook_registered;
    state.readiness_committed = pilot.readiness_committed;
    state.proof_enqueued_once = pilot.proof_enqueued_once;
    state.carrier_traffic_seen = pilot.carrier_traffic_seen;
    *state_out = state;
    return true;
}
#endif
