/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/net/command_canonical.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WORR_PREDICTION_ABI_VERSION 1u
/* Increment when intentional movement math changes without an ABI redesign. */
#define WORR_PREDICTION_MODEL_REVISION 1u
#define WORR_PREDICTION_NO_ENTITY UINT32_MAX
#define WORR_PREDICTION_MAX_TOUCH 32u

/*
 * Q2REPRO/Q2PRO batch commands encode movement in signed 10-bit integer
 * units.  The client currently clamps ordinary input to [-400, 400], so this
 * range covers every supported command and is identical in the legacy
 * 16-bit and batched encodings.
 */
#define WORR_PREDICTION_MOVE_MIN WORR_NET_USERCMD_MOVE_MIN
#define WORR_PREDICTION_MOVE_MAX WORR_NET_USERCMD_MOVE_MAX

enum {
    WORR_PREDICTION_CONFIG_N64_PHYSICS = 1u << 0,
    WORR_PREDICTION_CONFIG_Q3_OVERBOUNCE = 1u << 1,
};

enum {
    WORR_PREDICTION_TRACE_WORLD_ONLY = 1u << 0,
};

/*
 * These records are pointer-free and have a fixed, field-defined layout.
 * They may be copied across the engine/module boundary.  Reserved fields
 * must be zero.  Hashes are fieldwise and never include C padding.
 */
typedef struct worr_prediction_state_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    int32_t movement_type;
    float origin[3];
    float velocity[3];
    uint16_t movement_flags;
    uint16_t movement_time_ms;
    int16_t gravity;
    int8_t view_height;
    uint8_t reserved0;
    float delta_angles[3];
} worr_prediction_state_v1;

typedef struct worr_prediction_command_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint8_t duration_ms;
    uint8_t buttons;
    uint16_t reserved0;
    float view_angles[3];
    float forward_move;
    float side_move;
} worr_prediction_command_v1;

typedef struct worr_prediction_config_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t movement_model_revision;
    int32_t air_acceleration;
    uint32_t flags;
} worr_prediction_config_v1;

typedef struct worr_prediction_plane_v1_s {
    float normal[3];
    float distance;
    uint8_t type;
    uint8_t sign_bits;
    uint8_t reserved[2];
} worr_prediction_plane_v1;

typedef struct worr_prediction_trace_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint8_t all_solid;
    uint8_t start_solid;
    uint8_t has_second_surface;
    uint8_t reserved0;
    float fraction;
    float end[3];
    worr_prediction_plane_v1 plane;
    uint32_t surface_flags;
    uint32_t surface_id;
    uint32_t contents;
    uint32_t entity_id;
    worr_prediction_plane_v1 plane2;
    uint32_t surface2_flags;
    uint32_t surface2_id;
} worr_prediction_trace_v1;

typedef void (*worr_prediction_trace_fn_v1)(
    void *context,
    worr_prediction_trace_v1 *result,
    const float start[3],
    const float mins[3],
    const float maxs[3],
    const float end[3],
    uint32_t pass_entity_id,
    uint32_t contents_mask,
    uint32_t query_flags);

/*
 * The trace result arrives initialized as a valid no-hit result.  A callback
 * that supplies a hit must preserve struct_size/schema_version and zero all
 * reserved fields; malformed results fail the entire prediction step.
 */

typedef uint32_t (*worr_prediction_point_contents_fn_v1)(
    void *context, const float point[3]);

/*
 * Runtime call envelope.  Unlike the records above, this contains native
 * pointers and callbacks and must never be serialized, persisted, or hashed
 * as raw bytes.  It is valid only within one process and architecture.
 */
typedef struct worr_prediction_step_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;

    worr_prediction_state_v1 state;
    worr_prediction_command_v1 command;
    worr_prediction_config_v1 config;
    uint8_t snap_initial;
    uint8_t reserved0[3];
    uint32_t player_entity_id;
    float view_offset[3];

    void *collision_context;
    worr_prediction_trace_fn_v1 trace;
    worr_prediction_point_contents_fn_v1 point_contents;

    float view_angles[3];
    float mins[3];
    float maxs[3];
    worr_prediction_plane_v1 ground_plane;
    uint32_t ground_entity_id;
    uint32_t water_type;
    uint8_t water_level;
    uint8_t rd_flags;
    uint8_t jump_sound;
    uint8_t step_clip;
    float impact_delta;
    float screen_blend[4];
    uint32_t touch_count;
    uint32_t touch_entity_ids[WORR_PREDICTION_MAX_TOUCH];
    uint32_t collision_query_count;
    uint32_t reserved1;
    uint64_t state_hash;
    uint64_t collision_hash;
} worr_prediction_step_v1;

/*
 * Canonicalize a command to the representation decoded by supported wire
 * protocols: signed 16-bit short-angle round-trips and integer movement units.
 * The operation is atomic; an invalid command is rejected without mutation.
 */
bool Worr_PredictionCanonicalizeCommandV1(
    worr_prediction_command_v1 *command);

bool Worr_PredictionStepV1(worr_prediction_step_v1 *step);
uint64_t Worr_PredictionHashStateV1(const worr_prediction_state_v1 *state);
uint64_t Worr_PredictionHashCommandV1(
    const worr_prediction_command_v1 *command);
uint64_t Worr_PredictionHashConfigV1(
    const worr_prediction_config_v1 *config);
uint64_t Worr_PredictionReplayChainHashV1(uint64_t previous_chain_hash,
                                          uint32_t command_sequence,
                                          uint64_t command_hash,
                                          uint64_t collision_hash,
                                          uint64_t state_hash);

/*
 * Build and index a count-based replay plan using modular uint32 sequence
 * arithmetic.  A valid plan replays (acknowledged, current] and contains at
 * most capacity - 1 commands, matching a ring with one acknowledged slot.
 */
bool Worr_PredictionReplayCountV1(uint32_t acknowledged_sequence,
                                  uint32_t current_sequence,
                                  uint32_t capacity,
                                  uint32_t *replay_count);
bool Worr_PredictionReplaySequenceV1(uint32_t acknowledged_sequence,
                                     uint32_t replay_count,
                                     uint32_t replay_index,
                                     uint32_t *command_sequence);

#ifdef __cplusplus
}
#endif

/* Keep the pointer-free wire-independent records identical in C and C++. */
#if defined(__cplusplus)
#define WORR_PREDICTION_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define WORR_PREDICTION_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

WORR_PREDICTION_STATIC_ASSERT(sizeof(uint8_t) == 1,
                              "prediction ABI requires 8-bit bytes");
WORR_PREDICTION_STATIC_ASSERT(sizeof(float) == 4,
                              "prediction ABI requires 32-bit floats");
WORR_PREDICTION_STATIC_ASSERT(sizeof(worr_prediction_state_v1) == 56,
                              "prediction state v1 layout changed");
WORR_PREDICTION_STATIC_ASSERT(
    offsetof(worr_prediction_state_v1, origin) == 12,
    "prediction state v1 origin offset changed");
WORR_PREDICTION_STATIC_ASSERT(
    offsetof(worr_prediction_state_v1, movement_flags) == 36,
    "prediction state v1 flags offset changed");
WORR_PREDICTION_STATIC_ASSERT(
    offsetof(worr_prediction_state_v1, delta_angles) == 44,
    "prediction state v1 delta-angle offset changed");
WORR_PREDICTION_STATIC_ASSERT(sizeof(worr_prediction_command_v1) == 32,
                              "prediction command v1 layout changed");
WORR_PREDICTION_STATIC_ASSERT(
    offsetof(worr_prediction_command_v1, view_angles) == 12,
    "prediction command v1 angle offset changed");
WORR_PREDICTION_STATIC_ASSERT(
    offsetof(worr_prediction_command_v1, forward_move) == 24,
    "prediction command v1 movement offset changed");
WORR_PREDICTION_STATIC_ASSERT(sizeof(worr_prediction_config_v1) == 20,
                              "prediction config v1 layout changed");
WORR_PREDICTION_STATIC_ASSERT(
    offsetof(worr_prediction_config_v1, movement_model_revision) == 8,
    "prediction config v1 model-revision offset changed");
WORR_PREDICTION_STATIC_ASSERT(sizeof(worr_prediction_plane_v1) == 20,
                              "prediction plane v1 layout changed");
WORR_PREDICTION_STATIC_ASSERT(sizeof(worr_prediction_trace_v1) == 92,
                              "prediction trace v1 layout changed");
WORR_PREDICTION_STATIC_ASSERT(
    offsetof(worr_prediction_trace_v1, fraction) == 12,
    "prediction trace v1 fraction offset changed");
WORR_PREDICTION_STATIC_ASSERT(
    offsetof(worr_prediction_trace_v1, plane) == 28,
    "prediction trace v1 plane offset changed");
WORR_PREDICTION_STATIC_ASSERT(
    offsetof(worr_prediction_trace_v1, plane2) == 64,
    "prediction trace v1 second-plane offset changed");

#undef WORR_PREDICTION_STATIC_ASSERT
