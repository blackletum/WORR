/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/native_carrier.h"

#include <limits.h>
#include <string.h>

enum {
    WTC_ENTRY_TYPE_OFFSET = 0,
    WTC_ENTRY_FLAGS_OFFSET = 1,
    WTC_ENTRY_BYTES_OFFSET = 2,
    WTC_ENTRY_RESERVED_OFFSET = 4,
    WTC_ENTRY_PAYLOAD_OFFSET = 8,

    WTC_FOOTER_VERSION_OFFSET = 0,
    WTC_FOOTER_BYTES_OFFSET = 2,
    WTC_FOOTER_CARRIER_BYTES_OFFSET = 4,
    WTC_FOOTER_LEGACY_BYTES_OFFSET = 8,
    WTC_FOOTER_EPOCH_OFFSET = 12,
    WTC_FOOTER_ENTRY_COUNT_OFFSET = 16,
    WTC_FOOTER_FLAGS_OFFSET = 18,
    WTC_FOOTER_CRC_OFFSET = 20,
    WTC_FOOTER_MAGIC_OFFSET = 24,
};

static const uint8_t wtc_magic[8] = {
    'W', 'O', 'R', 'R', 'W', 'T', 'C', '1'
};

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

static uint16_t read_u16_le(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] |
                      ((uint16_t)bytes[1] << 8));
}

static uint32_t read_u32_le(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static void write_u16_le(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

/* Same reflected IEEE CRC-32 convention as the native WNE1 envelope. */
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

static uint32_t carrier_crc32(const uint8_t *carrier, size_t carrier_bytes)
{
    static const uint8_t zero_crc[sizeof(uint32_t)] = { 0, 0, 0, 0 };
    const size_t footer_offset =
        carrier_bytes - WORR_NATIVE_CARRIER_WIRE_FOOTER_BYTES;
    const size_t crc_offset = footer_offset + WTC_FOOTER_CRC_OFFSET;
    uint32_t crc = UINT32_MAX;

    crc = crc32_update(crc, carrier, crc_offset);
    crc = crc32_update(crc, zero_crc, sizeof(zero_crc));
    crc = crc32_update(crc, carrier + crc_offset + sizeof(uint32_t),
                       carrier_bytes - crc_offset - sizeof(uint32_t));
    return ~crc;
}

static bool entry_header_valid(const worr_native_carrier_entry_v1 *entry)
{
    return entry->struct_size == sizeof(*entry) &&
           entry->schema_version == WORR_NATIVE_CARRIER_ABI_VERSION &&
           entry->reserved0 == 0 && entry->reserved1 == 0;
}

static worr_native_carrier_result_v1 validate_data_entry(
    uint32_t transport_epoch,
    const void *data_arena,
    size_t data_arena_bytes,
    const worr_native_carrier_entry_v1 *entry)
{
    const uint8_t *data = (const uint8_t *)data_arena;
    worr_native_envelope_frame_info_v1 info;
    worr_native_envelope_decode_result_v1 decoded;

    if (entry->data_bytes == 0 ||
        entry->data_offset > data_arena_bytes ||
        entry->data_bytes > data_arena_bytes - entry->data_offset ||
        entry->first_message_sequence != 0 ||
        entry->last_message_sequence != 0 || data_arena == NULL) {
        return WORR_NATIVE_CARRIER_INVALID_ARGUMENT;
    }

    decoded = Worr_NativeEnvelopeDecodeV1(data + entry->data_offset,
                                          entry->data_bytes, &info);
    if (decoded != WORR_NATIVE_ENVELOPE_DECODE_OK ||
        info.transport_epoch != transport_epoch) {
        return WORR_NATIVE_CARRIER_INVALID_ARGUMENT;
    }
    return WORR_NATIVE_CARRIER_OK;
}

worr_native_carrier_result_v1 Worr_NativeCarrierEncodeV1(
    uint32_t transport_epoch,
    const void *legacy_packet,
    size_t legacy_bytes,
    const void *data_arena,
    size_t data_arena_bytes,
    const worr_native_carrier_entry_v1 *entries,
    uint16_t entry_count,
    void *packet_out,
    size_t packet_capacity,
    size_t *packet_bytes_out)
{
    uint8_t staged[WORR_NATIVE_CARRIER_MAX_PACKET_BYTES];
    size_t carrier_bytes = WORR_NATIVE_CARRIER_WIRE_FOOTER_BYTES;
    size_t packet_bytes;
    size_t cursor;
    uint8_t *footer;
    uint16_t index;

    if (transport_epoch == 0 || entries == NULL || entry_count == 0 ||
        packet_out == NULL || packet_bytes_out == NULL ||
        (legacy_bytes != 0 && legacy_packet == NULL) ||
        (data_arena_bytes != 0 && data_arena == NULL)) {
        return WORR_NATIVE_CARRIER_INVALID_ARGUMENT;
    }
    if (entry_count > WORR_NATIVE_CARRIER_MAX_ENTRIES ||
        legacy_bytes > WORR_NATIVE_CARRIER_MAX_PACKET_BYTES) {
        return WORR_NATIVE_CARRIER_LIMIT;
    }

    for (index = 0; index < entry_count; ++index) {
        const worr_native_carrier_entry_v1 *entry = &entries[index];
        size_t wire_bytes;

        if (!entry_header_valid(entry))
            return WORR_NATIVE_CARRIER_INVALID_ARGUMENT;

        if (entry->entry_type == WORR_NATIVE_CARRIER_ENTRY_DATA_V1) {
            const worr_native_carrier_result_v1 result =
                validate_data_entry(transport_epoch, data_arena,
                                    data_arena_bytes, entry);
            if (result != WORR_NATIVE_CARRIER_OK)
                return result;
            wire_bytes = WORR_NATIVE_CARRIER_WIRE_ENTRY_HEADER_BYTES +
                         (size_t)entry->data_bytes;
        } else if (entry->entry_type == WORR_NATIVE_CARRIER_ENTRY_ACK_V1) {
            if (entry->data_offset != 0 || entry->data_bytes != 0 ||
                entry->first_message_sequence == 0 ||
                entry->last_message_sequence <
                    entry->first_message_sequence) {
                return WORR_NATIVE_CARRIER_INVALID_ARGUMENT;
            }
            wire_bytes = WORR_NATIVE_CARRIER_WIRE_ACK_ENTRY_BYTES;
        } else {
            return WORR_NATIVE_CARRIER_INVALID_ARGUMENT;
        }

        if (wire_bytes > UINT16_MAX ||
            wire_bytes > WORR_NATIVE_CARRIER_MAX_PACKET_BYTES ||
            carrier_bytes > WORR_NATIVE_CARRIER_MAX_PACKET_BYTES -
                                wire_bytes) {
            return WORR_NATIVE_CARRIER_LIMIT;
        }
        carrier_bytes += wire_bytes;
    }

    if (legacy_bytes > WORR_NATIVE_CARRIER_MAX_PACKET_BYTES - carrier_bytes)
        return WORR_NATIVE_CARRIER_LIMIT;
    packet_bytes = legacy_bytes + carrier_bytes;
    if (packet_capacity < packet_bytes)
        return WORR_NATIVE_CARRIER_OUTPUT_TOO_SMALL;
    if (ranges_overlap(packet_out, packet_bytes,
                       packet_bytes_out, sizeof(*packet_bytes_out))) {
        return WORR_NATIVE_CARRIER_INVALID_ARGUMENT;
    }

    memset(staged, 0, packet_bytes);
    if (legacy_bytes != 0)
        memcpy(staged, legacy_packet, legacy_bytes);
    cursor = legacy_bytes;
    for (index = 0; index < entry_count; ++index) {
        const worr_native_carrier_entry_v1 *entry = &entries[index];
        uint8_t *wire_entry = staged + cursor;
        uint16_t wire_bytes;

        wire_entry[WTC_ENTRY_TYPE_OFFSET] = entry->entry_type;
        if (entry->entry_type == WORR_NATIVE_CARRIER_ENTRY_DATA_V1) {
            wire_bytes = (uint16_t)(
                WORR_NATIVE_CARRIER_WIRE_ENTRY_HEADER_BYTES +
                entry->data_bytes);
            memcpy(wire_entry + WTC_ENTRY_PAYLOAD_OFFSET,
                   (const uint8_t *)data_arena + entry->data_offset,
                   entry->data_bytes);
        } else {
            wire_bytes = WORR_NATIVE_CARRIER_WIRE_ACK_ENTRY_BYTES;
            write_u32_le(wire_entry + WTC_ENTRY_PAYLOAD_OFFSET,
                         entry->first_message_sequence);
            write_u32_le(wire_entry + WTC_ENTRY_PAYLOAD_OFFSET + 4,
                         entry->last_message_sequence);
        }
        write_u16_le(wire_entry + WTC_ENTRY_BYTES_OFFSET, wire_bytes);
        cursor += wire_bytes;
    }

    footer = staged + packet_bytes - WORR_NATIVE_CARRIER_WIRE_FOOTER_BYTES;
    write_u16_le(footer + WTC_FOOTER_VERSION_OFFSET,
                 WORR_NATIVE_CARRIER_WIRE_VERSION);
    write_u16_le(footer + WTC_FOOTER_BYTES_OFFSET,
                 WORR_NATIVE_CARRIER_WIRE_FOOTER_BYTES);
    write_u32_le(footer + WTC_FOOTER_CARRIER_BYTES_OFFSET,
                 (uint32_t)carrier_bytes);
    write_u32_le(footer + WTC_FOOTER_LEGACY_BYTES_OFFSET,
                 (uint32_t)legacy_bytes);
    write_u32_le(footer + WTC_FOOTER_EPOCH_OFFSET, transport_epoch);
    write_u16_le(footer + WTC_FOOTER_ENTRY_COUNT_OFFSET, entry_count);
    memcpy(footer + WTC_FOOTER_MAGIC_OFFSET,
           wtc_magic, sizeof(wtc_magic));
    write_u32_le(footer + WTC_FOOTER_CRC_OFFSET,
                 carrier_crc32(staged + legacy_bytes, carrier_bytes));

    memmove(packet_out, staged, packet_bytes);
    *packet_bytes_out = packet_bytes;
    return WORR_NATIVE_CARRIER_OK;
}

static worr_native_carrier_result_v1 nested_decode_result(
    worr_native_envelope_decode_result_v1 result)
{
    if (result == WORR_NATIVE_ENVELOPE_DECODE_UNSUPPORTED)
        return WORR_NATIVE_CARRIER_UNSUPPORTED;
    if (result == WORR_NATIVE_ENVELOPE_DECODE_CORRUPT)
        return WORR_NATIVE_CARRIER_CORRUPT;
    return WORR_NATIVE_CARRIER_MALFORMED;
}

worr_native_carrier_result_v1 Worr_NativeCarrierDecodeV1(
    const void *packet,
    size_t packet_bytes,
    worr_native_carrier_view_v1 *view_out)
{
    const uint8_t *bytes = (const uint8_t *)packet;
    const uint8_t *footer;
    const uint8_t *carrier;
    size_t footer_offset;
    size_t carrier_offset;
    size_t entry_end;
    size_t cursor;
    uint32_t carrier_bytes;
    uint32_t legacy_bytes;
    uint32_t transport_epoch;
    uint32_t stored_crc;
    uint16_t entry_count;
    worr_native_carrier_view_v1 decoded;
    uint16_t index;

    if (view_out == NULL || (packet_bytes != 0 && packet == NULL))
        return WORR_NATIVE_CARRIER_INVALID_ARGUMENT;
    if (packet_bytes < sizeof(wtc_magic) ||
        memcmp(bytes + packet_bytes - sizeof(wtc_magic),
               wtc_magic, sizeof(wtc_magic)) != 0) {
        return WORR_NATIVE_CARRIER_NO_CARRIER;
    }
    if (ranges_overlap(packet, packet_bytes, view_out, sizeof(*view_out)))
        return WORR_NATIVE_CARRIER_INVALID_ARGUMENT;
    if (packet_bytes < WORR_NATIVE_CARRIER_WIRE_FOOTER_BYTES)
        return WORR_NATIVE_CARRIER_MALFORMED;
    if (packet_bytes > WORR_NATIVE_CARRIER_MAX_PACKET_BYTES)
        return WORR_NATIVE_CARRIER_LIMIT;

    footer_offset = packet_bytes - WORR_NATIVE_CARRIER_WIRE_FOOTER_BYTES;
    footer = bytes + footer_offset;
    if (read_u16_le(footer + WTC_FOOTER_VERSION_OFFSET) !=
            WORR_NATIVE_CARRIER_WIRE_VERSION ||
        read_u16_le(footer + WTC_FOOTER_BYTES_OFFSET) !=
            WORR_NATIVE_CARRIER_WIRE_FOOTER_BYTES) {
        return WORR_NATIVE_CARRIER_UNSUPPORTED;
    }

    carrier_bytes = read_u32_le(footer + WTC_FOOTER_CARRIER_BYTES_OFFSET);
    legacy_bytes = read_u32_le(footer + WTC_FOOTER_LEGACY_BYTES_OFFSET);
    transport_epoch = read_u32_le(footer + WTC_FOOTER_EPOCH_OFFSET);
    entry_count = read_u16_le(footer + WTC_FOOTER_ENTRY_COUNT_OFFSET);
    if (carrier_bytes < WORR_NATIVE_CARRIER_WIRE_FOOTER_BYTES ||
        carrier_bytes > packet_bytes || legacy_bytes > packet_bytes ||
        (size_t)legacy_bytes + (size_t)carrier_bytes != packet_bytes ||
        transport_epoch == 0 || entry_count == 0 ||
        entry_count > WORR_NATIVE_CARRIER_MAX_ENTRIES ||
        read_u16_le(footer + WTC_FOOTER_FLAGS_OFFSET) != 0) {
        return WORR_NATIVE_CARRIER_MALFORMED;
    }

    carrier_offset = packet_bytes - carrier_bytes;
    if (carrier_offset != legacy_bytes)
        return WORR_NATIVE_CARRIER_MALFORMED;
    carrier = bytes + carrier_offset;
    stored_crc = read_u32_le(footer + WTC_FOOTER_CRC_OFFSET);
    if (stored_crc != carrier_crc32(carrier, carrier_bytes))
        return WORR_NATIVE_CARRIER_CORRUPT;

    memset(&decoded, 0, sizeof(decoded));
    decoded.struct_size = sizeof(decoded);
    decoded.schema_version = WORR_NATIVE_CARRIER_ABI_VERSION;
    decoded.entry_count = entry_count;
    decoded.transport_epoch = transport_epoch;
    decoded.packet_bytes = (uint32_t)packet_bytes;
    decoded.legacy_bytes = legacy_bytes;
    decoded.carrier_bytes = carrier_bytes;
    decoded.carrier_crc32 = stored_crc;

    cursor = carrier_offset;
    entry_end = footer_offset;
    for (index = 0; index < entry_count; ++index) {
        const uint8_t *wire_entry;
        uint16_t wire_bytes;
        uint8_t entry_type;
        worr_native_carrier_entry_v1 *entry = &decoded.entries[index];

        if (cursor > entry_end ||
            entry_end - cursor <
                WORR_NATIVE_CARRIER_WIRE_ENTRY_HEADER_BYTES) {
            return WORR_NATIVE_CARRIER_MALFORMED;
        }
        wire_entry = bytes + cursor;
        entry_type = wire_entry[WTC_ENTRY_TYPE_OFFSET];
        wire_bytes = read_u16_le(wire_entry + WTC_ENTRY_BYTES_OFFSET);
        if (wire_entry[WTC_ENTRY_FLAGS_OFFSET] != 0 ||
            read_u32_le(wire_entry + WTC_ENTRY_RESERVED_OFFSET) != 0 ||
            wire_bytes < WORR_NATIVE_CARRIER_WIRE_ENTRY_HEADER_BYTES ||
            wire_bytes > entry_end - cursor) {
            return WORR_NATIVE_CARRIER_MALFORMED;
        }

        entry->struct_size = sizeof(*entry);
        entry->schema_version = WORR_NATIVE_CARRIER_ABI_VERSION;
        entry->entry_type = entry_type;
        if (entry_type == WORR_NATIVE_CARRIER_ENTRY_DATA_V1) {
            worr_native_envelope_frame_info_v1 info;
            const size_t data_bytes =
                wire_bytes - WORR_NATIVE_CARRIER_WIRE_ENTRY_HEADER_BYTES;
            const worr_native_envelope_decode_result_v1 result =
                Worr_NativeEnvelopeDecodeV1(
                    wire_entry + WTC_ENTRY_PAYLOAD_OFFSET,
                    data_bytes, &info);

            if (result != WORR_NATIVE_ENVELOPE_DECODE_OK)
                return nested_decode_result(result);
            if (info.transport_epoch != transport_epoch)
                return WORR_NATIVE_CARRIER_MALFORMED;
            entry->data_offset =
                (uint32_t)(cursor + WTC_ENTRY_PAYLOAD_OFFSET);
            entry->data_bytes = (uint32_t)data_bytes;
        } else if (entry_type == WORR_NATIVE_CARRIER_ENTRY_ACK_V1) {
            if (wire_bytes != WORR_NATIVE_CARRIER_WIRE_ACK_ENTRY_BYTES)
                return WORR_NATIVE_CARRIER_MALFORMED;
            entry->first_message_sequence =
                read_u32_le(wire_entry + WTC_ENTRY_PAYLOAD_OFFSET);
            entry->last_message_sequence =
                read_u32_le(wire_entry + WTC_ENTRY_PAYLOAD_OFFSET + 4);
            if (entry->first_message_sequence == 0 ||
                entry->last_message_sequence <
                    entry->first_message_sequence) {
                return WORR_NATIVE_CARRIER_MALFORMED;
            }
        } else {
            return WORR_NATIVE_CARRIER_UNSUPPORTED;
        }
        cursor += wire_bytes;
    }

    if (cursor != entry_end)
        return WORR_NATIVE_CARRIER_MALFORMED;

    *view_out = decoded;
    return WORR_NATIVE_CARRIER_OK;
}
