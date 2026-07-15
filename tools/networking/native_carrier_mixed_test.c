/* Deterministic atomic WTC1 DATA+ACK transaction tests. */

#include "common/net/native_carrier_mixed.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                      \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "native_carrier_mixed_test:%d: %s\n", __LINE__,   \
                    #condition);                                              \
            return 1;                                                         \
        }                                                                     \
    } while (0)

enum {
    TEST_TX_CAPACITY = 8,
    TEST_PAYLOAD_CAPACITY = 900,
    TEST_APPLICATION_BUDGET = 512,
    TEST_LEGACY_BYTES = 16,
};

#define TEST_OWNER UINT64_C(0x574f52524d495831)

typedef struct test_fixture_s {
    worr_native_session_binding_v1 binding;
    worr_native_tx_session_v1 session;
    worr_native_tx_slot_v1 slots[TEST_TX_CAPACITY];
    worr_native_carrier_tx_gate_v1 gate;
    worr_native_carrier_dispatch_v1 dispatch;
    worr_native_carrier_ack_ledger_v1 ledger;
    uint8_t payload[TEST_PAYLOAD_CAPACITY];
    uint8_t legacy[TEST_LEGACY_BYTES];
    uint32_t payload_bytes;
    uint32_t payload_handle;
    uint32_t message_sequence;
} test_fixture;

typedef struct test_packet_s {
    uint8_t bytes[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t count;
    worr_native_carrier_mixed_token_v1 token;
} test_packet;

static worr_native_session_binding_v1 test_binding(uint32_t epoch,
                                                   uint64_t owner)
{
    worr_native_session_binding_v1 binding;

    memset(&binding, 0, sizeof(binding));
    binding.struct_size = sizeof(binding);
    binding.schema_version = WORR_NATIVE_SESSION_ABI_VERSION;
    binding.transport_epoch = epoch;
    binding.negotiated_capabilities = WORR_NET_CAP_NATIVE_ENVELOPE_V1;
    binding.connection_owner_id = owner;
    return binding;
}

static worr_native_record_ref_v1 test_record(uint32_t sequence)
{
    worr_native_record_ref_v1 record;

    memset(&record, 0, sizeof(record));
    record.record_class = WORR_NATIVE_RECORD_COMMAND_V1;
    record.record_schema_version = 1;
    record.object_epoch = 9;
    record.object_sequence = sequence;
    return record;
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

static int fixture_init(test_fixture *fixture, uint32_t payload_bytes,
                        uint64_t selection_tick)
{
    uint16_t max_datagram = 0;
    uint32_t index;

    CHECK(payload_bytes != 0 && payload_bytes <= TEST_PAYLOAD_CAPACITY);
    memset(fixture, 0, sizeof(*fixture));
    fixture->binding = test_binding(71, TEST_OWNER);
    fixture->payload_bytes = payload_bytes;
    fixture->payload_handle = 7001;
    for (index = 0; index < payload_bytes; ++index)
        fixture->payload[index] = (uint8_t)(index * 29u + 7u);
    memcpy(fixture->legacy, "mixed-final-data", TEST_LEGACY_BYTES);
    CHECK(Worr_NativeCarrierSessionDataBudgetV1(
        TEST_APPLICATION_BUDGET, TEST_LEGACY_BYTES,
        WORR_NATIVE_CARRIER_MIXED_ACK_RESERVE, &max_datagram));
    CHECK(Worr_NativeTxSessionInitV1(&fixture->session, fixture->slots,
                                     TEST_TX_CAPACITY, &fixture->binding));
    CHECK(Worr_NativeCarrierTxGateInitV1(&fixture->gate, &fixture->binding));
    CHECK(Worr_NativeCarrierAckLedgerInitV1(&fixture->ledger,
                                            &fixture->binding, 3));
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &fixture->session, fixture->slots, TEST_TX_CAPACITY,
              test_record(1), 1, fixture->payload_handle, payload_bytes,
              max_datagram, selection_tick,
              &fixture->message_sequence) == WORR_NATIVE_TX_RETAINED);
    CHECK(Worr_NativeCarrierMixedBeginV1(
              &fixture->gate, &fixture->session, fixture->slots,
              TEST_TX_CAPACITY, selection_tick, 1000, TEST_APPLICATION_BUDGET,
              TEST_LEGACY_BYTES,
              &fixture->dispatch) == WORR_NATIVE_CARRIER_MIXED_OK);
    CHECK(Worr_NativeCarrierSessionDispatchBindPayloadV1(
              &fixture->gate, &fixture->dispatch, fixture->payload_handle,
              fixture->payload,
              payload_bytes) == WORR_NATIVE_CARRIER_SESSION_OK);
    return 0;
}

static int add_receipt(test_fixture *fixture, uint32_t sequence)
{
    uint16_t index;

    for (index = 0; index < WORR_NATIVE_CARRIER_ACK_RECEIPT_CAPACITY;
         ++index) {
        worr_native_carrier_ack_receipt_v1 *receipt =
            &fixture->ledger.receipts[index];

        if ((receipt->state_flags &
             WORR_NATIVE_CARRIER_ACK_RECEIPT_OCCUPIED) != 0) {
            continue;
        }
        memset(receipt, 0, sizeof(*receipt));
        receipt->message_sequence = sequence;
        receipt->handoffs_remaining = fixture->ledger.proactive_handoffs;
        receipt->record_class = WORR_NATIVE_RECORD_COMMAND_V1;
        receipt->state_flags = WORR_NATIVE_CARRIER_ACK_RECEIPT_OCCUPIED;
        ++fixture->ledger.receipt_count;
        ++fixture->ledger.mutation_generation;
        CHECK(Worr_NativeCarrierAckLedgerValidateV1(&fixture->ledger));
        return 0;
    }
    CHECK(0);
    return 1;
}

static worr_native_carrier_ack_receipt_v1 *find_receipt(test_fixture *fixture,
                                                        uint32_t sequence)
{
    uint16_t index;

    for (index = 0; index < WORR_NATIVE_CARRIER_ACK_RECEIPT_CAPACITY;
         ++index) {
        if (fixture->ledger.receipts[index].message_sequence == sequence &&
            (fixture->ledger.receipts[index].state_flags &
             WORR_NATIVE_CARRIER_ACK_RECEIPT_OCCUPIED) != 0) {
            return &fixture->ledger.receipts[index];
        }
    }
    return NULL;
}

static int prepare(test_fixture *fixture, uint64_t now_tick,
                   uint32_t retry_ticks, test_packet *packet)
{
    memset(packet, 0, sizeof(*packet));
    CHECK(Worr_NativeCarrierMixedPreparePacketV1(
              &fixture->gate, &fixture->session, fixture->slots,
              TEST_TX_CAPACITY, &fixture->dispatch, &fixture->ledger, now_tick,
              retry_ticks, fixture->payload_handle, fixture->payload,
              fixture->payload_bytes, fixture->legacy, TEST_LEGACY_BYTES,
              packet->bytes, sizeof(packet->bytes), &packet->count,
              &packet->token) == WORR_NATIVE_CARRIER_MIXED_OK);
    return 0;
}

static int test_success_multifragment_and_backoff(void)
{
    test_fixture fixture;
    test_packet packet;
    worr_native_carrier_view_v1 view;
    worr_native_carrier_tx_gate_v1 gate_before;
    worr_native_tx_session_v1 session_before;
    worr_native_tx_slot_v1 slots_before[TEST_TX_CAPACITY];
    worr_native_carrier_dispatch_v1 dispatch_before;
    worr_native_carrier_mixed_result_v1 result;
    uint16_t fragments = 0;
    uint64_t tick = 2;

    CHECK(fixture_init(&fixture, 700, 1) == 0);
    CHECK(fixture.dispatch.fragmenter.fragment_count > 1);
    CHECK(add_receipt(&fixture, 10) == 0);
    CHECK(add_receipt(&fixture, 11) == 0);
    CHECK(add_receipt(&fixture, 20) == 0);
    CHECK(prepare(&fixture, tick, 100, &packet) == 0);
    CHECK(Worr_NativeCarrierDecodeV1(packet.bytes, packet.count, &view) ==
          WORR_NATIVE_CARRIER_OK);
    CHECK(view.entry_count == 3 &&
          view.entries[0].entry_type == WORR_NATIVE_CARRIER_ENTRY_DATA_V1 &&
          view.entries[1].first_message_sequence == 10 &&
          view.entries[1].last_message_sequence == 11 &&
          view.entries[2].first_message_sequence == 20 &&
          view.entries[2].last_message_sequence == 20 &&
          packet.token.ack_range_count == 2);

    /* The legacy API retains its exact one-DATA contract. */
    gate_before = fixture.gate;
    session_before = fixture.session;
    memcpy(slots_before, fixture.slots, sizeof(slots_before));
    dispatch_before = fixture.dispatch;
    CHECK(Worr_NativeCarrierSessionDispatchConfirmPacketV1(
              &fixture.gate, &fixture.session, fixture.slots, TEST_TX_CAPACITY,
              &fixture.dispatch, tick, packet.bytes,
              packet.count) == WORR_NATIVE_CARRIER_SESSION_STALE_DISPATCH);
    CHECK(memcmp(&fixture.gate, &gate_before, sizeof(gate_before)) == 0 &&
          memcmp(&fixture.session, &session_before, sizeof(session_before)) ==
              0 &&
          memcmp(fixture.slots, slots_before, sizeof(slots_before)) == 0 &&
          memcmp(&fixture.dispatch, &dispatch_before,
                 sizeof(dispatch_before)) == 0);

    result = Worr_NativeCarrierMixedConfirmPacketV1(
        &fixture.gate, &fixture.session, fixture.slots, TEST_TX_CAPACITY,
        &fixture.dispatch, &fixture.ledger, &packet.token, tick, packet.bytes,
        packet.count);
    CHECK(result == WORR_NATIVE_CARRIER_MIXED_OK);
    ++fragments;
    CHECK(find_receipt(&fixture, 10)->handoff_attempts == 1 &&
          find_receipt(&fixture, 11)->handoff_attempts == 1 &&
          find_receipt(&fixture, 20)->handoff_attempts == 1);

    while ((fixture.gate.state_flags & WORR_NATIVE_CARRIER_TX_GATE_ACTIVE) !=
           0) {
        ++tick;
        CHECK(prepare(&fixture, tick, 100, &packet) == 0);
        CHECK(packet.token.ack_range_count == 0 &&
              (packet.token.state_flags &
               WORR_NATIVE_CARRIER_MIXED_TOKEN_ACK_BOUND) == 0);
        result = Worr_NativeCarrierMixedConfirmPacketV1(
            &fixture.gate, &fixture.session, fixture.slots, TEST_TX_CAPACITY,
            &fixture.dispatch, &fixture.ledger, &packet.token, tick,
            packet.bytes, packet.count);
        ++fragments;
        CHECK(result ==
              (fragments == fixture.dispatch.fragmenter.fragment_count
                   ? WORR_NATIVE_CARRIER_MIXED_DISPATCH_COMMITTED
                   : WORR_NATIVE_CARRIER_MIXED_OK));
    }
    CHECK(fragments == fixture.dispatch.fragmenter.fragment_count &&
          fixture.gate.committed_bursts == 1 &&
          fixture.slots[0].send_attempts == 1 &&
          fixture.ledger.telemetry.handoff_commits == 1);
    return 0;
}

static int test_range_cap_and_ack_subsequence(void)
{
    test_fixture fixture;
    test_packet packet;
    worr_native_carrier_view_v1 view;
    uint32_t sequence;
    uint16_t index;

    CHECK(fixture_init(&fixture, 20, 1) == 0);
    for (index = 0; index < 8; ++index)
        CHECK(add_receipt(&fixture, (uint32_t)(10u + index * 10u)) == 0);
    CHECK(prepare(&fixture, 2, 0, &packet) == 0);
    CHECK(Worr_NativeCarrierDecodeV1(packet.bytes, packet.count, &view) ==
          WORR_NATIVE_CARRIER_OK);
    CHECK(packet.token.ack_range_count == 7 && view.entry_count == 8);
    for (index = 0; index < 7; ++index) {
        sequence = (uint32_t)(10u + index * 10u);
        CHECK(view.entries[index + 1u].entry_type ==
                  WORR_NATIVE_CARRIER_ENTRY_ACK_V1 &&
              view.entries[index + 1u].first_message_sequence == sequence &&
              view.entries[index + 1u].last_message_sequence == sequence);
    }
    CHECK(Worr_NativeCarrierMixedConfirmPacketV1(
              &fixture.gate, &fixture.session, fixture.slots, TEST_TX_CAPACITY,
              &fixture.dispatch, &fixture.ledger, &packet.token, 2,
              packet.bytes,
              packet.count) == WORR_NATIVE_CARRIER_MIXED_DISPATCH_COMMITTED);
    CHECK(find_receipt(&fixture, 70)->handoff_attempts == 1 &&
          find_receipt(&fixture, 80)->handoff_attempts == 0);
    return 0;
}

static int test_reject_retry_and_packet_identity(void)
{
    test_fixture fixture;
    test_packet first;
    test_packet retry;
    test_packet duplicate;
    worr_native_carrier_view_v1 view;
    worr_native_carrier_entry_v1 entries[3];
    worr_native_carrier_mixed_token_v1 wrong;
    worr_native_carrier_tx_gate_v1 gate_before;
    worr_native_carrier_dispatch_v1 dispatch_before;
    worr_native_carrier_ack_ledger_v1 ledger_before;
    uint16_t credits;

    CHECK(fixture_init(&fixture, 20, 1) == 0);
    CHECK(add_receipt(&fixture, 41) == 0);
    CHECK(prepare(&fixture, 2, 100, &first) == 0);
    credits = find_receipt(&fixture, 41)->handoffs_remaining;
    gate_before = fixture.gate;
    dispatch_before = fixture.dispatch;
    ledger_before = fixture.ledger;

    first.bytes[0] ^= 0x80;
    CHECK(Worr_NativeCarrierMixedConfirmPacketV1(
              &fixture.gate, &fixture.session, fixture.slots, TEST_TX_CAPACITY,
              &fixture.dispatch, &fixture.ledger, &first.token, 2, first.bytes,
              first.count) == WORR_NATIVE_CARRIER_MIXED_PACKET_MISMATCH);
    first.bytes[0] ^= 0x80;
    CHECK(memcmp(&fixture.gate, &gate_before, sizeof(gate_before)) == 0 &&
          memcmp(&fixture.dispatch, &dispatch_before,
                 sizeof(dispatch_before)) == 0 &&
          memcmp(&fixture.ledger, &ledger_before, sizeof(ledger_before)) == 0);

    wrong = first.token;
    ++wrong.transport_epoch;
    CHECK(Worr_NativeCarrierMixedConfirmPacketV1(
              &fixture.gate, &fixture.session, fixture.slots, TEST_TX_CAPACITY,
              &fixture.dispatch, &fixture.ledger, &wrong, 2, first.bytes,
              first.count) == WORR_NATIVE_CARRIER_MIXED_STALE_TRANSACTION);
    wrong = first.token;
    ++wrong.connection_owner_id;
    CHECK(Worr_NativeCarrierMixedRejectPacketV1(
              &fixture.gate, &fixture.dispatch, &fixture.ledger, &wrong,
              first.bytes,
              first.count) == WORR_NATIVE_CARRIER_MIXED_STALE_TRANSACTION);

    /* A valid carrier with a duplicate ACK tail is not the bound packet. */
    CHECK(Worr_NativeCarrierDecodeV1(first.bytes, first.count, &view) ==
          WORR_NATIVE_CARRIER_OK);
    memset(entries, 0, sizeof(entries));
    entries[0] = view.entries[0];
    entries[0].data_offset = 0;
    entries[1] = view.entries[1];
    entries[2] = view.entries[1];
    memset(&duplicate, 0, sizeof(duplicate));
    CHECK(Worr_NativeCarrierEncodeV1(
              fixture.binding.transport_epoch, fixture.legacy,
              TEST_LEGACY_BYTES, first.bytes + view.entries[0].data_offset,
              view.entries[0].data_bytes, entries, 3, duplicate.bytes,
              sizeof(duplicate.bytes),
              &duplicate.count) == WORR_NATIVE_CARRIER_OK);
    CHECK(Worr_NativeCarrierMixedConfirmPacketV1(
              &fixture.gate, &fixture.session, fixture.slots, TEST_TX_CAPACITY,
              &fixture.dispatch, &fixture.ledger, &first.token, 2,
              duplicate.bytes,
              duplicate.count) == WORR_NATIVE_CARRIER_MIXED_PACKET_MISMATCH);

    CHECK(Worr_NativeCarrierMixedRejectPacketV1(
              &fixture.gate, &fixture.dispatch, &fixture.ledger, &first.token,
              first.bytes, first.count) == WORR_NATIVE_CARRIER_MIXED_OK);
    CHECK(find_receipt(&fixture, 41)->handoffs_remaining == credits &&
          find_receipt(&fixture, 41)->handoff_attempts == 0 &&
          (fixture.ledger.state_flags &
           WORR_NATIVE_CARRIER_ACK_LEDGER_EMIT_ACTIVE) == 0 &&
          fixture.dispatch.fragmenter.next_fragment == 0);

    CHECK(prepare(&fixture, 3, 100, &retry) == 0);
    CHECK(retry.token.ack_range_count == 1 &&
          retry.token.ack_token.token_id != first.token.ack_token.token_id);
    CHECK(Worr_NativeCarrierMixedConfirmPacketV1(
              &fixture.gate, &fixture.session, fixture.slots, TEST_TX_CAPACITY,
              &fixture.dispatch, &fixture.ledger, &retry.token, 3, retry.bytes,
              retry.count) == WORR_NATIVE_CARRIER_MIXED_DISPATCH_COMMITTED);
    CHECK(find_receipt(&fixture, 41)->handoffs_remaining == credits - 1u &&
          find_receipt(&fixture, 41)->handoff_attempts == 1);
    gate_before = fixture.gate;
    ledger_before = fixture.ledger;
    CHECK(Worr_NativeCarrierMixedRejectPacketV1(
              &fixture.gate, &fixture.dispatch, &fixture.ledger, &retry.token,
              retry.bytes,
              retry.count) == WORR_NATIVE_CARRIER_MIXED_STALE_TRANSACTION);
    CHECK(memcmp(&fixture.gate, &gate_before, sizeof(gate_before)) == 0 &&
          memcmp(&fixture.ledger, &ledger_before, sizeof(ledger_before)) == 0);
    return 0;
}

static int test_prepared_abort_terminal(void)
{
    test_fixture fixture;
    test_packet packet;
    worr_native_carrier_mixed_token_v1 stale_token;
    worr_native_carrier_tx_gate_v1 gate_before;
    worr_native_tx_session_v1 session_before;
    worr_native_tx_slot_v1 slots_before[TEST_TX_CAPACITY];
    worr_native_carrier_dispatch_v1 dispatch_before;
    worr_native_carrier_ack_ledger_v1 ledger_before;
    uint16_t credits;

    CHECK(fixture_init(&fixture, 20, 1) == 0);
    CHECK(add_receipt(&fixture, 45) == 0);
    CHECK(prepare(&fixture, 2, 100, &packet) == 0);
    credits = find_receipt(&fixture, 45)->handoffs_remaining;
    gate_before = fixture.gate;
    session_before = fixture.session;
    memcpy(slots_before, fixture.slots, sizeof(slots_before));
    dispatch_before = fixture.dispatch;
    ledger_before = fixture.ledger;
    packet.bytes[0] ^= 0x40;
    CHECK(Worr_NativeCarrierMixedAbortPacketV1(
              &fixture.gate, &fixture.dispatch, &fixture.ledger, &packet.token,
              packet.bytes,
              packet.count) == WORR_NATIVE_CARRIER_MIXED_PACKET_MISMATCH);
    packet.bytes[0] ^= 0x40;
    CHECK(memcmp(&fixture.gate, &gate_before, sizeof(gate_before)) == 0 &&
          memcmp(&fixture.dispatch, &dispatch_before,
                 sizeof(dispatch_before)) == 0 &&
          memcmp(&fixture.ledger, &ledger_before, sizeof(ledger_before)) == 0);
    stale_token = packet.token;
    ++stale_token.dispatch_token_id;
    CHECK(Worr_NativeCarrierMixedAbortPacketV1(
              &fixture.gate, &fixture.dispatch, &fixture.ledger, &stale_token,
              packet.bytes,
              packet.count) == WORR_NATIVE_CARRIER_MIXED_STALE_TRANSACTION);
    CHECK(memcmp(&fixture.gate, &gate_before, sizeof(gate_before)) == 0 &&
          memcmp(&fixture.dispatch, &dispatch_before,
                 sizeof(dispatch_before)) == 0 &&
          memcmp(&fixture.ledger, &ledger_before, sizeof(ledger_before)) == 0);

    CHECK(Worr_NativeCarrierMixedAbortPacketV1(
              &fixture.gate, &fixture.dispatch, &fixture.ledger, &packet.token,
              packet.bytes, packet.count) == WORR_NATIVE_CARRIER_MIXED_OK);
    CHECK(fixture.dispatch.state_flags ==
              WORR_NATIVE_CARRIER_DISPATCH_ABORTED &&
          fixture.gate.aborted_bursts == 1 &&
          (fixture.ledger.state_flags &
           WORR_NATIVE_CARRIER_ACK_LEDGER_EMIT_ACTIVE) == 0 &&
          find_receipt(&fixture, 45)->handoff_attempts == 0 &&
          find_receipt(&fixture, 45)->handoffs_remaining == credits &&
          memcmp(&fixture.session, &session_before, sizeof(session_before)) ==
              0 &&
          memcmp(fixture.slots, slots_before, sizeof(slots_before)) == 0);
    gate_before = fixture.gate;
    ledger_before = fixture.ledger;
    CHECK(Worr_NativeCarrierMixedAbortPacketV1(
              &fixture.gate, &fixture.dispatch, &fixture.ledger, &packet.token,
              packet.bytes,
              packet.count) == WORR_NATIVE_CARRIER_MIXED_STALE_TRANSACTION);
    CHECK(memcmp(&fixture.gate, &gate_before, sizeof(gate_before)) == 0 &&
          memcmp(&fixture.ledger, &ledger_before, sizeof(ledger_before)) == 0);
    return 0;
}

static int test_atomic_terminal_rollback(void)
{
    test_fixture fixture;
    test_packet packet;
    worr_native_carrier_tx_gate_v1 gate_before;
    worr_native_tx_session_v1 session_before;
    worr_native_tx_slot_v1 slots_before[TEST_TX_CAPACITY];
    worr_native_carrier_dispatch_v1 dispatch_before;
    worr_native_carrier_ack_ledger_v1 ledger_before;

    /* DATA rejects a clock that the ACK half would accept. */
    CHECK(fixture_init(&fixture, 20, 20) == 0);
    CHECK(add_receipt(&fixture, 51) == 0);
    CHECK(prepare(&fixture, 10, 100, &packet) == 0);
    gate_before = fixture.gate;
    session_before = fixture.session;
    memcpy(slots_before, fixture.slots, sizeof(slots_before));
    dispatch_before = fixture.dispatch;
    ledger_before = fixture.ledger;
    CHECK(Worr_NativeCarrierMixedConfirmPacketV1(
              &fixture.gate, &fixture.session, fixture.slots, TEST_TX_CAPACITY,
              &fixture.dispatch, &fixture.ledger, &packet.token, 15,
              packet.bytes,
              packet.count) == WORR_NATIVE_CARRIER_MIXED_CLOCK_REGRESSION);
    CHECK(memcmp(&fixture.gate, &gate_before, sizeof(gate_before)) == 0 &&
          memcmp(&fixture.session, &session_before, sizeof(session_before)) ==
              0 &&
          memcmp(fixture.slots, slots_before, sizeof(slots_before)) == 0 &&
          memcmp(&fixture.dispatch, &dispatch_before,
                 sizeof(dispatch_before)) == 0 &&
          memcmp(&fixture.ledger, &ledger_before, sizeof(ledger_before)) == 0);
    CHECK(Worr_NativeCarrierMixedRejectPacketV1(
              &fixture.gate, &fixture.dispatch, &fixture.ledger, &packet.token,
              packet.bytes, packet.count) == WORR_NATIVE_CARRIER_MIXED_OK);

    /* DATA accepts, then ACK rejects a handoff before its prepare tick. */
    CHECK(fixture_init(&fixture, 20, 1) == 0);
    CHECK(add_receipt(&fixture, 52) == 0);
    CHECK(prepare(&fixture, 10, 100, &packet) == 0);
    gate_before = fixture.gate;
    session_before = fixture.session;
    memcpy(slots_before, fixture.slots, sizeof(slots_before));
    dispatch_before = fixture.dispatch;
    ledger_before = fixture.ledger;
    CHECK(Worr_NativeCarrierMixedConfirmPacketV1(
              &fixture.gate, &fixture.session, fixture.slots, TEST_TX_CAPACITY,
              &fixture.dispatch, &fixture.ledger, &packet.token, 5,
              packet.bytes,
              packet.count) == WORR_NATIVE_CARRIER_MIXED_CLOCK_REGRESSION);
    CHECK(memcmp(&fixture.gate, &gate_before, sizeof(gate_before)) == 0 &&
          memcmp(&fixture.session, &session_before, sizeof(session_before)) ==
              0 &&
          memcmp(fixture.slots, slots_before, sizeof(slots_before)) == 0 &&
          memcmp(&fixture.dispatch, &dispatch_before,
                 sizeof(dispatch_before)) == 0 &&
          memcmp(&fixture.ledger, &ledger_before, sizeof(ledger_before)) == 0);
    CHECK(Worr_NativeCarrierMixedRejectPacketV1(
              &fixture.gate, &fixture.dispatch, &fixture.ledger, &packet.token,
              packet.bytes, packet.count) == WORR_NATIVE_CARRIER_MIXED_OK);
    return 0;
}

static int test_begin_limits_prepare_limit_and_abort(void)
{
    test_fixture fixture;
    worr_native_session_binding_v1 binding = test_binding(81, TEST_OWNER);
    worr_native_tx_session_v1 session;
    worr_native_tx_slot_v1 slots[TEST_TX_CAPACITY];
    worr_native_carrier_tx_gate_v1 gate;
    worr_native_carrier_tx_gate_v1 gate_before;
    worr_native_carrier_dispatch_v1 dispatch;
    worr_native_carrier_dispatch_v1 dispatch_before;
    worr_native_carrier_ack_ledger_v1 ledger_before;
    worr_native_carrier_ack_ledger_v1 wrong_ledger;
    worr_native_ack_range_v1 ack_ranges[1];
    worr_native_carrier_ack_emit_token_v1 ack_token;
    test_packet packet;
    test_packet packet_before;
    uint16_t unreserved_datagram = 0;
    uint16_t ack_range_count = 0;
    uint32_t sequence = 0;

    CHECK(Worr_NativeCarrierSessionDataBudgetV1(
        TEST_APPLICATION_BUDGET, TEST_LEGACY_BYTES, 0, &unreserved_datagram));
    CHECK(Worr_NativeTxSessionInitV1(&session, slots, TEST_TX_CAPACITY,
                                     &binding));
    CHECK(Worr_NativeCarrierTxGateInitV1(&gate, &binding));
    CHECK(Worr_NativeTxSessionEnqueueV1(
              &session, slots, TEST_TX_CAPACITY, test_record(2), 1, 9001, 100,
              unreserved_datagram, 1, &sequence) == WORR_NATIVE_TX_RETAINED);
    gate_before = gate;
    memset(&dispatch, 0xa5, sizeof(dispatch));
    dispatch_before = dispatch;
    CHECK(Worr_NativeCarrierMixedBeginV1(
              &gate, &session, slots, TEST_TX_CAPACITY, 1, 1000,
              TEST_APPLICATION_BUDGET, TEST_LEGACY_BYTES,
              &dispatch) == WORR_NATIVE_CARRIER_MIXED_LIMIT);
    CHECK(memcmp(&gate, &gate_before, sizeof(gate_before)) == 0 &&
          memcmp(&dispatch, &dispatch_before, sizeof(dispatch_before)) == 0);

    CHECK(fixture_init(&fixture, 20, 1) == 0);
    CHECK(add_receipt(&fixture, 61) == 0);
    fixture.ledger.mutation_generation = UINT64_MAX - 2u;
    CHECK(Worr_NativeCarrierAckLedgerValidateV1(&fixture.ledger));
    gate_before = fixture.gate;
    dispatch_before = fixture.dispatch;
    ledger_before = fixture.ledger;
    memset(&packet, 0xa5, sizeof(packet));
    packet_before = packet;
    CHECK(Worr_NativeCarrierMixedPreparePacketV1(
              &fixture.gate, &fixture.session, fixture.slots, TEST_TX_CAPACITY,
              &fixture.dispatch, &fixture.ledger, 2, 100,
              fixture.payload_handle, fixture.payload, fixture.payload_bytes,
              fixture.legacy, TEST_LEGACY_BYTES, packet.bytes,
              sizeof(packet.bytes), &packet.count,
              &packet.token) == WORR_NATIVE_CARRIER_MIXED_LIMIT);
    CHECK(
        memcmp(&fixture.gate, &gate_before, sizeof(gate_before)) == 0 &&
        memcmp(&fixture.dispatch, &dispatch_before, sizeof(dispatch_before)) ==
            0 &&
        memcmp(&fixture.ledger, &ledger_before, sizeof(ledger_before)) == 0 &&
        memcmp(&packet, &packet_before, sizeof(packet_before)) == 0);

    CHECK(fixture_init(&fixture, 20, 1) == 0);
    wrong_ledger = fixture.ledger;
    ++wrong_ledger.transport_epoch;
    CHECK(Worr_NativeCarrierAckLedgerValidateV1(&wrong_ledger));
    gate_before = fixture.gate;
    dispatch_before = fixture.dispatch;
    CHECK(Worr_NativeCarrierMixedPreparePacketV1(
              &fixture.gate, &fixture.session, fixture.slots, TEST_TX_CAPACITY,
              &fixture.dispatch, &wrong_ledger, 2, 100, fixture.payload_handle,
              fixture.payload, fixture.payload_bytes, fixture.legacy,
              TEST_LEGACY_BYTES, packet.bytes, sizeof(packet.bytes),
              &packet.count,
              &packet.token) == WORR_NATIVE_CARRIER_MIXED_INVALID_STATE);
    CHECK(memcmp(&fixture.gate, &gate_before, sizeof(gate_before)) == 0 &&
          memcmp(&fixture.dispatch, &dispatch_before,
                 sizeof(dispatch_before)) == 0);
    wrong_ledger = fixture.ledger;
    ++wrong_ledger.connection_owner_id;
    CHECK(Worr_NativeCarrierAckLedgerValidateV1(&wrong_ledger));
    CHECK(Worr_NativeCarrierMixedPreparePacketV1(
              &fixture.gate, &fixture.session, fixture.slots, TEST_TX_CAPACITY,
              &fixture.dispatch, &wrong_ledger, 2, 100, fixture.payload_handle,
              fixture.payload, fixture.payload_bytes, fixture.legacy,
              TEST_LEGACY_BYTES, packet.bytes, sizeof(packet.bytes),
              &packet.count,
              &packet.token) == WORR_NATIVE_CARRIER_MIXED_INVALID_STATE);

    CHECK(add_receipt(&fixture, 62) == 0);
    CHECK(Worr_NativeCarrierAckPrepareRangesV1(
              &fixture.ledger, 2, 100, 1, ack_ranges, 1, &ack_range_count,
              &ack_token) == WORR_NATIVE_CARRIER_ACK_OK);
    gate_before = fixture.gate;
    dispatch_before = fixture.dispatch;
    ledger_before = fixture.ledger;
    CHECK(Worr_NativeCarrierMixedAbortV1(&fixture.gate, &fixture.dispatch,
                                         &fixture.ledger) ==
          WORR_NATIVE_CARRIER_MIXED_INVALID_STATE);
    CHECK(memcmp(&fixture.gate, &gate_before, sizeof(gate_before)) == 0 &&
          memcmp(&fixture.dispatch, &dispatch_before,
                 sizeof(dispatch_before)) == 0 &&
          memcmp(&fixture.ledger, &ledger_before, sizeof(ledger_before)) == 0);
    CHECK(Worr_NativeCarrierAckAbortV1(&fixture.ledger, &ack_token) ==
          WORR_NATIVE_CARRIER_ACK_OK);

    CHECK(fixture_init(&fixture, 20, 1) == 0);
    ledger_before = fixture.ledger;
    CHECK(Worr_NativeCarrierMixedAbortV1(&fixture.gate, &fixture.dispatch,
                                         &fixture.ledger) ==
          WORR_NATIVE_CARRIER_MIXED_OK);
    CHECK(fixture.dispatch.state_flags ==
              WORR_NATIVE_CARRIER_DISPATCH_ABORTED &&
          fixture.gate.aborted_bursts == 1 &&
          memcmp(&fixture.ledger, &ledger_before, sizeof(ledger_before)) == 0);
    return 0;
}

static worr_native_carrier_entry_v1 data_entry(uint32_t bytes)
{
    worr_native_carrier_entry_v1 entry;

    memset(&entry, 0, sizeof(entry));
    entry.struct_size = sizeof(entry);
    entry.schema_version = WORR_NATIVE_CARRIER_ABI_VERSION;
    entry.entry_type = WORR_NATIVE_CARRIER_ENTRY_DATA_V1;
    entry.data_bytes = bytes;
    return entry;
}

static worr_native_carrier_entry_v1 ack_entry(uint32_t first, uint32_t last)
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

static int test_strict_mixed_shape(void)
{
    test_fixture fixture;
    test_packet packet;
    uint8_t malformed[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t malformed_bytes = 0;
    worr_native_carrier_view_v1 view;
    worr_native_carrier_entry_v1 entries[2];
    worr_native_carrier_tx_gate_v1 gate_before;
    worr_native_tx_session_v1 session_before;
    worr_native_tx_slot_v1 slots_before[TEST_TX_CAPACITY];
    worr_native_carrier_dispatch_v1 dispatch_before;

    CHECK(fixture_init(&fixture, 20, 1) == 0);
    CHECK(prepare(&fixture, 2, 100, &packet) == 0);
    CHECK(Worr_NativeCarrierDecodeV1(packet.bytes, packet.count, &view) ==
          WORR_NATIVE_CARRIER_OK);

    entries[0] = ack_entry(5, 5);
    entries[1] = data_entry(view.entries[0].data_bytes);
    CHECK(Worr_NativeCarrierEncodeV1(
              fixture.binding.transport_epoch, fixture.legacy,
              TEST_LEGACY_BYTES, packet.bytes + view.entries[0].data_offset,
              view.entries[0].data_bytes, entries, 2, malformed,
              sizeof(malformed), &malformed_bytes) == WORR_NATIVE_CARRIER_OK);
    fixture.dispatch.pending_packet_bytes = (uint16_t)malformed_bytes;
    fixture.dispatch.pending_packet_crc32 =
        crc32_bytes(malformed, malformed_bytes);
    gate_before = fixture.gate;
    session_before = fixture.session;
    memcpy(slots_before, fixture.slots, sizeof(slots_before));
    dispatch_before = fixture.dispatch;
    CHECK(Worr_NativeCarrierSessionDispatchConfirmMixedPacketV1(
              &fixture.gate, &fixture.session, fixture.slots, TEST_TX_CAPACITY,
              &fixture.dispatch, 2, malformed,
              malformed_bytes) == WORR_NATIVE_CARRIER_SESSION_STALE_DISPATCH);
    CHECK(memcmp(&fixture.gate, &gate_before, sizeof(gate_before)) == 0 &&
          memcmp(&fixture.session, &session_before, sizeof(session_before)) ==
              0 &&
          memcmp(fixture.slots, slots_before, sizeof(slots_before)) == 0 &&
          memcmp(&fixture.dispatch, &dispatch_before,
                 sizeof(dispatch_before)) == 0);

    entries[0] = data_entry(view.entries[0].data_bytes);
    entries[1] = data_entry(view.entries[0].data_bytes);
    CHECK(Worr_NativeCarrierEncodeV1(
              fixture.binding.transport_epoch, fixture.legacy,
              TEST_LEGACY_BYTES, packet.bytes + view.entries[0].data_offset,
              view.entries[0].data_bytes, entries, 2, malformed,
              sizeof(malformed), &malformed_bytes) == WORR_NATIVE_CARRIER_OK);
    fixture.dispatch.pending_packet_bytes = (uint16_t)malformed_bytes;
    fixture.dispatch.pending_packet_crc32 =
        crc32_bytes(malformed, malformed_bytes);
    CHECK(Worr_NativeCarrierSessionDispatchConfirmMixedPacketV1(
              &fixture.gate, &fixture.session, fixture.slots, TEST_TX_CAPACITY,
              &fixture.dispatch, 2, malformed,
              malformed_bytes) == WORR_NATIVE_CARRIER_SESSION_STALE_DISPATCH);
    return 0;
}

static int test_old_data_and_ack_apis(void)
{
    test_fixture fixture;
    test_packet packet;
    worr_native_carrier_ack_emit_token_v1 ack_token;
    worr_native_carrier_view_v1 view;

    CHECK(fixture_init(&fixture, 20, 1) == 0);
    memset(&packet, 0, sizeof(packet));
    CHECK(Worr_NativeCarrierSessionDispatchPreparePacketV1(
              &fixture.gate, &fixture.session, fixture.slots, TEST_TX_CAPACITY,
              &fixture.dispatch, fixture.payload_handle, fixture.payload,
              fixture.payload_bytes, fixture.legacy, TEST_LEGACY_BYTES,
              packet.bytes, sizeof(packet.bytes),
              &packet.count) == WORR_NATIVE_CARRIER_SESSION_OK);
    CHECK(Worr_NativeCarrierDecodeV1(packet.bytes, packet.count, &view) ==
              WORR_NATIVE_CARRIER_OK &&
          view.entry_count == 1);
    CHECK(Worr_NativeCarrierSessionDispatchConfirmPacketV1(
              &fixture.gate, &fixture.session, fixture.slots, TEST_TX_CAPACITY,
              &fixture.dispatch, 2, packet.bytes,
              packet.count) == WORR_NATIVE_CARRIER_SESSION_DISPATCH_COMMITTED);

    CHECK(Worr_NativeCarrierAckLedgerInitV1(&fixture.ledger, &fixture.binding,
                                            3));
    CHECK(add_receipt(&fixture, 91) == 0);
    memset(&packet, 0, sizeof(packet));
    CHECK(Worr_NativeCarrierAckPreparePacketV1(
              &fixture.ledger, 3, 100, TEST_APPLICATION_BUDGET, fixture.legacy,
              TEST_LEGACY_BYTES, packet.bytes, sizeof(packet.bytes),
              &packet.count, &ack_token) == WORR_NATIVE_CARRIER_ACK_OK);
    CHECK(Worr_NativeCarrierAckCommitHandoffV1(&fixture.ledger, &ack_token, 3,
                                               packet.bytes, packet.count) ==
          WORR_NATIVE_CARRIER_ACK_OK);
    CHECK(find_receipt(&fixture, 91)->handoff_attempts == 1);
    return 0;
}

int main(void)
{
    CHECK(test_success_multifragment_and_backoff() == 0);
    CHECK(test_range_cap_and_ack_subsequence() == 0);
    CHECK(test_reject_retry_and_packet_identity() == 0);
    CHECK(test_prepared_abort_terminal() == 0);
    CHECK(test_atomic_terminal_rollback() == 0);
    CHECK(test_begin_limits_prepare_limit_and_abort() == 0);
    CHECK(test_strict_mixed_shape() == 0);
    CHECK(test_old_data_and_ack_apis() == 0);
    puts("native_carrier_mixed_test: ok");
    return 0;
}
