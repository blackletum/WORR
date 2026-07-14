/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "server/command_context.h"

#include <string.h>

static worr_authoritative_command_context_v1 current_context;
static uint32_t context_scope_state;

static bool command_id_equal(worr_command_id_v1 left,
                             worr_command_id_v1 right)
{
    return left.epoch == right.epoch &&
           left.sequence == right.sequence;
}

static bool snapshot_id_valid(worr_snapshot_id_v2 snapshot_id)
{
    return snapshot_id.epoch != 0 && snapshot_id.sequence != 0;
}

static bool current_snapshot_valid(
    const worr_rewind_snapshot_time_v1 *snapshot,
    worr_command_id_v1 command_id)
{
    const uint32_t allowed_flags =
        WORR_REWIND_SNAPSHOT_PAUSED |
        WORR_REWIND_SNAPSHOT_MAP_RESET |
        WORR_REWIND_SNAPSHOT_HARD_RESYNC;

    return snapshot && snapshot->struct_size == sizeof(*snapshot) &&
           snapshot->schema_version == WORR_REWIND_ABI_VERSION &&
           (snapshot->flags & ~allowed_flags) == 0 &&
           snapshot->tick_interval_us != 0 &&
           snapshot->tick_interval_us <=
               WORR_COMMAND_MAX_TICK_INTERVAL_US &&
           snapshot_id_valid(snapshot->snapshot_id) &&
           snapshot->reserved0 == 0 &&
           snapshot->consumed_command.provenance ==
               WORR_SNAPSHOT_CONSUMED_COMMAND_SERVER_CONSUMED &&
           snapshot->consumed_command.reserved0 == 0 &&
           snapshot->consumed_command.cursor.epoch == command_id.epoch &&
           snapshot->consumed_command.cursor.contiguous_sequence ==
               command_id.sequence;
}

static bool mapping_proof_matches_context(
    const worr_rewind_mapping_proof_v1 *proof,
    const worr_command_record_v1 *command,
    const worr_rewind_snapshot_time_v1 *snapshot)
{
    const worr_command_render_watermark_v1 *watermark =
        &command->render_watermark;

    return Worr_RewindMappingProofValidateV1(proof) &&
           command_id_equal(proof->command_id, command->command_id) &&
           proof->source_snapshot_id.epoch ==
               snapshot->snapshot_id.epoch &&
           proof->source_snapshot_id.sequence <=
               snapshot->snapshot_id.sequence &&
           proof->source_server_tick <= snapshot->server_tick &&
           proof->tick_interval_us == snapshot->tick_interval_us &&
           proof->source_server_time_us <= snapshot->server_time_us &&
           proof->watermark_provenance == watermark->provenance &&
           proof->watermark_flags == watermark->flags &&
           proof->source_server_tick == watermark->source_server_tick &&
           proof->tick_interval_us == watermark->tick_interval_us &&
           proof->source_server_time_us ==
               watermark->source_server_time_us &&
           proof->rendered_server_time_us ==
               watermark->rendered_server_time_us;
}

static bool context_valid(
    const worr_authoritative_command_context_v1 *context)
{
    return context && context->struct_size == sizeof(*context) &&
           context->schema_version ==
               WORR_COMMAND_CONTEXT_API_VERSION &&
           context->reserved0 == 0 &&
           Worr_CommandRecordValidateV1(
               &context->command,
               WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS) &&
           current_snapshot_valid(
               &context->current_snapshot,
               context->command.command_id) &&
           mapping_proof_matches_context(
               &context->mapping_proof, &context->command,
               &context->current_snapshot);
}

void SV_CommandContextReset(void)
{
    memset(&current_context, 0, sizeof(current_context));
    context_scope_state =
        WORR_COMMAND_CONTEXT_SCOPE_INACTIVE_LEGACY;
}

bool SV_CommandContextBegin(
    const worr_authoritative_command_context_v1 *context)
{
    if (context_scope_state !=
            WORR_COMMAND_CONTEXT_SCOPE_INACTIVE_LEGACY ||
        !context_valid(context))
        return false;
    current_context = *context;
    context_scope_state = WORR_COMMAND_CONTEXT_SCOPE_ACTIVE_VALID;
    return true;
}

bool SV_CommandContextBeginRejected(void)
{
    if (context_scope_state !=
        WORR_COMMAND_CONTEXT_SCOPE_INACTIVE_LEGACY) {
        return false;
    }
    memset(&current_context, 0, sizeof(current_context));
    context_scope_state = WORR_COMMAND_CONTEXT_SCOPE_ACTIVE_REJECTED;
    return true;
}

void SV_CommandContextEnd(void)
{
    SV_CommandContextReset();
}

static bool get_current(
    worr_authoritative_command_context_v1 *context_out)
{
    if (!context_out ||
        context_scope_state != WORR_COMMAND_CONTEXT_SCOPE_ACTIVE_VALID ||
        !context_valid(&current_context)) {
        return false;
    }
    *context_out = current_context;
    return true;
}

static uint32_t get_scope_state(void)
{
    if (context_scope_state == WORR_COMMAND_CONTEXT_SCOPE_ACTIVE_VALID &&
        !context_valid(&current_context)) {
        return WORR_COMMAND_CONTEXT_SCOPE_ACTIVE_REJECTED;
    }
    if (context_scope_state > WORR_COMMAND_CONTEXT_SCOPE_ACTIVE_REJECTED)
        return WORR_COMMAND_CONTEXT_SCOPE_ACTIVE_REJECTED;
    return context_scope_state;
}

static const worr_command_context_import_v1 command_context_import = {
    sizeof(command_context_import),
    WORR_COMMAND_CONTEXT_API_VERSION,
    get_current,
    get_scope_state,
};

const worr_command_context_import_v1 *SV_CommandContextImportV1(void)
{
    return &command_context_import;
}
