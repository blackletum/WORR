/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/usercmd_delta.h"

#include <string.h>

bool NetUsercmd_Canonicalize(usercmd_t *command)
{
    if (!command)
        return false;

    usercmd_t canonical = *command;
    for (unsigned i = 0; i < 3; ++i) {
        if (!NetUsercmd_CanonicalizeAngle(command->angles[i],
                                          &canonical.angles[i])) {
            return false;
        }
    }
    if (!NetUsercmd_CanonicalizeMove(command->forwardmove,
                                     &canonical.forwardmove) ||
        !NetUsercmd_CanonicalizeMove(command->sidemove,
                                     &canonical.sidemove)) {
        return false;
    }
    *command = canonical;
    return true;
}

bool NetUsercmd_ToPredictionCommandV1(
    const usercmd_t *command, worr_prediction_command_v1 *prediction_out)
{
    usercmd_t canonical;
    worr_prediction_command_v1 prediction;

    if (!command || !prediction_out)
        return false;

    canonical = *command;
    if (!NetUsercmd_Canonicalize(&canonical))
        return false;

    memset(&prediction, 0, sizeof(prediction));
    prediction.struct_size = sizeof(prediction);
    prediction.schema_version = WORR_PREDICTION_ABI_VERSION;
    prediction.duration_ms = canonical.msec;
    prediction.buttons = canonical.buttons;
    for (unsigned i = 0; i < 3; ++i)
        prediction.view_angles[i] = canonical.angles[i];
    prediction.forward_move = canonical.forwardmove;
    prediction.side_move = canonical.sidemove;

    memcpy(prediction_out, &prediction, sizeof(prediction));
    return true;
}

static void buttons_to_legacy_upmove(uint8_t buttons, uint8_t *wire_buttons,
                                     float *upmove)
{
    *wire_buttons = buttons & ~(BUTTON_HOLSTER | BUTTON_JUMP | BUTTON_CROUCH);
    *upmove = 0.0f;
    if (buttons & BUTTON_JUMP)
        *upmove += 200.0f;
    if (buttons & BUTTON_CROUCH)
        *upmove -= 200.0f;
}

static uint8_t buttons_from_legacy_upmove(uint8_t wire_buttons,
                                          float upmove)
{
    uint8_t buttons =
        wire_buttons & ~(BUTTON_HOLSTER | BUTTON_JUMP | BUTTON_CROUCH);
    if (upmove > 0.0f)
        buttons |= BUTTON_JUMP;
    else if (upmove < 0.0f)
        buttons |= BUTTON_CROUCH;
    return buttons;
}

bool NetUsercmd_CanonicalizeForTransport(usercmd_t *command,
                                         bool has_upmove)
{
    if (!command)
        return false;

    usercmd_t canonical = *command;
    if (!NetUsercmd_Canonicalize(&canonical))
        return false;
    if (has_upmove) {
        uint8_t wire_buttons;
        float upmove;
        buttons_to_legacy_upmove(canonical.buttons, &wire_buttons, &upmove);
        canonical.buttons =
            buttons_from_legacy_upmove(wire_buttons, upmove);
    }
    *command = canonical;
    return true;
}

bool NetUsercmd_BuildDelta(q2proto_clc_move_delta_t *delta_move,
                           const usercmd_t *from, const usercmd_t *command,
                           uint8_t lightlevel, bool has_upmove)
{
    if (!delta_move || !command)
        return false;

    usercmd_t zero = {0};
    usercmd_t canonical_from = from ? *from : zero;
    usercmd_t canonical_command = *command;
    if (!NetUsercmd_CanonicalizeForTransport(&canonical_from, has_upmove) ||
        !NetUsercmd_CanonicalizeForTransport(&canonical_command,
                                              has_upmove)) {
        return false;
    }

    memset(delta_move, 0, sizeof(*delta_move));
    uint8_t from_buttons = canonical_from.buttons;
    uint8_t new_buttons = canonical_command.buttons;
    float from_upmove = 0.0f;
    float new_upmove = 0.0f;
    if (has_upmove) {
        buttons_to_legacy_upmove(canonical_from.buttons, &from_buttons,
                                 &from_upmove);
        buttons_to_legacy_upmove(canonical_command.buttons, &new_buttons,
                                 &new_upmove);
    }

    for (unsigned i = 0; i < 3; ++i) {
        if (canonical_command.angles[i] == canonical_from.angles[i])
            continue;
        q2proto_var_angles_set_float_comp(&delta_move->angles, i,
                                           canonical_command.angles[i]);
        delta_move->delta_bits |= Q2P_CMD_ANGLE0 << i;
    }
    if (canonical_command.forwardmove != canonical_from.forwardmove) {
        q2proto_var_coords_set_float_comp(&delta_move->move, 0,
                                           canonical_command.forwardmove);
        delta_move->delta_bits |= Q2P_CMD_MOVE_FORWARD;
    }
    if (canonical_command.sidemove != canonical_from.sidemove) {
        q2proto_var_coords_set_float_comp(&delta_move->move, 1,
                                           canonical_command.sidemove);
        delta_move->delta_bits |= Q2P_CMD_MOVE_SIDE;
    }
    if (has_upmove && new_upmove != from_upmove) {
        q2proto_var_coords_set_float_comp(&delta_move->move, 2, new_upmove);
        delta_move->delta_bits |= Q2P_CMD_MOVE_UP;
    }
    if (new_buttons != from_buttons) {
        delta_move->buttons = new_buttons;
        delta_move->delta_bits |= Q2P_CMD_BUTTONS;
    }
    delta_move->msec = canonical_command.msec;
    delta_move->lightlevel = lightlevel;
    return true;
}

bool NetUsercmd_ApplyDelta(const q2proto_clc_move_delta_t *move_delta,
                           const usercmd_t *from, usercmd_t *command,
                           bool has_upmove)
{
    if (!move_delta || !command)
        return false;

    usercmd_t decoded = {0};
    if (from)
        decoded = *from;

    if (move_delta->delta_bits & Q2P_CMD_ANGLE0)
        decoded.angles[0] =
            q2proto_var_angles_get_float_comp(&move_delta->angles, 0);
    if (move_delta->delta_bits & Q2P_CMD_ANGLE1)
        decoded.angles[1] =
            q2proto_var_angles_get_float_comp(&move_delta->angles, 1);
    if (move_delta->delta_bits & Q2P_CMD_ANGLE2)
        decoded.angles[2] =
            q2proto_var_angles_get_float_comp(&move_delta->angles, 2);
    if (move_delta->delta_bits & Q2P_CMD_MOVE_FORWARD)
        decoded.forwardmove =
            q2proto_var_coords_get_float_comp(&move_delta->move, 0);
    if (move_delta->delta_bits & Q2P_CMD_MOVE_SIDE)
        decoded.sidemove =
            q2proto_var_coords_get_float_comp(&move_delta->move, 1);

    if (has_upmove) {
        uint8_t wire_buttons;
        float upmove;
        buttons_to_legacy_upmove(decoded.buttons, &wire_buttons, &upmove);
        if (move_delta->delta_bits & Q2P_CMD_BUTTONS)
            wire_buttons = move_delta->buttons;
        if (move_delta->delta_bits & Q2P_CMD_MOVE_UP)
            upmove = q2proto_var_coords_get_float_comp(&move_delta->move, 2);
        decoded.buttons = buttons_from_legacy_upmove(wire_buttons, upmove);
    } else if (move_delta->delta_bits & Q2P_CMD_BUTTONS) {
        decoded.buttons = move_delta->buttons;
    }

    decoded.msec = move_delta->msec;
    if (!NetUsercmd_CanonicalizeForTransport(&decoded, has_upmove))
        return false;
    *command = decoded;
    return true;
}
