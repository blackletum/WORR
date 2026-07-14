/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "client.h"
#include "client/command_identity.h"

#include <array>
#include <cstring>

namespace {

struct command_identity_state_t {
    std::array<uint32_t, CMD_BACKUP> legacy_numbers{};
    std::array<worr_command_id_v1, CMD_BACKUP> ids{};
    std::array<bool, CMD_BACKUP> valid{};
    worr_command_id_v1 latest{};
    uint32_t epoch{};
    uint32_t baseline_legacy_sequence{};
    uint32_t latest_legacy_sequence{};
    bool initialized{};
    bool has_latest{};
};

command_identity_state_t identity;

bool id_equal(worr_command_id_v1 left, worr_command_id_v1 right)
{
    return left.epoch == right.epoch && left.sequence == right.sequence;
}

} // namespace

extern "C" void CL_CommandIdentityReset(uint32_t command_epoch)
{
    std::memset(&identity, 0, sizeof(identity));
    identity.epoch = command_epoch;
    identity.baseline_legacy_sequence = cl.cmdNumber;
    identity.latest_legacy_sequence = cl.cmdNumber;
    identity.initialized = command_epoch != 0;
}

extern "C" void CL_CommandIdentityShutdown(void)
{
    std::memset(&identity, 0, sizeof(identity));
}

extern "C" bool CL_CommandIdentityFinalize(
    uint32_t legacy_command_number)
{
    worr_command_id_v1 next{};
    if (!identity.initialized)
        return false;
    if (legacy_command_number != identity.latest_legacy_sequence + 1u)
        return false;
    if (!identity.has_latest) {
        next.epoch = identity.epoch;
        next.sequence = 1;
    } else if (!Worr_CommandIdNextV1(identity.latest, &next)) {
        return false;
    }
    const uint32_t slot = legacy_command_number & CMD_MASK;
    identity.legacy_numbers[slot] = legacy_command_number;
    identity.ids[slot] = next;
    identity.valid[slot] = true;
    identity.latest = next;
    identity.latest_legacy_sequence = legacy_command_number;
    identity.has_latest = true;
    return true;
}

extern "C" bool CL_CommandIdentityForNumber(
    uint32_t legacy_command_number, worr_command_id_v1 *id_out)
{
    if (!id_out)
        return false;
    const uint32_t slot = legacy_command_number & CMD_MASK;
    if (!identity.valid[slot] ||
        identity.legacy_numbers[slot] != legacy_command_number ||
        !Worr_CommandIdValidV1(identity.ids[slot], false)) {
        return false;
    }
    *id_out = identity.ids[slot];
    return true;
}

extern "C" bool CL_CommandIdentityGetState(
    uint32_t *initial_epoch_out,
    uint32_t *baseline_legacy_sequence_out)
{
    if (!initial_epoch_out || !baseline_legacy_sequence_out ||
        !identity.initialized || identity.epoch == 0) {
        return false;
    }
    *initial_epoch_out = identity.epoch;
    *baseline_legacy_sequence_out = identity.baseline_legacy_sequence;
    return true;
}

extern "C" bool CL_CommandIdentityWriteSideband(
    uintptr_t write_io_arg, uint32_t first_legacy_command_number,
    uint32_t command_count)
{
    worr_command_id_v1 first{};
    worr_command_id_v1 current{};
    worr_legacy_command_range_v1 range{};
    std::array<worr_legacy_command_setting_pair_v1,
               WORR_LEGACY_COMMAND_SIDEBAND_PAIR_COUNT> pairs{};

    if (write_io_arg == 0 || command_count == 0 ||
        command_count > WORR_LEGACY_COMMAND_BATCH_MAX_COUNT ||
        !CL_CommandIdentityForNumber(first_legacy_command_number, &first)) {
        return false;
    }
    current = first;
    for (uint32_t index = 1; index < command_count; ++index) {
        worr_command_id_v1 observed{};
        if (!Worr_CommandIdNextV1(current, &current) ||
            !CL_CommandIdentityForNumber(
                first_legacy_command_number + index, &observed) ||
            !id_equal(current, observed)) {
            return false;
        }
    }
    if (!Worr_LegacyCommandRangeInitV1(
            &range, first, static_cast<uint16_t>(command_count)) ||
        !Worr_LegacyCommandSidebandEncodeV1(
            &range, pairs.data(), pairs.size())) {
        return false;
    }

    q2proto_clc_message_t message{.type = Q2P_CLC_SETTING};
    for (const auto &pair : pairs) {
        message.setting.index = pair.index;
        message.setting.value = pair.value;
        if (q2proto_client_write(&cls.q2proto_ctx,
                                 write_io_arg,
                                 &message) != Q2P_ERR_SUCCESS) {
            return false;
        }
    }
    return true;
}
