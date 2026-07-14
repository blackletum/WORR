/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <float.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "shared/event_abi.h"
#include "shared/command_abi.h"
#include "shared/prediction_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WORR_SNAPSHOT_ABI_VERSION 2u
#define WORR_SNAPSHOT_MODEL_REVISION 2u
#define WORR_SNAPSHOT_STATS_CAPACITY 64u

/* Epoch and sequence zero are reserved.  { 0, 0 } is the absent ID. */
typedef struct worr_snapshot_id_v2_s {
    uint32_t epoch;
    uint32_t sequence;
} worr_snapshot_id_v2;

/* Process-local immutable-store handle.  Never serialize or hash this value. */
typedef struct worr_snapshot_ref_v2_s {
    uint32_t slot;
    uint32_t generation;
} worr_snapshot_ref_v2;

/* Store-local arena identity.  first_serial is zero exactly when count is zero. */
typedef struct worr_snapshot_serial_range_v2_s {
    uint64_t first_serial;
    uint32_t count;
    uint32_t reserved0;
} worr_snapshot_serial_range_v2;

enum {
    WORR_SNAPSHOT_DISCONTINUITY_INITIAL = 1u << 0,
    WORR_SNAPSHOT_DISCONTINUITY_FULL_SNAPSHOT = 1u << 1,
    WORR_SNAPSHOT_DISCONTINUITY_SEQUENCE_GAP = 1u << 2,
    WORR_SNAPSHOT_DISCONTINUITY_BASE_JUMP = 1u << 3,
    WORR_SNAPSHOT_DISCONTINUITY_RATE_SUPPRESSED = 1u << 4,
    WORR_SNAPSHOT_DISCONTINUITY_FRAGMENT_STALL = 1u << 5,
    WORR_SNAPSHOT_DISCONTINUITY_MAP_RESET = 1u << 6,
    WORR_SNAPSHOT_DISCONTINUITY_DEMO_REWIND = 1u << 7,
    WORR_SNAPSHOT_DISCONTINUITY_TRANSPORT_TRUNCATED = 1u << 8,
    WORR_SNAPSHOT_DISCONTINUITY_HARD_RESYNC = 1u << 9,
    /* The observer attached after transport sequencing had already begun. */
    WORR_SNAPSHOT_DISCONTINUITY_OBSERVER_ATTACH = 1u << 10,
};

typedef enum worr_snapshot_discontinuity_reason_v2_e {
    WORR_SNAPSHOT_DISCONTINUITY_REASON_NONE = 0,
    WORR_SNAPSHOT_DISCONTINUITY_REASON_INITIAL = 1,
    WORR_SNAPSHOT_DISCONTINUITY_REASON_FULL_SNAPSHOT = 2,
    WORR_SNAPSHOT_DISCONTINUITY_REASON_SEQUENCE_GAP = 3,
    WORR_SNAPSHOT_DISCONTINUITY_REASON_BASE_JUMP = 4,
    WORR_SNAPSHOT_DISCONTINUITY_REASON_RATE_SUPPRESSED = 5,
    WORR_SNAPSHOT_DISCONTINUITY_REASON_FRAGMENT_STALL = 6,
    WORR_SNAPSHOT_DISCONTINUITY_REASON_MAP_RESET = 7,
    WORR_SNAPSHOT_DISCONTINUITY_REASON_DEMO_REWIND = 8,
    WORR_SNAPSHOT_DISCONTINUITY_REASON_TRANSPORT_TRUNCATED = 9,
    WORR_SNAPSHOT_DISCONTINUITY_REASON_HARD_RESYNC = 10,
    WORR_SNAPSHOT_DISCONTINUITY_REASON_OBSERVER_ATTACH = 11,
} worr_snapshot_discontinuity_reason_v2;

typedef struct worr_snapshot_discontinuity_v2_s {
    uint32_t flags;
    uint16_t reason;
    uint16_t reserved0;
    worr_snapshot_id_v2 previous;
    uint32_t server_tick_delta;
    uint32_t skipped_sequences;
} worr_snapshot_discontinuity_v2;

enum {
    WORR_SNAPSHOT_GENERATION_AUTHORITATIVE = 1u << 0,
    WORR_SNAPSHOT_GENERATION_LEGACY_INFERRED = 1u << 1,
    WORR_SNAPSHOT_GENERATION_EPOCH_RESET = 1u << 2,
};

/* Reuses the T05 entity identity; provenance says who assigned generation. */
typedef struct worr_snapshot_entity_generation_v2_s {
    worr_event_entity_ref_v1 identity;
    uint32_t provenance_flags;
    uint32_t reserved0;
} worr_snapshot_entity_generation_v2;

typedef enum worr_snapshot_consumed_command_provenance_v2_e {
    WORR_SNAPSHOT_CONSUMED_COMMAND_NONE = 0,
    /* Exact server execution state; packet acknowledgements are not valid. */
    WORR_SNAPSHOT_CONSUMED_COMMAND_SERVER_CONSUMED = 1,
} worr_snapshot_consumed_command_provenance_v2;

typedef struct worr_snapshot_consumed_command_v2_s {
    worr_command_cursor_v1 cursor;
    uint32_t provenance;
    uint32_t reserved0;
} worr_snapshot_consumed_command_v2;

typedef enum worr_snapshot_event_provenance_v2_e {
    WORR_SNAPSHOT_EVENT_PROVENANCE_NONE = 0,
    WORR_SNAPSHOT_EVENT_PROVENANCE_AUTHORITY = 1,
    WORR_SNAPSHOT_EVENT_PROVENANCE_LEGACY_INFERRED = 2,
} worr_snapshot_event_provenance_v2;

/*
 * A projected legacy event is never assigned a fabricated T05 authority ID.
 * carrier_ordinal is zero-based and snapshot-local. semantic_hash is the
 * fieldwise T05 semantic projection identified by semantic_version.
 */
typedef struct worr_snapshot_event_ref_v2_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t provenance;
    uint32_t carrier_ordinal;
    uint32_t semantic_version;
    worr_event_id_v1 authority_id;
    uint64_t semantic_hash;
} worr_snapshot_event_ref_v2;

enum {
    WORR_SNAPSHOT_EVENT_RANGE_CONTIGUOUS_AUTHORITY = 1u << 0,
    WORR_SNAPSHOT_EVENT_RANGE_CONTIGUOUS_CARRIER = 1u << 1,
};

typedef struct worr_snapshot_event_range_v2_s {
    uint64_t first_ref_serial;
    uint32_t count;
    uint16_t provenance;
    uint16_t flags;
    worr_event_id_v1 first_authority_id;
    worr_event_id_v1 one_past_authority_id;
    uint32_t first_carrier_ordinal;
    uint32_t reserved0;
} worr_snapshot_event_range_v2;

enum {
    WORR_SNAPSHOT_ENTITY_TRANSFORM = UINT64_C(1) << 0,
    WORR_SNAPSHOT_ENTITY_INTERPOLATION = UINT64_C(1) << 1,
    WORR_SNAPSHOT_ENTITY_MODELS = UINT64_C(1) << 2,
    WORR_SNAPSHOT_ENTITY_ANIMATION = UINT64_C(1) << 3,
    WORR_SNAPSHOT_ENTITY_APPEARANCE = UINT64_C(1) << 4,
    WORR_SNAPSHOT_ENTITY_EFFECTS = UINT64_C(1) << 5,
    WORR_SNAPSHOT_ENTITY_COLLISION = UINT64_C(1) << 6,
    WORR_SNAPSHOT_ENTITY_LOOP_SOUND = UINT64_C(1) << 7,
    WORR_SNAPSHOT_ENTITY_OWNER = UINT64_C(1) << 8,
    WORR_SNAPSHOT_ENTITY_INSTANCE = UINT64_C(1) << 9,
};

#define WORR_SNAPSHOT_ENTITY_COMPONENTS_V2                              \
    (WORR_SNAPSHOT_ENTITY_TRANSFORM |                                   \
     WORR_SNAPSHOT_ENTITY_INTERPOLATION | WORR_SNAPSHOT_ENTITY_MODELS | \
     WORR_SNAPSHOT_ENTITY_ANIMATION | WORR_SNAPSHOT_ENTITY_APPEARANCE | \
     WORR_SNAPSHOT_ENTITY_EFFECTS | WORR_SNAPSHOT_ENTITY_COLLISION |    \
     WORR_SNAPSHOT_ENTITY_LOOP_SOUND | WORR_SNAPSHOT_ENTITY_OWNER |     \
     WORR_SNAPSHOT_ENTITY_INSTANCE)

/*
 * A component bit makes the grouped fields meaningful.  Fields for absent
 * components must have their canonical zero/absent representation.  TRANSFORM
 * is mandatory in v2, including for a stationary entity.  OWNER is present
 * only for a non-absent owner reference.  Entity impulse events are
 * deliberately not duplicated here; snapshots reference T05 event records
 * through worr_snapshot_event_range_v2.
 */
typedef struct worr_snapshot_entity_v2_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t model_revision;
    uint32_t flags;
    worr_snapshot_entity_generation_v2 generation;
    uint64_t component_mask;
    float origin[3];
    float angles[3];
    float old_origin[3];
    uint16_t model_index[4];
    uint16_t frame;
    uint16_t sound;
    uint32_t skin;
    uint32_t solid;
    uint64_t effects;
    uint32_t renderfx;
    float alpha;
    float scale;
    float loop_volume;
    float loop_attenuation;
    worr_event_entity_ref_v1 owner;
    int32_t old_frame;
    uint8_t instance_bits;
    uint8_t reserved0[7];
} worr_snapshot_entity_v2;

enum {
    WORR_SNAPSHOT_PLAYER_MOVEMENT = UINT64_C(1) << 0,
    WORR_SNAPSHOT_PLAYER_VIEW = UINT64_C(1) << 1,
    WORR_SNAPSHOT_PLAYER_WEAPON = UINT64_C(1) << 2,
    WORR_SNAPSHOT_PLAYER_BLEND = UINT64_C(1) << 3,
    WORR_SNAPSHOT_PLAYER_PRESENTATION = UINT64_C(1) << 4,
    WORR_SNAPSHOT_PLAYER_STATS = UINT64_C(1) << 5,
};

#define WORR_SNAPSHOT_PLAYER_COMPONENTS_V2                              \
    (WORR_SNAPSHOT_PLAYER_MOVEMENT | WORR_SNAPSHOT_PLAYER_VIEW |        \
     WORR_SNAPSHOT_PLAYER_WEAPON | WORR_SNAPSHOT_PLAYER_BLEND |         \
     WORR_SNAPSHOT_PLAYER_PRESENTATION | WORR_SNAPSHOT_PLAYER_STATS)

/* The movement component is the T02 prediction state, not a parallel schema. */
typedef struct worr_snapshot_player_v2_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t model_revision;
    uint32_t flags;
    worr_snapshot_entity_generation_v2 controlled_entity;
    uint64_t component_mask;
    worr_prediction_state_v1 movement;
    float view_angles[3];
    float view_offset[3];
    float kick_angles[3];
    float gun_angles[3];
    float gun_offset[3];
    float screen_blend[4];
    float damage_blend[4];
    uint16_t gun_index;
    uint16_t gun_frame;
    uint8_t gun_skin;
    uint8_t gun_rate;
    uint8_t rdflags;
    uint8_t team_id;
    float fov;
    int16_t stats[WORR_SNAPSHOT_STATS_CAPACITY];
} worr_snapshot_player_v2;

enum {
    WORR_SNAPSHOT_FLAG_KEYFRAME = 1u << 0,
    WORR_SNAPSHOT_FLAG_COMPLETE = 1u << 1,
    WORR_SNAPSHOT_FLAG_AUTHORITATIVE_GENERATIONS = 1u << 2,
    WORR_SNAPSHOT_FLAG_LEGACY_PROJECTION = 1u << 3,
    WORR_SNAPSHOT_FLAG_PROMOTION_ELIGIBLE = 1u << 4,
    WORR_SNAPSHOT_FLAG_TRANSPORT_TRUNCATED = 1u << 5,
};

/*
 * Immutable snapshot metadata.  Player state is owned alongside this record;
 * the remaining payloads live in fixed store arenas.  Arena serials are local
 * lifetime guards and are intentionally excluded from snapshot_hash.
 */
typedef struct worr_snapshot_v2_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t model_revision;
    uint32_t flags;
    worr_snapshot_id_v2 snapshot_id;
    worr_snapshot_id_v2 base_id;
    uint32_t server_tick;
    uint32_t reserved0;
    uint64_t server_time_us;
    worr_snapshot_entity_generation_v2 controlled_entity;
    worr_snapshot_consumed_command_v2 consumed_command;
    worr_snapshot_discontinuity_v2 discontinuity;
    worr_snapshot_serial_range_v2 entity_range;
    worr_snapshot_serial_range_v2 area_range;
    worr_snapshot_event_range_v2 event_range;
    uint64_t player_hash;
    uint64_t entity_hash;
    uint64_t area_hash;
    uint64_t event_hash;
    uint64_t snapshot_hash;
} worr_snapshot_v2;

bool Worr_SnapshotIdValidV2(worr_snapshot_id_v2 id, bool allow_absent);
bool Worr_SnapshotIdNextV2(worr_snapshot_id_v2 current,
                           worr_snapshot_id_v2 *next);
bool Worr_SnapshotGenerationValidV2(
    worr_snapshot_entity_generation_v2 generation,
    uint32_t max_entities,
    bool allow_absent);

bool Worr_SnapshotPlayerValidateV2(const worr_snapshot_player_v2 *player,
                                   uint32_t max_entities);
bool Worr_SnapshotEntityValidateV2(const worr_snapshot_entity_v2 *entity,
                                   uint32_t max_entities);
bool Worr_SnapshotEventRefsValidateV2(
    const worr_snapshot_event_ref_v2 *event_refs,
    uint32_t count);

bool Worr_SnapshotPlayerHashV2(const worr_snapshot_player_v2 *player,
                               uint32_t max_entities,
                               uint64_t *hash_out);
bool Worr_SnapshotEntityHashV2(const worr_snapshot_entity_v2 *entity,
                               uint32_t max_entities,
                               uint64_t *hash_out);
bool Worr_SnapshotEntityListHashV2(
    const worr_snapshot_entity_v2 *entities,
    uint32_t count,
    uint32_t max_entities,
    uint64_t *hash_out);
bool Worr_SnapshotAreaHashV2(const uint8_t *area_bytes,
                             uint32_t count,
                             uint64_t *hash_out);
bool Worr_SnapshotEventRefsHashV2(
    const worr_snapshot_event_ref_v2 *event_refs,
    uint32_t count,
    uint64_t *hash_out);

/* Calculates the semantic hash from metadata and component hashes. */
bool Worr_SnapshotCalculateHashV2(const worr_snapshot_v2 *snapshot,
                                  uint32_t max_entities,
                                  uint64_t *hash_out);
bool Worr_SnapshotValidateV2(const worr_snapshot_v2 *snapshot,
                             uint32_t max_entities);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_SNAPSHOT_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define WORR_SNAPSHOT_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

WORR_SNAPSHOT_STATIC_ASSERT(sizeof(float) == 4,
                            "snapshot ABI requires 32-bit floats");
WORR_SNAPSHOT_STATIC_ASSERT(FLT_RADIX == 2 && FLT_MANT_DIG == 24 &&
                                FLT_MAX_EXP == 128,
                            "snapshot ABI requires IEEE-754 binary32 floats");
WORR_SNAPSHOT_STATIC_ASSERT(sizeof(worr_snapshot_id_v2) == 8,
                            "snapshot ID v1 layout changed");
WORR_SNAPSHOT_STATIC_ASSERT(sizeof(worr_snapshot_ref_v2) == 8,
                            "snapshot ref v1 layout changed");
WORR_SNAPSHOT_STATIC_ASSERT(sizeof(worr_snapshot_serial_range_v2) == 16,
                            "snapshot serial range v1 layout changed");
WORR_SNAPSHOT_STATIC_ASSERT(sizeof(worr_snapshot_discontinuity_v2) == 24,
                            "snapshot discontinuity v1 layout changed");
WORR_SNAPSHOT_STATIC_ASSERT(
    sizeof(worr_snapshot_entity_generation_v2) == 16,
    "snapshot entity generation v1 layout changed");
WORR_SNAPSHOT_STATIC_ASSERT(sizeof(worr_snapshot_consumed_command_v2) == 16,
                            "snapshot consumed-command v2 layout changed");
WORR_SNAPSHOT_STATIC_ASSERT(sizeof(worr_snapshot_event_ref_v2) == 32,
                            "snapshot event ref v2 layout changed");
WORR_SNAPSHOT_STATIC_ASSERT(sizeof(worr_snapshot_event_range_v2) == 40,
                            "snapshot event range v2 layout changed");
WORR_SNAPSHOT_STATIC_ASSERT(sizeof(worr_snapshot_entity_v2) == 144,
                            "snapshot entity v1 layout changed");
WORR_SNAPSHOT_STATIC_ASSERT(offsetof(worr_snapshot_entity_v2, generation) == 16,
                            "snapshot entity generation offset changed");
WORR_SNAPSHOT_STATIC_ASSERT(offsetof(worr_snapshot_entity_v2, origin) == 40,
                            "snapshot entity origin offset changed");
WORR_SNAPSHOT_STATIC_ASSERT(offsetof(worr_snapshot_entity_v2, effects) == 96,
                            "snapshot entity effects offset changed");
WORR_SNAPSHOT_STATIC_ASSERT(offsetof(worr_snapshot_entity_v2, owner) == 124,
                            "snapshot entity owner offset changed");
WORR_SNAPSHOT_STATIC_ASSERT(sizeof(worr_snapshot_player_v2) == 328,
                            "snapshot player v1 layout changed");
WORR_SNAPSHOT_STATIC_ASSERT(
    offsetof(worr_snapshot_player_v2, controlled_entity) == 16,
    "snapshot player controlled-entity offset changed");
WORR_SNAPSHOT_STATIC_ASSERT(offsetof(worr_snapshot_player_v2, movement) == 40,
                            "snapshot player movement offset changed");
WORR_SNAPSHOT_STATIC_ASSERT(offsetof(worr_snapshot_player_v2, view_angles) == 96,
                            "snapshot player view-angle offset changed");
WORR_SNAPSHOT_STATIC_ASSERT(offsetof(worr_snapshot_player_v2, stats) == 200,
                            "snapshot player stats offset changed");
WORR_SNAPSHOT_STATIC_ASSERT(sizeof(worr_snapshot_v2) == 216,
                            "snapshot v2 layout changed");
WORR_SNAPSHOT_STATIC_ASSERT(offsetof(worr_snapshot_v2, snapshot_id) == 16,
                            "snapshot ID offset changed");
WORR_SNAPSHOT_STATIC_ASSERT(offsetof(worr_snapshot_v2, server_time_us) == 40,
                            "snapshot simulation-time offset changed");
WORR_SNAPSHOT_STATIC_ASSERT(
    offsetof(worr_snapshot_v2, controlled_entity) == 48,
    "snapshot controlled-entity offset changed");
WORR_SNAPSHOT_STATIC_ASSERT(offsetof(worr_snapshot_v2, consumed_command) == 64,
                            "snapshot consumed-command offset changed");
WORR_SNAPSHOT_STATIC_ASSERT(offsetof(worr_snapshot_v2, entity_range) == 104,
                            "snapshot entity-range offset changed");
WORR_SNAPSHOT_STATIC_ASSERT(offsetof(worr_snapshot_v2, player_hash) == 176,
                            "snapshot player-hash offset changed");
WORR_SNAPSHOT_STATIC_ASSERT(offsetof(worr_snapshot_v2, snapshot_hash) == 208,
                            "snapshot final-hash offset changed");

#undef WORR_SNAPSHOT_STATIC_ASSERT
