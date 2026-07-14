/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/rewind.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WORR_COMMAND_CONTEXT_IMPORT_V1 \
    "WORR_AUTHORITATIVE_COMMAND_CONTEXT_IMPORT_V1"
#define WORR_COMMAND_CONTEXT_API_VERSION 2u

typedef enum worr_command_context_scope_state_v1_e {
    /* No canonical command callback is active; legacy fallback is permitted. */
    WORR_COMMAND_CONTEXT_SCOPE_INACTIVE_LEGACY = 0,
    /* A validated authoritative context is available through GetCurrent. */
    WORR_COMMAND_CONTEXT_SCOPE_ACTIVE_VALID = 1,
    /* A canonical callback is active but its authority proof was rejected. */
    WORR_COMMAND_CONTEXT_SCOPE_ACTIVE_REJECTED = 2,
} worr_command_context_scope_state_v1;

/*
 * Callback-scoped, pointer-free authority made visible while the server is
 * executing one canonical command.  The game must copy it and must not retain
 * the import pointer across module shutdown.
 */
typedef struct worr_authoritative_command_context_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t client_index;
    uint32_t reserved0;
    worr_command_record_v1 command;
    worr_rewind_snapshot_time_v1 current_snapshot;
    worr_rewind_mapping_proof_v1 mapping_proof;
} worr_authoritative_command_context_v1;

typedef struct worr_command_context_import_v1_s {
    uint32_t struct_size;
    uint32_t api_version;
    bool (*GetCurrent)(worr_authoritative_command_context_v1 *context_out);
    uint32_t (*GetScopeState)(void);
} worr_command_context_import_v1;

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
static_assert(sizeof(worr_authoritative_command_context_v1) == 256,
              "authoritative command context layout changed");
static_assert(offsetof(worr_authoritative_command_context_v1, command) == 16,
              "authoritative command offset changed");
static_assert(offsetof(worr_authoritative_command_context_v1,
                       current_snapshot) == 120,
              "authoritative current-snapshot offset changed");
static_assert(offsetof(worr_authoritative_command_context_v1,
                       mapping_proof) == 176,
              "authoritative mapping-proof offset changed");
static_assert(offsetof(worr_command_context_import_v1, GetCurrent) == 8,
              "command-context import function offset changed");
static_assert(offsetof(worr_command_context_import_v1, GetScopeState) == 16,
              "command-context scope function offset changed");
static_assert(sizeof(worr_command_context_import_v1) == 24,
              "command-context import layout changed");
#else
_Static_assert(sizeof(worr_authoritative_command_context_v1) == 256,
               "authoritative command context layout changed");
_Static_assert(offsetof(worr_authoritative_command_context_v1, command) == 16,
               "authoritative command offset changed");
_Static_assert(offsetof(worr_authoritative_command_context_v1,
                        current_snapshot) == 120,
               "authoritative current-snapshot offset changed");
_Static_assert(offsetof(worr_authoritative_command_context_v1,
                        mapping_proof) == 176,
               "authoritative mapping-proof offset changed");
_Static_assert(offsetof(worr_command_context_import_v1, GetCurrent) == 8,
               "command-context import function offset changed");
_Static_assert(offsetof(worr_command_context_import_v1, GetScopeState) == 16,
               "command-context scope function offset changed");
_Static_assert(sizeof(worr_command_context_import_v1) == 24,
               "command-context import layout changed");
#endif
