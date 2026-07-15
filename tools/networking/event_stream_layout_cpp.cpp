/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/native_codec.h"
#include "shared/event_stream.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

using event_stream_init_fn_v1 = bool (*)(
    worr_event_stream_descriptor_v1 *, std::uint32_t, std::uint32_t);
using event_stream_validate_fn_v1 = bool (*)(
    const worr_event_stream_descriptor_v1 *);
using event_stream_equal_fn_v1 = bool (*)(
    const worr_event_stream_descriptor_v1 *,
    const worr_event_stream_descriptor_v1 *);
using event_stream_preflight_fn_v1 = worr_native_codec_result_v1 (*)(
    const worr_event_stream_descriptor_v1 *, std::uint32_t *);
using event_stream_encode_fn_v1 = worr_native_codec_result_v1 (*)(
    const worr_event_stream_descriptor_v1 *, void *, std::size_t,
    std::size_t *);
using event_stream_decode_fn_v1 = worr_native_codec_result_v1 (*)(
    const void *, std::size_t, worr_event_stream_descriptor_v1 *);

static_assert(std::is_same_v<decltype(&Worr_EventStreamDescriptorInitV1),
                             event_stream_init_fn_v1>);
static_assert(std::is_same_v<decltype(&Worr_EventStreamDescriptorValidateV1),
                             event_stream_validate_fn_v1>);
static_assert(std::is_same_v<decltype(&Worr_EventStreamDescriptorEqualV1),
                             event_stream_equal_fn_v1>);
static_assert(std::is_same_v<decltype(&Worr_NativeCodecEventStreamPreflightV1),
                             event_stream_preflight_fn_v1>);
static_assert(std::is_same_v<decltype(&Worr_NativeCodecEventStreamEncodeV1),
                             event_stream_encode_fn_v1>);
static_assert(std::is_same_v<decltype(&Worr_NativeCodecEventStreamDecodeV1),
                             event_stream_decode_fn_v1>);

static_assert(std::is_standard_layout_v<worr_event_stream_descriptor_v1>);
static_assert(std::is_trivially_copyable_v<worr_event_stream_descriptor_v1>);
static_assert(std::is_aggregate_v<worr_event_stream_descriptor_v1>);
static_assert(sizeof(worr_event_stream_descriptor_v1) == 24);
static_assert(alignof(worr_event_stream_descriptor_v1) == 4);
static_assert(offsetof(worr_event_stream_descriptor_v1, struct_size) == 0);
static_assert(offsetof(worr_event_stream_descriptor_v1, schema_version) == 4);
static_assert(offsetof(worr_event_stream_descriptor_v1, flags) == 6);
static_assert(offsetof(worr_event_stream_descriptor_v1, stream_epoch) == 8);
static_assert(offsetof(worr_event_stream_descriptor_v1, first_sequence) == 12);
static_assert(offsetof(worr_event_stream_descriptor_v1,
                       event_schema_version) == 16);
static_assert(offsetof(worr_event_stream_descriptor_v1, model_revision) == 20);

static_assert(std::is_same_v<
              decltype(worr_event_stream_descriptor_v1::struct_size),
              std::uint32_t>);
static_assert(std::is_same_v<
              decltype(worr_event_stream_descriptor_v1::schema_version),
              std::uint16_t>);
static_assert(std::is_same_v<
              decltype(worr_event_stream_descriptor_v1::flags),
              std::uint16_t>);
static_assert(std::is_same_v<
              decltype(worr_event_stream_descriptor_v1::stream_epoch),
              std::uint32_t>);
static_assert(std::is_same_v<
              decltype(worr_event_stream_descriptor_v1::first_sequence),
              std::uint32_t>);
static_assert(std::is_same_v<
              decltype(worr_event_stream_descriptor_v1::event_schema_version),
              std::uint32_t>);
static_assert(std::is_same_v<
              decltype(worr_event_stream_descriptor_v1::model_revision),
              std::uint32_t>);

static_assert(WORR_EVENT_STREAM_ABI_VERSION == 1u);
static_assert(WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1 == 4);
static_assert(WORR_NATIVE_CODEC_EVENT_STREAM_FIXED_BODY_BYTES == 8u);
static_assert(WORR_NATIVE_CODEC_WIRE_HEADER_BYTES +
                      WORR_NATIVE_CODEC_EVENT_STREAM_FIXED_BODY_BYTES ==
              56u);

int main()
{
    return 0;
}
