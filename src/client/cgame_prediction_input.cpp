/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "client.h"
#include "client/cgame_prediction_input.h"
#include "client/command_identity.h"
#include "client/consumed_cursor.h"
#include "client/net_capability.h"
#include "common/net/prediction_input.h"
#include "common/net/usercmd_delta.h"

static_assert(CMD_BACKUP == WORR_CGAME_PREDICTION_INPUT_CAPACITY,
              "prediction input ABI must retain the engine command ring");

namespace {

cl_cgame_prediction_input_diagnostics_v1 diagnostics;

void reset_diagnostics()
{
    diagnostics = {};
    diagnostics.struct_size = sizeof(diagnostics);
    diagnostics.schema_version =
        CL_CGAME_PREDICTION_INPUT_DIAGNOSTICS_VERSION;
}

void ensure_diagnostics()
{
    if (diagnostics.struct_size != sizeof(diagnostics) ||
        diagnostics.schema_version !=
            CL_CGAME_PREDICTION_INPUT_DIAGNOSTICS_VERSION) {
        reset_diagnostics();
    }
}

void increment_saturated(uint64_t &value)
{
    if (value != UINT64_MAX)
        ++value;
}

void note_result(
    uint32_t result,
    const worr_cgame_prediction_input_range_v1 *range)
{
    ensure_diagnostics();
    diagnostics.last_result = result;
    diagnostics.last_source = range ? range->source : 0;
    diagnostics.last_flags = range ? range->flags : 0;
    diagnostics.last_authoritative_legacy_sequence =
        range ? range->authoritative_legacy_sequence : 0;
    diagnostics.last_current_legacy_sequence =
        range ? range->current_legacy_sequence : 0;
    diagnostics.last_command_count =
        range ? range->command_count : 0;
    if (result != WORR_CGAME_PREDICTION_INPUT_OK) {
        increment_saturated(diagnostics.failures);
    } else if (range &&
               range->source ==
                   WORR_CGAME_PREDICTION_INPUT_SOURCE_CANONICAL_CURSOR) {
        increment_saturated(diagnostics.canonical_successes);
    } else if (range &&
               (range->source ==
                    WORR_CGAME_PREDICTION_INPUT_SOURCE_LEGACY_PACKET_ACK ||
                range->source ==
                    WORR_CGAME_PREDICTION_INPUT_SOURCE_CANONICAL_BOOTSTRAP)) {
        increment_saturated(diagnostics.legacy_fallbacks);
    }
}

worr_prediction_command_v1 prediction_input_command(
    const usercmd_t &command, const float *pending_move)
{
    worr_prediction_command_v1 output{};
    if (!pending_move) {
        (void)NetUsercmd_ToPredictionCommandV1(&command, &output);
        return output;
    }
    output.struct_size = sizeof(output);
    output.schema_version = WORR_PREDICTION_ABI_VERSION;
    output.duration_ms = command.msec;
    output.buttons = command.buttons;
    VectorCopy(command.angles, output.view_angles);
    output.forward_move = pending_move[0];
    output.side_move = pending_move[1];
    return output;
}

uint32_t prediction_input_fail(
    worr_cgame_prediction_input_range_v1 *range_out, uint32_t result)
{
    if (range_out) {
        *range_out = {};
        range_out->struct_size = sizeof(*range_out);
        range_out->api_version = WORR_CGAME_PREDICTION_INPUT_API_VERSION;
        range_out->result = result;
        range_out->flags =
            WORR_CGAME_PREDICTION_INPUT_HARD_RESYNC_REQUIRED;
    }
    return result;
}

bool consumed_command_equal(
    const worr_snapshot_consumed_command_v2 &left,
    const worr_snapshot_consumed_command_v2 &right)
{
    return left.cursor.epoch == right.cursor.epoch &&
           left.cursor.contiguous_sequence ==
               right.cursor.contiguous_sequence &&
           left.provenance == right.provenance &&
           left.reserved0 == right.reserved0;
}

uint32_t resolve_prediction_input_range_from_cursor(
    const worr_snapshot_consumed_command_v2 &consumed_command,
    bool canonical_required,
    worr_cgame_prediction_input_range_v1 *range_out)
{
    worr_cgame_prediction_input_command_v1
        history[WORR_CGAME_PREDICTION_INPUT_CAPACITY]{};
    worr_prediction_input_resolve_request_v1 request{};

    if (!range_out)
        return WORR_CGAME_PREDICTION_INPUT_INVALID_ARGUMENT;

    for (uint32_t age = 0; age < CMD_BACKUP; ++age) {
        const uint32_t sequence = cl.cmdNumber - age;
        auto &entry = history[age];
        entry.legacy_sequence = sequence;
        (void)CL_CommandIdentityForNumber(sequence, &entry.command_id);
        entry.command = prediction_input_command(
            cl.cmds[sequence & CMD_MASK], nullptr);
    }

    request.struct_size = sizeof(request);
    request.schema_version = WORR_PREDICTION_INPUT_RESOLVER_VERSION;
    const bool canonical_capability =
        CL_NetCapabilityHas(WORR_NET_CAP_CONSUMED_COMMAND_CURSOR_V1);
    if (canonical_capability) {
        request.flags |=
            WORR_PREDICTION_INPUT_RESOLVE_CANONICAL_CAPABILITY;
        if (canonical_required ||
            CL_ConsumedCursorCanonicalEstablished()) {
            request.flags |=
                WORR_PREDICTION_INPUT_RESOLVE_CANONICAL_ESTABLISHED;
        }
    }
    if (canonical_required &&
        (!canonical_capability ||
         consumed_command.provenance !=
             WORR_SNAPSHOT_CONSUMED_COMMAND_SERVER_CONSUMED ||
         consumed_command.reserved0 != 0 ||
         !Worr_CommandCursorValidV1(consumed_command.cursor))) {
        return prediction_input_fail(
            range_out,
            WORR_CGAME_PREDICTION_INPUT_CANONICAL_METADATA_REQUIRED);
    }
    (void)CL_CommandIdentityGetState(
        &request.identity_initial_epoch,
        &request.identity_baseline_legacy_sequence);
    request.current_legacy_sequence = cl.cmdNumber;
    request.legacy_acknowledged_sequence =
        cl.history[cls.netchan.incoming_acknowledged & CMD_MASK].cmdNumber;
    request.history_count = q_countof(history);
    request.consumed_command = consumed_command;
    request.pending_present = cl.cmd.msec != 0;
    if (request.pending_present) {
        request.pending_command.legacy_sequence = cl.cmdNumber + 1u;
        request.pending_command.command =
            prediction_input_command(cl.cmd, cl.localmove);
    }
    request.history = history;
    const uint32_t result =
        Worr_PredictionInputResolveV1(&request, range_out);
    if (canonical_required &&
        (result != WORR_CGAME_PREDICTION_INPUT_OK ||
         range_out->source !=
             WORR_CGAME_PREDICTION_INPUT_SOURCE_CANONICAL_CURSOR ||
         !consumed_command_equal(range_out->consumed_command,
                                 consumed_command))) {
        if (result != WORR_CGAME_PREDICTION_INPUT_OK)
            return result;
        return prediction_input_fail(
            range_out, WORR_CGAME_PREDICTION_INPUT_INVALID_METADATA);
    }
    return result;
}

uint32_t resolve_prediction_input_range(
    worr_cgame_prediction_input_range_v1 *range_out)
{
    ensure_diagnostics();
    increment_saturated(diagnostics.v1_requests);
    const uint32_t result = resolve_prediction_input_range_from_cursor(
        cl.frame.consumed_command, false, range_out);
    note_result(result, range_out);
    return result;
}

uint32_t resolve_prediction_input_range_for_cursor(
    const worr_cgame_prediction_input_request_v2 *request,
    worr_cgame_prediction_input_range_v1 *range_out)
{
    ensure_diagnostics();
    increment_saturated(diagnostics.v2_requests);
    if (!request || !range_out ||
        request->struct_size != sizeof(*request) ||
        request->api_version !=
            WORR_CGAME_PREDICTION_INPUT_API_VERSION_V2 ||
        request->flags !=
            WORR_CGAME_PREDICTION_INPUT_REQUEST_CANONICAL_REQUIRED ||
        request->reserved0 != 0) {
        const uint32_t result = prediction_input_fail(
            range_out,
            WORR_CGAME_PREDICTION_INPUT_INVALID_ARGUMENT);
        note_result(result, range_out);
        return result;
    }
    const uint32_t result = resolve_prediction_input_range_from_cursor(
        request->consumed_command, true, range_out);
    note_result(result, range_out);
    return result;
}

const worr_cgame_prediction_input_import_v1 prediction_input_import = {
    .struct_size = sizeof(prediction_input_import),
    .api_version = WORR_CGAME_PREDICTION_INPUT_API_VERSION,
    .ResolveInputRange = resolve_prediction_input_range,
};

const worr_cgame_prediction_input_import_v2 prediction_input_import_v2 = {
    .struct_size = sizeof(prediction_input_import_v2),
    .api_version = WORR_CGAME_PREDICTION_INPUT_API_VERSION_V2,
    .ResolveInputRangeForCursor =
        resolve_prediction_input_range_for_cursor,
};

} // namespace

extern "C" const worr_cgame_prediction_input_import_v1 *
CL_GetCGamePredictionInputImportV1(void)
{
    return &prediction_input_import;
}

extern "C" const worr_cgame_prediction_input_import_v2 *
CL_GetCGamePredictionInputImportV2(void)
{
    return &prediction_input_import_v2;
}

extern "C" void CL_ResetCGamePredictionInputDiagnostics(void)
{
    reset_diagnostics();
}

extern "C" bool CL_GetCGamePredictionInputDiagnostics(
    cl_cgame_prediction_input_diagnostics_v1 *diagnostics_out)
{
    if (!diagnostics_out)
        return false;
    ensure_diagnostics();
    *diagnostics_out = diagnostics;
    return true;
}
