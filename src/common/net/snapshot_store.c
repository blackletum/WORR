/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/snapshot_store.h"

#include <string.h>

static bool capacity_valid(uint32_t slot_capacity,
                           uint32_t per_slot,
                           uint32_t total_capacity,
                           const void *storage,
                           size_t element_size)
{
    if (per_slot == 0)
        return total_capacity == 0 && storage == NULL;
    return storage != NULL &&
           element_size != 0 &&
           (size_t)total_capacity <= SIZE_MAX / element_size &&
           slot_capacity <= total_capacity / per_slot;
}

static bool slot_bytes_valid(uint32_t slot_capacity)
{
#if SIZE_MAX < UINT64_MAX
    return (size_t)slot_capacity <=
           SIZE_MAX / sizeof(worr_snapshot_store_slot_v2);
#else
    (void)slot_capacity;
    return true;
#endif
}

static bool store_valid(const worr_snapshot_store_v2 *store)
{
    return store && store->struct_size == sizeof(*store) &&
           store->schema_version == WORR_SNAPSHOT_STORE_VERSION &&
           store->slots && store->slot_capacity != 0 &&
           store->max_entities != 0 &&
           store->entities_per_slot <= store->max_entities &&
           slot_bytes_valid(store->slot_capacity) &&
           capacity_valid(store->slot_capacity, store->entities_per_slot,
                          store->entity_storage_capacity,
                          store->entities, sizeof(*store->entities)) &&
           capacity_valid(store->slot_capacity, store->area_bytes_per_slot,
                          store->area_storage_capacity, store->area_bytes,
                          sizeof(*store->area_bytes)) &&
           capacity_valid(store->slot_capacity, store->event_refs_per_slot,
                          store->event_storage_capacity, store->event_refs,
                          sizeof(*store->event_refs)) &&
           store->next_slot < store->slot_capacity &&
           store->occupied <= store->slot_capacity &&
           store->occupied_high_water <= store->slot_capacity &&
           store->entity_high_water <= store->entities_per_slot &&
           store->area_high_water <= store->area_bytes_per_slot &&
           store->event_high_water <= store->event_refs_per_slot &&
           store->next_entity_serial != 0 &&
           store->next_area_serial != 0 &&
           store->next_event_serial != 0;
}

static bool generation_equal(worr_snapshot_entity_generation_v2 left,
                             worr_snapshot_entity_generation_v2 right)
{
    return left.identity.index == right.identity.index &&
           left.identity.generation == right.identity.generation &&
           left.provenance_flags == right.provenance_flags &&
           left.reserved0 == right.reserved0;
}

static bool event_id_equal(worr_event_id_v1 left, worr_event_id_v1 right)
{
    return left.stream_epoch == right.stream_epoch &&
           left.sequence == right.sequence;
}

static int event_id_compare(worr_event_id_v1 left, worr_event_id_v1 right)
{
    if (left.stream_epoch < right.stream_epoch)
        return -1;
    if (left.stream_epoch > right.stream_epoch)
        return 1;
    if (left.sequence < right.sequence)
        return -1;
    if (left.sequence > right.sequence)
        return 1;
    return 0;
}

static bool serial_reserve(uint64_t current,
                           uint32_t count,
                           uint64_t *first_out,
                           uint64_t *next_out)
{
    if (!first_out || !next_out || current == 0)
        return false;
    if (count == 0) {
        *first_out = 0;
        *next_out = current;
        return true;
    }
    if ((uint64_t)count > UINT64_MAX - current)
        return false;
    *first_out = current;
    *next_out = current + count;
    return true;
}

static bool serial_range_live(uint64_t first,
                              uint32_t count,
                              uint64_t next)
{
    if (count == 0)
        return first == 0;
    return first != 0 && first < next &&
           (uint64_t)count <= next - first;
}

static uint32_t primary_generation_source(
    worr_snapshot_entity_generation_v2 generation)
{
    return generation.provenance_flags &
           (WORR_SNAPSHOT_GENERATION_AUTHORITATIVE |
            WORR_SNAPSHOT_GENERATION_LEGACY_INFERRED);
}

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
    uint32_t max_entities)
{
    worr_snapshot_store_v2 initialized;

    if (!store || !slots || slot_capacity == 0 || max_entities == 0 ||
        entities_per_slot > max_entities ||
        !slot_bytes_valid(slot_capacity) ||
        !capacity_valid(slot_capacity, entities_per_slot,
                        entity_storage_capacity, entities,
                        sizeof(*entities)) ||
        !capacity_valid(slot_capacity, area_bytes_per_slot,
                        area_storage_capacity, area_bytes,
                        sizeof(*area_bytes)) ||
        !capacity_valid(slot_capacity, event_refs_per_slot,
                        event_storage_capacity, event_refs,
                        sizeof(*event_refs))) {
        return WORR_SNAPSHOT_STORE_INVALID_ARGUMENT;
    }

    memset(&initialized, 0, sizeof(initialized));
    initialized.struct_size = sizeof(initialized);
    initialized.schema_version = WORR_SNAPSHOT_STORE_VERSION;
    initialized.slots = slots;
    initialized.entities = entities;
    initialized.area_bytes = area_bytes;
    initialized.event_refs = event_refs;
    initialized.slot_capacity = slot_capacity;
    initialized.entities_per_slot = entities_per_slot;
    initialized.area_bytes_per_slot = area_bytes_per_slot;
    initialized.event_refs_per_slot = event_refs_per_slot;
    initialized.entity_storage_capacity = entity_storage_capacity;
    initialized.area_storage_capacity = area_storage_capacity;
    initialized.event_storage_capacity = event_storage_capacity;
    initialized.max_entities = max_entities;
    initialized.next_entity_serial = 1;
    initialized.next_area_serial = 1;
    initialized.next_event_serial = 1;

    memset(slots, 0, sizeof(*slots) * (size_t)slot_capacity);
    *store = initialized;
    return WORR_SNAPSHOT_STORE_OK;
}

static worr_snapshot_store_result_v2 validate_entities(
    const worr_snapshot_store_v2 *store,
    const worr_snapshot_entity_v2 *entities,
    uint32_t count,
    bool authoritative,
    worr_snapshot_entity_generation_v2 controlled_entity)
{
    uint32_t i;
    const uint32_t expected_source =
        authoritative ? WORR_SNAPSHOT_GENERATION_AUTHORITATIVE
                      : WORR_SNAPSHOT_GENERATION_LEGACY_INFERRED;

    if (count != 0 && !entities)
        return WORR_SNAPSHOT_STORE_INVALID_ARGUMENT;
    for (i = 0; i < count; ++i) {
        if (!Worr_SnapshotEntityValidateV2(&entities[i],
                                           store->max_entities) ||
            primary_generation_source(entities[i].generation) !=
                expected_source) {
            return WORR_SNAPSHOT_STORE_INVALID_ENTITY;
        }
        if (entities[i].generation.identity.index ==
                controlled_entity.identity.index &&
            !generation_equal(entities[i].generation, controlled_entity)) {
            return WORR_SNAPSHOT_STORE_INVALID_ENTITY;
        }
        if (i != 0 &&
            entities[i - 1].generation.identity.index >=
                entities[i].generation.identity.index) {
            return WORR_SNAPSHOT_STORE_INVALID_ENTITY_ORDER;
        }
    }
    return WORR_SNAPSHOT_STORE_OK;
}

static worr_snapshot_store_result_v2 validate_events(
    const worr_snapshot_event_ref_v2 *events,
    uint32_t count)
{
    uint32_t i;

    if (count != 0 && !events)
        return WORR_SNAPSHOT_STORE_INVALID_ARGUMENT;
    for (i = 0; i < count; ++i) {
        const bool authoritative =
            events[i].provenance ==
            WORR_SNAPSHOT_EVENT_PROVENANCE_AUTHORITY;
        const bool inferred =
            events[i].provenance ==
            WORR_SNAPSHOT_EVENT_PROVENANCE_LEGACY_INFERRED;

        if (events[i].struct_size != sizeof(events[i]) ||
            events[i].schema_version != WORR_SNAPSHOT_ABI_VERSION ||
            (!authoritative && !inferred) ||
            events[i].semantic_version != WORR_EVENT_MODEL_REVISION ||
            (authoritative &&
             (events[i].authority_id.stream_epoch == 0 ||
              events[i].authority_id.sequence == 0)) ||
            (inferred &&
             (events[i].authority_id.stream_epoch != 0 ||
              events[i].authority_id.sequence != 0))) {
            return WORR_SNAPSHOT_STORE_INVALID_EVENT;
        }
        if (events[i].carrier_ordinal != i ||
            (i != 0 &&
             (events[i - 1].provenance != events[i].provenance ||
              (authoritative &&
               event_id_compare(events[i - 1].authority_id,
                                events[i].authority_id) >= 0)))) {
            return WORR_SNAPSHOT_STORE_INVALID_EVENT_ORDER;
        }
    }
    return WORR_SNAPSHOT_STORE_OK;
}

static bool build_event_range(const worr_snapshot_event_ref_v2 *events,
                              uint32_t count,
                              uint64_t first_serial,
                              worr_snapshot_event_range_v2 *range_out)
{
    worr_snapshot_event_range_v2 range;
    bool contiguous = true;
    uint32_t i;

    if (!range_out || (count != 0 && !events))
        return false;
    memset(&range, 0, sizeof(range));
    if (count != 0) {
        range.provenance = events[0].provenance;
        range.first_carrier_ordinal = events[0].carrier_ordinal;
        range.flags = WORR_SNAPSHOT_EVENT_RANGE_CONTIGUOUS_CARRIER;
        if (range.provenance ==
            WORR_SNAPSHOT_EVENT_PROVENANCE_AUTHORITY) {
            range.first_authority_id = events[0].authority_id;
            if (!Worr_EventIdNextV1(events[count - 1].authority_id,
                                    &range.one_past_authority_id)) {
                return false;
            }
            for (i = 1; i < count; ++i) {
                worr_event_id_v1 next;
                if (!Worr_EventIdNextV1(events[i - 1].authority_id,
                                        &next) ||
                    !event_id_equal(next, events[i].authority_id)) {
                    contiguous = false;
                }
            }
            if (contiguous) {
                range.flags |=
                    WORR_SNAPSHOT_EVENT_RANGE_CONTIGUOUS_AUTHORITY;
            }
        }
        range.first_ref_serial = first_serial;
        range.count = count;
    }
    *range_out = range;
    return true;
}

worr_snapshot_store_result_v2 Worr_SnapshotStorePublishV2(
    worr_snapshot_store_v2 *store,
    const worr_snapshot_store_publish_v2 *publication,
    worr_snapshot_ref_v2 *ref_out)
{
    worr_snapshot_store_slot_v2 committed;
    worr_snapshot_v2 snapshot;
    worr_snapshot_store_slot_v2 *destination;
    worr_snapshot_store_result_v2 result;
    worr_snapshot_ref_v2 ref;
    uint64_t entity_first;
    uint64_t area_first;
    uint64_t event_first;
    uint64_t next_entity;
    uint64_t next_area;
    uint64_t next_event;
    uint64_t snapshot_hash;
    uint32_t slot_index;
    uint32_t generation;
    uint32_t occupied;
    bool destination_was_committed;
    bool authoritative;
    size_t entity_offset;
    size_t area_offset;
    size_t event_offset;

    if (!store_valid(store))
        return WORR_SNAPSHOT_STORE_INVALID_STORE;
    if (!publication || !ref_out ||
        publication->struct_size != sizeof(*publication) ||
        publication->schema_version != WORR_SNAPSHOT_STORE_VERSION ||
        publication->reserved0 != 0 || !publication->snapshot ||
        !publication->player ||
        (publication->entity_count != 0 && !publication->entities) ||
        (publication->area_byte_count != 0 && !publication->area_bytes) ||
        (publication->event_ref_count != 0 && !publication->event_refs)) {
        return WORR_SNAPSHOT_STORE_INVALID_ARGUMENT;
    }
    if (publication->entity_count > store->entities_per_slot ||
        publication->area_byte_count > store->area_bytes_per_slot ||
        publication->event_ref_count > store->event_refs_per_slot) {
        return WORR_SNAPSHOT_STORE_CAPACITY;
    }
    if (!Worr_SnapshotPlayerValidateV2(publication->player,
                                       store->max_entities)) {
        return WORR_SNAPSHOT_STORE_INVALID_PLAYER;
    }

    authoritative =
        (publication->snapshot->flags &
         WORR_SNAPSHOT_FLAG_AUTHORITATIVE_GENERATIONS) != 0;
    if (!generation_equal(publication->snapshot->controlled_entity,
                          publication->player->controlled_entity) ||
        primary_generation_source(publication->player->controlled_entity) !=
            (authoritative ? WORR_SNAPSHOT_GENERATION_AUTHORITATIVE
                           : WORR_SNAPSHOT_GENERATION_LEGACY_INFERRED)) {
        return WORR_SNAPSHOT_STORE_INVALID_PLAYER;
    }

    result = validate_entities(store, publication->entities,
                               publication->entity_count, authoritative,
                               publication->snapshot->controlled_entity);
    if (result != WORR_SNAPSHOT_STORE_OK)
        return result;
    result = validate_events(publication->event_refs,
                             publication->event_ref_count);
    if (result != WORR_SNAPSHOT_STORE_OK)
        return result;

    if (!serial_reserve(store->next_entity_serial,
                        publication->entity_count, &entity_first,
                        &next_entity) ||
        !serial_reserve(store->next_area_serial,
                        publication->area_byte_count, &area_first,
                        &next_area) ||
        !serial_reserve(store->next_event_serial,
                        publication->event_ref_count, &event_first,
                        &next_event) ||
        store->publish_count == UINT64_MAX) {
        return WORR_SNAPSHOT_STORE_SERIAL_EXHAUSTED;
    }

    slot_index = store->next_slot;
    destination = &store->slots[slot_index];
    if (destination->committed > 1 ||
        (destination->committed != 0 && destination->generation == 0)) {
        return WORR_SNAPSHOT_STORE_INVALID_STORE;
    }
    if (destination->generation == UINT32_MAX)
        return WORR_SNAPSHOT_STORE_GENERATION_EXHAUSTED;
    destination_was_committed = destination->committed != 0;
    generation = destination->generation + 1u;
    if (generation == 0)
        return WORR_SNAPSHOT_STORE_GENERATION_EXHAUSTED;

    snapshot = *publication->snapshot;
    memset(&snapshot.entity_range, 0, sizeof(snapshot.entity_range));
    memset(&snapshot.area_range, 0, sizeof(snapshot.area_range));
    memset(&snapshot.event_range, 0, sizeof(snapshot.event_range));
    snapshot.player_hash = 0;
    snapshot.entity_hash = 0;
    snapshot.area_hash = 0;
    snapshot.event_hash = 0;
    snapshot.snapshot_hash = 0;
    snapshot.entity_range.first_serial = entity_first;
    snapshot.entity_range.count = publication->entity_count;
    snapshot.area_range.first_serial = area_first;
    snapshot.area_range.count = publication->area_byte_count;
    if (!build_event_range(publication->event_refs,
                           publication->event_ref_count, event_first,
                           &snapshot.event_range) ||
        !Worr_SnapshotPlayerHashV2(publication->player,
                                   store->max_entities,
                                   &snapshot.player_hash) ||
        !Worr_SnapshotEntityListHashV2(publication->entities,
                                       publication->entity_count,
                                       store->max_entities,
                                       &snapshot.entity_hash) ||
        !Worr_SnapshotAreaHashV2(publication->area_bytes,
                                 publication->area_byte_count,
                                 &snapshot.area_hash) ||
        !Worr_SnapshotEventRefsHashV2(publication->event_refs,
                                      publication->event_ref_count,
                                      &snapshot.event_hash) ||
        !Worr_SnapshotCalculateHashV2(&snapshot, store->max_entities,
                                      &snapshot_hash)) {
        return WORR_SNAPSHOT_STORE_INVALID_SNAPSHOT;
    }
    snapshot.snapshot_hash = snapshot_hash;
    if (!Worr_SnapshotValidateV2(&snapshot, store->max_entities))
        return WORR_SNAPSHOT_STORE_INVALID_SNAPSHOT;

    memset(&committed, 0, sizeof(committed));
    committed.snapshot = snapshot;
    committed.player = *publication->player;
    committed.entity_first_serial = entity_first;
    committed.area_first_serial = area_first;
    committed.event_first_serial = event_first;
    committed.generation = generation;
    committed.committed = 1;

    entity_offset = (size_t)slot_index * store->entities_per_slot;
    area_offset = (size_t)slot_index * store->area_bytes_per_slot;
    event_offset = (size_t)slot_index * store->event_refs_per_slot;
    if (publication->entity_count != 0) {
        memmove(store->entities + entity_offset, publication->entities,
                sizeof(*store->entities) * publication->entity_count);
    }
    if (publication->area_byte_count != 0) {
        memmove(store->area_bytes + area_offset, publication->area_bytes,
                publication->area_byte_count);
    }
    if (publication->event_ref_count != 0) {
        memmove(store->event_refs + event_offset, publication->event_refs,
                sizeof(*store->event_refs) * publication->event_ref_count);
    }
    *destination = committed;

    occupied = store->occupied;
    if (!destination_was_committed && occupied < store->slot_capacity)
        ++occupied;
    store->occupied = occupied;
    if (store->occupied_high_water < occupied)
        store->occupied_high_water = occupied;
    if (store->entity_high_water < publication->entity_count)
        store->entity_high_water = publication->entity_count;
    if (store->area_high_water < publication->area_byte_count)
        store->area_high_water = publication->area_byte_count;
    if (store->event_high_water < publication->event_ref_count)
        store->event_high_water = publication->event_ref_count;
    store->next_entity_serial = next_entity;
    store->next_area_serial = next_area;
    store->next_event_serial = next_event;
    store->publish_count += 1u;
    store->next_slot = slot_index + 1u;
    if (store->next_slot == store->slot_capacity)
        store->next_slot = 0;

    ref.slot = slot_index;
    ref.generation = generation;
    *ref_out = ref;
    return WORR_SNAPSHOT_STORE_OK;
}

static worr_snapshot_store_result_v2 resolve_slot(
    const worr_snapshot_store_v2 *store,
    worr_snapshot_ref_v2 ref,
    const worr_snapshot_store_slot_v2 **slot_out)
{
    const worr_snapshot_store_slot_v2 *slot;
    const worr_snapshot_v2 *snapshot;

    if (!store_valid(store))
        return WORR_SNAPSHOT_STORE_INVALID_STORE;
    if (!slot_out || ref.slot >= store->slot_capacity || ref.generation == 0)
        return WORR_SNAPSHOT_STORE_INVALID_ARGUMENT;
    slot = &store->slots[ref.slot];
    if (slot->committed > 1)
        return WORR_SNAPSHOT_STORE_CORRUPT;
    if (slot->committed == 0 || slot->generation != ref.generation)
        return WORR_SNAPSHOT_STORE_STALE_REF;
    snapshot = &slot->snapshot;
    if (snapshot->entity_range.count > store->entities_per_slot ||
        snapshot->area_range.count > store->area_bytes_per_slot ||
        snapshot->event_range.count > store->event_refs_per_slot ||
        snapshot->entity_range.first_serial != slot->entity_first_serial ||
        snapshot->area_range.first_serial != slot->area_first_serial ||
        snapshot->event_range.first_ref_serial != slot->event_first_serial ||
        !serial_range_live(slot->entity_first_serial,
                           snapshot->entity_range.count,
                           store->next_entity_serial) ||
        !serial_range_live(slot->area_first_serial,
                           snapshot->area_range.count,
                           store->next_area_serial) ||
        !serial_range_live(slot->event_first_serial,
                           snapshot->event_range.count,
                           store->next_event_serial) ||
        !Worr_SnapshotValidateV2(snapshot, store->max_entities)) {
        return WORR_SNAPSHOT_STORE_CORRUPT;
    }
    *slot_out = slot;
    return WORR_SNAPSHOT_STORE_OK;
}

bool Worr_SnapshotStoreRefValidV2(const worr_snapshot_store_v2 *store,
                                  worr_snapshot_ref_v2 ref)
{
    const worr_snapshot_store_slot_v2 *slot;
    return resolve_slot(store, ref, &slot) == WORR_SNAPSHOT_STORE_OK;
}

worr_snapshot_store_result_v2 Worr_SnapshotStoreCopySnapshotV2(
    const worr_snapshot_store_v2 *store,
    worr_snapshot_ref_v2 ref,
    worr_snapshot_v2 *snapshot_out)
{
    const worr_snapshot_store_slot_v2 *slot;
    worr_snapshot_store_result_v2 result;

    if (!snapshot_out)
        return WORR_SNAPSHOT_STORE_INVALID_ARGUMENT;
    result = resolve_slot(store, ref, &slot);
    if (result != WORR_SNAPSHOT_STORE_OK)
        return result;
    *snapshot_out = slot->snapshot;
    return WORR_SNAPSHOT_STORE_OK;
}

worr_snapshot_store_result_v2 Worr_SnapshotStoreCopyPlayerV2(
    const worr_snapshot_store_v2 *store,
    worr_snapshot_ref_v2 ref,
    worr_snapshot_player_v2 *player_out)
{
    const worr_snapshot_store_slot_v2 *slot;
    worr_snapshot_store_result_v2 result;
    uint64_t hash;

    if (!player_out)
        return WORR_SNAPSHOT_STORE_INVALID_ARGUMENT;
    result = resolve_slot(store, ref, &slot);
    if (result != WORR_SNAPSHOT_STORE_OK)
        return result;
    if (!generation_equal(slot->snapshot.controlled_entity,
                          slot->player.controlled_entity) ||
        !Worr_SnapshotPlayerHashV2(&slot->player, store->max_entities,
                                   &hash) ||
        hash != slot->snapshot.player_hash) {
        return WORR_SNAPSHOT_STORE_CORRUPT;
    }
    *player_out = slot->player;
    return WORR_SNAPSHOT_STORE_OK;
}

worr_snapshot_store_result_v2 Worr_SnapshotStoreCopyEntitiesV2(
    const worr_snapshot_store_v2 *store,
    worr_snapshot_ref_v2 ref,
    worr_snapshot_entity_v2 *entities_out,
    uint32_t capacity,
    uint32_t *count_out)
{
    const worr_snapshot_store_slot_v2 *slot;
    const worr_snapshot_entity_v2 *source;
    worr_snapshot_store_result_v2 result;
    uint64_t hash;
    uint32_t count;
    size_t offset;

    if (!count_out)
        return WORR_SNAPSHOT_STORE_INVALID_ARGUMENT;
    result = resolve_slot(store, ref, &slot);
    if (result != WORR_SNAPSHOT_STORE_OK)
        return result;
    count = slot->snapshot.entity_range.count;
    if (capacity < count || (count != 0 && !entities_out))
        return WORR_SNAPSHOT_STORE_BUFFER_TOO_SMALL;
    offset = (size_t)ref.slot * store->entities_per_slot;
    source = count == 0 ? NULL : store->entities + offset;
    if (!Worr_SnapshotEntityListHashV2(source, count, store->max_entities,
                                       &hash) ||
        hash != slot->snapshot.entity_hash) {
        return WORR_SNAPSHOT_STORE_CORRUPT;
    }
    if (count != 0)
        memmove(entities_out, source, sizeof(*entities_out) * count);
    *count_out = count;
    return WORR_SNAPSHOT_STORE_OK;
}

worr_snapshot_store_result_v2 Worr_SnapshotStoreCopyAreaV2(
    const worr_snapshot_store_v2 *store,
    worr_snapshot_ref_v2 ref,
    uint8_t *area_bytes_out,
    uint32_t capacity,
    uint32_t *count_out)
{
    const worr_snapshot_store_slot_v2 *slot;
    const uint8_t *source;
    worr_snapshot_store_result_v2 result;
    uint64_t hash;
    uint32_t count;
    size_t offset;

    if (!count_out)
        return WORR_SNAPSHOT_STORE_INVALID_ARGUMENT;
    result = resolve_slot(store, ref, &slot);
    if (result != WORR_SNAPSHOT_STORE_OK)
        return result;
    count = slot->snapshot.area_range.count;
    if (capacity < count || (count != 0 && !area_bytes_out))
        return WORR_SNAPSHOT_STORE_BUFFER_TOO_SMALL;
    offset = (size_t)ref.slot * store->area_bytes_per_slot;
    source = count == 0 ? NULL : store->area_bytes + offset;
    if (!Worr_SnapshotAreaHashV2(source, count, &hash) ||
        hash != slot->snapshot.area_hash) {
        return WORR_SNAPSHOT_STORE_CORRUPT;
    }
    if (count != 0)
        memmove(area_bytes_out, source, count);
    *count_out = count;
    return WORR_SNAPSHOT_STORE_OK;
}

worr_snapshot_store_result_v2 Worr_SnapshotStoreCopyEventRefsV2(
    const worr_snapshot_store_v2 *store,
    worr_snapshot_ref_v2 ref,
    worr_snapshot_event_ref_v2 *event_refs_out,
    uint32_t capacity,
    uint32_t *count_out)
{
    const worr_snapshot_store_slot_v2 *slot;
    const worr_snapshot_event_ref_v2 *source;
    worr_snapshot_store_result_v2 result;
    uint64_t hash;
    uint32_t count;
    size_t offset;

    if (!count_out)
        return WORR_SNAPSHOT_STORE_INVALID_ARGUMENT;
    result = resolve_slot(store, ref, &slot);
    if (result != WORR_SNAPSHOT_STORE_OK)
        return result;
    count = slot->snapshot.event_range.count;
    if (capacity < count || (count != 0 && !event_refs_out))
        return WORR_SNAPSHOT_STORE_BUFFER_TOO_SMALL;
    offset = (size_t)ref.slot * store->event_refs_per_slot;
    source = count == 0 ? NULL : store->event_refs + offset;
    if (!Worr_SnapshotEventRefsHashV2(source, count, &hash) ||
        hash != slot->snapshot.event_hash) {
        return WORR_SNAPSHOT_STORE_CORRUPT;
    }
    if (count != 0) {
        memmove(event_refs_out, source, sizeof(*event_refs_out) * count);
    }
    *count_out = count;
    return WORR_SNAPSHOT_STORE_OK;
}

worr_snapshot_store_result_v2 Worr_SnapshotStoreGetStatsV2(
    const worr_snapshot_store_v2 *store,
    worr_snapshot_store_stats_v2 *stats_out)
{
    worr_snapshot_store_stats_v2 stats;

    if (!stats_out)
        return WORR_SNAPSHOT_STORE_INVALID_ARGUMENT;
    if (!store_valid(store))
        return WORR_SNAPSHOT_STORE_INVALID_STORE;
    memset(&stats, 0, sizeof(stats));
    stats.struct_size = sizeof(stats);
    stats.schema_version = WORR_SNAPSHOT_STORE_VERSION;
    stats.slot_capacity = store->slot_capacity;
    stats.occupied = store->occupied;
    stats.occupied_high_water = store->occupied_high_water;
    stats.entity_high_water = store->entity_high_water;
    stats.area_high_water = store->area_high_water;
    stats.event_high_water = store->event_high_water;
    stats.next_entity_serial = store->next_entity_serial;
    stats.next_area_serial = store->next_area_serial;
    stats.next_event_serial = store->next_event_serial;
    stats.publish_count = store->publish_count;
    *stats_out = stats;
    return WORR_SNAPSHOT_STORE_OK;
}

worr_snapshot_store_result_v2 Worr_SnapshotStoreResetV2(
    worr_snapshot_store_v2 *store)
{
    uint32_t i;

    if (!store_valid(store))
        return WORR_SNAPSHOT_STORE_INVALID_STORE;
    for (i = 0; i < store->slot_capacity; ++i) {
        const worr_snapshot_store_slot_v2 *slot = &store->slots[i];
        if (slot->committed > 1 ||
            (slot->committed != 0 && slot->generation == 0)) {
            return WORR_SNAPSHOT_STORE_CORRUPT;
        }
        if (slot->generation == UINT32_MAX)
            return WORR_SNAPSHOT_STORE_GENERATION_EXHAUSTED;
    }
    for (i = 0; i < store->slot_capacity; ++i) {
        worr_snapshot_store_slot_v2 *slot = &store->slots[i];
        slot->generation = slot->generation == 0 ? 1u
                                                  : slot->generation + 1u;
        slot->committed = 0;
    }
    store->next_slot = 0;
    store->occupied = 0;
    return WORR_SNAPSHOT_STORE_OK;
}
