/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/legacy_game_event_candidate.h"

#include "shared/shared.h"
#include "common/protocol.h"

#include <string.h>

worr_legacy_game_event_candidate_result_v1
Worr_LegacyGameEventDecodeRawSequenceV1(
    const uint8_t *raw_message, size_t raw_message_size,
    worr_legacy_game_event_candidate_carrier_v1 *carriers_out,
    uint32_t capacity, uint32_t *count_out)
{
    worr_legacy_game_event_candidate_carrier_v1
        carriers[WORR_LEGACY_GAME_EVENT_CANDIDATE_SEQUENCE_MAX];
    uint32_t count = 0;
    size_t cursor = 0;

    if (!raw_message || !carriers_out || !count_out || capacity == 0)
        return WORR_LEGACY_GAME_EVENT_CANDIDATE_INVALID_ARGUMENT;
    if (raw_message_size == 0)
        return WORR_LEGACY_GAME_EVENT_CANDIDATE_INVALID_SHAPE;

    while (cursor != raw_message_size) {
        worr_legacy_game_event_candidate_carrier_v1 carrier;
        size_t consumed = 0;
        const size_t remaining = raw_message_size - cursor;

        if (count == capacity ||
            count == WORR_LEGACY_GAME_EVENT_CANDIDATE_SEQUENCE_MAX) {
            return WORR_LEGACY_GAME_EVENT_CANDIDATE_CAPACITY;
        }
        memset(&carrier, 0, sizeof(carrier));
        if (raw_message[cursor] == svc_temp_entity) {
            if (Worr_LegacyTempEventDecodeRawPrefixV1(
                    raw_message + cursor, remaining, &carrier.temp_entity,
                    &consumed) != WORR_LEGACY_TEMP_EVENT_CANDIDATE_OK) {
                return WORR_LEGACY_GAME_EVENT_CANDIDATE_INVALID_SHAPE;
            }
            carrier.kind = WORR_LEGACY_GAME_EVENT_CANDIDATE_TEMP_ENTITY;
        } else if (raw_message[cursor] == svc_muzzleflash ||
                   raw_message[cursor] == svc_muzzleflash2 ||
                   raw_message[cursor] == svc_muzzleflash3) {
            if (Worr_LegacyMuzzleEventDecodeRawPrefixV1(
                    raw_message + cursor, remaining, &carrier.muzzleflash,
                    &carrier.muzzle_family, &consumed) !=
                WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_OK) {
                return WORR_LEGACY_GAME_EVENT_CANDIDATE_INVALID_SHAPE;
            }
            carrier.kind = WORR_LEGACY_GAME_EVENT_CANDIDATE_MUZZLEFLASH;
        } else {
            return WORR_LEGACY_GAME_EVENT_CANDIDATE_INVALID_SHAPE;
        }
        if (consumed == 0 || consumed > remaining)
            return WORR_LEGACY_GAME_EVENT_CANDIDATE_INVALID_SHAPE;
        carriers[count++] = carrier;
        cursor += consumed;
    }

    memcpy(carriers_out, carriers, sizeof(carriers[0]) * count);
    *count_out = count;
    return WORR_LEGACY_GAME_EVENT_CANDIDATE_OK;
}
