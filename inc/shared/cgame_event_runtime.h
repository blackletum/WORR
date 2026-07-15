/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "event_abi.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WORR_CGAME_EVENT_RUNTIME_EXPORT_V1 \
    "WORR_CGAME_EVENT_RUNTIME_EXPORT_V1"
#define WORR_CGAME_EVENT_RUNTIME_API_VERSION 1u
#define WORR_CGAME_EVENT_RUNTIME_MAX_BATCH 512u

typedef uint32_t worr_cgame_event_runtime_result_v1;
enum {
    WORR_CGAME_EVENT_RUNTIME_OK = 0u,
    WORR_CGAME_EVENT_RUNTIME_DUPLICATE = 1u,
    WORR_CGAME_EVENT_RUNTIME_MATCHED = 2u,
    WORR_CGAME_EVENT_RUNTIME_CORRECTED = 3u,
    WORR_CGAME_EVENT_RUNTIME_CORRECTED_AFTER_PRESENTATION = 4u,
    WORR_CGAME_EVENT_RUNTIME_EMPTY = 5u,
    WORR_CGAME_EVENT_RUNTIME_NOT_READY = 6u,
    WORR_CGAME_EVENT_RUNTIME_INVALID_ARGUMENT = 7u,
    WORR_CGAME_EVENT_RUNTIME_UNINITIALIZED = 8u,
    WORR_CGAME_EVENT_RUNTIME_WRONG_EPOCH = 9u,
    WORR_CGAME_EVENT_RUNTIME_INVALID_RECORD = 10u,
    WORR_CGAME_EVENT_RUNTIME_CONFLICT = 11u,
    WORR_CGAME_EVENT_RUNTIME_CAPACITY = 12u,
    WORR_CGAME_EVENT_RUNTIME_DEGRADED = 13u,
    WORR_CGAME_EVENT_RUNTIME_NOT_FOUND = 14u,
    WORR_CGAME_EVENT_RUNTIME_TERMINAL = 15u,
    WORR_CGAME_EVENT_RUNTIME_REENTRANT = 16u,
};

enum {
    WORR_CGAME_EVENT_RUNTIME_STATE_ACTIVE = 1u << 0,
    WORR_CGAME_EVENT_RUNTIME_STATE_DEGRADED = 1u << 1,
    WORR_CGAME_EVENT_RUNTIME_STATE_AUDIT_ENABLED = 1u << 2,
    WORR_CGAME_EVENT_RUNTIME_STATE_REQUIRES_RESYNC = 1u << 3,
};

/* Stable transport-facing health and exact-receipt summary. Detailed journal,
 * join, prediction, and presentation telemetry remains cgame-private. */
typedef struct worr_cgame_event_runtime_status_v1_s {
    uint32_t struct_size;
    uint32_t api_version;
    uint32_t authority_epoch;
    uint32_t next_presentation_sequence;
    uint32_t authority_count;
    uint32_t state_flags;
    worr_event_receipt_ack_v1 receipt;
} worr_cgame_event_runtime_status_v1;

/* Process-local, main-thread, synchronous extension. `records` is borrowed
 * only for SubmitAuthoritativeBatch; the cgame copies accepted values before
 * returning. {0, 0} deactivates and scrubs the authority domain. */
typedef struct worr_cgame_event_runtime_export_v1_s {
    uint32_t struct_size;
    uint32_t api_version;
    worr_cgame_event_runtime_result_v1 (*ResetAuthority)(
        uint32_t stream_epoch, uint32_t first_sequence);
    worr_cgame_event_runtime_result_v1 (*SubmitAuthoritativeBatch)(
        const worr_event_record_v1 *records, uint32_t count);
    bool (*GetStatus)(worr_cgame_event_runtime_status_v1 *status_out);
} worr_cgame_event_runtime_export_v1;

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_CGAME_EVENT_RUNTIME_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define WORR_CGAME_EVENT_RUNTIME_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

WORR_CGAME_EVENT_RUNTIME_STATIC_ASSERT(
    sizeof(worr_cgame_event_runtime_status_v1) == 48,
    "cgame event runtime status v1 layout changed");
WORR_CGAME_EVENT_RUNTIME_STATIC_ASSERT(
    offsetof(worr_cgame_event_runtime_status_v1, receipt) == 24,
    "cgame event runtime status receipt offset changed");

#undef WORR_CGAME_EVENT_RUNTIME_STATIC_ASSERT
