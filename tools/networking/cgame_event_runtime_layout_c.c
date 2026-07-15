/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "shared/cgame_event_runtime.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct export_header_s {
    uint32_t struct_size;
    uint32_t api_version;
} export_header;

_Static_assert(sizeof(worr_cgame_event_runtime_status_v1) == 48,
               "runtime status ABI size");
_Static_assert(offsetof(worr_cgame_event_runtime_status_v1, struct_size) == 0,
               "runtime status size offset");
_Static_assert(offsetof(worr_cgame_event_runtime_status_v1, api_version) == 4,
               "runtime status version offset");
_Static_assert(offsetof(worr_cgame_event_runtime_status_v1,
                        authority_epoch) == 8,
               "runtime status epoch offset");
_Static_assert(offsetof(worr_cgame_event_runtime_status_v1,
                        next_presentation_sequence) == 12,
               "runtime status sequence offset");
_Static_assert(offsetof(worr_cgame_event_runtime_status_v1,
                        authority_count) == 16,
               "runtime status count offset");
_Static_assert(offsetof(worr_cgame_event_runtime_status_v1,
                        state_flags) == 20,
               "runtime status flags offset");
_Static_assert(offsetof(worr_cgame_event_runtime_status_v1, receipt) == 24,
               "runtime status receipt offset");
_Static_assert(sizeof(worr_event_receipt_ack_v1) == 24,
               "runtime receipt ABI size");

/* Every extension table begins with this size/version header.  Keeping the
 * first callback at byte 8 lets an engine validate the header before reading
 * any version-specific function pointer. */
_Static_assert(sizeof(export_header) == 8, "extension header size");
_Static_assert(offsetof(worr_cgame_event_runtime_export_v1, struct_size) == 0,
               "runtime table size offset");
_Static_assert(offsetof(worr_cgame_event_runtime_export_v1, api_version) == 4,
               "runtime table version offset");
_Static_assert(offsetof(worr_cgame_event_runtime_export_v1,
                        ResetAuthority) == sizeof(export_header),
               "runtime table header compatibility");
_Static_assert(
    offsetof(worr_cgame_event_runtime_export_v1,
             SubmitAuthoritativeBatch) ==
        offsetof(worr_cgame_event_runtime_export_v1, ResetAuthority) +
            sizeof(((worr_cgame_event_runtime_export_v1 *)0)->ResetAuthority),
    "runtime table submit offset");
_Static_assert(
    offsetof(worr_cgame_event_runtime_export_v1, GetStatus) ==
        offsetof(worr_cgame_event_runtime_export_v1,
                 SubmitAuthoritativeBatch) +
            sizeof(((worr_cgame_event_runtime_export_v1 *)0)
                       ->SubmitAuthoritativeBatch),
    "runtime table status offset");
_Static_assert(
    sizeof(worr_cgame_event_runtime_export_v1) ==
        offsetof(worr_cgame_event_runtime_export_v1, GetStatus) +
            sizeof(((worr_cgame_event_runtime_export_v1 *)0)->GetStatus),
    "runtime table trailing layout");

_Static_assert(WORR_CGAME_EVENT_RUNTIME_API_VERSION == 1u,
               "runtime API version");
_Static_assert(WORR_CGAME_EVENT_RUNTIME_MAX_BATCH == 512u,
               "runtime batch ceiling");
_Static_assert(WORR_CGAME_EVENT_RUNTIME_OK == 0u,
               "runtime result numbering");
_Static_assert(WORR_CGAME_EVENT_RUNTIME_DUPLICATE == 1u,
               "runtime result numbering");
_Static_assert(WORR_CGAME_EVENT_RUNTIME_MATCHED == 2u,
               "runtime result numbering");
_Static_assert(WORR_CGAME_EVENT_RUNTIME_CORRECTED == 3u,
               "runtime result numbering");
_Static_assert(WORR_CGAME_EVENT_RUNTIME_CORRECTED_AFTER_PRESENTATION == 4u,
               "runtime result numbering");
_Static_assert(WORR_CGAME_EVENT_RUNTIME_EMPTY == 5u,
               "runtime result numbering");
_Static_assert(WORR_CGAME_EVENT_RUNTIME_NOT_READY == 6u,
               "runtime result numbering");
_Static_assert(WORR_CGAME_EVENT_RUNTIME_INVALID_ARGUMENT == 7u,
               "runtime result numbering");
_Static_assert(WORR_CGAME_EVENT_RUNTIME_UNINITIALIZED == 8u,
               "runtime result numbering");
_Static_assert(WORR_CGAME_EVENT_RUNTIME_WRONG_EPOCH == 9u,
               "runtime result numbering");
_Static_assert(WORR_CGAME_EVENT_RUNTIME_INVALID_RECORD == 10u,
               "runtime result numbering");
_Static_assert(WORR_CGAME_EVENT_RUNTIME_CONFLICT == 11u,
               "runtime result numbering");
_Static_assert(WORR_CGAME_EVENT_RUNTIME_CAPACITY == 12u,
               "runtime result numbering");
_Static_assert(WORR_CGAME_EVENT_RUNTIME_DEGRADED == 13u,
               "runtime result numbering");
_Static_assert(WORR_CGAME_EVENT_RUNTIME_NOT_FOUND == 14u,
               "runtime result numbering");
_Static_assert(WORR_CGAME_EVENT_RUNTIME_TERMINAL == 15u,
               "runtime result numbering");
_Static_assert(WORR_CGAME_EVENT_RUNTIME_REENTRANT == 16u,
               "runtime result numbering");
_Static_assert(WORR_CGAME_EVENT_RUNTIME_STATE_ACTIVE == (1u << 0),
               "runtime active flag");
_Static_assert(WORR_CGAME_EVENT_RUNTIME_STATE_DEGRADED == (1u << 1),
               "runtime degraded flag");
_Static_assert(WORR_CGAME_EVENT_RUNTIME_STATE_AUDIT_ENABLED == (1u << 2),
               "runtime audit flag");
_Static_assert(WORR_CGAME_EVENT_RUNTIME_STATE_REQUIRES_RESYNC == (1u << 3),
               "runtime resync flag");

int main(void)
{
    return strcmp(WORR_CGAME_EVENT_RUNTIME_EXPORT_V1,
                  "WORR_CGAME_EVENT_RUNTIME_EXPORT_V1") == 0
               ? 0
               : 1;
}
