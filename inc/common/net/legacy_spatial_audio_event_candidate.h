/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "shared/event_abi.h"
#include "q2proto/q2proto.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Converts decoded legacy spatial audio into an ID-less T05 action template.
 * The record's entity references remain absent: source_entity_index_out is
 * resolved against caller-owned observed or final-emission lineage before
 * publication. All output pointers are required and unchanged on failure.
 */
typedef enum worr_legacy_spatial_audio_event_candidate_result_v1_e {
    WORR_LEGACY_SPATIAL_AUDIO_EVENT_CANDIDATE_OK = 0,
    WORR_LEGACY_SPATIAL_AUDIO_EVENT_CANDIDATE_INVALID_ARGUMENT = 1,
    WORR_LEGACY_SPATIAL_AUDIO_EVENT_CANDIDATE_INVALID_SHAPE = 2,
    WORR_LEGACY_SPATIAL_AUDIO_EVENT_CANDIDATE_ENTITY_OUT_OF_RANGE = 3,
    WORR_LEGACY_SPATIAL_AUDIO_EVENT_CANDIDATE_INVALID_RECORD = 4,
} worr_legacy_spatial_audio_event_candidate_result_v1;

worr_legacy_spatial_audio_event_candidate_result_v1
Worr_LegacySpatialAudioEventCandidateBuildV1(
    const q2proto_sound_t *sound, uint32_t source_tick,
    uint64_t source_time_us, uint32_t max_entities,
    worr_event_record_v1 *candidate_out, uint32_t *source_entity_index_out);

#ifdef __cplusplus
}
#endif
