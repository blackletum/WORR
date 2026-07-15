/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "shared/cgame_event_runtime.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

struct export_header {
    std::uint32_t struct_size;
    std::uint32_t api_version;
};

static_assert(std::is_standard_layout_v<
              worr_cgame_event_runtime_status_v1>);
static_assert(std::is_trivially_copyable_v<
              worr_cgame_event_runtime_status_v1>);
static_assert(sizeof(worr_cgame_event_runtime_status_v1) == 48);
static_assert(offsetof(worr_cgame_event_runtime_status_v1, struct_size) == 0);
static_assert(offsetof(worr_cgame_event_runtime_status_v1, api_version) == 4);
static_assert(offsetof(worr_cgame_event_runtime_status_v1,
                       authority_epoch) == 8);
static_assert(offsetof(worr_cgame_event_runtime_status_v1,
                       next_presentation_sequence) == 12);
static_assert(offsetof(worr_cgame_event_runtime_status_v1,
                       authority_count) == 16);
static_assert(offsetof(worr_cgame_event_runtime_status_v1,
                       state_flags) == 20);
static_assert(offsetof(worr_cgame_event_runtime_status_v1, receipt) == 24);
static_assert(sizeof(worr_event_receipt_ack_v1) == 24);

static_assert(std::is_standard_layout_v<
              worr_cgame_event_runtime_export_v1>);
static_assert(std::is_trivially_copyable_v<
              worr_cgame_event_runtime_export_v1>);
static_assert(sizeof(export_header) == 8);
static_assert(offsetof(worr_cgame_event_runtime_export_v1, struct_size) == 0);
static_assert(offsetof(worr_cgame_event_runtime_export_v1, api_version) == 4);
static_assert(offsetof(worr_cgame_event_runtime_export_v1,
                       ResetAuthority) == sizeof(export_header));
static_assert(
    offsetof(worr_cgame_event_runtime_export_v1,
             SubmitAuthoritativeBatch) ==
    offsetof(worr_cgame_event_runtime_export_v1, ResetAuthority) +
        sizeof(worr_cgame_event_runtime_export_v1::ResetAuthority));
static_assert(
    offsetof(worr_cgame_event_runtime_export_v1, GetStatus) ==
    offsetof(worr_cgame_event_runtime_export_v1,
             SubmitAuthoritativeBatch) +
        sizeof(worr_cgame_event_runtime_export_v1::SubmitAuthoritativeBatch));
static_assert(
    sizeof(worr_cgame_event_runtime_export_v1) ==
    offsetof(worr_cgame_event_runtime_export_v1, GetStatus) +
        sizeof(worr_cgame_event_runtime_export_v1::GetStatus));

static_assert(WORR_CGAME_EVENT_RUNTIME_API_VERSION == 1u);
static_assert(WORR_CGAME_EVENT_RUNTIME_MAX_BATCH == 512u);
static_assert(WORR_CGAME_EVENT_RUNTIME_OK == 0u);
static_assert(WORR_CGAME_EVENT_RUNTIME_DUPLICATE == 1u);
static_assert(WORR_CGAME_EVENT_RUNTIME_MATCHED == 2u);
static_assert(WORR_CGAME_EVENT_RUNTIME_CORRECTED == 3u);
static_assert(WORR_CGAME_EVENT_RUNTIME_CORRECTED_AFTER_PRESENTATION == 4u);
static_assert(WORR_CGAME_EVENT_RUNTIME_EMPTY == 5u);
static_assert(WORR_CGAME_EVENT_RUNTIME_NOT_READY == 6u);
static_assert(WORR_CGAME_EVENT_RUNTIME_INVALID_ARGUMENT == 7u);
static_assert(WORR_CGAME_EVENT_RUNTIME_UNINITIALIZED == 8u);
static_assert(WORR_CGAME_EVENT_RUNTIME_WRONG_EPOCH == 9u);
static_assert(WORR_CGAME_EVENT_RUNTIME_INVALID_RECORD == 10u);
static_assert(WORR_CGAME_EVENT_RUNTIME_CONFLICT == 11u);
static_assert(WORR_CGAME_EVENT_RUNTIME_CAPACITY == 12u);
static_assert(WORR_CGAME_EVENT_RUNTIME_DEGRADED == 13u);
static_assert(WORR_CGAME_EVENT_RUNTIME_NOT_FOUND == 14u);
static_assert(WORR_CGAME_EVENT_RUNTIME_TERMINAL == 15u);
static_assert(WORR_CGAME_EVENT_RUNTIME_REENTRANT == 16u);
static_assert(WORR_CGAME_EVENT_RUNTIME_STATE_ACTIVE == (1u << 0));
static_assert(WORR_CGAME_EVENT_RUNTIME_STATE_DEGRADED == (1u << 1));
static_assert(WORR_CGAME_EVENT_RUNTIME_STATE_AUDIT_ENABLED == (1u << 2));
static_assert(WORR_CGAME_EVENT_RUNTIME_STATE_REQUIRES_RESYNC == (1u << 3));

int main()
{
    return std::strcmp(WORR_CGAME_EVENT_RUNTIME_EXPORT_V1,
                       "WORR_CGAME_EVENT_RUNTIME_EXPORT_V1") == 0
               ? 0
               : 1;
}
