/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "shared/snapshot_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WORR_SNAPSHOT_STORE_VERSION 2u

typedef enum worr_snapshot_store_result_v2_e {
    WORR_SNAPSHOT_STORE_OK = 0,
    WORR_SNAPSHOT_STORE_INVALID_ARGUMENT = 1,
    WORR_SNAPSHOT_STORE_INVALID_STORE = 2,
    WORR_SNAPSHOT_STORE_INVALID_SNAPSHOT = 3,
    WORR_SNAPSHOT_STORE_INVALID_PLAYER = 4,
    WORR_SNAPSHOT_STORE_INVALID_ENTITY = 5,
    WORR_SNAPSHOT_STORE_INVALID_ENTITY_ORDER = 6,
    WORR_SNAPSHOT_STORE_INVALID_EVENT = 7,
    WORR_SNAPSHOT_STORE_INVALID_EVENT_ORDER = 8,
    WORR_SNAPSHOT_STORE_CAPACITY = 9,
    WORR_SNAPSHOT_STORE_SERIAL_EXHAUSTED = 10,
    WORR_SNAPSHOT_STORE_GENERATION_EXHAUSTED = 11,
    WORR_SNAPSHOT_STORE_STALE_REF = 12,
    WORR_SNAPSHOT_STORE_CORRUPT = 13,
    WORR_SNAPSHOT_STORE_BUFFER_TOO_SMALL = 14,
} worr_snapshot_store_result_v2;

/* Caller-owned slot.  Payload arenas are supplied separately at init. */
typedef struct worr_snapshot_store_slot_v2_s {
    worr_snapshot_v2 snapshot;
    worr_snapshot_player_v2 player;
    uint64_t entity_first_serial;
    uint64_t area_first_serial;
    uint64_t event_first_serial;
    uint32_t generation;
    uint32_t committed;
} worr_snapshot_store_slot_v2;

/*
 * Runtime publication envelope.  It contains native pointers and must never
 * be serialized, persisted, or hashed.  snapshot supplies only metadata;
 * store-owned ranges and all five hashes are always recomputed.
 */
typedef struct worr_snapshot_store_publish_v2_s {
    uint32_t struct_size;
    uint32_t schema_version;
    const worr_snapshot_v2 *snapshot;
    const worr_snapshot_player_v2 *player;
    const worr_snapshot_entity_v2 *entities;
    const uint8_t *area_bytes;
    const worr_snapshot_event_ref_v2 *event_refs;
    uint32_t entity_count;
    uint32_t area_byte_count;
    uint32_t event_ref_count;
    uint32_t reserved0;
} worr_snapshot_store_publish_v2;

/* Runtime-only owner of caller-provided fixed storage. */
typedef struct worr_snapshot_store_v2_s {
    uint32_t struct_size;
    uint32_t schema_version;
    worr_snapshot_store_slot_v2 *slots;
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
    uint64_t next_entity_serial;
    uint64_t next_area_serial;
    uint64_t next_event_serial;
    uint64_t publish_count;
} worr_snapshot_store_v2;

typedef struct worr_snapshot_store_stats_v2_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t slot_capacity;
    uint32_t occupied;
    uint32_t occupied_high_water;
    uint32_t entity_high_water;
    uint32_t area_high_water;
    uint32_t event_high_water;
    uint64_t next_entity_serial;
    uint64_t next_area_serial;
    uint64_t next_event_serial;
    uint64_t publish_count;
} worr_snapshot_store_stats_v2;

/*
 * Initialization and publication perform no allocation.  A zero per-slot
 * capacity permits a NULL arena pointer with zero total capacity.  The store
 * is externally synchronized; committed records are immutable until their
 * slot is deterministically reused or reset.  The store object, slot array,
 * and three backing arenas must be distinct non-overlapping regions for the
 * lifetime of the store.  Publication payloads may alias their corresponding
 * arena because commit copies use overlap-safe moves.
 */
worr_snapshot_store_result_v2 Worr_SnapshotStoreInitV2(
    worr_snapshot_store_v2 *store,
    worr_snapshot_store_slot_v2 *slots,
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

worr_snapshot_store_result_v2 Worr_SnapshotStorePublishV2(
    worr_snapshot_store_v2 *store,
    const worr_snapshot_store_publish_v2 *publication,
    worr_snapshot_ref_v2 *ref_out);

/* Invalidates every outstanding ref while retaining monotonic serials. */
worr_snapshot_store_result_v2 Worr_SnapshotStoreResetV2(
    worr_snapshot_store_v2 *store);

bool Worr_SnapshotStoreRefValidV2(const worr_snapshot_store_v2 *store,
                                  worr_snapshot_ref_v2 ref);
worr_snapshot_store_result_v2 Worr_SnapshotStoreCopySnapshotV2(
    const worr_snapshot_store_v2 *store,
    worr_snapshot_ref_v2 ref,
    worr_snapshot_v2 *snapshot_out);
worr_snapshot_store_result_v2 Worr_SnapshotStoreCopyPlayerV2(
    const worr_snapshot_store_v2 *store,
    worr_snapshot_ref_v2 ref,
    worr_snapshot_player_v2 *player_out);
worr_snapshot_store_result_v2 Worr_SnapshotStoreCopyEntitiesV2(
    const worr_snapshot_store_v2 *store,
    worr_snapshot_ref_v2 ref,
    worr_snapshot_entity_v2 *entities_out,
    uint32_t capacity,
    uint32_t *count_out);
worr_snapshot_store_result_v2 Worr_SnapshotStoreCopyAreaV2(
    const worr_snapshot_store_v2 *store,
    worr_snapshot_ref_v2 ref,
    uint8_t *area_bytes_out,
    uint32_t capacity,
    uint32_t *count_out);
worr_snapshot_store_result_v2 Worr_SnapshotStoreCopyEventRefsV2(
    const worr_snapshot_store_v2 *store,
    worr_snapshot_ref_v2 ref,
    worr_snapshot_event_ref_v2 *event_refs_out,
    uint32_t capacity,
    uint32_t *count_out);
worr_snapshot_store_result_v2 Worr_SnapshotStoreGetStatsV2(
    const worr_snapshot_store_v2 *store,
    worr_snapshot_store_stats_v2 *stats_out);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_SNAPSHOT_STORE_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define WORR_SNAPSHOT_STORE_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

WORR_SNAPSHOT_STORE_STATIC_ASSERT(sizeof(worr_snapshot_store_slot_v2) == 576,
                                  "snapshot store slot v2 layout changed");
WORR_SNAPSHOT_STORE_STATIC_ASSERT(
    offsetof(worr_snapshot_store_slot_v2, player) == 216,
    "snapshot store player offset changed");
WORR_SNAPSHOT_STORE_STATIC_ASSERT(
    offsetof(worr_snapshot_store_slot_v2, entity_first_serial) == 544,
    "snapshot store entity serial offset changed");
WORR_SNAPSHOT_STORE_STATIC_ASSERT(
    offsetof(worr_snapshot_store_slot_v2, generation) == 568,
    "snapshot store generation offset changed");
WORR_SNAPSHOT_STORE_STATIC_ASSERT(sizeof(worr_snapshot_store_stats_v2) == 64,
                                  "snapshot store stats v2 layout changed");

#undef WORR_SNAPSHOT_STORE_STATIC_ASSERT
