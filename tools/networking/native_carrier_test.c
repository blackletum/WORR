/* Deterministic bounded WTC1 native carrier tests. */

#include "common/net/native_carrier.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "native_carrier_test:%d: %s\n", __LINE__,     \
                    #condition);                                             \
            return false;                                                    \
        }                                                                    \
    } while (0)

static uint8_t envelope_payload[WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES];

static uint16_t load_u16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] |
                      ((uint16_t)bytes[1] << 8));
}

static uint32_t load_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static void store_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void store_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *bytes, size_t count)
{
    size_t index;

    for (index = 0; index < count; ++index) {
        unsigned bit;

        crc ^= bytes[index];
        for (bit = 0; bit < 8; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return crc;
}

static uint32_t test_carrier_crc(const uint8_t *carrier,
                                 size_t carrier_bytes)
{
    static const uint8_t zero_crc[4] = { 0, 0, 0, 0 };
    const size_t crc_offset = carrier_bytes - 12u;
    uint32_t crc = UINT32_MAX;

    crc = crc32_update(crc, carrier, crc_offset);
    crc = crc32_update(crc, zero_crc, sizeof(zero_crc));
    crc = crc32_update(crc, carrier + crc_offset + 4u,
                       carrier_bytes - crc_offset - 4u);
    return ~crc;
}

static bool repair_carrier_crc(uint8_t *packet, size_t packet_bytes)
{
    uint8_t *footer;
    uint32_t carrier_bytes;

    CHECK(packet_bytes >= WORR_NATIVE_CARRIER_WIRE_FOOTER_BYTES);
    footer = packet + packet_bytes - WORR_NATIVE_CARRIER_WIRE_FOOTER_BYTES;
    carrier_bytes = load_u32(footer + 4);
    CHECK(carrier_bytes >= WORR_NATIVE_CARRIER_WIRE_FOOTER_BYTES);
    CHECK(carrier_bytes <= packet_bytes);
    memset(footer + 20, 0, 4);
    store_u32(footer + 20,
              test_carrier_crc(packet + packet_bytes - carrier_bytes,
                               carrier_bytes));
    return true;
}

static worr_native_record_ref_v1 make_record(uint32_t sequence)
{
    worr_native_record_ref_v1 record;

    memset(&record, 0, sizeof(record));
    record.record_class = WORR_NATIVE_RECORD_EVENT_V1;
    record.record_schema_version = 1;
    record.object_epoch = 9;
    record.object_sequence = sequence;
    return record;
}

static bool make_envelope(uint32_t transport_epoch,
                          uint32_t message_sequence,
                          uint16_t datagram_bytes,
                          uint32_t payload_bytes,
                          uint8_t *output,
                          size_t output_capacity,
                          size_t *output_bytes)
{
    worr_native_envelope_fragmenter_v1 fragmenter;
    uint32_t index;

    CHECK(payload_bytes != 0);
    CHECK(payload_bytes <= sizeof(envelope_payload));
    for (index = 0; index < payload_bytes; ++index)
        envelope_payload[index] =
            (uint8_t)((index * 113u + message_sequence * 7u) & 0xffu);
    CHECK(Worr_NativeEnvelopeFragmenterInitV1(
        &fragmenter, transport_epoch, message_sequence,
        make_record(message_sequence), 3, envelope_payload, payload_bytes,
        datagram_bytes));
    CHECK(fragmenter.fragment_count == 1);
    CHECK(Worr_NativeEnvelopeFragmentNextV1(
              &fragmenter, envelope_payload, payload_bytes, output,
              output_capacity, output_bytes) ==
          WORR_NATIVE_ENVELOPE_EMIT_OK);
    CHECK((fragmenter.state_flags & WORR_NATIVE_FRAGMENTER_EXHAUSTED) != 0);
    CHECK(*output_bytes == datagram_bytes);
    return true;
}

static worr_native_carrier_entry_v1 data_entry(uint32_t offset,
                                                uint32_t bytes)
{
    worr_native_carrier_entry_v1 entry;

    memset(&entry, 0, sizeof(entry));
    entry.struct_size = sizeof(entry);
    entry.schema_version = WORR_NATIVE_CARRIER_ABI_VERSION;
    entry.entry_type = WORR_NATIVE_CARRIER_ENTRY_DATA_V1;
    entry.data_offset = offset;
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

static bool expect_decode(const uint8_t *packet,
                          size_t packet_bytes,
                          worr_native_carrier_result_v1 expected)
{
    worr_native_carrier_view_v1 view;
    worr_native_carrier_view_v1 before;

    memset(&view, 0xa5, sizeof(view));
    before = view;
    CHECK(Worr_NativeCarrierDecodeV1(packet, packet_bytes, &view) == expected);
    CHECK(memcmp(&view, &before, sizeof(view)) == 0);
    return true;
}

static bool expect_encode_failure(
    uint32_t transport_epoch,
    const void *legacy_packet,
    size_t legacy_bytes,
    const void *data_arena,
    size_t data_arena_bytes,
    const worr_native_carrier_entry_v1 *entries,
    uint16_t entry_count,
    size_t packet_capacity,
    worr_native_carrier_result_v1 expected)
{
    uint8_t output[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    uint8_t before[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t output_bytes = SIZE_MAX;

    memset(output, 0x5c, sizeof(output));
    memcpy(before, output, sizeof(output));
    CHECK(Worr_NativeCarrierEncodeV1(
              transport_epoch, legacy_packet, legacy_bytes,
              data_arena, data_arena_bytes, entries, entry_count,
              output, packet_capacity, &output_bytes) == expected);
    CHECK(output_bytes == SIZE_MAX);
    CHECK(memcmp(output, before, sizeof(output)) == 0);
    return true;
}

static bool test_legacy_detection(void)
{
    static const uint8_t legacy[] = { 0x12, 0x34, 0x56, 0x78, 0x9a };
    static const uint8_t marker[] = {
        'W', 'O', 'R', 'R', 'W', 'T', 'C', '1'
    };
    uint8_t oversized[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES + 1u];

    CHECK(expect_decode(NULL, 0, WORR_NATIVE_CARRIER_NO_CARRIER));
    CHECK(expect_decode(legacy, sizeof(legacy),
                        WORR_NATIVE_CARRIER_NO_CARRIER));
    CHECK(expect_decode(marker, sizeof(marker),
                        WORR_NATIVE_CARRIER_MALFORMED));
    memset(oversized, 0x44, sizeof(oversized));
    CHECK(expect_decode(oversized, sizeof(oversized),
                        WORR_NATIVE_CARRIER_NO_CARRIER));
    memcpy(oversized + sizeof(oversized) - sizeof(marker),
           marker, sizeof(marker));
    CHECK(expect_decode(oversized, sizeof(oversized),
                        WORR_NATIVE_CARRIER_LIMIT));
    return true;
}

static bool test_golden_packet_and_crc_domain(void)
{
    static const uint8_t legacy[] = { 0xde, 0xad, 0xbe };
    static const uint8_t golden[] = {
        0xde, 0xad, 0xbe,
        0x02, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x44, 0x33, 0x22, 0x11, 0x88, 0x77, 0x66, 0x55,
        0x01, 0x00, 0x20, 0x00, 0x30, 0x00, 0x00, 0x00,
        0x03, 0x00, 0x00, 0x00, 0xd4, 0xc3, 0xb2, 0xa1,
        0x01, 0x00, 0x00, 0x00, 0xa1, 0x45, 0x7f, 0xa6,
        0x57, 0x4f, 0x52, 0x52, 0x57, 0x54, 0x43, 0x31,
    };
    uint8_t encoded[sizeof(golden)];
    uint8_t legacy_mutated[sizeof(golden)];
    worr_native_carrier_entry_v1 acknowledgement =
        ack_entry(UINT32_C(0x11223344), UINT32_C(0x55667788));
    worr_native_carrier_view_v1 view;
    size_t encoded_bytes = 0;

    CHECK(Worr_NativeCarrierEncodeV1(
              UINT32_C(0xa1b2c3d4), legacy, sizeof(legacy), NULL, 0,
              &acknowledgement, 1, encoded, sizeof(encoded),
              &encoded_bytes) == WORR_NATIVE_CARRIER_OK);
    CHECK(encoded_bytes == sizeof(golden));
    CHECK(memcmp(encoded, golden, sizeof(golden)) == 0);

    memcpy(legacy_mutated, golden, sizeof(golden));
    legacy_mutated[0] ^= 0xffu;
    CHECK(test_carrier_crc(golden + sizeof(legacy),
                           sizeof(golden) - sizeof(legacy)) ==
          test_carrier_crc(legacy_mutated + sizeof(legacy),
                           sizeof(legacy_mutated) - sizeof(legacy)));
    CHECK(Worr_NativeCarrierDecodeV1(legacy_mutated,
                                     sizeof(legacy_mutated), &view) ==
          WORR_NATIVE_CARRIER_OK);
    CHECK(view.legacy_bytes == sizeof(legacy));
    CHECK(view.carrier_crc32 == UINT32_C(0xa67f45a1));
    return true;
}

static bool test_mixed_round_trip_and_alias(void)
{
    static const uint8_t legacy[] = {
        0x01, 0x7f, 0x80, 0x55, 0xaa, 0x04, 0x10
    };
    uint8_t arena[512];
    uint8_t encoded[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    uint8_t reencoded[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    uint8_t inplace[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    worr_native_carrier_entry_v1 entries[4];
    worr_native_carrier_view_v1 view;
    worr_native_carrier_view_v1 second_view;
    size_t first_bytes;
    size_t second_bytes;
    size_t second_offset;
    size_t encoded_bytes = 0;
    size_t reencoded_bytes = 0;
    size_t inplace_bytes = 0;
    const uint8_t *footer;
    uint32_t expected_second_data_offset;
    const uint32_t transport_epoch = UINT32_C(0x01020304);
    uint16_t entry_index;

    memset(arena, 0xcc, sizeof(arena));
    CHECK(make_envelope(transport_epoch, 41, 88, 32,
                        arena, sizeof(arena), &first_bytes));
    second_offset = first_bytes + 11u;
    CHECK(make_envelope(transport_epoch, 42, 96, 40,
                        arena + second_offset,
                        sizeof(arena) - second_offset, &second_bytes));
    entries[0] = data_entry(0, (uint32_t)first_bytes);
    entries[1] = ack_entry(3, 7);
    entries[2] = data_entry((uint32_t)second_offset,
                            (uint32_t)second_bytes);
    entries[3] = ack_entry(UINT32_MAX, UINT32_MAX);

    memset(encoded, 0x5a, sizeof(encoded));
    CHECK(Worr_NativeCarrierEncodeV1(
              transport_epoch, legacy, sizeof(legacy), arena,
              second_offset + second_bytes, entries, 4,
              encoded, sizeof(encoded), &encoded_bytes) ==
          WORR_NATIVE_CARRIER_OK);
    CHECK(encoded_bytes == sizeof(legacy) +
                               (8u + first_bytes) + 16u +
                               (8u + second_bytes) + 16u +
                               WORR_NATIVE_CARRIER_WIRE_FOOTER_BYTES);
    CHECK(encoded_bytes <= WORR_NATIVE_CARRIER_MAX_PACKET_BYTES);
    CHECK(memcmp(encoded, legacy, sizeof(legacy)) == 0);

    footer = encoded + encoded_bytes -
             WORR_NATIVE_CARRIER_WIRE_FOOTER_BYTES;
    CHECK(load_u16(footer) == WORR_NATIVE_CARRIER_WIRE_VERSION);
    CHECK(load_u16(footer + 2) ==
          WORR_NATIVE_CARRIER_WIRE_FOOTER_BYTES);
    CHECK(load_u32(footer + 4) == encoded_bytes - sizeof(legacy));
    CHECK(load_u32(footer + 8) == sizeof(legacy));
    CHECK(load_u32(footer + 12) == transport_epoch);
    CHECK(footer[12] == 0x04 && footer[13] == 0x03 &&
          footer[14] == 0x02 && footer[15] == 0x01);
    CHECK(load_u16(footer + 16) == 4);
    CHECK(load_u16(footer + 18) == 0);
    CHECK(memcmp(footer + 24, "WORRWTC1", 8) == 0);
    CHECK(load_u32(footer + 20) ==
          test_carrier_crc(encoded + sizeof(legacy),
                           encoded_bytes - sizeof(legacy)));

    memset(&view, 0xcc, sizeof(view));
    CHECK(Worr_NativeCarrierDecodeV1(encoded, encoded_bytes, &view) ==
          WORR_NATIVE_CARRIER_OK);
    CHECK(view.struct_size == sizeof(view));
    CHECK(view.schema_version == WORR_NATIVE_CARRIER_ABI_VERSION);
    CHECK(view.entry_count == 4);
    CHECK(view.transport_epoch == transport_epoch);
    CHECK(view.packet_bytes == encoded_bytes);
    CHECK(view.legacy_bytes == sizeof(legacy));
    CHECK(view.carrier_bytes == encoded_bytes - sizeof(legacy));
    CHECK(view.carrier_crc32 == load_u32(footer + 20));
    CHECK(view.reserved0 == 0);
    for (entry_index = 0; entry_index < view.entry_count; ++entry_index) {
        CHECK(view.entries[entry_index].struct_size ==
              sizeof(view.entries[entry_index]));
        CHECK(view.entries[entry_index].schema_version ==
              WORR_NATIVE_CARRIER_ABI_VERSION);
        CHECK(view.entries[entry_index].reserved0 == 0);
        CHECK(view.entries[entry_index].reserved1 == 0);
    }

    CHECK(view.entries[0].entry_type ==
          WORR_NATIVE_CARRIER_ENTRY_DATA_V1);
    CHECK(view.entries[0].data_offset == sizeof(legacy) + 8u);
    CHECK(view.entries[0].data_bytes == first_bytes);
    CHECK(view.entries[0].first_message_sequence == 0);
    CHECK(view.entries[0].last_message_sequence == 0);
    CHECK(memcmp(encoded + view.entries[0].data_offset,
                 arena, first_bytes) == 0);
    CHECK(view.entries[1].entry_type ==
          WORR_NATIVE_CARRIER_ENTRY_ACK_V1);
    CHECK(view.entries[1].first_message_sequence == 3);
    CHECK(view.entries[1].last_message_sequence == 7);
    CHECK(view.entries[1].data_offset == 0);
    CHECK(view.entries[1].data_bytes == 0);
    CHECK(load_u32(encoded + sizeof(legacy) + 8u + first_bytes + 8u) == 3);
    CHECK(load_u32(encoded + sizeof(legacy) + 8u + first_bytes + 12u) == 7);

    expected_second_data_offset =
        (uint32_t)(sizeof(legacy) + 8u + first_bytes + 16u + 8u);
    CHECK(view.entries[2].data_offset == expected_second_data_offset);
    CHECK(view.entries[2].data_bytes == second_bytes);
    CHECK(memcmp(encoded + view.entries[2].data_offset,
                 arena + second_offset, second_bytes) == 0);
    CHECK(view.entries[3].first_message_sequence == UINT32_MAX);
    CHECK(view.entries[3].last_message_sequence == UINT32_MAX);
    CHECK(view.entries[4].struct_size == 0);

    CHECK(Worr_NativeCarrierEncodeV1(
              view.transport_epoch, encoded, view.legacy_bytes,
              encoded, encoded_bytes, view.entries, view.entry_count,
              reencoded, sizeof(reencoded), &reencoded_bytes) ==
          WORR_NATIVE_CARRIER_OK);
    CHECK(reencoded_bytes == encoded_bytes);
    CHECK(memcmp(reencoded, encoded, encoded_bytes) == 0);
    CHECK(Worr_NativeCarrierDecodeV1(reencoded, reencoded_bytes,
                                     &second_view) ==
          WORR_NATIVE_CARRIER_OK);

    memcpy(inplace, encoded, encoded_bytes);
    CHECK(Worr_NativeCarrierEncodeV1(
              view.transport_epoch, inplace, view.legacy_bytes,
              inplace, encoded_bytes, view.entries, view.entry_count,
              inplace, sizeof(inplace), &inplace_bytes) ==
          WORR_NATIVE_CARRIER_OK);
    CHECK(inplace_bytes == encoded_bytes);
    CHECK(memcmp(inplace, encoded, encoded_bytes) == 0);
    return true;
}

static bool test_complete_packet_boundaries(void)
{
    uint8_t legacy[1041];
    uint8_t output[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    uint8_t envelope[WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES];
    worr_native_carrier_entry_v1 acknowledgements[9];
    worr_native_carrier_entry_v1 data;
    worr_native_carrier_view_v1 view;
    size_t envelope_bytes;
    size_t output_bytes = 0;
    uint16_t index;

    memset(legacy, 0x72, sizeof(legacy));
    for (index = 0; index < 9; ++index)
        acknowledgements[index] = ack_entry(index + 1u, index + 1u);
    acknowledgements[7] = ack_entry(100, UINT32_MAX);

    CHECK(Worr_NativeCarrierEncodeV1(
              5, legacy, 1040, NULL, 0, acknowledgements, 8,
              output, sizeof(output), &output_bytes) ==
          WORR_NATIVE_CARRIER_OK);
    CHECK(output_bytes == WORR_NATIVE_CARRIER_MAX_PACKET_BYTES);
    CHECK(Worr_NativeCarrierDecodeV1(output, output_bytes, &view) ==
          WORR_NATIVE_CARRIER_OK);
    CHECK(view.legacy_bytes == 1040);
    CHECK(view.entry_count == 8);
    CHECK(view.entries[7].first_message_sequence == 100);
    CHECK(view.entries[7].last_message_sequence == UINT32_MAX);

    CHECK(expect_encode_failure(
        5, legacy, 1040, NULL, 0, acknowledgements, 8,
        WORR_NATIVE_CARRIER_MAX_PACKET_BYTES - 1u,
        WORR_NATIVE_CARRIER_OUTPUT_TOO_SMALL));
    CHECK(expect_encode_failure(
        5, legacy, 1041, NULL, 0, acknowledgements, 8,
        WORR_NATIVE_CARRIER_MAX_PACKET_BYTES, WORR_NATIVE_CARRIER_LIMIT));
    CHECK(expect_encode_failure(
        5, legacy, 1, NULL, 0, acknowledgements, 9,
        WORR_NATIVE_CARRIER_MAX_PACKET_BYTES, WORR_NATIVE_CARRIER_LIMIT));

    CHECK(make_envelope(5, 88, 1160, 1104,
                        envelope, sizeof(envelope), &envelope_bytes));
    CHECK(envelope_bytes == 1160);
    data = data_entry(0, (uint32_t)envelope_bytes);
    CHECK(Worr_NativeCarrierEncodeV1(
              5, NULL, 0, envelope, envelope_bytes, &data, 1,
              output, sizeof(output), &output_bytes) ==
          WORR_NATIVE_CARRIER_OK);
    CHECK(output_bytes == WORR_NATIVE_CARRIER_MAX_PACKET_BYTES);
    CHECK(Worr_NativeCarrierDecodeV1(output, output_bytes, &view) ==
          WORR_NATIVE_CARRIER_OK);
    CHECK(view.legacy_bytes == 0);
    CHECK(view.entries[0].data_bytes == envelope_bytes);
    return true;
}

static bool test_matching_magic_malformed_and_corrupt(void)
{
    static const uint8_t legacy[] = { 0x11, 0x22, 0x33, 0x44 };
    uint8_t valid[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    uint8_t bad[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    uint8_t oversized[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES + 1u];
    worr_native_carrier_entry_v1 acknowledgement = ack_entry(20, 30);
    size_t valid_bytes = 0;
    size_t footer_offset;
    size_t cut;

    CHECK(Worr_NativeCarrierEncodeV1(
              12, legacy, sizeof(legacy), NULL, 0,
              &acknowledgement, 1, valid, sizeof(valid), &valid_bytes) ==
          WORR_NATIVE_CARRIER_OK);
    footer_offset = valid_bytes - WORR_NATIVE_CARRIER_WIRE_FOOTER_BYTES;

    memcpy(bad, valid, valid_bytes);
    bad[sizeof(legacy) + 8u] ^= 1u;
    CHECK(expect_decode(bad, valid_bytes, WORR_NATIVE_CARRIER_CORRUPT));

    memcpy(bad, valid, valid_bytes);
    bad[footer_offset + 18u] = 1;
    CHECK(repair_carrier_crc(bad, valid_bytes));
    CHECK(expect_decode(bad, valid_bytes, WORR_NATIVE_CARRIER_MALFORMED));
    memcpy(bad, valid, valid_bytes);
    store_u32(bad + footer_offset + 12u, 0);
    CHECK(repair_carrier_crc(bad, valid_bytes));
    CHECK(expect_decode(bad, valid_bytes, WORR_NATIVE_CARRIER_MALFORMED));
    memcpy(bad, valid, valid_bytes);
    store_u16(bad + footer_offset + 16u, 0);
    CHECK(repair_carrier_crc(bad, valid_bytes));
    CHECK(expect_decode(bad, valid_bytes, WORR_NATIVE_CARRIER_MALFORMED));
    memcpy(bad, valid, valid_bytes);
    store_u16(bad + footer_offset + 16u, 9);
    CHECK(repair_carrier_crc(bad, valid_bytes));
    CHECK(expect_decode(bad, valid_bytes, WORR_NATIVE_CARRIER_MALFORMED));
    memcpy(bad, valid, valid_bytes);
    store_u32(bad + footer_offset + 8u, sizeof(legacy) + 1u);
    CHECK(expect_decode(bad, valid_bytes, WORR_NATIVE_CARRIER_MALFORMED));
    memcpy(bad, valid, valid_bytes);
    store_u32(bad + footer_offset + 4u,
              WORR_NATIVE_CARRIER_WIRE_FOOTER_BYTES - 1u);
    CHECK(expect_decode(bad, valid_bytes, WORR_NATIVE_CARRIER_MALFORMED));

    memcpy(bad, valid, valid_bytes);
    bad[sizeof(legacy) + 1u] = 1;
    CHECK(repair_carrier_crc(bad, valid_bytes));
    CHECK(expect_decode(bad, valid_bytes, WORR_NATIVE_CARRIER_MALFORMED));
    memcpy(bad, valid, valid_bytes);
    bad[sizeof(legacy) + 4u] = 1;
    CHECK(repair_carrier_crc(bad, valid_bytes));
    CHECK(expect_decode(bad, valid_bytes, WORR_NATIVE_CARRIER_MALFORMED));
    memcpy(bad, valid, valid_bytes);
    store_u16(bad + sizeof(legacy) + 2u, 8);
    CHECK(repair_carrier_crc(bad, valid_bytes));
    CHECK(expect_decode(bad, valid_bytes, WORR_NATIVE_CARRIER_MALFORMED));
    memcpy(bad, valid, valid_bytes);
    store_u32(bad + sizeof(legacy) + 8u, 0);
    CHECK(repair_carrier_crc(bad, valid_bytes));
    CHECK(expect_decode(bad, valid_bytes, WORR_NATIVE_CARRIER_MALFORMED));
    memcpy(bad, valid, valid_bytes);
    store_u32(bad + sizeof(legacy) + 8u, 31);
    store_u32(bad + sizeof(legacy) + 12u, 30);
    CHECK(repair_carrier_crc(bad, valid_bytes));
    CHECK(expect_decode(bad, valid_bytes, WORR_NATIVE_CARRIER_MALFORMED));

    memcpy(bad, valid, valid_bytes);
    bad[sizeof(legacy)] = 3;
    CHECK(repair_carrier_crc(bad, valid_bytes));
    CHECK(expect_decode(bad, valid_bytes, WORR_NATIVE_CARRIER_UNSUPPORTED));
    memcpy(bad, valid, valid_bytes);
    store_u16(bad + footer_offset, 2);
    CHECK(expect_decode(bad, valid_bytes, WORR_NATIVE_CARRIER_UNSUPPORTED));
    memcpy(bad, valid, valid_bytes);
    store_u16(bad + footer_offset + 2u, 31);
    CHECK(expect_decode(bad, valid_bytes, WORR_NATIVE_CARRIER_UNSUPPORTED));

    for (cut = 1; cut <= valid_bytes - 8u; ++cut) {
        CHECK(expect_decode(valid + cut, valid_bytes - cut,
                            WORR_NATIVE_CARRIER_MALFORMED));
    }
    CHECK(expect_decode(valid, valid_bytes - 1u,
                        WORR_NATIVE_CARRIER_NO_CARRIER));
    CHECK(expect_decode(valid + valid_bytes - 7u, 7,
                        WORR_NATIVE_CARRIER_NO_CARRIER));

    memset(oversized, 0x6a, sizeof(oversized));
    memcpy(oversized + sizeof(oversized) - 8u, "WORRWTC1", 8);
    CHECK(expect_decode(oversized, sizeof(oversized),
                        WORR_NATIVE_CARRIER_LIMIT));
    return true;
}

static bool test_data_validation_and_epoch(void)
{
    uint8_t epoch_a[128];
    uint8_t epoch_b[128];
    uint8_t valid[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    uint8_t bad[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    worr_native_carrier_entry_v1 data;
    size_t epoch_a_bytes;
    size_t epoch_b_bytes;
    size_t valid_bytes = 0;

    CHECK(make_envelope(21, 1, 88, 32,
                        epoch_a, sizeof(epoch_a), &epoch_a_bytes));
    CHECK(make_envelope(22, 1, 88, 32,
                        epoch_b, sizeof(epoch_b), &epoch_b_bytes));
    CHECK(epoch_a_bytes == epoch_b_bytes);
    data = data_entry(0, (uint32_t)epoch_a_bytes);
    CHECK(Worr_NativeCarrierEncodeV1(
              21, NULL, 0, epoch_a, epoch_a_bytes, &data, 1,
              valid, sizeof(valid), &valid_bytes) ==
          WORR_NATIVE_CARRIER_OK);

    memcpy(bad, valid, valid_bytes);
    bad[8] = 'X';
    CHECK(repair_carrier_crc(bad, valid_bytes));
    CHECK(expect_decode(bad, valid_bytes, WORR_NATIVE_CARRIER_MALFORMED));

    memcpy(bad, valid, valid_bytes);
    bad[8u + 4u] = 2;
    CHECK(repair_carrier_crc(bad, valid_bytes));
    CHECK(expect_decode(bad, valid_bytes, WORR_NATIVE_CARRIER_UNSUPPORTED));

    memcpy(bad, valid, valid_bytes);
    bad[8u + WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES] ^= 1u;
    CHECK(repair_carrier_crc(bad, valid_bytes));
    CHECK(expect_decode(bad, valid_bytes, WORR_NATIVE_CARRIER_CORRUPT));

    memcpy(bad, valid, valid_bytes);
    memcpy(bad + 8u, epoch_b, epoch_b_bytes);
    CHECK(repair_carrier_crc(bad, valid_bytes));
    CHECK(expect_decode(bad, valid_bytes, WORR_NATIVE_CARRIER_MALFORMED));
    return true;
}

static bool test_transactional_encode_rejections(void)
{
    static const uint8_t legacy[] = { 1, 2, 3 };
    uint8_t envelope_a[128];
    uint8_t envelope_b[128];
    uint8_t invalid_envelope[128];
    worr_native_carrier_entry_v1 entry;
    size_t envelope_a_bytes;
    size_t envelope_b_bytes;
    union {
        max_align_t alignment;
        uint8_t bytes[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    } aliased;
    uint8_t alias_before[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];

    CHECK(make_envelope(31, 1, 88, 32,
                        envelope_a, sizeof(envelope_a), &envelope_a_bytes));
    CHECK(make_envelope(32, 1, 88, 32,
                        envelope_b, sizeof(envelope_b), &envelope_b_bytes));
    memset(invalid_envelope, 0, sizeof(invalid_envelope));

    entry = ack_entry(4, 8);
    entry.reserved1 = 1;
    CHECK(expect_encode_failure(
        31, legacy, sizeof(legacy), NULL, 0, &entry, 1,
        WORR_NATIVE_CARRIER_MAX_PACKET_BYTES,
        WORR_NATIVE_CARRIER_INVALID_ARGUMENT));
    entry = ack_entry(4, 8);
    entry.reserved0 = 1;
    CHECK(expect_encode_failure(
        31, legacy, sizeof(legacy), NULL, 0, &entry, 1,
        WORR_NATIVE_CARRIER_MAX_PACKET_BYTES,
        WORR_NATIVE_CARRIER_INVALID_ARGUMENT));
    entry = ack_entry(4, 8);
    entry.struct_size--;
    CHECK(expect_encode_failure(
        31, legacy, sizeof(legacy), NULL, 0, &entry, 1,
        WORR_NATIVE_CARRIER_MAX_PACKET_BYTES,
        WORR_NATIVE_CARRIER_INVALID_ARGUMENT));
    entry = ack_entry(8, 4);
    CHECK(expect_encode_failure(
        31, legacy, sizeof(legacy), NULL, 0, &entry, 1,
        WORR_NATIVE_CARRIER_MAX_PACKET_BYTES,
        WORR_NATIVE_CARRIER_INVALID_ARGUMENT));
    entry = ack_entry(0, 4);
    CHECK(expect_encode_failure(
        31, legacy, sizeof(legacy), NULL, 0, &entry, 1,
        WORR_NATIVE_CARRIER_MAX_PACKET_BYTES,
        WORR_NATIVE_CARRIER_INVALID_ARGUMENT));
    entry = ack_entry(1, 1);
    entry.data_bytes = 1;
    CHECK(expect_encode_failure(
        31, legacy, sizeof(legacy), envelope_a, envelope_a_bytes,
        &entry, 1, WORR_NATIVE_CARRIER_MAX_PACKET_BYTES,
        WORR_NATIVE_CARRIER_INVALID_ARGUMENT));

    entry = data_entry(0, (uint32_t)envelope_a_bytes);
    entry.first_message_sequence = 1;
    CHECK(expect_encode_failure(
        31, legacy, sizeof(legacy), envelope_a, envelope_a_bytes,
        &entry, 1, WORR_NATIVE_CARRIER_MAX_PACKET_BYTES,
        WORR_NATIVE_CARRIER_INVALID_ARGUMENT));
    entry = data_entry((uint32_t)envelope_a_bytes, 1);
    CHECK(expect_encode_failure(
        31, legacy, sizeof(legacy), envelope_a, envelope_a_bytes,
        &entry, 1, WORR_NATIVE_CARRIER_MAX_PACKET_BYTES,
        WORR_NATIVE_CARRIER_INVALID_ARGUMENT));
    entry = data_entry(0, (uint32_t)envelope_b_bytes);
    CHECK(expect_encode_failure(
        31, legacy, sizeof(legacy), envelope_b, envelope_b_bytes,
        &entry, 1, WORR_NATIVE_CARRIER_MAX_PACKET_BYTES,
        WORR_NATIVE_CARRIER_INVALID_ARGUMENT));
    entry = data_entry(0, 88);
    CHECK(expect_encode_failure(
        31, legacy, sizeof(legacy), invalid_envelope,
        sizeof(invalid_envelope), &entry, 1,
        WORR_NATIVE_CARRIER_MAX_PACKET_BYTES,
        WORR_NATIVE_CARRIER_INVALID_ARGUMENT));

    entry = ack_entry(1, 1);
    CHECK(expect_encode_failure(
        0, legacy, sizeof(legacy), NULL, 0, &entry, 1,
        WORR_NATIVE_CARRIER_MAX_PACKET_BYTES,
        WORR_NATIVE_CARRIER_INVALID_ARGUMENT));
    CHECK(expect_encode_failure(
        31, legacy, sizeof(legacy), NULL, 0, &entry, 0,
        WORR_NATIVE_CARRIER_MAX_PACKET_BYTES,
        WORR_NATIVE_CARRIER_INVALID_ARGUMENT));

    memset(&aliased, 0x3d, sizeof(aliased));
    memcpy(alias_before, aliased.bytes, sizeof(alias_before));
    CHECK(Worr_NativeCarrierEncodeV1(
              31, legacy, sizeof(legacy), NULL, 0, &entry, 1,
              aliased.bytes, sizeof(aliased.bytes),
              (size_t *)(void *)aliased.bytes) ==
          WORR_NATIVE_CARRIER_INVALID_ARGUMENT);
    CHECK(memcmp(aliased.bytes, alias_before, sizeof(alias_before)) == 0);
    return true;
}

static bool test_decode_alias_rejection(void)
{
    worr_native_carrier_entry_v1 entry = ack_entry(1, 1);
    union {
        max_align_t alignment;
        uint8_t bytes[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    } packet;
    uint8_t before[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t packet_bytes = 0;

    memset(&packet, 0xa6, sizeof(packet));
    CHECK(Worr_NativeCarrierEncodeV1(
              2, NULL, 0, NULL, 0, &entry, 1,
              packet.bytes, sizeof(packet.bytes), &packet_bytes) ==
          WORR_NATIVE_CARRIER_OK);
    memcpy(before, packet.bytes, sizeof(before));
    CHECK(Worr_NativeCarrierDecodeV1(
              packet.bytes, packet_bytes,
              (worr_native_carrier_view_v1 *)(void *)packet.bytes) ==
          WORR_NATIVE_CARRIER_INVALID_ARGUMENT);
    CHECK(memcmp(packet.bytes, before, sizeof(before)) == 0);
    return true;
}

int main(void)
{
    if (!test_legacy_detection() ||
        !test_golden_packet_and_crc_domain() ||
        !test_mixed_round_trip_and_alias() ||
        !test_complete_packet_boundaries() ||
        !test_matching_magic_malformed_and_corrupt() ||
        !test_data_validation_and_epoch() ||
        !test_transactional_encode_rejections() ||
        !test_decode_alias_rejection()) {
        return 1;
    }

    puts("native carrier tests passed");
    return 0;
}
