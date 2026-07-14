/* Standalone FR-10-T04 client readiness/carrier integration tests. */

#include "client.h"
#include "client/native_readiness_pilot.h"

#include "common/net/native_carrier.h"
#include "common/net/native_codec.h"
#include "common/net/native_readiness.h"
#include "common/net/native_readiness_sideband.h"
#include "shared/native_envelope.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

client_static_t cls{};
client_state_t cl{};
unsigned com_localTime{};

namespace {

constexpr uint32_t kPrivateCapabilities =
    WORR_NET_CAP_LEGACY_STAGE_MASK | WORR_NET_CAP_NATIVE_ENVELOPE_V1;
constexpr size_t kApplicationCeiling = 1024;

cvar_t pilot_cvar{};
cvar_t probe_hold_cvar{};
std::array<byte, 512> reliable_storage{};

struct packet_t {
    std::array<byte, WORR_NATIVE_CARRIER_MAX_PACKET_BYTES> bytes{};
    size_t count{};
};

struct tx_attempt_t {
    netchan_app_tx_prepare_result_t result{NETCHAN_APP_TX_PREPARE_BYPASS};
    netchan_app_tx_prepare_output_v1_t output{};
    std::array<byte, WORR_NATIVE_CARRIER_MAX_PACKET_BYTES> candidate{};
    std::array<byte, WORR_NATIVE_CARRIER_MAX_PACKET_BYTES> legacy{};
    size_t legacy_bytes{};
};

netchan_app_tx_prepare_result_t occupied_tx_prepare(
    void *, const netchan_app_tx_prepare_info_v1_t *, const byte *, byte *,
    netchan_app_tx_prepare_output_v1_t *)
{
    return NETCHAN_APP_TX_PREPARE_BYPASS;
}

void occupied_tx_completion(
    void *, const netchan_app_tx_completion_info_v1_t *, const byte *)
{
}

netchan_app_rx_result_t occupied_rx(
    void *, const netchan_app_rx_info_v1_t *, const byte *,
    netchan_app_rx_output_v1_t *)
{
    return NETCHAN_APP_RX_BYPASS;
}

[[noreturn]] void fail(const char *expression, int line)
{
    std::fprintf(stderr, "native_client_readiness_pilot_test:%d: %s\n",
                 line, expression);
    std::exit(EXIT_FAILURE);
}

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression))                                                    \
            fail(#expression, __LINE__);                                      \
    } while (0)

cl_native_readiness_pilot_test_state_t pilot_state()
{
    cl_native_readiness_pilot_test_state_t state{};
    CHECK(CL_NativeReadinessPilotGetTestState(&state));
    return state;
}

cl_native_readiness_pilot_status_v1 pilot_status()
{
    cl_native_readiness_pilot_status_v1 status{};
    CHECK(CL_NativeReadinessPilotGetStatusV1(&status));
    CHECK(status.struct_size == sizeof(status));
    CHECK(status.schema_version ==
          CL_NATIVE_READINESS_PILOT_STATUS_ABI_V1);
    return status;
}

void reset_environment(uint32_t raw_time, size_t reliable_capacity)
{
    CL_NativeReadinessPilotBeforeNetchanClose(&cls.netchan);
    std::memset(&cls, 0, sizeof(cls));
    std::memset(&cl, 0, sizeof(cl));
    std::memset(&pilot_cvar, 0, sizeof(pilot_cvar));
    std::memset(&probe_hold_cvar, 0, sizeof(probe_hold_cvar));
    reliable_storage.fill(0);
    com_localTime = raw_time;
    cls.netchan.type = NETCHAN_NEW;
    cls.serverProtocol = PROTOCOL_VERSION_RERELEASE;
    cls.netchan.maxpacketlen = kApplicationCeiling;
    SZ_InitWrite(&cls.netchan.message, reliable_storage.data(),
                 reliable_capacity);
    CL_NativeReadinessPilotRegisterCvar();
}

bool begin_enabled(uint32_t raw_time,
                   size_t reliable_capacity = reliable_storage.size())
{
    reset_environment(raw_time, reliable_capacity);
    pilot_cvar.integer = 1;
    return CL_NativeReadinessPilotBeginConnection(&cls.netchan);
}

worr_net_capability_state_v1 confirmed_capability(uint32_t epoch)
{
    worr_net_capability_state_v1 state{};
    CHECK(Worr_NetCapabilityStateInitV1(
        &state, epoch, WORR_NET_CAP_LEGACY_STAGE_MASK,
        WORR_NET_CAP_LEGACY_STAGE_MASK));
    worr_net_capability_confirm_v1 confirm{};
    confirm.struct_size = sizeof(confirm);
    confirm.schema_version = WORR_NET_CAPABILITY_VERSION;
    confirm.connection_epoch = epoch;
    confirm.supported = WORR_NET_CAP_LEGACY_STAGE_MASK;
    confirm.negotiated = WORR_NET_CAP_LEGACY_STAGE_MASK;
    CHECK(Worr_NetCapabilityConfirmV1(&state, &confirm) ==
          WORR_NET_CAPABILITY_OK);
    return state;
}

void confirm_capability(uint32_t epoch)
{
    const auto state = confirmed_capability(epoch);
    CL_NativeReadinessPilotCapabilityConfirmed(&state);
}

void feed_svc_record(const worr_native_readiness_record_v1 &record)
{
    std::array<worr_native_readiness_setting_pair_v1,
               WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT> pairs{};
    CHECK(Worr_NativeReadinessSidebandEncodeV1(
        &record, pairs.data(), static_cast<uint32_t>(pairs.size())));
    for (const auto &pair : pairs) {
        CHECK(CL_NativeReadinessPilotObserveSetting(
            static_cast<int32_t>(pair.index),
            static_cast<int32_t>(pair.value)));
    }
}

void feed_packet_record(const worr_native_readiness_record_v1 &record)
{
    CL_NativeReadinessPilotPacketBegin();
    feed_svc_record(record);
    CL_NativeReadinessPilotPacketEnd();
}

worr_native_readiness_record_v1 decode_client_record(size_t offset)
{
    CHECK(cls.netchan.message.cursize >= offset + 65u);
    worr_native_readiness_sideband_parser_v1 parser{};
    CHECK(Worr_NativeReadinessSidebandParserInitV1(&parser));
    CHECK(Worr_NativeReadinessSidebandPacketBeginV1(&parser) ==
          WORR_NATIVE_READINESS_SIDEBAND_PACKET_STARTED);
    const byte *wire = cls.netchan.message.data + offset;
    for (uint32_t index = 0;
         index < WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT; ++index) {
        const byte *field = wire + index * 5u;
        CHECK(field[0] == static_cast<byte>(clc_setting));
        const auto setting_index = static_cast<int16_t>(
            static_cast<uint16_t>(field[1]) |
            (static_cast<uint16_t>(field[2]) << 8));
        const auto setting_value = static_cast<int16_t>(
            static_cast<uint16_t>(field[3]) |
            (static_cast<uint16_t>(field[4]) << 8));
        const auto result = Worr_NativeReadinessSidebandObservePairV1(
            &parser, setting_index, setting_value);
        CHECK(result == WORR_NATIVE_READINESS_SIDEBAND_FIELD_ACCEPTED ||
              result == WORR_NATIVE_READINESS_SIDEBAND_RECORD_COMMITTED);
    }
    worr_native_readiness_record_v1 record{};
    CHECK(Worr_NativeReadinessSidebandTakeRecordV1(&parser, &record) ==
          WORR_NATIVE_READINESS_SIDEBAND_RECORD_TAKEN);
    CHECK(Worr_NativeReadinessSidebandPacketEndV1(&parser) ==
          WORR_NATIVE_READINESS_SIDEBAND_PACKET_ENDED);
    return record;
}

worr_native_readiness_record_v1 server_challenge(
    worr_native_readiness_state_v1 &server, uint32_t transport_epoch,
    uint64_t nonce, uint64_t now, bool advance)
{
    worr_native_readiness_record_v1 challenge{};
    const auto result = advance
                            ? Worr_NativeReadinessServerAdvanceEpochV1(
                                  &server, transport_epoch,
                                  kPrivateCapabilities, nonce, now, 10000,
                                  &challenge)
                            : Worr_NativeReadinessServerInitV1(
                                  &server, transport_epoch,
                                  kPrivateCapabilities, nonce, now, 10000,
                                  &challenge);
    CHECK(result == WORR_NATIVE_READINESS_OK);
    return challenge;
}

worr_native_readiness_record_v1 server_accept_ready(
    worr_native_readiness_state_v1 &server,
    const worr_native_readiness_record_v1 &ready, uint64_t now)
{
    worr_native_readiness_record_v1 active{};
    CHECK(Worr_NativeReadinessServerObserveClientReadyV1(
              &server, &ready, now, &active) == WORR_NATIVE_READINESS_OK);
    return active;
}

worr_native_readiness_record_v1 begin_handshake(
    worr_native_readiness_state_v1 &server, uint32_t transport_epoch,
    uint64_t nonce, bool advance)
{
    const size_t ready_offset = cls.netchan.message.cursize;
    const auto challenge = server_challenge(
        server, transport_epoch, nonce, com_localTime, advance);
    feed_packet_record(challenge);
    CHECK(cls.netchan.message.cursize == ready_offset + 65u);
    const auto ready = decode_client_record(ready_offset);
    CHECK(ready.record_kind == WORR_NATIVE_READINESS_RECORD_CLIENT_READY);
    return server_accept_ready(server, ready, com_localTime);
}

void complete_handshake(
    const worr_native_readiness_record_v1 &server_active)
{
    feed_packet_record(server_active);
    const auto state = pilot_state();
    CHECK(state.mode == 2u && state.hooks_installed);
    CHECK(state.transport_epoch == server_active.transport_epoch);
}

worr_prediction_command_v1 prediction_command(uint32_t sequence)
{
    worr_prediction_command_v1 command{};
    command.struct_size = sizeof(command);
    command.schema_version = WORR_PREDICTION_ABI_VERSION;
    command.duration_ms = 10;
    command.buttons = static_cast<uint8_t>(sequence & 3u);
    command.view_angles[1] = static_cast<float>(sequence * 10u);
    command.forward_move = static_cast<float>(sequence * 3u);
    command.side_move = -static_cast<float>(sequence);
    return command;
}

void build_commands(uint32_t command_epoch, uint32_t count)
{
    for (uint32_t sequence = 1; sequence <= count; ++sequence) {
        const worr_command_id_v1 id{command_epoch, sequence};
        const auto command = prediction_command(sequence);
        CL_NativeReadinessPilotObserveFinalizedCommand(
            sequence, &id, &command);
    }
}

tx_attempt_t prepare_tx(size_t legacy_bytes,
                        uint32_t application_ceiling = kApplicationCeiling)
{
    CHECK(legacy_bytes <= WORR_NATIVE_CARRIER_MAX_PACKET_BYTES);
    tx_attempt_t attempt{};
    attempt.legacy_bytes = legacy_bytes;
    for (size_t index = 0; index < legacy_bytes; ++index)
        attempt.legacy[index] = static_cast<byte>((index * 17u) ^ 0x5au);
    attempt.candidate.fill(0xcc);

    netchan_app_tx_prepare_info_v1_t info{};
    info.abi_version = NETCHAN_APP_TX_HOOK_ABI_V1;
    info.struct_size = sizeof(info);
    info.outgoing_sequence = cls.netchan.outgoing_sequence;
    info.max_application_bytes = application_ceiling;
    info.unreliable_bytes = static_cast<uint32_t>(legacy_bytes);
    info.legacy_application_bytes = static_cast<uint32_t>(legacy_bytes);
    info.packet_copies = 1;
    attempt.output.abi_version = NETCHAN_APP_TX_HOOK_ABI_V1;
    attempt.output.struct_size = sizeof(attempt.output);
    CHECK(cls.netchan.app_tx_prepare);
    attempt.result = cls.netchan.app_tx_prepare(
        cls.netchan.app_tx_opaque, &info, attempt.legacy.data(),
        attempt.candidate.data(), &attempt.output);
    return attempt;
}

void complete_tx(const tx_attempt_t &attempt,
                 netchan_app_tx_completion_result_t result)
{
    CHECK(attempt.result == NETCHAN_APP_TX_PREPARE_PREPARED);
    netchan_app_tx_completion_info_v1_t info{};
    info.abi_version = NETCHAN_APP_TX_HOOK_ABI_V1;
    info.struct_size = sizeof(info);
    info.result = result;
    info.packet_copies = 1;
    info.accepted_copies =
        result == NETCHAN_APP_TX_COMPLETION_ACCEPTED ? 1 : 0;
    const bool fallback =
        result == NETCHAN_APP_TX_COMPLETION_PREPARE_INVALID;
    info.application_bytes = static_cast<uint32_t>(
        fallback ? attempt.legacy_bytes : attempt.output.application_bytes);
    info.token = attempt.output.token;
    CHECK(cls.netchan.app_tx_completion);
    cls.netchan.app_tx_completion(
        cls.netchan.app_tx_opaque, &info,
        fallback ? attempt.legacy.data() : attempt.candidate.data());
}

worr_native_envelope_frame_info_v1 inspect_command_packet(
    const tx_attempt_t &attempt, uint32_t expected_epoch,
    uint32_t expected_command_sequence)
{
    CHECK(attempt.result == NETCHAN_APP_TX_PREPARE_PREPARED);
    CHECK(attempt.output.application_bytes ==
          attempt.legacy_bytes + 206u);
    CHECK(std::memcmp(attempt.candidate.data(), attempt.legacy.data(),
                      attempt.legacy_bytes) == 0);

    worr_native_carrier_view_v1 view{};
    CHECK(Worr_NativeCarrierDecodeV1(
              attempt.candidate.data(), attempt.output.application_bytes,
              &view) == WORR_NATIVE_CARRIER_OK);
    CHECK(view.transport_epoch == expected_epoch);
    CHECK(view.legacy_bytes == attempt.legacy_bytes);
    CHECK(view.entry_count == 1);
    CHECK(view.entries[0].entry_type == WORR_NATIVE_CARRIER_ENTRY_DATA_V1);
    CHECK(view.entries[0].data_bytes == 166u);

    const byte *datagram =
        attempt.candidate.data() + view.entries[0].data_offset;
    worr_native_envelope_frame_info_v1 frame{};
    CHECK(Worr_NativeEnvelopeDecodeV1(
              datagram, view.entries[0].data_bytes, &frame) ==
          WORR_NATIVE_ENVELOPE_DECODE_OK);
    CHECK(frame.transport_epoch == expected_epoch);
    CHECK(frame.message_sequence == 1u);
    CHECK(frame.record.record_class == WORR_NATIVE_RECORD_COMMAND_V1);
    CHECK(frame.record.object_sequence == expected_command_sequence);
    CHECK(frame.priority == 0u);
    CHECK(frame.fragment_count == 1u && frame.fragment_index == 0u);
    CHECK(frame.total_payload_bytes == 110u);

    worr_command_record_v1 command{};
    CHECK(Worr_NativeCodecCommandDecodeV1(
              datagram + frame.payload_offset, frame.fragment_payload_bytes,
              WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS, &command) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(command.command_id.sequence == expected_command_sequence);
    CHECK(command.sample_time_us ==
          static_cast<uint64_t>(expected_command_sequence) * 10000u);
    return frame;
}

packet_t ack_packet(uint32_t transport_epoch, uint32_t first,
                    uint32_t last, size_t legacy_bytes = 0)
{
    packet_t packet{};
    std::array<byte, 32> legacy{};
    for (size_t index = 0; index < legacy_bytes; ++index)
        legacy[index] = static_cast<byte>(0xa0u + index);
    worr_native_carrier_entry_v1 entry{};
    entry.struct_size = sizeof(entry);
    entry.schema_version = WORR_NATIVE_CARRIER_ABI_VERSION;
    entry.entry_type = WORR_NATIVE_CARRIER_ENTRY_ACK_V1;
    entry.first_message_sequence = first;
    entry.last_message_sequence = last;
    CHECK(Worr_NativeCarrierEncodeV1(
              transport_epoch, legacy_bytes ? legacy.data() : nullptr,
              legacy_bytes, nullptr, 0, &entry, 1, packet.bytes.data(),
              packet.bytes.size(), &packet.count) == WORR_NATIVE_CARRIER_OK);
    return packet;
}

packet_t multi_ack_packet(uint32_t transport_epoch)
{
    packet_t packet{};
    std::array<worr_native_carrier_entry_v1, 2> entries{};
    for (auto &entry : entries) {
        entry.struct_size = sizeof(entry);
        entry.schema_version = WORR_NATIVE_CARRIER_ABI_VERSION;
        entry.entry_type = WORR_NATIVE_CARRIER_ENTRY_ACK_V1;
        entry.first_message_sequence = 1;
        entry.last_message_sequence = 1;
    }
    CHECK(Worr_NativeCarrierEncodeV1(
              transport_epoch, nullptr, 0, nullptr, 0, entries.data(),
              static_cast<uint16_t>(entries.size()), packet.bytes.data(),
              packet.bytes.size(), &packet.count) == WORR_NATIVE_CARRIER_OK);
    return packet;
}

netchan_app_rx_result_t receive_packet(
    const packet_t &packet, netchan_app_rx_output_v1_t &output)
{
    netchan_app_rx_info_v1_t info{};
    info.abi_version = NETCHAN_APP_RX_HOOK_ABI_V1;
    info.struct_size = sizeof(info);
    info.message_bytes = static_cast<uint32_t>(packet.count);
    info.application_bytes = static_cast<uint32_t>(packet.count);
    output = {};
    output.abi_version = NETCHAN_APP_RX_HOOK_ABI_V1;
    output.struct_size = sizeof(output);
    CHECK(cls.netchan.app_rx);
    return cls.netchan.app_rx(cls.netchan.app_rx_opaque, &info,
                              packet.bytes.data(), &output);
}

void test_default_off_demo_and_hook_ownership()
{
    reset_environment(10, reliable_storage.size());
    CHECK(!CL_NativeReadinessPilotBeginConnection(&cls.netchan));
    CHECK(!cls.netchan.app_tx_prepare && !cls.netchan.app_rx);
    /* With no pilot hook installed, Netchan_Transmit remains on its legacy
     * path. The real transmit/capture byte-identity contract is exercised by
     * network-netchan-application-tx-hook. */
    CHECK(CL_NativeReadinessPilotIsCarrierSetting(
        WORR_NATIVE_READINESS_SETTING_BEGIN));

    reset_environment(11, reliable_storage.size());
    pilot_cvar.integer = 1;
    cls.demo.playback = 1;
    CHECK(!CL_NativeReadinessPilotBeginConnection(&cls.netchan));
    CHECK(!cls.netchan.app_tx_prepare && !cls.netchan.app_rx);

    reset_environment(12, reliable_storage.size());
    pilot_cvar.integer = 1;
    cls.demo.seeking = true;
    CHECK(!CL_NativeReadinessPilotBeginConnection(&cls.netchan));
    CHECK(!cls.netchan.app_tx_prepare && !cls.netchan.app_rx);

    reset_environment(13, reliable_storage.size());
    pilot_cvar.integer = 1;
    cls.netchan.app_tx_prepare = occupied_tx_prepare;
    cls.netchan.app_tx_completion = occupied_tx_completion;
    cls.netchan.app_tx_opaque = &pilot_cvar;
    cls.netchan.app_rx = occupied_rx;
    cls.netchan.app_rx_opaque = &reliable_storage;
    CHECK(!CL_NativeReadinessPilotBeginConnection(&cls.netchan));
    CHECK(cls.netchan.app_tx_prepare == occupied_tx_prepare);
    CHECK(cls.netchan.app_tx_completion == occupied_tx_completion);
    CHECK(cls.netchan.app_tx_opaque == &pilot_cvar);
    CHECK(cls.netchan.app_rx == occupied_rx);
    CHECK(cls.netchan.app_rx_opaque == &reliable_storage);

    /* The read-only status reflects exact live hook ownership, not stale
     * adapter bookkeeping, and does not mutate or detach either hook. */
    CHECK(begin_enabled(14));
    const auto saved_rx = cls.netchan.app_rx;
    void *const saved_rx_opaque = cls.netchan.app_rx_opaque;
    cls.netchan.app_rx = occupied_rx;
    cls.netchan.app_rx_opaque = &reliable_storage;
    CHECK(pilot_status().enabled == 1u && pilot_status().hooks == 0u);
    CHECK(cls.netchan.app_rx == occupied_rx &&
          cls.netchan.app_rx_opaque == &reliable_storage);
    cls.netchan.app_rx = saved_rx;
    cls.netchan.app_rx_opaque = saved_rx_opaque;
    CHECK(pilot_status().hooks == 1u);
}

void test_one_shot_exact_boundary_loss_retry_ack_release()
{
    CHECK(begin_enabled(100));
    confirm_capability(7);
    worr_native_readiness_state_v1 server{};
    const auto active = begin_handshake(server, 101, 500, false);

    /* The builder observes every finalized command before CLIENT_ACTIVE. */
    /* Command four is already built, but it is outside the exact encoded
     * [1,3] range and must not be selected. */
    build_commands(7, 4);
    complete_handshake(active);

    /* The acceptance-only hold suppresses native retention without changing
     * finalized-command observation or the already-encoded legacy range. */
    probe_hold_cvar.integer = 1;
    CL_NativeReadinessPilotObserveEncodedCommandRange(1, 3);
    auto state = pilot_state();
    CHECK(!state.proof_enqueued_once && state.retained_messages == 0u);
    auto status = pilot_status();
    CHECK(status.enabled == 1u && status.mode == 2u && status.hooks == 1u);
    CHECK(status.probe_hold == 1u && status.proof_enqueued == 0u &&
          status.retained == 0u && status.retained_highwater == 0u);

    probe_hold_cvar.integer = 0;
    CL_NativeReadinessPilotObserveEncodedCommandRange(1, 3);
    state = pilot_state();
    CHECK(state.proof_enqueued_once && state.retained_messages == 1u);
    CHECK(state.retained_payloads == 1u);
    CHECK(state.message_sequence_highwater == 1u);

    const auto too_large = prepare_tx(819);
    CHECK(too_large.result == NETCHAN_APP_TX_PREPARE_BYPASS);
    CHECK(pilot_state().selected_send_attempts == 0u);

    const auto exact = prepare_tx(818);
    CHECK(exact.result == NETCHAN_APP_TX_PREPARE_PREPARED);
    CHECK(exact.output.application_bytes == 1024u);
    inspect_command_packet(exact, 101, 3);
    complete_tx(exact, NETCHAN_APP_TX_COMPLETION_PREPARE_INVALID);
    state = pilot_state();
    CHECK(state.retained_messages == 1u &&
          state.retained_payloads == 1u &&
          state.selected_send_attempts == 0u);

    const auto rejected_retry = prepare_tx(818);
    CHECK(rejected_retry.result == NETCHAN_APP_TX_PREPARE_PREPARED);
    inspect_command_packet(rejected_retry, 101, 3);
    complete_tx(rejected_retry, NETCHAN_APP_TX_COMPLETION_NOT_ACCEPTED);
    CHECK(pilot_state().selected_send_attempts == 0u);

    const auto retry = prepare_tx(818);
    CHECK(retry.result == NETCHAN_APP_TX_PREPARE_PREPARED);
    inspect_command_packet(retry, 101, 3);
    CHECK(std::memcmp(exact.candidate.data(), retry.candidate.data(),
                      retry.output.application_bytes) == 0);
    complete_tx(retry, NETCHAN_APP_TX_COMPLETION_ACCEPTED);
    CHECK(pilot_state().selected_send_attempts == 1u);

    /* Lost receipt: the retained command becomes due without re-enqueue. */
    ++com_localTime;
    CHECK(prepare_tx(0).result == NETCHAN_APP_TX_PREPARE_BYPASS);
    com_localTime += 100;
    const auto resend = prepare_tx(0);
    CHECK(resend.result == NETCHAN_APP_TX_PREPARE_PREPARED);
    inspect_command_packet(resend, 101, 3);
    complete_tx(resend, NETCHAN_APP_TX_COMPLETION_ACCEPTED);
    CHECK(pilot_state().selected_send_attempts == 2u);

    const auto acknowledgement = ack_packet(101, 1, 1, 3);
    netchan_app_rx_output_v1_t output{};
    CHECK(receive_packet(acknowledgement, output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(output.legacy_bytes == 3u);
    state = pilot_state();
    CHECK(state.retained_messages == 0u && state.retained_payloads == 0u);

    /* Exact duplicate ACK is idempotent and the per-epoch latch stays shut. */
    CHECK(receive_packet(acknowledgement, output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(pilot_state().retained_payloads == 0u);
    status = pilot_status();
    CHECK(status.readiness_phase == WORR_NATIVE_READINESS_PHASE_CLIENT_ACTIVE);
    CHECK(status.official_epoch == 7u && status.transport_epoch == 101u);
    CHECK(status.protocol == PROTOCOL_VERSION_RERELEASE);
    CHECK(status.public_mask == WORR_NET_CAP_LEGACY_STAGE_MASK &&
          status.private_mask == kPrivateCapabilities);
    CHECK(status.challenges == 1u && status.client_ready_queued == 1u &&
          status.server_active == 1u && status.proof_enqueued == 1u);
    CHECK(status.retained == 0u && status.retained_highwater == 1u &&
          status.retained_releases == 1u);
    CHECK(status.tx_first_sends == 1u && status.tx_retries == 1u &&
          status.tx_handoffs == 2u);
    CHECK(status.ack_carriers == 2u &&
          status.acknowledged_reliable == 1u);
    CHECK(status.drains == 0u && status.failures == 0u &&
          status.last_failure == 0u);
    CL_NativeReadinessPilotObserveEncodedCommandRange(4, 1);
    CHECK(pilot_state().message_sequence_highwater == 1u);
    CHECK(prepare_tx(0).result == NETCHAN_APP_TX_PREPARE_BYPASS);
}

void test_map_drain_old_ack_epoch_switch_and_reconnect()
{
    CHECK(begin_enabled(500));
    confirm_capability(20);
    worr_native_readiness_state_v1 server{};
    auto active = begin_handshake(server, 501, 900, false);
    complete_handshake(active);
    build_commands(20, 1);
    CL_NativeReadinessPilotObserveEncodedCommandRange(1, 1);
    auto first = prepare_tx(0);
    inspect_command_packet(first, 501, 1);
    complete_tx(first, NETCHAN_APP_TX_COMPLETION_ACCEPTED);

    CL_NativeReadinessPilotQuiesceMap();
    CHECK(pilot_state().mode == 3u);
    CHECK(prepare_tx(0).result == NETCHAN_APP_TX_PREPARE_BYPASS);
    feed_packet_record(active);
    CHECK(pilot_state().mode == 3u &&
          pilot_state().transport_epoch == 501u);

    CL_NativeReadinessPilotServerDataReset();
    confirm_capability(21);
    ++com_localTime;
    active = begin_handshake(server, 502, 901, true);
    complete_handshake(active);
    CHECK(pilot_state().retired_transport_epoch == 501u);
    CHECK(pilot_state().retired_messages == 1u &&
          pilot_state().retired_payloads == 1u);
    build_commands(21, 1);
    CL_NativeReadinessPilotObserveEncodedCommandRange(1, 1);
    CHECK(pilot_state().retained_messages == 1u);
    CHECK(pilot_status().retained == 2u &&
          pilot_status().retained_highwater == 2u);

    /* A post-active old-epoch ACK routes only to the one retired bank. */
    netchan_app_rx_output_v1_t output{};
    const auto old_ack = ack_packet(501, 1, 1);
    CHECK(receive_packet(old_ack, output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(pilot_state().retired_messages == 0u &&
          pilot_state().retired_payloads == 0u);
    CHECK(pilot_state().retained_messages == 1u &&
          pilot_state().retained_payloads == 1u);
    CHECK(pilot_status().retained == 1u &&
          pilot_status().retained_releases == 1u);

    auto second = prepare_tx(0);
    const auto second_frame = inspect_command_packet(second, 502, 1);
    CHECK(second_frame.message_sequence == 1u);
    complete_tx(second, NETCHAN_APP_TX_COMPLETION_ACCEPTED);
    const auto current_ack = ack_packet(502, 1, 1);
    CHECK(receive_packet(current_ack, output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(pilot_state().retained_messages == 0u &&
          pilot_state().retained_payloads == 0u);

    /* A third activation replaces, rather than accumulates, retired banks. */
    CL_NativeReadinessPilotQuiesceMap();
    CL_NativeReadinessPilotServerDataReset();
    confirm_capability(23);
    ++com_localTime;
    active = begin_handshake(server, 503, 902, true);
    complete_handshake(active);
    CHECK(pilot_state().retired_transport_epoch == 502u);
    CHECK(pilot_state().retired_messages == 0u &&
          pilot_state().retired_payloads == 0u);

    CL_NativeReadinessPilotBeforeNetchanClose(&cls.netchan);
    CHECK(!cls.netchan.app_tx_prepare && !cls.netchan.app_tx_completion &&
          !cls.netchan.app_tx_opaque && !cls.netchan.app_rx &&
          !cls.netchan.app_rx_opaque);

    /* A fresh owner/session may bind a numerically lower server epoch. */
    CHECK(begin_enabled(700));
    confirm_capability(22);
    server = {};
    active = begin_handshake(server, 41, 1000, false);
    complete_handshake(active);
    build_commands(22, 1);
    CL_NativeReadinessPilotObserveEncodedCommandRange(1, 1);
    const auto reconnect = prepare_tx(0);
    inspect_command_packet(reconnect, 41, 1);
}

void test_no_carrier_malformed_wrong_direction_and_epoch()
{
    CHECK(begin_enabled(800));
    confirm_capability(30);
    worr_native_readiness_state_v1 server{};
    auto active = begin_handshake(server, 801, 1100, false);
    complete_handshake(active);
    build_commands(30, 1);
    CL_NativeReadinessPilotObserveEncodedCommandRange(1, 1);
    auto outgoing = prepare_tx(5);
    inspect_command_packet(outgoing, 801, 1);

    packet_t legacy{};
    legacy.count = 7;
    for (size_t index = 0; index < legacy.count; ++index)
        legacy.bytes[index] = static_cast<byte>(index + 1u);
    const auto legacy_before = legacy;
    netchan_app_rx_output_v1_t output{};
    CHECK(receive_packet(legacy, output) == NETCHAN_APP_RX_BYPASS);
    CHECK(std::memcmp(legacy.bytes.data(), legacy_before.bytes.data(),
                      legacy.count) == 0);

    /* Client receive direction is ACK-only; DATA is rejected into DRAIN. */
    packet_t wrong_direction{};
    wrong_direction.count = outgoing.output.application_bytes;
    std::memcpy(wrong_direction.bytes.data(), outgoing.candidate.data(),
                wrong_direction.count);
    complete_tx(outgoing, NETCHAN_APP_TX_COMPLETION_NOT_ACCEPTED);
    CHECK(receive_packet(wrong_direction, output) ==
          NETCHAN_APP_RX_REJECT);
    CHECK(pilot_state().mode == 3u && pilot_state().hooks_installed);

    CHECK(begin_enabled(900));
    confirm_capability(31);
    server = {};
    active = begin_handshake(server, 901, 1200, false);
    complete_handshake(active);
    build_commands(31, 1);
    CL_NativeReadinessPilotObserveEncodedCommandRange(1, 1);
    outgoing = prepare_tx(0);
    complete_tx(outgoing, NETCHAN_APP_TX_COMPLETION_ACCEPTED);
    const auto wrong_epoch = ack_packet(902, 1, 1);
    CHECK(receive_packet(wrong_epoch, output) == NETCHAN_APP_RX_REJECT);
    CHECK(pilot_state().mode == 3u && pilot_state().retained_payloads == 1u);
    const auto correct_after_failure = ack_packet(901, 1, 1);
    CHECK(receive_packet(correct_after_failure, output) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(pilot_state().mode == 3u &&
          pilot_state().retained_payloads == 0u);

    CHECK(begin_enabled(950));
    confirm_capability(33);
    server = {};
    active = begin_handshake(server, 951, 1250, false);
    complete_handshake(active);
    build_commands(33, 1);
    CL_NativeReadinessPilotObserveEncodedCommandRange(1, 1);
    outgoing = prepare_tx(0);
    complete_tx(outgoing, NETCHAN_APP_TX_COMPLETION_ACCEPTED);
    const auto multiple = multi_ack_packet(951);
    CHECK(receive_packet(multiple, output) == NETCHAN_APP_RX_REJECT);
    CHECK(pilot_state().mode == 3u &&
          pilot_state().retained_payloads == 1u);

    CHECK(begin_enabled(1000));
    confirm_capability(34);
    server = {};
    active = begin_handshake(server, 1001, 1300, false);
    complete_handshake(active);
    build_commands(34, 1);
    CL_NativeReadinessPilotObserveEncodedCommandRange(1, 1);
    outgoing = prepare_tx(0);
    complete_tx(outgoing, NETCHAN_APP_TX_COMPLETION_ACCEPTED);
    auto corrupt = ack_packet(1001, 1, 1);
    CHECK(corrupt.count > WORR_NATIVE_CARRIER_WIRE_FOOTER_BYTES);
    corrupt.bytes[0] ^= 0x40u;
    CHECK(receive_packet(corrupt, output) == NETCHAN_APP_RX_REJECT);
    CHECK(pilot_state().mode == 3u && pilot_state().hooks_installed);
}

void test_readiness_atomic_capacity_and_pretraffic_failure()
{
    worr_native_readiness_state_v1 server{};
    CHECK(begin_enabled(1100, 64));
    confirm_capability(40);
    const auto challenge = server_challenge(server, 1101, 1400, 1100,
                                             false);
    feed_packet_record(challenge);
    CHECK(cls.netchan.message.cursize == 0u);
    CHECK(!cls.netchan.app_tx_prepare && !cls.netchan.app_rx);

    CHECK(begin_enabled(UINT32_MAX - 5u));
    confirm_capability(41);
    server = {};
    const auto wrapped = server_challenge(
        server, 1102, 1401, UINT32_MAX - UINT64_C(5), false);
    com_localTime = 3;
    feed_packet_record(wrapped);
    CHECK(cls.netchan.message.cursize == 65u);

    CHECK(begin_enabled(1200));
    CL_NativeReadinessPilotPacketBegin();
    CL_NativeReadinessPilotPacketBegin();
    CHECK(!cls.netchan.app_tx_prepare && !cls.netchan.app_rx);

    /* SERVERDATA deliberately resets the parser inside an already-open
     * packet; the canonical parse loop immediately opens the post-reset
     * transaction before consuming capability/readiness settings. */
    CHECK(begin_enabled(1300));
    CL_NativeReadinessPilotPacketBegin();
    CL_NativeReadinessPilotServerDataReset();
    CL_NativeReadinessPilotPacketBegin();
    confirm_capability(42);
    server = {};
    const auto same_packet =
        server_challenge(server, 1301, 1500, 1300, false);
    feed_svc_record(same_packet);
    CL_NativeReadinessPilotPacketEnd();
    CHECK(cls.netchan.message.cursize == 65u);
    CHECK(pilot_state().hooks_installed);

    /* Once CLIENT_READY is in the reliable queue it cannot be withdrawn.
     * A later invalid ACTIVE must retain DRAIN hooks so peer WTC bytes can
     * never fall through into the legacy parser. */
    CHECK(begin_enabled(1400));
    confirm_capability(43);
    server = {};
    const auto valid_active = begin_handshake(
        server, 1401, 1600, false);
    CHECK(pilot_state().readiness_committed);
    worr_native_readiness_record_v1 invalid_active{};
    CHECK(Worr_NativeReadinessRecordInitV1(
        &invalid_active, WORR_NATIVE_READINESS_RECORD_SERVER_ACTIVE,
        valid_active.transport_epoch, kPrivateCapabilities,
        valid_active.readiness_nonce + 1u));
    feed_packet_record(invalid_active);
    CHECK(pilot_state().mode == 3u && pilot_state().hooks_installed);
    CHECK(!pilot_state().carrier_traffic_seen);

    /* Fail-closed DRAIN is monotonic for this map epoch.  A later valid
     * SERVER_ACTIVE cannot resurrect the native session or select DATA. */
    feed_packet_record(valid_active);
    CHECK(pilot_state().mode == 3u && pilot_state().hooks_installed);
    CHECK(pilot_state().transport_epoch == 0u);
    build_commands(43, 1);
    CL_NativeReadinessPilotObserveEncodedCommandRange(1, 1);
    CHECK(pilot_state().retained_messages == 0u &&
          pilot_state().retained_payloads == 0u);
    CHECK(prepare_tx(0).result == NETCHAN_APP_TX_PREPARE_BYPASS);
}

} // namespace

extern "C" cvar_t *Cvar_Get(const char *name, const char *, int)
{
    if (name && std::strcmp(name, "cl_worr_native_shadow_probe_hold") == 0)
        return &probe_hold_cvar;
    return &pilot_cvar;
}

extern "C" bool Netchan_SetApplicationTxHook(
    netchan_t *channel, netchan_app_tx_prepare_fn prepare,
    netchan_app_tx_completion_fn completion, void *opaque)
{
    if (!channel || channel->type != NETCHAN_NEW ||
        (!!prepare != !!completion)) {
        return false;
    }
    channel->app_tx_prepare = prepare;
    channel->app_tx_completion = completion;
    channel->app_tx_opaque = prepare ? opaque : nullptr;
    return true;
}

extern "C" bool Netchan_SetApplicationRxHook(
    netchan_t *channel, netchan_app_rx_fn receive, void *opaque)
{
    if (!channel || channel->type != NETCHAN_NEW)
        return false;
    channel->app_rx = receive;
    channel->app_rx_opaque = receive ? opaque : nullptr;
    return true;
}

extern "C" void SZ_Init(sizebuf_t *buffer, void *data, size_t size,
                         const char *tag)
{
    std::memset(buffer, 0, sizeof(*buffer));
    buffer->data = static_cast<byte *>(data);
    buffer->maxsize = static_cast<uint32_t>(size);
    buffer->tag = tag;
}

extern "C" void SZ_InitWrite(sizebuf_t *buffer, void *data, size_t size)
{
    SZ_Init(buffer, data, size, "pilot test");
    buffer->allowoverflow = true;
}

extern "C" void SZ_Clear(sizebuf_t *buffer)
{
    buffer->cursize = 0;
    buffer->readcount = 0;
    buffer->overflowed = false;
}

extern "C" void *SZ_GetSpace(sizebuf_t *buffer, size_t size)
{
    CHECK(buffer && buffer->data && size <= buffer->maxsize);
    if (size > buffer->maxsize - buffer->cursize) {
        CHECK(buffer->allowoverflow);
        SZ_Clear(buffer);
        buffer->overflowed = true;
    }
    byte *result = buffer->data + buffer->cursize;
    buffer->cursize += static_cast<uint32_t>(size);
    return result;
}

extern "C" q2proto_error_t q2proto_client_write(
    q2proto_clientcontext_t *, uintptr_t io_argument,
    const q2proto_clc_message_t *message)
{
    if (!io_argument || !message || message->type != Q2P_CLC_SETTING)
        return Q2P_ERR_BAD_COMMAND;
    auto *io = reinterpret_cast<q2protoio_ioarg_t *>(io_argument);
    if (!io->sz_write)
        return Q2P_ERR_BAD_DATA;
    byte *wire = static_cast<byte *>(SZ_GetSpace(io->sz_write, 5));
    wire[0] = static_cast<byte>(clc_setting);
    const uint16_t index = static_cast<uint16_t>(message->setting.index);
    const uint16_t value = static_cast<uint16_t>(message->setting.value);
    wire[1] = static_cast<byte>(index);
    wire[2] = static_cast<byte>(index >> 8);
    wire[3] = static_cast<byte>(value);
    wire[4] = static_cast<byte>(value >> 8);
    return Q2P_ERR_SUCCESS;
}

int main()
{
    test_default_off_demo_and_hook_ownership();
    test_one_shot_exact_boundary_loss_retry_ack_release();
    test_map_drain_old_ack_epoch_switch_and_reconnect();
    test_no_carrier_malformed_wrong_direction_and_epoch();
    test_readiness_atomic_capacity_and_pretraffic_failure();
    CL_NativeReadinessPilotBeforeNetchanClose(&cls.netchan);
    std::puts("native_client_readiness_pilot_test: ok");
    return EXIT_SUCCESS;
}
