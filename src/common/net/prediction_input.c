/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/prediction_input.h"

#include <string.h>

#include "common/net/command_canonical.h"

static bool command_id_equal(worr_command_id_v1 a, worr_command_id_v1 b)
{
    return a.epoch == b.epoch && a.sequence == b.sequence;
}

static bool command_id_absent(worr_command_id_v1 id)
{
    return id.epoch == 0 && id.sequence == 0;
}

static bool canonicalize_command(worr_prediction_command_v1 *command)
{
    unsigned i;
    worr_prediction_command_v1 output;

    if (!command || command->struct_size != sizeof(*command) ||
        command->schema_version != WORR_PREDICTION_ABI_VERSION ||
        command->reserved0 != 0) {
        return false;
    }

    output = *command;
    for (i = 0; i < 3; ++i) {
        if (!NetUsercmd_CanonicalizeAngle(command->view_angles[i],
                                          &output.view_angles[i])) {
            return false;
        }
    }
    if (!NetUsercmd_CanonicalizeMove(command->forward_move,
                                     &output.forward_move) ||
        !NetUsercmd_CanonicalizeMove(command->side_move,
                                     &output.side_move)) {
        return false;
    }
    *command = output;
    return true;
}

static uint32_t fail(worr_cgame_prediction_input_range_v1 *range,
                     uint32_t result)
{
    range->result = result;
    range->flags |= WORR_CGAME_PREDICTION_INPUT_HARD_RESYNC_REQUIRED;
    range->command_count = 0;
    return result;
}

static uint32_t find_by_legacy(
    const worr_prediction_input_resolve_request_v1 *request,
    uint32_t legacy_sequence,
    worr_cgame_prediction_input_command_v1 *entry_out)
{
    uint32_t matches = 0;
    uint32_t i;

    for (i = 0; i < request->history_count; ++i) {
        if (request->history[i].legacy_sequence != legacy_sequence)
            continue;
        *entry_out = request->history[i];
        ++matches;
    }
    if (matches == 0)
        return WORR_CGAME_PREDICTION_INPUT_HISTORY_MISSING;
    if (matches != 1)
        return WORR_CGAME_PREDICTION_INPUT_HISTORY_AMBIGUOUS;
    return WORR_CGAME_PREDICTION_INPUT_OK;
}

static uint32_t find_by_id(
    const worr_prediction_input_resolve_request_v1 *request,
    worr_command_id_v1 command_id,
    worr_cgame_prediction_input_command_v1 *entry_out)
{
    uint32_t matches = 0;
    uint32_t i;

    for (i = 0; i < request->history_count; ++i) {
        if (!command_id_equal(request->history[i].command_id, command_id))
            continue;
        *entry_out = request->history[i];
        ++matches;
    }
    if (matches == 0)
        return WORR_CGAME_PREDICTION_INPUT_HISTORY_MISSING;
    if (matches != 1)
        return WORR_CGAME_PREDICTION_INPUT_HISTORY_AMBIGUOUS;
    return WORR_CGAME_PREDICTION_INPUT_OK;
}

static bool metadata_absent(
    const worr_snapshot_consumed_command_v2 *consumed)
{
    return consumed->provenance == WORR_SNAPSHOT_CONSUMED_COMMAND_NONE &&
           consumed->reserved0 == 0 && consumed->cursor.epoch == 0 &&
           consumed->cursor.contiguous_sequence == 0;
}

uint32_t Worr_PredictionInputResolveV1(
    const worr_prediction_input_resolve_request_v1 *request,
    worr_cgame_prediction_input_range_v1 *range_out)
{
    bool canonical;
    uint32_t baseline;
    uint32_t count;
    uint32_t i;
    worr_command_id_v1 expected_id = {0, 0};

    if (!range_out)
        return WORR_CGAME_PREDICTION_INPUT_INVALID_ARGUMENT;

    memset(range_out, 0, sizeof(*range_out));
    range_out->struct_size = sizeof(*range_out);
    range_out->api_version = WORR_CGAME_PREDICTION_INPUT_API_VERSION;

    if (!request || request->struct_size != sizeof(*request) ||
        request->schema_version != WORR_PREDICTION_INPUT_RESOLVER_VERSION ||
        request->reserved0 != 0 ||
        (request->flags &
         ~(WORR_PREDICTION_INPUT_RESOLVE_CANONICAL_CAPABILITY |
           WORR_PREDICTION_INPUT_RESOLVE_CANONICAL_ESTABLISHED)) != 0 ||
        ((request->flags &
          WORR_PREDICTION_INPUT_RESOLVE_CANONICAL_ESTABLISHED) != 0 &&
         (request->flags &
          WORR_PREDICTION_INPUT_RESOLVE_CANONICAL_CAPABILITY) == 0) ||
        request->history_count > WORR_CGAME_PREDICTION_INPUT_CAPACITY ||
        (request->history_count != 0 && !request->history) ||
        request->pending_present > 1) {
        return fail(range_out, WORR_CGAME_PREDICTION_INPUT_INVALID_ARGUMENT);
    }

    range_out->consumed_command = request->consumed_command;
    range_out->current_legacy_sequence = request->current_legacy_sequence;

    if (request->consumed_command.reserved0 != 0 ||
        (request->consumed_command.provenance !=
             WORR_SNAPSHOT_CONSUMED_COMMAND_NONE &&
         request->consumed_command.provenance !=
             WORR_SNAPSHOT_CONSUMED_COMMAND_SERVER_CONSUMED)) {
        return fail(range_out, WORR_CGAME_PREDICTION_INPUT_INVALID_METADATA);
    }

    canonical = request->consumed_command.provenance ==
                WORR_SNAPSHOT_CONSUMED_COMMAND_SERVER_CONSUMED;
    if (!canonical) {
        if (!metadata_absent(&request->consumed_command))
            return fail(range_out,
                        WORR_CGAME_PREDICTION_INPUT_INVALID_METADATA);
        if (request->flags &
            WORR_PREDICTION_INPUT_RESOLVE_CANONICAL_ESTABLISHED) {
            return fail(
                range_out,
                WORR_CGAME_PREDICTION_INPUT_CANONICAL_METADATA_REQUIRED);
        }
    } else if (!Worr_CommandCursorValidV1(
                   request->consumed_command.cursor)) {
        return fail(range_out, WORR_CGAME_PREDICTION_INPUT_INVALID_METADATA);
    }

    if (canonical) {
        const worr_command_cursor_v1 cursor =
            request->consumed_command.cursor;
        worr_cgame_prediction_input_command_v1 baseline_entry;
        uint32_t result;

        range_out->source =
            WORR_CGAME_PREDICTION_INPUT_SOURCE_CANONICAL_CURSOR;
        range_out->flags |= WORR_CGAME_PREDICTION_INPUT_CANONICAL;

        if (cursor.contiguous_sequence == 0) {
            if (request->identity_initial_epoch == 0 ||
                cursor.epoch != request->identity_initial_epoch) {
                return fail(
                    range_out,
                    WORR_CGAME_PREDICTION_INPUT_IDENTITY_EPOCH_MISMATCH);
            }
            baseline = request->identity_baseline_legacy_sequence;
            if (!Worr_CommandCursorNextIdV1(cursor, &expected_id)) {
                return fail(range_out,
                            WORR_CGAME_PREDICTION_INPUT_INVALID_METADATA);
            }
        } else {
            const worr_command_id_v1 consumed_id = {
                cursor.epoch, cursor.contiguous_sequence};
            result = find_by_id(request, consumed_id, &baseline_entry);
            if (result != WORR_CGAME_PREDICTION_INPUT_OK)
                return fail(range_out, result);
            baseline = baseline_entry.legacy_sequence;
            expected_id = consumed_id;
            if (!Worr_CommandIdNextV1(expected_id, &expected_id)) {
                return fail(range_out,
                            WORR_CGAME_PREDICTION_INPUT_INVALID_METADATA);
            }
        }
    } else {
        if (request->flags &
            WORR_PREDICTION_INPUT_RESOLVE_CANONICAL_CAPABILITY) {
            range_out->source =
                WORR_CGAME_PREDICTION_INPUT_SOURCE_CANONICAL_BOOTSTRAP;
            range_out->flags |=
                WORR_CGAME_PREDICTION_INPUT_LEGACY_FALLBACK |
                WORR_CGAME_PREDICTION_INPUT_CANONICAL_BOOTSTRAP;
        } else {
            range_out->source =
                WORR_CGAME_PREDICTION_INPUT_SOURCE_LEGACY_PACKET_ACK;
            range_out->flags |=
                WORR_CGAME_PREDICTION_INPUT_LEGACY_FALLBACK;
        }
        baseline = request->legacy_acknowledged_sequence;
    }

    range_out->authoritative_legacy_sequence = baseline;
    count = request->current_legacy_sequence - baseline;
    if (count >= WORR_CGAME_PREDICTION_INPUT_CAPACITY)
        return fail(range_out,
                    WORR_CGAME_PREDICTION_INPUT_RANGE_EXHAUSTED);

    for (i = 0; i < count; ++i) {
        const uint32_t legacy_sequence = baseline + i + 1u;
        worr_cgame_prediction_input_command_v1 entry;
        uint32_t result = find_by_legacy(request, legacy_sequence, &entry);
        if (result != WORR_CGAME_PREDICTION_INPUT_OK)
            return fail(range_out, result);
        if (entry.reserved0 != 0 ||
            !canonicalize_command(&entry.command)) {
            return fail(range_out,
                        WORR_CGAME_PREDICTION_INPUT_COMMAND_INVALID);
        }
        if (canonical) {
            if (!command_id_equal(entry.command_id, expected_id)) {
                return fail(
                    range_out,
                    WORR_CGAME_PREDICTION_INPUT_IDENTITY_DISCONTINUITY);
            }
            if (i + 1u < count &&
                !Worr_CommandIdNextV1(expected_id, &expected_id)) {
                return fail(range_out,
                            WORR_CGAME_PREDICTION_INPUT_INVALID_METADATA);
            }
        } else {
            entry.command_id = (worr_command_id_v1){0, 0};
        }
        range_out->commands[i] = entry;
    }
    range_out->command_count = count;

    if (request->pending_present) {
        worr_cgame_prediction_input_command_v1 pending =
            request->pending_command;
        if (pending.reserved0 != 0 ||
            pending.legacy_sequence !=
                request->current_legacy_sequence + 1u ||
            !command_id_absent(pending.command_id) ||
            !canonicalize_command(&pending.command)) {
            return fail(range_out,
                        WORR_CGAME_PREDICTION_INPUT_COMMAND_INVALID);
        }
        range_out->pending_command = pending;
        range_out->flags |= WORR_CGAME_PREDICTION_INPUT_HAS_PENDING;
    }

    range_out->result = WORR_CGAME_PREDICTION_INPUT_OK;
    return WORR_CGAME_PREDICTION_INPUT_OK;
}
