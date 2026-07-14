/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "shared/native_envelope.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Bounded native sideband trailer foundation.  WTC1 is not emitted by a live
 * path yet.  The fixed footer ends in the eight-byte WORRWTC1 marker, allowing
 * a receiver to distinguish an ordinary legacy packet without probing its
 * contents.  A matching terminal marker always denotes a carrier candidate;
 * malformed candidates are errors and never silently fall back to legacy.
 */
#define WORR_NATIVE_CARRIER_ABI_VERSION 1u
#define WORR_NATIVE_CARRIER_WIRE_VERSION 1u
#define WORR_NATIVE_CARRIER_MAX_PACKET_BYTES \
    WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES
#define WORR_NATIVE_CARRIER_MAX_ENTRIES 8u
#define WORR_NATIVE_CARRIER_WIRE_ENTRY_HEADER_BYTES 8u
#define WORR_NATIVE_CARRIER_WIRE_ACK_ENTRY_BYTES 16u
#define WORR_NATIVE_CARRIER_WIRE_FOOTER_BYTES 32u

/*
 * Wire entries begin with:
 *   type:u8, flags:u8=0, entry_bytes:u16le, reserved:u32le=0.
 * DATA appends one complete WNE1 datagram.  ACK appends first:u32le and
 * last:u32le.  The fixed terminal footer is:
 *   version:u16le, footer_bytes:u16le, carrier_bytes:u32le,
 *   legacy_bytes:u32le, transport_epoch:u32le, entry_count:u16le,
 *   flags:u16le=0, carrier_crc32:u32le, magic:"WORRWTC1".
 * CRC-32 covers the entry region and footer, excluding the legacy prefix and
 * treating the footer CRC field as zero.
 */

typedef enum worr_native_carrier_entry_type_v1_e {
    WORR_NATIVE_CARRIER_ENTRY_DATA_V1 = 1,
    WORR_NATIVE_CARRIER_ENTRY_ACK_V1 = 2,
} worr_native_carrier_entry_type_v1;

/*
 * Pointer-free entry descriptor.
 *
 * For EncodeV1 DATA input, data_offset/data_bytes name a complete WNE1
 * datagram inside data_arena.  For decoded DATA, they are offsets into the
 * complete input packet.  ACK entries keep both data fields zero and describe
 * the inclusive, exact message-sequence range [first,last].  No sequence
 * outside that range is implied to have arrived.  DATA entries keep both ACK
 * fields zero.  The carrier footer's transport_epoch applies to every entry.
 */
typedef struct worr_native_carrier_entry_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint8_t entry_type;
    uint8_t reserved0;
    uint32_t data_offset;
    uint32_t data_bytes;
    uint32_t first_message_sequence;
    uint32_t last_message_sequence;
    uint32_t reserved1;
} worr_native_carrier_entry_v1;

/* Pointer-free decoded view.  All byte locations are packet-relative. */
typedef struct worr_native_carrier_view_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t entry_count;
    uint32_t transport_epoch;
    uint32_t packet_bytes;
    uint32_t legacy_bytes;
    uint32_t carrier_bytes;
    uint32_t carrier_crc32;
    uint32_t reserved0;
    worr_native_carrier_entry_v1
        entries[WORR_NATIVE_CARRIER_MAX_ENTRIES];
} worr_native_carrier_view_v1;

typedef enum worr_native_carrier_result_v1_e {
    WORR_NATIVE_CARRIER_OK = 0,
    WORR_NATIVE_CARRIER_NO_CARRIER = 1,
    WORR_NATIVE_CARRIER_INVALID_ARGUMENT = 2,
    WORR_NATIVE_CARRIER_OUTPUT_TOO_SMALL = 3,
    WORR_NATIVE_CARRIER_LIMIT = 4,
    WORR_NATIVE_CARRIER_MALFORMED = 5,
    WORR_NATIVE_CARRIER_UNSUPPORTED = 6,
    WORR_NATIVE_CARRIER_CORRUPT = 7,
} worr_native_carrier_result_v1;

/*
 * Encodes legacy bytes followed by typed entries and the terminal footer.
 * The 1,200-byte limit applies to that complete netchan application datagram,
 * including the legacy prefix, every entry header/payload, and the footer.
 * DATA slices are copied from data_arena and must decode as complete WNE1
 * datagrams whose transport epoch equals transport_epoch.  The implementation
 * stages the complete result before writing, so packet_out may overlap any
 * byte input, including for in-place replacement.  A DATA envelope is atomic
 * and is never fragmented to make it fit; callers retain unsent work for a
 * later packet.  packet_bytes_out must not overlap the encoded packet range.
 * On every failure, packet_out and
 * packet_bytes_out are byte-identical to their entry state.
 */
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
    size_t *packet_bytes_out);

/*
 * Inspects only the terminal marker to distinguish legacy input.  NO_CARRIER
 * and every error leave view_out byte-identical.  A successful view remains
 * valid only while the caller-owned packet bytes remain available and
 * unchanged; it contains offsets, never retained pointers.  view_out may not
 * overlap the input packet.
 */
worr_native_carrier_result_v1 Worr_NativeCarrierDecodeV1(
    const void *packet,
    size_t packet_bytes,
    worr_native_carrier_view_v1 *view_out);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_NATIVE_CARRIER_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define WORR_NATIVE_CARRIER_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

WORR_NATIVE_CARRIER_STATIC_ASSERT(sizeof(uint8_t) == 1,
                                  "native carrier requires 8-bit bytes");
WORR_NATIVE_CARRIER_STATIC_ASSERT(sizeof(worr_native_carrier_entry_v1) == 28,
                                  "native carrier entry v1 layout changed");
WORR_NATIVE_CARRIER_STATIC_ASSERT(
    offsetof(worr_native_carrier_entry_v1, data_offset) == 8,
    "native carrier data offset changed");
WORR_NATIVE_CARRIER_STATIC_ASSERT(
    offsetof(worr_native_carrier_entry_v1, first_message_sequence) == 16,
    "native carrier ACK offset changed");
WORR_NATIVE_CARRIER_STATIC_ASSERT(sizeof(worr_native_carrier_view_v1) == 256,
                                  "native carrier view v1 layout changed");
WORR_NATIVE_CARRIER_STATIC_ASSERT(
    offsetof(worr_native_carrier_view_v1, entries) == 32,
    "native carrier entry-array offset changed");

#undef WORR_NATIVE_CARRIER_STATIC_ASSERT
