/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/native_codec.h"
#include "shared/event_stream.h"

#include <stddef.h>
#include <stdint.h>

typedef bool (*event_stream_init_fn_v1)(
    worr_event_stream_descriptor_v1 *, uint32_t, uint32_t);
typedef bool (*event_stream_validate_fn_v1)(
    const worr_event_stream_descriptor_v1 *);
typedef bool (*event_stream_equal_fn_v1)(
    const worr_event_stream_descriptor_v1 *,
    const worr_event_stream_descriptor_v1 *);
typedef worr_native_codec_result_v1 (*event_stream_preflight_fn_v1)(
    const worr_event_stream_descriptor_v1 *, uint32_t *);
typedef worr_native_codec_result_v1 (*event_stream_encode_fn_v1)(
    const worr_event_stream_descriptor_v1 *, void *, size_t, size_t *);
typedef worr_native_codec_result_v1 (*event_stream_decode_fn_v1)(
    const void *, size_t, worr_event_stream_descriptor_v1 *);

_Static_assert(
    _Generic(&Worr_EventStreamDescriptorInitV1,
             event_stream_init_fn_v1: 1,
             default: 0),
    "event stream descriptor initializer signature");
_Static_assert(
    _Generic(&Worr_EventStreamDescriptorValidateV1,
             event_stream_validate_fn_v1: 1,
             default: 0),
    "event stream descriptor validator signature");
_Static_assert(
    _Generic(&Worr_EventStreamDescriptorEqualV1,
             event_stream_equal_fn_v1: 1,
             default: 0),
    "event stream descriptor equality signature");
_Static_assert(
    _Generic(&Worr_NativeCodecEventStreamPreflightV1,
             event_stream_preflight_fn_v1: 1,
             default: 0),
    "event stream codec preflight signature");
_Static_assert(
    _Generic(&Worr_NativeCodecEventStreamEncodeV1,
             event_stream_encode_fn_v1: 1,
             default: 0),
    "event stream codec encoder signature");
_Static_assert(
    _Generic(&Worr_NativeCodecEventStreamDecodeV1,
             event_stream_decode_fn_v1: 1,
             default: 0),
    "event stream codec decoder signature");

_Static_assert(WORR_EVENT_STREAM_ABI_VERSION == 1u,
               "event stream ABI version");
_Static_assert(sizeof(worr_event_stream_descriptor_v1) == 24,
               "event stream descriptor size");
_Static_assert(_Alignof(worr_event_stream_descriptor_v1) == 4,
               "event stream descriptor alignment");
_Static_assert(offsetof(worr_event_stream_descriptor_v1, struct_size) == 0,
               "event stream descriptor size offset");
_Static_assert(offsetof(worr_event_stream_descriptor_v1, schema_version) == 4,
               "event stream descriptor schema offset");
_Static_assert(offsetof(worr_event_stream_descriptor_v1, flags) == 6,
               "event stream descriptor flags offset");
_Static_assert(offsetof(worr_event_stream_descriptor_v1, stream_epoch) == 8,
               "event stream descriptor epoch offset");
_Static_assert(offsetof(worr_event_stream_descriptor_v1, first_sequence) == 12,
               "event stream descriptor first-sequence offset");
_Static_assert(offsetof(worr_event_stream_descriptor_v1,
                        event_schema_version) == 16,
               "event stream descriptor event-schema offset");
_Static_assert(offsetof(worr_event_stream_descriptor_v1, model_revision) == 20,
               "event stream descriptor model offset");

_Static_assert(WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1 == 4,
               "event stream native record class");
_Static_assert(WORR_NATIVE_CODEC_EVENT_STREAM_FIXED_BODY_BYTES == 8u,
               "event stream native fixed body");
_Static_assert(WORR_NATIVE_CODEC_WIRE_HEADER_BYTES +
                       WORR_NATIVE_CODEC_EVENT_STREAM_FIXED_BODY_BYTES ==
                   56u,
               "event stream native encoded size");

int main(void)
{
    return 0;
}
