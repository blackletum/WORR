/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "event_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WORR_EVENT_SHADOW_IMPORT_V1 "WORR_CANONICAL_EVENT_SHADOW_IMPORT_V1"
#define WORR_EVENT_SHADOW_API_VERSION 1u
#define WORR_EVENT_SHADOW_CAPACITY 4096u
#define WORR_EVENT_SHADOW_MAX_ENTITIES 8192u
#define WORR_EVENT_SHADOW_MAX_LEGACY_EVENT 63u

typedef enum worr_event_shadow_submit_result_v1_e {
    WORR_EVENT_SHADOW_ACCEPTED = 0,
    WORR_EVENT_SHADOW_DUPLICATE = 1,
    WORR_EVENT_SHADOW_INVALID = 2,
    WORR_EVENT_SHADOW_CAPACITY_EXHAUSTED = 3,
    WORR_EVENT_SHADOW_CONFLICT = 4,
    WORR_EVENT_SHADOW_UNAVAILABLE = 5,
    WORR_EVENT_SHADOW_ID_EXHAUSTED = 6,
} worr_event_shadow_submit_result_v1;

typedef enum worr_event_shadow_map_result_v1_e {
    WORR_EVENT_SHADOW_MAP_MAPPED = 0,
    WORR_EVENT_SHADOW_MAP_DUPLICATE = 1,
    WORR_EVENT_SHADOW_MAP_INVALID = 2,
} worr_event_shadow_map_result_v1;

/* Caller-owned source state.  It suppresses repeated scans of the same legacy
 * entity event while preserving distinct values with deterministic ordinals. */
typedef struct worr_event_shadow_source_state_v1_s {
    uint32_t source_tick;
    uint32_t entity_generation;
    uint64_t seen_event_mask;
    uint32_t next_ordinal;
    uint8_t active;
    uint8_t reserved[3];
} worr_event_shadow_source_state_v1;

typedef struct worr_event_shadow_legacy_input_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t source_tick;
    uint32_t raw_event;
    uint64_t source_time_us;
    worr_event_entity_ref_v1 source_entity;
    uint32_t max_entities;
    uint32_t reserved0;
} worr_event_shadow_legacy_input_v1;

typedef struct worr_event_shadow_status_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t stream_epoch;
    uint32_t capacity;
    uint32_t occupied;
    uint32_t last_sequence;
    uint32_t last_result;
    uint32_t reserved0;
    uint64_t reset_count;
    uint64_t submit_attempts;
    uint64_t accepted;
    uint64_t duplicates;
    uint64_t invalid;
    uint64_t capacity_failures;
    uint64_t conflicts;
    uint64_t id_exhaustions;
    uint64_t sequence_wraps;
    uint64_t query_count;
    uint64_t last_record_hash;
} worr_event_shadow_status_v1;

/* Optional game-import extension.  It is process-local and must never be
 * serialized.  SubmitCandidate copies its input before assigning authority. */
typedef struct worr_event_shadow_import_v1_s {
    uint32_t struct_size;
    uint32_t api_version;
    worr_event_shadow_submit_result_v1 (*SubmitCandidate)(
        const worr_event_record_v1 *candidate);
    bool (*GetStatus)(worr_event_shadow_status_v1 *status_out);
    bool (*GetRecordFromNewest)(uint32_t age,
                                worr_event_record_v1 *record_out,
                                uint32_t *slot_state_out,
                                uint64_t *record_hash_out);
} worr_event_shadow_import_v1;

worr_event_shadow_map_result_v1 Worr_EventShadowMapLegacyEntityV1(
    worr_event_shadow_source_state_v1 *source_state,
    const worr_event_shadow_legacy_input_v1 *input,
    worr_event_record_v1 *candidate_out);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_EVENT_SHADOW_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define WORR_EVENT_SHADOW_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

WORR_EVENT_SHADOW_STATIC_ASSERT(
    sizeof(worr_event_shadow_source_state_v1) == 24,
    "event shadow source state v1 layout changed");
WORR_EVENT_SHADOW_STATIC_ASSERT(
    sizeof(worr_event_shadow_legacy_input_v1) == 40,
    "event shadow legacy input v1 layout changed");
WORR_EVENT_SHADOW_STATIC_ASSERT(
    sizeof(worr_event_shadow_status_v1) == 120,
    "event shadow status v1 layout changed");

#undef WORR_EVENT_SHADOW_STATIC_ASSERT
