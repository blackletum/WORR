/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "shared/event_stream.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Process-local connection owner for a canonical descriptor.  The nonzero
 * connection_owner_id is never serialized.  A full reconnect initializes a
 * fresh owner and may reuse a lower numeric event epoch; within one owner,
 * high-water is strictly increasing and survives map quiesce/resync.
 */
#define WORR_EVENT_STREAM_OWNER_ABI_VERSION 1u

enum {
    WORR_EVENT_STREAM_OWNER_INITIALIZED = 1u << 0,
    WORR_EVENT_STREAM_OWNER_ACTIVE = 1u << 1,
    WORR_EVENT_STREAM_OWNER_REQUIRES_RESYNC = 1u << 2,
    WORR_EVENT_STREAM_OWNER_GENERATION_EXHAUSTED = 1u << 3,
};

typedef struct worr_event_stream_owner_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t state_flags;
    worr_event_stream_descriptor_v1 descriptor;
    uint32_t epoch_high_water;
    uint32_t reserved0;
    uint64_t mutation_generation;
    uint64_t connection_owner_id;
} worr_event_stream_owner_v1;

typedef enum worr_event_stream_owner_result_v1_e {
    WORR_EVENT_STREAM_OWNER_ACTIVATED = 0,
    WORR_EVENT_STREAM_OWNER_EXACT_DUPLICATE = 1,
    WORR_EVENT_STREAM_OWNER_WRONG_EPOCH = 2,
    WORR_EVENT_STREAM_OWNER_CONFLICT = 3,
    WORR_EVENT_STREAM_OWNER_GENERATION_LIMIT = 4,
    WORR_EVENT_STREAM_OWNER_INVALID_DESCRIPTOR = 5,
    WORR_EVENT_STREAM_OWNER_INVALID_ARGUMENT = 6,
    WORR_EVENT_STREAM_OWNER_INVALID_STATE = 7,
} worr_event_stream_owner_result_v1;

/* Initialization is the full-connection reset boundary. */
bool Worr_EventStreamOwnerInitV1(
    worr_event_stream_owner_v1 *owner_out,
    uint64_t connection_owner_id);
bool Worr_EventStreamOwnerValidateV1(
    const worr_event_stream_owner_v1 *owner);

/* Exact duplicates are idempotent. Every failure leaves owner unchanged. */
worr_event_stream_owner_result_v1 Worr_EventStreamOwnerObserveV1(
    worr_event_stream_owner_v1 *owner,
    const worr_event_stream_descriptor_v1 *descriptor);

/*
 * Scrubs active semantic authority while retaining its epoch high-water.
 * Recovery therefore requires a strictly newer descriptor.  Idempotent once
 * the barrier exists.  A fresh connection uses OwnerInit instead.
 */
bool Worr_EventStreamOwnerRequireResyncV1(
    worr_event_stream_owner_v1 *owner);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_EVENT_STREAM_OWNER_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define WORR_EVENT_STREAM_OWNER_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

WORR_EVENT_STREAM_OWNER_STATIC_ASSERT(
    sizeof(worr_event_stream_owner_v1) == 56,
    "event stream owner V1 layout changed");
WORR_EVENT_STREAM_OWNER_STATIC_ASSERT(
    offsetof(worr_event_stream_owner_v1, descriptor) == 8,
    "event stream owner descriptor offset changed");
WORR_EVENT_STREAM_OWNER_STATIC_ASSERT(
    offsetof(worr_event_stream_owner_v1, connection_owner_id) == 48,
    "event stream owner provenance offset changed");

#undef WORR_EVENT_STREAM_OWNER_STATIC_ASSERT
