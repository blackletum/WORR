/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/snapshot_projection.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WORR_SNAPSHOT_TIMELINE_VERSION 1u
#define WORR_SNAPSHOT_TIMELINE_RATE_ONE_Q16 65536u
#define WORR_SNAPSHOT_TIMELINE_RATE_MAX_Q16 262144u
#define WORR_SNAPSHOT_TIMELINE_MAX_INTERPOLATION_DELAY_US UINT64_C(1000000)
#define WORR_SNAPSHOT_TIMELINE_MAX_EXTRAPOLATION_US UINT64_C(250000)
#define WORR_SNAPSHOT_TIMELINE_MAX_LINEAR_VELOCITY 8192.0f
#define WORR_SNAPSHOT_TIMELINE_MAX_ANGULAR_VELOCITY 1440.0f
#define WORR_SNAPSHOT_TIMELINE_REF_NONE_SLOT UINT32_MAX

typedef enum worr_snapshot_timeline_result_v1_e {
    WORR_SNAPSHOT_TIMELINE_OK = 0,
    WORR_SNAPSHOT_TIMELINE_INVALID_ARGUMENT = 1,
    WORR_SNAPSHOT_TIMELINE_INVALID_TIMELINE = 2,
    WORR_SNAPSHOT_TIMELINE_INVALID_PROJECTION = 3,
    WORR_SNAPSHOT_TIMELINE_CAPACITY = 4,
    WORR_SNAPSHOT_TIMELINE_OVERLAP = 5,
    WORR_SNAPSHOT_TIMELINE_DUPLICATE = 6,
    WORR_SNAPSHOT_TIMELINE_CONFLICT = 7,
    WORR_SNAPSHOT_TIMELINE_OUT_OF_ORDER = 8,
    WORR_SNAPSHOT_TIMELINE_TIME_ORDER = 9,
    WORR_SNAPSHOT_TIMELINE_SERIAL_EXHAUSTED = 10,
    WORR_SNAPSHOT_TIMELINE_GENERATION_EXHAUSTED = 11,
    WORR_SNAPSHOT_TIMELINE_SEGMENT_EXHAUSTED = 12,
    WORR_SNAPSHOT_TIMELINE_CLOCK_UNINITIALIZED = 13,
    WORR_SNAPSHOT_TIMELINE_CLOCK_REGRESSION = 14,
    WORR_SNAPSHOT_TIMELINE_CLOCK_OVERFLOW = 15,
    WORR_SNAPSHOT_TIMELINE_INVALID_POLICY = 16,
    WORR_SNAPSHOT_TIMELINE_STALE_REF = 17,
    WORR_SNAPSHOT_TIMELINE_BUFFER_TOO_SMALL = 18,
    WORR_SNAPSHOT_TIMELINE_CURSOR_STALE = 19,
    WORR_SNAPSHOT_TIMELINE_CURSOR_OVERRUN = 20,
    WORR_SNAPSHOT_TIMELINE_EVENT_CONFLICT = 21,
    WORR_SNAPSHOT_TIMELINE_NOT_FOUND = 22,
    WORR_SNAPSHOT_TIMELINE_CORRUPT = 23,
} worr_snapshot_timeline_result_v1;

typedef struct worr_snapshot_timeline_ref_v1_s {
    uint32_t slot;
    uint32_t generation;
} worr_snapshot_timeline_ref_v1;

/* One durable slot. Payload records occupy the caller-owned per-slot arenas. */
typedef struct worr_snapshot_timeline_slot_v1_s {
    worr_snapshot_v2 snapshot;
    worr_snapshot_player_v2 player;
    uint64_t publish_serial;
    uint64_t receive_time_us;
    uint64_t segment;
    uint32_t generation;
    uint32_t entity_count;
    uint32_t area_byte_count;
    uint32_t event_ref_count;
    uint32_t committed;
    uint32_t reserved0;
} worr_snapshot_timeline_slot_v1;

typedef enum worr_snapshot_timeline_clock_operation_v1_e {
    WORR_SNAPSHOT_TIMELINE_CLOCK_ANCHOR = 1,
    WORR_SNAPSHOT_TIMELINE_CLOCK_ADVANCE = 2,
    WORR_SNAPSHOT_TIMELINE_CLOCK_PAUSE = 3,
    WORR_SNAPSHOT_TIMELINE_CLOCK_RESUME = 4,
    WORR_SNAPSHOT_TIMELINE_CLOCK_SET_RATE = 5,
    WORR_SNAPSHOT_TIMELINE_CLOCK_DEMO_SEEK = 6,
    WORR_SNAPSHOT_TIMELINE_CLOCK_RESET = 7,
} worr_snapshot_timeline_clock_operation_v1;

typedef enum worr_snapshot_timeline_clock_reset_reason_v1_e {
    WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_NONE = 0,
    WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_INITIAL = 1,
    WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_MAP = 2,
    WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_DEMO_REWIND = 3,
    WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_HARD_RESYNC = 4,
    WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_USER = 5,
} worr_snapshot_timeline_clock_reset_reason_v1;

/*
 * host_time_us is monotonic. render_time_us is consumed only by ANCHOR,
 * DEMO_SEEK and RESET. rate_q16 is consumed only by ANCHOR and SET_RATE.
 */
typedef struct worr_snapshot_timeline_clock_request_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint16_t operation;
    uint16_t reset_reason;
    uint32_t rate_q16;
    uint64_t host_time_us;
    uint64_t render_time_us;
    uint64_t reserved0;
} worr_snapshot_timeline_clock_request_v1;

typedef struct worr_snapshot_timeline_clock_state_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint64_t epoch;
    uint64_t host_time_us;
    uint64_t render_time_us;
    uint32_t rate_q16;
    uint16_t paused;
    uint16_t initialized;
    uint32_t last_reset_reason;
    /* Remainder below one render microsecond, in unsigned Q16 units. */
    uint32_t fractional_q16;
} worr_snapshot_timeline_clock_state_v1;

typedef struct worr_snapshot_timeline_policy_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint64_t interpolation_delay_us;
    uint64_t max_extrapolation_us;
    float teleport_distance;
    float max_linear_velocity;
    float max_angular_velocity;
    uint32_t allow_extrapolation;
    uint32_t reserved0;
} worr_snapshot_timeline_policy_v1;

typedef enum worr_snapshot_timeline_pair_mode_v1_e {
    WORR_SNAPSHOT_TIMELINE_PAIR_NONE = 0,
    WORR_SNAPSHOT_TIMELINE_PAIR_CLAMP_EARLIEST = 1,
    WORR_SNAPSHOT_TIMELINE_PAIR_HOLD = 2,
    WORR_SNAPSHOT_TIMELINE_PAIR_INTERPOLATE = 3,
    WORR_SNAPSHOT_TIMELINE_PAIR_EXACT = 4,
    WORR_SNAPSHOT_TIMELINE_PAIR_EXTRAPOLATE = 5,
    WORR_SNAPSHOT_TIMELINE_PAIR_CLAMP_LATEST = 6,
} worr_snapshot_timeline_pair_mode_v1;

enum {
    WORR_SNAPSHOT_TIMELINE_PAIR_BLOCK_DISCONTINUITY = 1u << 0,
    WORR_SNAPSHOT_TIMELINE_PAIR_BLOCK_NO_PREVIOUS = 1u << 1,
    WORR_SNAPSHOT_TIMELINE_PAIR_BLOCK_ZERO_INTERVAL = 1u << 2,
    WORR_SNAPSHOT_TIMELINE_PAIR_BLOCK_POLICY = 1u << 3,
};

typedef struct worr_snapshot_timeline_pair_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    worr_snapshot_timeline_ref_v1 previous;
    worr_snapshot_timeline_ref_v1 current;
    uint64_t timeline_instance;
    uint64_t clock_epoch;
    uint64_t segment;
    uint64_t policy_hash;
    uint64_t target_time_us;
    uint64_t previous_time_us;
    uint64_t current_time_us;
    uint64_t phase_numerator_us;
    uint64_t phase_denominator_us;
    uint64_t extrapolation_us;
    uint32_t mode;
    uint32_t blocking_reasons;
    uint32_t discontinuity_flags;
    uint32_t reserved0;
} worr_snapshot_timeline_pair_v1;

typedef enum worr_snapshot_timeline_visibility_v1_e {
    WORR_SNAPSHOT_TIMELINE_VISIBILITY_ABSENT = 0,
    WORR_SNAPSHOT_TIMELINE_VISIBILITY_PRESENT = 1,
    WORR_SNAPSHOT_TIMELINE_VISIBILITY_ADDED_AT_CURRENT = 2,
    WORR_SNAPSHOT_TIMELINE_VISIBILITY_REMOVED_AT_CURRENT = 3,
    WORR_SNAPSHOT_TIMELINE_VISIBILITY_GENERATION_REPLACED = 4,
} worr_snapshot_timeline_visibility_v1;

typedef enum worr_snapshot_timeline_entity_mode_v1_e {
    WORR_SNAPSHOT_TIMELINE_ENTITY_NONE = 0,
    WORR_SNAPSHOT_TIMELINE_ENTITY_PREVIOUS = 1,
    WORR_SNAPSHOT_TIMELINE_ENTITY_CURRENT = 2,
    WORR_SNAPSHOT_TIMELINE_ENTITY_INTERPOLATED = 3,
    WORR_SNAPSHOT_TIMELINE_ENTITY_EXTRAPOLATED = 4,
} worr_snapshot_timeline_entity_mode_v1;

enum {
    WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_SNAPSHOT = 1u << 0,
    WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_GENERATION = 1u << 1,
    WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_MISSING = 1u << 2,
    WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_COMPONENT = 1u << 3,
    WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_TELEPORT = 1u << 4,
    WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_LINEAR_SPEED = 1u << 5,
    WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_ANGULAR_SPEED = 1u << 6,
    WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_DISCRETE_TRANSITION = 1u << 7,
    WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_POLICY = 1u << 8,
};

/* The sampled entity is still the canonical V2 record, never a new schema. */
typedef struct worr_snapshot_timeline_entity_sample_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t entity_index;
    uint16_t visible;
    uint16_t visibility;
    uint32_t mode;
    uint32_t blocking_reasons;
    uint64_t compatible_component_mask;
    uint64_t interpolated_component_mask;
    uint64_t extrapolated_component_mask;
    float linear_velocity[3];
    float angular_velocity[3];
    worr_snapshot_entity_v2 entity;
} worr_snapshot_timeline_entity_sample_v1;

typedef struct worr_snapshot_timeline_event_cursor_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint64_t timeline_instance;
    uint64_t segment;
    uint64_t retention_floor_serial;
    uint64_t publish_serial;
    uint32_t event_index;
    uint32_t reserved0;
} worr_snapshot_timeline_event_cursor_v1;

typedef enum worr_snapshot_timeline_event_dedup_v1_e {
    WORR_SNAPSHOT_TIMELINE_EVENT_DEDUP_NONE = 0,
    WORR_SNAPSHOT_TIMELINE_EVENT_DEDUP_AUTHORITY_ID = 1,
    WORR_SNAPSHOT_TIMELINE_EVENT_DEDUP_LEGACY_SEMANTIC = 2,
} worr_snapshot_timeline_event_dedup_v1;

enum {
    WORR_SNAPSHOT_TIMELINE_EVENT_HISTORY_COMPLETE = 1u << 0,
    WORR_SNAPSHOT_TIMELINE_EVENT_RETAINED_MATCH = 1u << 1,
};

typedef struct worr_snapshot_timeline_event_observation_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    worr_snapshot_timeline_ref_v1 snapshot_ref;
    worr_snapshot_id_v2 snapshot_id;
    uint64_t publish_serial;
    uint64_t dedup_key_hash;
    uint64_t first_match_publish_serial;
    uint32_t event_index;
    uint32_t dedup_kind;
    uint32_t retained_match_count;
    uint32_t flags;
    worr_snapshot_event_ref_v2 event_ref;
} worr_snapshot_timeline_event_observation_v1;

typedef struct worr_snapshot_timeline_stats_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t slot_capacity;
    uint32_t occupied;
    uint32_t occupied_high_water;
    uint32_t entity_high_water;
    uint32_t area_high_water;
    uint32_t event_high_water;
    uint64_t publish_count;
    uint64_t overwrite_count;
    uint64_t segment_count;
    uint64_t reset_count;
    uint64_t clock_update_count;
    uint64_t clock_seek_count;
    uint64_t selection_count;
    uint64_t interpolation_pair_count;
    uint64_t extrapolation_pair_count;
    uint64_t clamped_pair_count;
    uint64_t event_observation_count;
    uint64_t event_retained_match_count;
} worr_snapshot_timeline_stats_v1;

typedef struct worr_snapshot_timeline_hashes_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint64_t retained_content_hash;
    uint64_t clock_hash;
    uint64_t telemetry_hash;
} worr_snapshot_timeline_hashes_v1;

/* Runtime-only owner of caller-provided fixed storage. */
typedef struct worr_snapshot_timeline_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    worr_snapshot_timeline_slot_v1 *slots;
    worr_snapshot_entity_v2 *entities;
    uint8_t *area_bytes;
    worr_snapshot_event_ref_v2 *event_refs;
    uint32_t slot_capacity;
    uint32_t entities_per_slot;
    uint32_t area_bytes_per_slot;
    uint32_t event_refs_per_slot;
    uint32_t entity_storage_capacity;
    uint32_t area_storage_capacity;
    uint32_t event_storage_capacity;
    uint32_t max_entities;
    uint32_t next_slot;
    uint32_t occupied;
    uint32_t occupied_high_water;
    uint32_t entity_high_water;
    uint32_t area_high_water;
    uint32_t event_high_water;
    uint32_t reserved0;
    uint64_t next_publish_serial;
    uint64_t latest_publish_serial;
    uint64_t active_segment;
    uint64_t active_segment_first_serial;
    uint64_t instance_generation;
    worr_snapshot_timeline_clock_state_v1 clock;
    uint64_t publish_count;
    uint64_t overwrite_count;
    uint64_t segment_count;
    uint64_t reset_count;
    uint64_t clock_update_count;
    uint64_t clock_seek_count;
    uint64_t selection_count;
    uint64_t interpolation_pair_count;
    uint64_t extrapolation_pair_count;
    uint64_t clamped_pair_count;
    uint64_t event_observation_count;
    uint64_t event_retained_match_count;
} worr_snapshot_timeline_v1;

worr_snapshot_timeline_result_v1 Worr_SnapshotTimelineInitV1(
    worr_snapshot_timeline_v1 *timeline,
    worr_snapshot_timeline_slot_v1 *slots,
    uint32_t slot_capacity,
    worr_snapshot_entity_v2 *entities,
    uint32_t entity_storage_capacity,
    uint32_t entities_per_slot,
    uint8_t *area_bytes,
    uint32_t area_storage_capacity,
    uint32_t area_bytes_per_slot,
    worr_snapshot_event_ref_v2 *event_refs,
    uint32_t event_storage_capacity,
    uint32_t event_refs_per_slot,
    uint32_t max_entities);

worr_snapshot_timeline_result_v1 Worr_SnapshotTimelinePublishV1(
    worr_snapshot_timeline_v1 *timeline,
    const worr_snapshot_projection_view_v2 *view,
    uint64_t receive_time_us,
    worr_snapshot_timeline_ref_v1 *ref_out);

/* Invalidates all refs/cursors but retains monotonic publication serials. */
worr_snapshot_timeline_result_v1 Worr_SnapshotTimelineResetV1(
    worr_snapshot_timeline_v1 *timeline);

bool Worr_SnapshotTimelineRefValidV1(
    const worr_snapshot_timeline_v1 *timeline,
    worr_snapshot_timeline_ref_v1 ref);

worr_snapshot_timeline_result_v1 Worr_SnapshotTimelineCopySnapshotV1(
    const worr_snapshot_timeline_v1 *timeline,
    worr_snapshot_timeline_ref_v1 ref,
    worr_snapshot_v2 *snapshot_out);
worr_snapshot_timeline_result_v1 Worr_SnapshotTimelineCopyPlayerV1(
    const worr_snapshot_timeline_v1 *timeline,
    worr_snapshot_timeline_ref_v1 ref,
    worr_snapshot_player_v2 *player_out);
worr_snapshot_timeline_result_v1 Worr_SnapshotTimelineCopyEntitiesV1(
    const worr_snapshot_timeline_v1 *timeline,
    worr_snapshot_timeline_ref_v1 ref,
    worr_snapshot_entity_v2 *entities_out,
    uint32_t capacity,
    uint32_t *count_out);
worr_snapshot_timeline_result_v1 Worr_SnapshotTimelineCopyAreaV1(
    const worr_snapshot_timeline_v1 *timeline,
    worr_snapshot_timeline_ref_v1 ref,
    uint8_t *area_out,
    uint32_t capacity,
    uint32_t *count_out);
worr_snapshot_timeline_result_v1 Worr_SnapshotTimelineCopyEventRefsV1(
    const worr_snapshot_timeline_v1 *timeline,
    worr_snapshot_timeline_ref_v1 ref,
    worr_snapshot_event_ref_v2 *events_out,
    uint32_t capacity,
    uint32_t *count_out);

worr_snapshot_timeline_result_v1 Worr_SnapshotTimelineClockApplyV1(
    worr_snapshot_timeline_v1 *timeline,
    const worr_snapshot_timeline_clock_request_v1 *request,
    worr_snapshot_timeline_clock_state_v1 *state_out);

/*
 * Clock operations are transactional and host_time_us never regresses.
 * ANCHOR initializes an uninitialized clock and consumes render_time_us and
 * rate_q16. ADVANCE, PAUSE and RESUME consume only host_time_us. SET_RATE
 * advances to host_time_us at the old rate before installing rate_q16.
 * DEMO_SEEK and RESET consume render_time_us, clear the fractional remainder
 * and advance the clock epoch; RESET also resumes the clock. Ignored request
 * fields must be zero. A non-NONE reset_reason is required by ANCHOR,
 * DEMO_SEEK and RESET and forbidden by all other operations.
 */

worr_snapshot_timeline_result_v1 Worr_SnapshotTimelineSelectPairV1(
    worr_snapshot_timeline_v1 *timeline,
    const worr_snapshot_timeline_policy_v1 *policy,
    worr_snapshot_timeline_pair_v1 *pair_out);

worr_snapshot_timeline_result_v1 Worr_SnapshotTimelineSampleEntityV1(
    worr_snapshot_timeline_v1 *timeline,
    const worr_snapshot_timeline_policy_v1 *policy,
    const worr_snapshot_timeline_pair_v1 *pair,
    uint32_t entity_index,
    worr_snapshot_timeline_entity_sample_v1 *sample_out);

worr_snapshot_timeline_result_v1 Worr_SnapshotTimelineEventCursorBeginV1(
    const worr_snapshot_timeline_v1 *timeline,
    worr_snapshot_timeline_event_cursor_v1 *cursor_out);
worr_snapshot_timeline_result_v1 Worr_SnapshotTimelineEventNextV1(
    worr_snapshot_timeline_v1 *timeline,
    const worr_snapshot_timeline_event_cursor_v1 *cursor,
    worr_snapshot_timeline_event_cursor_v1 *next_cursor_out,
    worr_snapshot_timeline_event_observation_v1 *observation_out);

/*
 * Begin starts at the oldest snapshot retained in the active segment (or at
 * the next publication serial when the segment is empty). EventNext returns
 * events in publication/carrier order. A cursor is stale after reset or a
 * segment boundary and overruns if an unread publication is overwritten.
 * AUTHORITY_ID dedup compares exact authority IDs; LEGACY_SEMANTIC compares
 * semantic_version plus semantic_hash. Matches are prior retained events in
 * the active segment. HISTORY_COMPLETE is set only while the segment's first
 * publication remains retained, so callers never mistake bounded history for
 * proof that an inferred legacy event is globally unique.
 */

worr_snapshot_timeline_result_v1 Worr_SnapshotTimelineGetStatsV1(
    const worr_snapshot_timeline_v1 *timeline,
    worr_snapshot_timeline_stats_v1 *stats_out);
worr_snapshot_timeline_result_v1 Worr_SnapshotTimelineHashesV1(
    const worr_snapshot_timeline_v1 *timeline,
    worr_snapshot_timeline_hashes_v1 *hashes_out);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_SNAPSHOT_TIMELINE_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define WORR_SNAPSHOT_TIMELINE_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

WORR_SNAPSHOT_TIMELINE_STATIC_ASSERT(
    sizeof(worr_snapshot_timeline_ref_v1) == 8,
    "snapshot timeline ref layout changed");
WORR_SNAPSHOT_TIMELINE_STATIC_ASSERT(
    offsetof(worr_snapshot_timeline_slot_v1, player) == 216,
    "snapshot timeline slot player offset changed");
WORR_SNAPSHOT_TIMELINE_STATIC_ASSERT(
    offsetof(worr_snapshot_timeline_slot_v1, publish_serial) == 544,
    "snapshot timeline slot serial offset changed");
WORR_SNAPSHOT_TIMELINE_STATIC_ASSERT(
    sizeof(worr_snapshot_timeline_slot_v1) == 592,
    "snapshot timeline slot layout changed");
WORR_SNAPSHOT_TIMELINE_STATIC_ASSERT(
    sizeof(worr_snapshot_timeline_clock_request_v1) == 40,
    "snapshot timeline clock request layout changed");
WORR_SNAPSHOT_TIMELINE_STATIC_ASSERT(
    sizeof(worr_snapshot_timeline_clock_state_v1) == 48,
    "snapshot timeline clock state layout changed");
WORR_SNAPSHOT_TIMELINE_STATIC_ASSERT(
    sizeof(worr_snapshot_timeline_policy_v1) == 48,
    "snapshot timeline policy layout changed");
WORR_SNAPSHOT_TIMELINE_STATIC_ASSERT(
    sizeof(worr_snapshot_timeline_pair_v1) == 120,
    "snapshot timeline pair layout changed");
WORR_SNAPSHOT_TIMELINE_STATIC_ASSERT(
    offsetof(worr_snapshot_timeline_pair_v1, target_time_us) == 56,
    "snapshot timeline pair target offset changed");
WORR_SNAPSHOT_TIMELINE_STATIC_ASSERT(
    sizeof(worr_snapshot_timeline_entity_sample_v1) == 216,
    "snapshot timeline entity sample layout changed");
WORR_SNAPSHOT_TIMELINE_STATIC_ASSERT(
    offsetof(worr_snapshot_timeline_entity_sample_v1, entity) == 72,
    "snapshot timeline sampled entity offset changed");
WORR_SNAPSHOT_TIMELINE_STATIC_ASSERT(
    sizeof(worr_snapshot_timeline_event_cursor_v1) == 48,
    "snapshot timeline event cursor layout changed");
WORR_SNAPSHOT_TIMELINE_STATIC_ASSERT(
    sizeof(worr_snapshot_timeline_event_observation_v1) == 96,
    "snapshot timeline event observation layout changed");
WORR_SNAPSHOT_TIMELINE_STATIC_ASSERT(
    sizeof(worr_snapshot_timeline_stats_v1) == 128,
    "snapshot timeline stats layout changed");
WORR_SNAPSHOT_TIMELINE_STATIC_ASSERT(
    sizeof(worr_snapshot_timeline_hashes_v1) == 32,
    "snapshot timeline hashes layout changed");

#undef WORR_SNAPSHOT_TIMELINE_STATIC_ASSERT
