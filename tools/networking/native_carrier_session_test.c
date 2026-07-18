/* Deterministic native-session <-> WTC1 transport-confirmation tests. */

#include "common/net/native_carrier_session.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            fprintf(stderr, "native_carrier_session_test:%d: %s\n",      \
                    __LINE__, #condition);                                 \
            return 1;                                                      \
        }                                                                  \
    } while (0)

enum {
    TEST_TX_CAPACITY = 8,
    TEST_RX_CAPACITY = 4,
    TEST_PAYLOAD_STRIDE = 4096,
};

typedef struct test_packet_s {
    uint8_t bytes[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t count;
} test_packet;

static worr_native_session_binding_v1 test_binding_owned(
    uint32_t epoch, uint64_t connection_owner_id)
{
    worr_native_session_binding_v1 binding;

    memset(&binding, 0, sizeof(binding));
    binding.struct_size = sizeof(binding);
    binding.schema_version = WORR_NATIVE_SESSION_ABI_VERSION;
    binding.transport_epoch = epoch;
    binding.negotiated_capabilities = WORR_NET_CAP_NATIVE_ENVELOPE_V1;
    binding.connection_owner_id = connection_owner_id;
    return binding;
}

static worr_native_session_binding_v1 test_binding(uint32_t epoch)
{
    return test_binding_owned(epoch, UINT64_C(0x574f525200000001));
}

static worr_native_record_ref_v1 test_record(uint8_t record_class,
                                             uint32_t sequence)
{
    worr_native_record_ref_v1 record;

    memset(&record, 0, sizeof(record));
    record.record_class = record_class;
    record.record_schema_version = 1;
    record.object_epoch = 7;
    record.object_sequence = sequence;
    return record;
}

static worr_native_carrier_entry_v1 test_ack_entry(uint32_t first,
                                                   uint32_t last)
{
    worr_native_carrier_entry_v1 entry;

    memset(&entry, 0, sizeof(entry));
    entry.struct_size = sizeof(entry);
    entry.schema_version = WORR_NATIVE_CARRIER_ABI_VERSION;
    entry.entry_type = WORR_NATIVE_CARRIER_ENTRY_ACK_V1;
    entry.first_message_sequence = first;
    entry.last_message_sequence = last;
    return entry;
}

static int encode_ack_packet(uint32_t epoch,
                             const worr_native_carrier_entry_v1 *entries,
                             uint16_t entry_count,
                             test_packet *packet)
{
    size_t bytes = 0;

    CHECK(Worr_NativeCarrierEncodeV1(
              epoch, NULL, 0, NULL, 0, entries, entry_count,
              packet->bytes, sizeof(packet->bytes), &bytes) ==
          WORR_NATIVE_CARRIER_OK);
    packet->count = bytes;
    return 0;
}

static int enqueue_message(worr_native_tx_session_v1 *session,
                           worr_native_tx_slot_v1 *slots,
                           worr_native_record_ref_v1 record,
                           uint32_t payload_handle,
                           uint32_t payload_bytes,
                           uint16_t max_datagram_bytes,
                           uint64_t tick,
                           uint32_t *sequence_out)
{
    CHECK(Worr_NativeTxSessionEnqueueV1(
              session, slots, TEST_TX_CAPACITY, record, 1,
              payload_handle, payload_bytes, max_datagram_bytes, tick,
              sequence_out) == WORR_NATIVE_TX_RETAINED);
    return 0;
}

static int test_budget_gate_and_abort(void)
{
    worr_native_session_binding_v1 binding = test_binding(11);
    worr_native_tx_session_v1 tx;
    worr_native_tx_session_v1 tx_before;
    worr_native_tx_slot_v1 slots[TEST_TX_CAPACITY];
    worr_native_tx_slot_v1 slots_before[TEST_TX_CAPACITY];
    worr_native_carrier_tx_gate_v1 gate;
    worr_native_carrier_tx_gate_v1 gate_before;
    worr_native_carrier_dispatch_v1 dispatch;
    worr_native_carrier_dispatch_v1 dispatch_before;
    uint8_t payload[100];
    uint16_t budget = UINT16_C(0xaaaa);
    uint32_t sequence = 0;

    CHECK(Worr_NativeCarrierSessionDataBudgetV1(1024, 200, 2,
                                                 &budget));
    CHECK(budget == 752);
    CHECK(Worr_NativeCarrierSessionDataBudgetV1(1200, 1047, 0,
                                                 &budget));
    CHECK(budget == 113);
    budget = UINT16_C(0xaaaa);
    CHECK(!Worr_NativeCarrierSessionDataBudgetV1(1201, 0, 0,
                                                  &budget));
    CHECK(budget == UINT16_C(0xaaaa));
    CHECK(!Worr_NativeCarrierSessionDataBudgetV1(1200, 0, 8,
                                                  &budget));

    memset(payload, 0x5a, sizeof(payload));
    CHECK(Worr_NativeTxSessionInitV1(&tx, slots, TEST_TX_CAPACITY,
                                      &binding));
    CHECK(Worr_NativeCarrierTxGateInitV1(&gate, &binding));
    CHECK(Worr_NativeCarrierTxGateValidateV1(&gate));
    CHECK(enqueue_message(
              &tx, slots,
              test_record(WORR_NATIVE_RECORD_COMMAND_V1, 1), 50,
              sizeof(payload), 752, 1, &sequence) == 0);
    tx_before = tx;
    memcpy(slots_before, slots, sizeof(slots));

    memset(&dispatch, 0xa5, sizeof(dispatch));
    dispatch_before = dispatch;
    gate_before = gate;
    tx.connection_owner_id = UINT64_C(0x574f525200000002);
    CHECK(Worr_NativeCarrierSessionDispatchBeginV1(
              &gate, &tx, slots, TEST_TX_CAPACITY, 1, 1000, 1024,
              200, &dispatch) == WORR_NATIVE_CARRIER_SESSION_INVALID_STATE);
    CHECK(memcmp(&gate, &gate_before, sizeof(gate)) == 0 &&
          memcmp(&dispatch, &dispatch_before, sizeof(dispatch)) == 0);
    tx = tx_before;

    CHECK(Worr_NativeCarrierSessionDispatchBeginV1(
              &gate, &tx, slots, TEST_TX_CAPACITY, 1, 1000, 1024,
              200, &dispatch) == WORR_NATIVE_CARRIER_SESSION_OK);
    CHECK(memcmp(&tx, &tx_before, sizeof(tx)) == 0 &&
          memcmp(slots, slots_before, sizeof(slots)) == 0);
    CHECK((gate.state_flags & WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) != 0 &&
          dispatch.send_ticket.pre_send_slot.send_attempts == 0 &&
          dispatch.send_ticket.pre_send_slot.message_sequence == sequence &&
          dispatch.payload_handle == 0);

    dispatch_before = dispatch;
    CHECK(Worr_NativeCarrierSessionDispatchBindPayloadV1(
              &gate, &dispatch, 51, payload, sizeof(payload)) ==
          WORR_NATIVE_CARRIER_SESSION_INVALID_ARGUMENT);
    CHECK(memcmp(&dispatch, &dispatch_before, sizeof(dispatch)) == 0);
    CHECK(Worr_NativeCarrierSessionDispatchBindPayloadV1(
              &gate, &dispatch, 50, payload, sizeof(payload)) ==
          WORR_NATIVE_CARRIER_SESSION_OK);
    CHECK((dispatch.state_flags &
           WORR_NATIVE_CARRIER_DISPATCH_PAYLOAD_BOUND) != 0);

    CHECK(Worr_NativeCarrierSessionDispatchAbortV1(&gate, &dispatch) ==
          WORR_NATIVE_CARRIER_SESSION_OK);
    CHECK((gate.state_flags & WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) == 0 &&
          gate.aborted_bursts == 1 &&
          dispatch.state_flags == WORR_NATIVE_CARRIER_DISPATCH_ABORTED &&
          memcmp(&tx, &tx_before, sizeof(tx)) == 0 &&
          memcmp(slots, slots_before, sizeof(slots)) == 0);

    /* The slot's frozen 752-byte WNE1 plan cannot be silently replanned for
     * a later packet budget with only 460 WNE1 bytes remaining. */
    gate_before = gate;
    memset(&dispatch, 0x9c, sizeof(dispatch));
    dispatch_before = dispatch;
    CHECK(Worr_NativeCarrierSessionDispatchBeginV1(
              &gate, &tx, slots, TEST_TX_CAPACITY, 1, 1000, 700,
              200, &dispatch) == WORR_NATIVE_CARRIER_SESSION_LIMIT);
    CHECK(memcmp(&gate, &gate_before, sizeof(gate)) == 0 &&
          memcmp(&dispatch, &dispatch_before, sizeof(dispatch)) == 0);

    CHECK(Worr_NativeCarrierSessionDispatchBeginV1(
              &gate, &tx, slots, TEST_TX_CAPACITY, 1, 1000, 1024,
              200, &dispatch) == WORR_NATIVE_CARRIER_SESSION_OK);
    CHECK(dispatch.token_id == 2);
    CHECK(Worr_NativeCarrierSessionDispatchAbortV1(&gate, &dispatch) ==
          WORR_NATIVE_CARRIER_SESSION_OK);
    return 0;
}

static int test_transport_confirmed_burst_and_receipt(void)
{
    enum { PAYLOAD_BYTES = 1800, LEGACY_BYTES = 64 };
    worr_native_session_binding_v1 binding = test_binding(21);
    worr_native_tx_session_v1 tx;
    worr_native_tx_session_v1 tx_before_final;
    worr_native_tx_slot_v1 tx_slots[TEST_TX_CAPACITY];
    worr_native_tx_slot_v1 tx_slots_before_final[TEST_TX_CAPACITY];
    worr_native_carrier_tx_gate_v1 gate;
    worr_native_carrier_tx_gate_v1 gate_before;
    worr_native_carrier_dispatch_v1 dispatch;
    worr_native_carrier_dispatch_v1 dispatch_before;
    worr_native_carrier_dispatch_v1 stale_dispatch;
    worr_native_carrier_view_v1 view;
    worr_native_rx_session_v1 rx;
    worr_native_rx_slot_v1 rx_slots[TEST_RX_CAPACITY];
    worr_native_rx_message_v1 message;
    worr_native_rx_message_v1 message_before;
    worr_native_ack_range_v1 repeat;
    worr_native_ack_range_v1 repeat_before;
    worr_native_ack_range_v1 commit_ack;
    worr_native_rx_result_v1 rx_result = WORR_NATIVE_RX_IDLE;
    uint8_t payload[PAYLOAD_BYTES];
    uint8_t reassembly[TEST_RX_CAPACITY * TEST_PAYLOAD_STRIDE];
    uint8_t legacy[LEGACY_BYTES];
    test_packet packets[WORR_NATIVE_ENVELOPE_MAX_FRAGMENTS];
    test_packet rejected_packet;
    uint8_t wrong_packet[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    uint16_t datagram_budget = 0;
    uint16_t packet_count = 0;
    uint32_t sequence = 0;
    uint16_t index;

    for (index = 0; index < PAYLOAD_BYTES; ++index)
        payload[index] = (uint8_t)(index * 17u + 3u);
    memset(legacy, 0, sizeof(legacy));
    memcpy(legacy, "reliable+unreliable-final-prefix", 32);
    memset(reassembly, 0, sizeof(reassembly));
    CHECK(Worr_NativeCarrierSessionDataBudgetV1(
              512, LEGACY_BYTES, 0, &datagram_budget));
    CHECK(datagram_budget == 408);
    CHECK(Worr_NativeTxSessionInitV1(&tx, tx_slots, TEST_TX_CAPACITY,
                                      &binding));
    CHECK(Worr_NativeCarrierTxGateInitV1(&gate, &binding));
    CHECK(enqueue_message(
              &tx, tx_slots,
              test_record(WORR_NATIVE_RECORD_COMMAND_V1, 100), 1001,
              PAYLOAD_BYTES, datagram_budget, 5, &sequence) == 0);
    CHECK(Worr_NativeCarrierSessionDispatchBeginV1(
              &gate, &tx, tx_slots, TEST_TX_CAPACITY, 5, 1000, 512,
              LEGACY_BYTES, &dispatch) == WORR_NATIVE_CARRIER_SESSION_OK);
    CHECK(Worr_NativeCarrierSessionDispatchBindPayloadV1(
              &gate, &dispatch, 1001, payload, PAYLOAD_BYTES) ==
          WORR_NATIVE_CARRIER_SESSION_OK);
    CHECK(dispatch.fragmenter.fragment_count > 1);

    /* Build is reversible and cannot count as a send. */
    rejected_packet.count = 0;
    CHECK(Worr_NativeCarrierSessionDispatchPreparePacketV1(
              &gate, &tx, tx_slots, TEST_TX_CAPACITY, &dispatch, 1001,
              payload, PAYLOAD_BYTES, legacy, LEGACY_BYTES,
              rejected_packet.bytes, sizeof(rejected_packet.bytes),
              &rejected_packet.count) == WORR_NATIVE_CARRIER_SESSION_OK);
    CHECK(tx_slots[0].send_attempts == 0 &&
          dispatch.fragmenter.next_fragment == 0 &&
          dispatch.pending_fragmenter.next_fragment == 1);
    gate_before = gate;
    dispatch_before = dispatch;
    CHECK(Worr_NativeCarrierSessionDispatchPreparePacketV1(
              &gate, &tx, tx_slots, TEST_TX_CAPACITY, &dispatch, 1001,
              payload, PAYLOAD_BYTES, legacy, LEGACY_BYTES,
              packets[0].bytes, sizeof(packets[0].bytes),
              &packets[0].count) ==
          WORR_NATIVE_CARRIER_SESSION_PACKET_PENDING);
    CHECK(memcmp(&gate, &gate_before, sizeof(gate)) == 0 &&
          memcmp(&dispatch, &dispatch_before, sizeof(dispatch)) == 0);

    memcpy(wrong_packet, rejected_packet.bytes, rejected_packet.count);
    wrong_packet[0] ^= 1u; /* Full packet binding includes legacy bytes. */
    CHECK(Worr_NativeCarrierSessionDispatchConfirmPacketV1(
              &gate, &tx, tx_slots, TEST_TX_CAPACITY, &dispatch, 6,
              wrong_packet, rejected_packet.count) ==
          WORR_NATIVE_CARRIER_SESSION_STALE_DISPATCH);
    CHECK(memcmp(&gate, &gate_before, sizeof(gate)) == 0 &&
          memcmp(&dispatch, &dispatch_before, sizeof(dispatch)) == 0 &&
          tx_slots[0].send_attempts == 0);

    CHECK(Worr_NativeCarrierSessionDispatchRejectPacketV1(
              &gate, &dispatch) == WORR_NATIVE_CARRIER_SESSION_OK);
    packets[0].count = 0;
    CHECK(Worr_NativeCarrierSessionDispatchPreparePacketV1(
              &gate, &tx, tx_slots, TEST_TX_CAPACITY, &dispatch, 1001,
              payload, PAYLOAD_BYTES, legacy, LEGACY_BYTES,
              packets[0].bytes, sizeof(packets[0].bytes),
              &packets[0].count) == WORR_NATIVE_CARRIER_SESSION_OK);
    CHECK(packets[0].count == rejected_packet.count &&
          memcmp(packets[0].bytes, rejected_packet.bytes,
                 packets[0].count) == 0);
    CHECK(Worr_NativeCarrierSessionDispatchConfirmPacketV1(
              &gate, &tx, tx_slots, TEST_TX_CAPACITY, &dispatch, 6,
              packets[0].bytes, packets[0].count) ==
          WORR_NATIVE_CARRIER_SESSION_OK);
    CHECK(tx_slots[0].send_attempts == 0 && gate.confirmed_fragments == 1);

    /* Caller-copyable fragmenter metadata cannot diverge from the prepared
     * retained-slot ticket while preserving send accounting. */
    dispatch_before = dispatch;
    gate_before = gate;
    ++dispatch.fragmenter.record.object_sequence;
    CHECK(Worr_NativeCarrierSessionDispatchPreparePacketV1(
              &gate, &tx, tx_slots, TEST_TX_CAPACITY, &dispatch, 1001,
              payload, PAYLOAD_BYTES, legacy, LEGACY_BYTES, wrong_packet,
              sizeof(wrong_packet), &rejected_packet.count) ==
          WORR_NATIVE_CARRIER_SESSION_INVALID_STATE);
    CHECK(memcmp(&gate, &gate_before, sizeof(gate)) == 0);
    dispatch = dispatch_before;
    stale_dispatch = dispatch;
    ++packet_count;

    for (;;) {
        worr_native_carrier_session_result_v1 confirmed;

        CHECK(packet_count < WORR_NATIVE_ENVELOPE_MAX_FRAGMENTS);
        packets[packet_count].count = 0;
        CHECK(Worr_NativeCarrierSessionDispatchPreparePacketV1(
                  &gate, &tx, tx_slots, TEST_TX_CAPACITY, &dispatch,
                  1001, payload, PAYLOAD_BYTES, legacy, LEGACY_BYTES,
                  packets[packet_count].bytes,
                  sizeof(packets[packet_count].bytes),
                  &packets[packet_count].count) ==
              WORR_NATIVE_CARRIER_SESSION_OK);
        CHECK(Worr_NativeCarrierDecodeV1(
                  packets[packet_count].bytes,
                  packets[packet_count].count, &view) ==
              WORR_NATIVE_CARRIER_OK);
        CHECK(view.legacy_bytes == LEGACY_BYTES && view.entry_count == 1 &&
              view.entries[0].entry_type ==
                  WORR_NATIVE_CARRIER_ENTRY_DATA_V1 &&
              memcmp(packets[packet_count].bytes, legacy,
                     LEGACY_BYTES) == 0);
        if (packet_count == 1) {
            /* Pending state must be the exact one-fragment advancement, not
             * merely a plausible cursor/count combination. */
            dispatch_before = dispatch;
            gate_before = gate;
            dispatch.pending_fragmenter.priority =
                (uint8_t)(dispatch.pending_fragmenter.priority + 1u);
            CHECK(Worr_NativeCarrierSessionDispatchConfirmPacketV1(
                      &gate, &tx, tx_slots, TEST_TX_CAPACITY, &dispatch,
                      7, packets[packet_count].bytes,
                      packets[packet_count].count) ==
                  WORR_NATIVE_CARRIER_SESSION_INVALID_STATE);
            CHECK(memcmp(&gate, &gate_before, sizeof(gate)) == 0);
            dispatch = dispatch_before;
        }
        if (packet_count + 1u == dispatch.fragmenter.fragment_count) {
            tx_before_final = tx;
            memcpy(tx_slots_before_final, tx_slots,
                   sizeof(tx_slots_before_final));
        }
        confirmed = Worr_NativeCarrierSessionDispatchConfirmPacketV1(
            &gate, &tx, tx_slots, TEST_TX_CAPACITY, &dispatch,
            (uint64_t)(6u + packet_count), packets[packet_count].bytes,
            packets[packet_count].count);
        ++packet_count;
        if (confirmed == WORR_NATIVE_CARRIER_SESSION_DISPATCH_COMMITTED)
            break;
        CHECK(confirmed == WORR_NATIVE_CARRIER_SESSION_OK);
        CHECK(tx_slots[0].send_attempts == 0);
    }
    CHECK(packet_count == dispatch.fragmenter.fragment_count &&
          tx_before_final.telemetry.select_attempts ==
              tx.telemetry.select_attempts - 1u &&
          tx_slots_before_final[0].send_attempts == 0 &&
          tx_slots[0].send_attempts == 1 &&
          gate.committed_bursts == 1 &&
          (gate.state_flags & WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) == 0 &&
          dispatch.state_flags == WORR_NATIVE_CARRIER_DISPATCH_COMMITTED);

    /* A copied earlier cursor cannot rebuild against the advanced gate. */
    dispatch_before = stale_dispatch;
    gate_before = gate;
    CHECK(Worr_NativeCarrierSessionDispatchPreparePacketV1(
              &gate, &tx, tx_slots, TEST_TX_CAPACITY, &stale_dispatch,
              1001, payload, PAYLOAD_BYTES, legacy, LEGACY_BYTES,
              wrong_packet, sizeof(wrong_packet), &packets[0].count) ==
          WORR_NATIVE_CARRIER_SESSION_INVALID_STATE);
    CHECK(memcmp(&stale_dispatch, &dispatch_before,
                 sizeof(stale_dispatch)) == 0 &&
          memcmp(&gate, &gate_before, sizeof(gate)) == 0);

    memset(&dispatch_before, 0x72, sizeof(dispatch_before));
    dispatch = dispatch_before;
    gate_before = gate;
    CHECK(Worr_NativeCarrierSessionDispatchBeginV1(
              &gate, &tx, tx_slots, TEST_TX_CAPACITY,
              gate.last_handoff_tick, 1000, 512, LEGACY_BYTES,
              &dispatch) == WORR_NATIVE_CARRIER_SESSION_NOT_DUE);
    CHECK(memcmp(&dispatch, &dispatch_before, sizeof(dispatch)) == 0 &&
          memcmp(&gate, &gate_before, sizeof(gate)) == 0);

    /* Reassembly and an exact repeat receipt prove the sent burst is usable. */
    CHECK(Worr_NativeRxSessionInitV1(
              &rx, rx_slots, TEST_RX_CAPACITY, TEST_PAYLOAD_STRIDE,
              1000, 1000, &binding));
    memset(&message, 0x91, sizeof(message));
    message_before = message;
    memset(&repeat, 0x92, sizeof(repeat));
    repeat_before = repeat;
    for (index = 0; index < packet_count; ++index) {
        CHECK(Worr_NativeCarrierSessionAcceptDataV1(
                  &rx, rx_slots, TEST_RX_CAPACITY, reassembly,
                  sizeof(reassembly), 20 + index, packets[index].bytes,
                  packets[index].count, 0, &rx_result, &message,
                  &repeat) == WORR_NATIVE_CARRIER_SESSION_OK);
        CHECK(rx_result ==
              (index + 1u == packet_count
                   ? WORR_NATIVE_RX_MESSAGE_COMPLETE
                   : WORR_NATIVE_RX_FRAGMENT_ACCEPTED));
        CHECK(memcmp(&repeat, &repeat_before, sizeof(repeat)) == 0);
        if (index + 1u != packet_count)
            CHECK(memcmp(&message, &message_before, sizeof(message)) == 0);
    }
    CHECK(message.message_sequence == sequence &&
          memcmp(reassembly + message.payload_offset, payload,
                 PAYLOAD_BYTES) == 0);
    CHECK(Worr_NativeRxSessionCommitV1(
              &rx, rx_slots, TEST_RX_CAPACITY, message.slot_index,
              message.message_sequence, &commit_ack) ==
          WORR_NATIVE_RX_COMMITTED);
    memset(&message, 0x93, sizeof(message));
    message_before = message;
    memset(&repeat, 0x94, sizeof(repeat));
    CHECK(Worr_NativeCarrierSessionAcceptDataV1(
              &rx, rx_slots, TEST_RX_CAPACITY, reassembly,
              sizeof(reassembly), 100, packets[0].bytes,
              packets[0].count, 0, &rx_result, &message, &repeat) ==
          WORR_NATIVE_CARRIER_SESSION_OK);
    CHECK(rx_result == WORR_NATIVE_RX_ALREADY_COMMITTED &&
          memcmp(&message, &message_before, sizeof(message)) == 0 &&
          repeat.first_message_sequence == sequence &&
          repeat.last_message_sequence == sequence);
    {
        const worr_native_carrier_entry_v1 ack =
            test_ack_entry(sequence, sequence);
        test_packet ack_packet;
        uint32_t acknowledged = UINT32_MAX;

        CHECK(encode_ack_packet(21, &ack, 1, &ack_packet) == 0);
        CHECK(Worr_NativeCarrierSessionApplyAcksV1(
                  &tx, tx_slots, TEST_TX_CAPACITY, ack_packet.bytes,
                  ack_packet.count, &acknowledged) ==
              WORR_NATIVE_CARRIER_SESSION_OK);
        CHECK(acknowledged == 1 && tx.retained_count == 0);
    }
    return 0;
}

static int test_stale_snapshot_and_unsent_ack(void)
{
    enum { PAYLOAD_BYTES = 800 };
    worr_native_session_binding_v1 binding = test_binding(31);
    worr_native_tx_session_v1 tx;
    worr_native_tx_slot_v1 slots[TEST_TX_CAPACITY];
    worr_native_carrier_tx_gate_v1 gate;
    worr_native_carrier_tx_gate_v1 gate_before;
    worr_native_carrier_dispatch_v1 dispatch;
    worr_native_carrier_dispatch_v1 dispatch_before;
    uint8_t old_payload[PAYLOAD_BYTES];
    uint8_t new_payload[PAYLOAD_BYTES];
    uint8_t blocked_legacy = 0x5a;
    test_packet packet;
    worr_native_carrier_entry_v1 ack;
    uint16_t datagram_budget = 0;
    uint32_t old_sequence = 0;
    uint32_t new_sequence = 0;
    uint32_t newest_sequence = 0;
    uint32_t acknowledged = UINT32_C(0xabcdef01);

    memset(old_payload, 0x31, sizeof(old_payload));
    memset(new_payload, 0x32, sizeof(new_payload));
    CHECK(Worr_NativeCarrierSessionDataBudgetV1(
              256, 0, 0, &datagram_budget));
    CHECK(datagram_budget == 216);
    CHECK(Worr_NativeTxSessionInitV1(&tx, slots, TEST_TX_CAPACITY,
                                      &binding));
    CHECK(Worr_NativeCarrierTxGateInitV1(&gate, &binding));
    CHECK(enqueue_message(
              &tx, slots,
              test_record(WORR_NATIVE_RECORD_SNAPSHOT_V1, 1), 3001,
              PAYLOAD_BYTES, datagram_budget, 1, &old_sequence) == 0);
    CHECK(Worr_NativeCarrierSessionDispatchBeginV1(
              &gate, &tx, slots, TEST_TX_CAPACITY, 1, 1000, 256, 0,
              &dispatch) == WORR_NATIVE_CARRIER_SESSION_OK);
    CHECK(Worr_NativeCarrierSessionDispatchBindPayloadV1(
              &gate, &dispatch, 3001, old_payload, PAYLOAD_BYTES) ==
          WORR_NATIVE_CARRIER_SESSION_OK);
    packet.count = 0;
    CHECK(Worr_NativeCarrierSessionDispatchPreparePacketV1(
              &gate, &tx, slots, TEST_TX_CAPACITY, &dispatch, 3001,
              old_payload, PAYLOAD_BYTES, NULL, 0, packet.bytes,
              sizeof(packet.bytes), &packet.count) ==
          WORR_NATIVE_CARRIER_SESSION_OK);
    CHECK(Worr_NativeCarrierSessionDispatchConfirmPacketV1(
              &gate, &tx, slots, TEST_TX_CAPACITY, &dispatch, 2,
              packet.bytes, packet.count) == WORR_NATIVE_CARRIER_SESSION_OK);
    CHECK(slots[0].send_attempts == 0);

    /* A burst selected by an async zero-legacy packet must defer, not fail,
     * if an ordinary packet later offers a larger legacy prefix. */
    gate_before = gate;
    dispatch_before = dispatch;
    packet.count = UINT16_MAX;
    CHECK(Worr_NativeCarrierSessionDispatchPreparePacketV1(
              &gate, &tx, slots, TEST_TX_CAPACITY, &dispatch, 3001,
              old_payload, PAYLOAD_BYTES, &blocked_legacy, 1,
              packet.bytes, sizeof(packet.bytes), &packet.count) ==
          WORR_NATIVE_CARRIER_SESSION_OUTPUT_TOO_SMALL);
    CHECK(memcmp(&gate, &gate_before, sizeof(gate)) == 0 &&
          memcmp(&dispatch, &dispatch_before, sizeof(dispatch)) == 0 &&
          packet.count == UINT16_MAX);

    CHECK(Worr_NativeTxSessionEnqueueV1(
              &tx, slots, TEST_TX_CAPACITY,
              test_record(WORR_NATIVE_RECORD_SNAPSHOT_V1, 2), 1, 3002,
              PAYLOAD_BYTES, datagram_budget, 2, &new_sequence) ==
          WORR_NATIVE_TX_SUPERSEDED);
    CHECK(new_sequence != old_sequence && slots[0].send_attempts == 0);
    CHECK(Worr_NativeCarrierSessionDispatchPreparePacketV1(
              &gate, &tx, slots, TEST_TX_CAPACITY, &dispatch, 3001,
              old_payload, PAYLOAD_BYTES, NULL, 0, packet.bytes,
              sizeof(packet.bytes), &packet.count) ==
          WORR_NATIVE_CARRIER_SESSION_STALE_DISPATCH);
    CHECK(Worr_NativeCarrierSessionDispatchAbortV1(&gate, &dispatch) ==
          WORR_NATIVE_CARRIER_SESSION_OK);

    /* If supersession occurs after synchronous transport acceptance but
     * before confirmation, the exact accepted outcome retires the stale
     * dispatch and releases the gate without recording a send. */
    CHECK(Worr_NativeCarrierSessionDispatchBeginV1(
              &gate, &tx, slots, TEST_TX_CAPACITY, 2, 1000, 256, 0,
              &dispatch) == WORR_NATIVE_CARRIER_SESSION_OK);
    CHECK(Worr_NativeCarrierSessionDispatchBindPayloadV1(
              &gate, &dispatch, 3002, new_payload, PAYLOAD_BYTES) ==
          WORR_NATIVE_CARRIER_SESSION_OK);
    packet.count = 0;
    CHECK(Worr_NativeCarrierSessionDispatchPreparePacketV1(
              &gate, &tx, slots, TEST_TX_CAPACITY, &dispatch, 3002,
              new_payload, PAYLOAD_BYTES, NULL, 0, packet.bytes,
              sizeof(packet.bytes), &packet.count) ==
          WORR_NATIVE_CARRIER_SESSION_OK);
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &tx, slots, TEST_TX_CAPACITY,
              test_record(WORR_NATIVE_RECORD_SNAPSHOT_V1, 3), 1, 3003,
              PAYLOAD_BYTES, datagram_budget, 2, &newest_sequence) ==
          WORR_NATIVE_TX_SUPERSEDED);
    CHECK(Worr_NativeCarrierSessionDispatchConfirmPacketV1(
              &gate, &tx, slots, TEST_TX_CAPACITY, &dispatch, 3,
              packet.bytes, packet.count) ==
          WORR_NATIVE_CARRIER_SESSION_DISPATCH_RETIRED);
    CHECK((gate.state_flags & WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) == 0 &&
          gate.retired_bursts == 1 &&
          dispatch.state_flags == WORR_NATIVE_CARRIER_DISPATCH_RETIRED &&
          slots[0].message_sequence == newest_sequence &&
          slots[0].send_attempts == 0);

    ack = test_ack_entry(newest_sequence, newest_sequence);
    CHECK(encode_ack_packet(31, &ack, 1, &packet) == 0);
    CHECK(Worr_NativeCarrierSessionApplyAcksV1(
              &tx, slots, TEST_TX_CAPACITY, packet.bytes, packet.count,
              &acknowledged) == WORR_NATIVE_CARRIER_SESSION_UNSENT_ACK);
    CHECK(acknowledged == UINT32_C(0xabcdef01) && tx.retained_count == 1 &&
          slots[0].message_sequence == newest_sequence);
    return 0;
}

static int test_transactional_ack_batch(void)
{
    worr_native_session_binding_v1 binding = test_binding(41);
    worr_native_tx_session_v1 tx;
    worr_native_tx_session_v1 tx_before;
    worr_native_tx_slot_v1 slots[TEST_TX_CAPACITY];
    worr_native_tx_slot_v1 slots_before[TEST_TX_CAPACITY];
    worr_native_tx_slot_v1 selected;
    worr_native_carrier_entry_v1 entries[2];
    test_packet packet;
    uint8_t payload[2][32];
    uint32_t sequences[2];
    uint32_t acknowledged = UINT32_C(0x11223344);
    uint16_t index;

    memset(payload, 0x41, sizeof(payload));
    CHECK(Worr_NativeTxSessionInitV1(&tx, slots, TEST_TX_CAPACITY,
                                      &binding));
    for (index = 0; index < 2; ++index) {
        CHECK(enqueue_message(
                  &tx, slots,
                  test_record(WORR_NATIVE_RECORD_EVENT_V1, index + 1u),
                  4000 + index, sizeof(payload[index]), 256, 1,
                  &sequences[index]) == 0);
    }
    CHECK(Worr_NativeTxSessionSelectDueV1(
              &tx, slots, TEST_TX_CAPACITY, 1, 1000, &selected) ==
          WORR_NATIVE_TX_SELECTED);
    entries[0] = test_ack_entry(sequences[0], sequences[0]);
    entries[1] = test_ack_entry(sequences[1], sequences[1]);
    CHECK(encode_ack_packet(41, entries, 2, &packet) == 0);
    tx_before = tx;
    memcpy(slots_before, slots, sizeof(slots));
    CHECK(Worr_NativeCarrierSessionApplyAcksV1(
              &tx, slots, TEST_TX_CAPACITY, packet.bytes, packet.count,
              &acknowledged) == WORR_NATIVE_CARRIER_SESSION_UNSENT_ACK);
    CHECK(memcmp(&tx, &tx_before, sizeof(tx)) == 0 &&
          memcmp(slots, slots_before, sizeof(slots)) == 0 &&
          acknowledged == UINT32_C(0x11223344));

    CHECK(Worr_NativeTxSessionSelectDueV1(
              &tx, slots, TEST_TX_CAPACITY, 1, 1000, &selected) ==
          WORR_NATIVE_TX_SELECTED);
    CHECK(Worr_NativeCarrierSessionApplyAcksV1(
              &tx, slots, TEST_TX_CAPACITY, packet.bytes, packet.count,
              &acknowledged) == WORR_NATIVE_CARRIER_SESSION_OK);
    CHECK(acknowledged == 2 && tx.retained_count == 0);
    return 0;
}

static int test_token_exhaustion_and_epoch_reset(void)
{
    worr_native_session_binding_v1 binding = test_binding(51);
    worr_native_session_binding_v1 newer = test_binding(52);
    worr_native_session_binding_v1 wrong_owner =
        test_binding_owned(52, UINT64_C(0x574f525200000002));
    worr_native_tx_session_v1 tx;
    worr_native_tx_slot_v1 slots[TEST_TX_CAPACITY];
    worr_native_carrier_tx_gate_v1 gate;
    worr_native_carrier_dispatch_v1 dispatch;
    uint8_t payload[16];
    uint32_t sequence = 0;

    memset(payload, 0x51, sizeof(payload));
    CHECK(Worr_NativeTxSessionInitV1(&tx, slots, TEST_TX_CAPACITY,
                                      &binding));
    CHECK(Worr_NativeCarrierTxGateInitV1(&gate, &binding));
    CHECK(enqueue_message(
              &tx, slots,
              test_record(WORR_NATIVE_RECORD_COMMAND_V1, 1), 5100,
              sizeof(payload), 256, 1, &sequence) == 0);
    gate.next_token_id = UINT64_MAX;
    CHECK(Worr_NativeCarrierTxGateValidateV1(&gate));
    CHECK(Worr_NativeCarrierSessionDispatchBeginV1(
              &gate, &tx, slots, TEST_TX_CAPACITY, 1, 1000, 512, 0,
              &dispatch) == WORR_NATIVE_CARRIER_SESSION_OK);
    CHECK(dispatch.token_id == UINT64_MAX &&
          (gate.state_flags &
           WORR_NATIVE_CARRIER_TX_GATE_TOKEN_EXHAUSTED) != 0);
    CHECK(Worr_NativeCarrierSessionDispatchAbortV1(&gate, &dispatch) ==
          WORR_NATIVE_CARRIER_SESSION_OK);
    CHECK(Worr_NativeCarrierSessionDispatchBeginV1(
              &gate, &tx, slots, TEST_TX_CAPACITY, 1, 1000, 512, 0,
              &dispatch) == WORR_NATIVE_CARRIER_SESSION_TOKEN_EXHAUSTED);
    CHECK(!Worr_NativeCarrierTxGateAdvanceEpochV1(&gate, &wrong_owner));
    CHECK(Worr_NativeCarrierTxGateAdvanceEpochV1(&gate, &newer));
    CHECK(gate.transport_epoch == 52 && gate.next_token_id == 1 &&
          gate.connection_owner_id == binding.connection_owner_id &&
          gate.committed_bursts == 0 && gate.aborted_bursts == 0);
    return 0;
}

int main(void)
{
    CHECK(test_budget_gate_and_abort() == 0);
    CHECK(test_transport_confirmed_burst_and_receipt() == 0);
    CHECK(test_stale_snapshot_and_unsent_ack() == 0);
    CHECK(test_transactional_ack_batch() == 0);
    CHECK(test_token_exhaustion_and_epoch_reset() == 0);
    puts("native carrier/session transport-confirmation tests passed");
    return 0;
}
