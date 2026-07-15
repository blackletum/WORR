/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/legacy_muzzle_event_candidate.h"
#include "common/net/legacy_temp_event_candidate.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WORR_LEGACY_GAME_EVENT_CANDIDATE_SEQUENCE_MAX 16u

typedef enum worr_legacy_game_event_candidate_kind_v1_e {
    WORR_LEGACY_GAME_EVENT_CANDIDATE_TEMP_ENTITY = 1,
    WORR_LEGACY_GAME_EVENT_CANDIDATE_MUZZLEFLASH = 2,
} worr_legacy_game_event_candidate_kind_v1;

typedef struct worr_legacy_game_event_candidate_carrier_v1_s {
    uint16_t kind;
    uint16_t reserved0;
    uint32_t muzzle_family;
    q2proto_svc_temp_entity_t temp_entity;
    q2proto_svc_muzzleflash_t muzzleflash;
} worr_legacy_game_event_candidate_carrier_v1;

typedef enum worr_legacy_game_event_candidate_result_v1_e {
    WORR_LEGACY_GAME_EVENT_CANDIDATE_OK = 0,
    WORR_LEGACY_GAME_EVENT_CANDIDATE_INVALID_ARGUMENT = 1,
    WORR_LEGACY_GAME_EVENT_CANDIDATE_INVALID_SHAPE = 2,
    WORR_LEGACY_GAME_EVENT_CANDIDATE_CAPACITY = 3,
} worr_legacy_game_event_candidate_result_v1;

/* Decodes one ordered, homogeneous-or-mixed sequence of direct game-DLL
 * temporary-entity and muzzle carriers. Outputs remain untouched unless the
 * complete bounded sequence is decoded. */
worr_legacy_game_event_candidate_result_v1
Worr_LegacyGameEventDecodeRawSequenceV1(
    const uint8_t *raw_message, size_t raw_message_size,
    worr_legacy_game_event_candidate_carrier_v1 *carriers_out,
    uint32_t capacity, uint32_t *count_out);

#ifdef __cplusplus
}
#endif
