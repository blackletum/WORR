/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/snapshot_timeline.h"

#include <math.h>
#include <string.h>

#define TIMELINE_BOUNDARY_FLAGS                                          \
    ((uint32_t)(WORR_SNAPSHOT_DISCONTINUITY_INITIAL |                   \
                WORR_SNAPSHOT_DISCONTINUITY_MAP_RESET |                 \
                WORR_SNAPSHOT_DISCONTINUITY_DEMO_REWIND |               \
                WORR_SNAPSHOT_DISCONTINUITY_HARD_RESYNC))

#define TIMELINE_BLEND_BLOCK_FLAGS                                       \
    ((uint32_t)(WORR_SNAPSHOT_DISCONTINUITY_INITIAL |                   \
                WORR_SNAPSHOT_DISCONTINUITY_FULL_SNAPSHOT |             \
                WORR_SNAPSHOT_DISCONTINUITY_SEQUENCE_GAP |              \
                WORR_SNAPSHOT_DISCONTINUITY_BASE_JUMP |                  \
                WORR_SNAPSHOT_DISCONTINUITY_RATE_SUPPRESSED |           \
                WORR_SNAPSHOT_DISCONTINUITY_FRAGMENT_STALL |            \
                WORR_SNAPSHOT_DISCONTINUITY_MAP_RESET |                  \
                WORR_SNAPSHOT_DISCONTINUITY_DEMO_REWIND |                \
                WORR_SNAPSHOT_DISCONTINUITY_TRANSPORT_TRUNCATED |       \
                WORR_SNAPSHOT_DISCONTINUITY_HARD_RESYNC |                \
                WORR_SNAPSHOT_DISCONTINUITY_OBSERVER_ATTACH))

#define TIMELINE_PAIR_BLOCK_FLAGS                                        \
    ((uint32_t)(WORR_SNAPSHOT_TIMELINE_PAIR_BLOCK_DISCONTINUITY |       \
                WORR_SNAPSHOT_TIMELINE_PAIR_BLOCK_NO_PREVIOUS |         \
                WORR_SNAPSHOT_TIMELINE_PAIR_BLOCK_ZERO_INTERVAL |       \
                WORR_SNAPSHOT_TIMELINE_PAIR_BLOCK_POLICY))

static const uint64_t fnv_offset_basis = UINT64_C(14695981039346656037);
static const uint64_t fnv_prime = UINT64_C(1099511628211);

typedef struct address_range_s {
    uintptr_t first;
    uintptr_t one_past;
} address_range;

static bool checked_multiply_size(uint32_t count, size_t item_size,
                                  size_t *bytes_out)
{
    if (!bytes_out || (count != 0 && item_size > SIZE_MAX / count))
        return false;
    *bytes_out = (size_t)count * item_size;
    return true;
}

static bool make_range(const void *pointer, size_t bytes, address_range *out)
{
    uintptr_t first;

    if (!out || (bytes != 0 && !pointer))
        return false;
    if (bytes == 0) {
        out->first = 0;
        out->one_past = 0;
        return true;
    }
    first = (uintptr_t)pointer;
    if (bytes > UINTPTR_MAX ||
        first > UINTPTR_MAX - (uintptr_t)bytes)
        return false;
    out->first = first;
    out->one_past = first + (uintptr_t)bytes;
    return true;
}

static bool range_empty(address_range range)
{
    return range.first == range.one_past;
}

static bool ranges_overlap(address_range left, address_range right)
{
    if (range_empty(left) || range_empty(right))
        return false;
    return left.first < right.one_past && right.first < left.one_past;
}

static bool ranges_pairwise_disjoint(const address_range *ranges,
                                     uint32_t count)
{
    uint32_t i;
    uint32_t j;

    if (!ranges)
        return false;
    for (i = 0; i < count; ++i) {
        for (j = i + 1; j < count; ++j) {
            if (ranges_overlap(ranges[i], ranges[j]))
                return false;
        }
    }
    return true;
}

static uint64_t hash_u8(uint64_t hash, uint8_t value)
{
    return (hash ^ value) * fnv_prime;
}

static uint64_t hash_u16(uint64_t hash, uint16_t value)
{
    hash = hash_u8(hash, (uint8_t)value);
    return hash_u8(hash, (uint8_t)(value >> 8));
}

static uint64_t hash_u32(uint64_t hash, uint32_t value)
{
    unsigned int i;
    for (i = 0; i < 4; ++i)
        hash = hash_u8(hash, (uint8_t)(value >> (i * 8)));
    return hash;
}

static uint64_t hash_u64(uint64_t hash, uint64_t value)
{
    unsigned int i;
    for (i = 0; i < 8; ++i)
        hash = hash_u8(hash, (uint8_t)(value >> (i * 8)));
    return hash;
}

static uint64_t begin_hash(uint32_t domain)
{
    uint64_t hash = fnv_offset_basis;
    hash = hash_u32(hash, UINT32_C(0x574f5252));
    hash = hash_u32(hash, WORR_SNAPSHOT_TIMELINE_VERSION);
    return hash_u32(hash, domain);
}

static uint64_t hash_float(uint64_t hash, float value)
{
    uint32_t bits;
    if (value == 0.0f)
        bits = 0;
    else
        memcpy(&bits, &value, sizeof(bits));
    return hash_u32(hash, bits);
}

static void saturating_increment(uint64_t *value)
{
    if (*value != UINT64_MAX)
        ++*value;
}

static void saturating_add(uint64_t *value, uint64_t amount)
{
    if (amount > UINT64_MAX - *value)
        *value = UINT64_MAX;
    else
        *value += amount;
}

static bool pointer_aligned(const void *pointer, size_t alignment)
{
    return !pointer || ((uintptr_t)pointer % alignment) == 0;
}

static bool timeline_ranges(const worr_snapshot_timeline_v1 *timeline,
                            address_range ranges[5])
{
    size_t slot_bytes;
    size_t entity_bytes;
    size_t event_bytes;

    if (!timeline ||
        !checked_multiply_size(timeline->slot_capacity,
                               sizeof(*timeline->slots), &slot_bytes) ||
        !checked_multiply_size(timeline->entity_storage_capacity,
                               sizeof(*timeline->entities), &entity_bytes) ||
        !checked_multiply_size(timeline->event_storage_capacity,
                               sizeof(*timeline->event_refs), &event_bytes)) {
        return false;
    }
    return make_range(timeline, sizeof(*timeline), &ranges[0]) &&
           make_range(timeline->slots, slot_bytes, &ranges[1]) &&
           make_range(timeline->entities, entity_bytes, &ranges[2]) &&
           make_range(timeline->area_bytes,
                      timeline->area_storage_capacity, &ranges[3]) &&
           make_range(timeline->event_refs, event_bytes, &ranges[4]);
}

static bool timeline_valid(const worr_snapshot_timeline_v1 *timeline)
{
    address_range ranges[5];
    size_t required;
    uint64_t latest_serial = 0;
    uint32_t committed_count = 0;
    uint32_t i;

    if (!timeline || timeline->struct_size != sizeof(*timeline) ||
        timeline->schema_version != WORR_SNAPSHOT_TIMELINE_VERSION ||
        !timeline->slots || timeline->slot_capacity == 0 ||
        timeline->max_entities == 0 || timeline->reserved0 != 0 ||
        timeline->next_slot >= timeline->slot_capacity ||
        timeline->occupied > timeline->slot_capacity ||
        timeline->occupied_high_water < timeline->occupied ||
        timeline->occupied_high_water > timeline->slot_capacity ||
        timeline->entity_high_water > timeline->entities_per_slot ||
        timeline->area_high_water > timeline->area_bytes_per_slot ||
        timeline->event_high_water > timeline->event_refs_per_slot ||
        timeline->entities_per_slot > timeline->max_entities ||
        timeline->clock.struct_size != sizeof(timeline->clock) ||
        timeline->clock.schema_version !=
            WORR_SNAPSHOT_TIMELINE_VERSION ||
        timeline->clock.paused > 1 || timeline->clock.initialized > 1 ||
        timeline->clock.fractional_q16 >=
            WORR_SNAPSHOT_TIMELINE_RATE_ONE_Q16 ||
        timeline->clock.last_reset_reason >
            WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_USER ||
        !pointer_aligned(timeline->slots,
                         _Alignof(worr_snapshot_timeline_slot_v1)) ||
        !pointer_aligned(timeline->entities,
                         _Alignof(worr_snapshot_entity_v2)) ||
        !pointer_aligned(timeline->event_refs,
                         _Alignof(worr_snapshot_event_ref_v2)) ||
        !checked_multiply_size(timeline->slot_capacity,
                               timeline->entities_per_slot, &required) ||
        required > timeline->entity_storage_capacity ||
        !checked_multiply_size(timeline->slot_capacity,
                               timeline->area_bytes_per_slot, &required) ||
        required > timeline->area_storage_capacity ||
        !checked_multiply_size(timeline->slot_capacity,
                               timeline->event_refs_per_slot, &required) ||
        required > timeline->event_storage_capacity ||
        (timeline->entity_storage_capacity != 0 && !timeline->entities) ||
        (timeline->area_storage_capacity != 0 && !timeline->area_bytes) ||
        (timeline->event_storage_capacity != 0 && !timeline->event_refs) ||
        !timeline_ranges(timeline, ranges) ||
        !ranges_pairwise_disjoint(ranges, 5)) {
        return false;
    }
    if (timeline->next_publish_serial == 0 || timeline->segment_count == 0 ||
        timeline->active_segment == 0 ||
        timeline->instance_generation == 0) {
        return false;
    }
    if (timeline->clock.initialized) {
        if (timeline->clock.epoch == 0 || timeline->clock.rate_q16 == 0 ||
            timeline->clock.rate_q16 >
                WORR_SNAPSHOT_TIMELINE_RATE_MAX_Q16) {
            return false;
        }
    } else if (timeline->clock.epoch != 0 ||
               timeline->clock.host_time_us != 0 ||
               timeline->clock.render_time_us != 0 ||
               timeline->clock.rate_q16 != 0 || timeline->clock.paused != 0 ||
               timeline->clock.last_reset_reason != 0 ||
               timeline->clock.fractional_q16 != 0) {
        return false;
    }

    for (i = 0; i < timeline->slot_capacity; ++i) {
        const worr_snapshot_timeline_slot_v1 *slot = &timeline->slots[i];

        if (slot->generation == 0 || slot->committed > 1 ||
            slot->reserved0 != 0) {
            return false;
        }
        if (!slot->committed)
            continue;
        if (slot->publish_serial == 0 ||
            slot->publish_serial >= timeline->next_publish_serial ||
            slot->segment == 0 || slot->segment > timeline->active_segment ||
            slot->entity_count > timeline->entities_per_slot ||
            slot->area_byte_count > timeline->area_bytes_per_slot ||
            slot->event_ref_count > timeline->event_refs_per_slot ||
            slot->snapshot.entity_range.count != slot->entity_count ||
            slot->snapshot.area_range.count != slot->area_byte_count ||
            slot->snapshot.event_range.count != slot->event_ref_count) {
            return false;
        }
        ++committed_count;
        if (slot->publish_serial > latest_serial)
            latest_serial = slot->publish_serial;
    }
    if (committed_count != timeline->occupied ||
        latest_serial != timeline->latest_publish_serial ||
        ((committed_count == 0) !=
         (timeline->latest_publish_serial == 0))) {
        return false;
    }
    if (committed_count == 0)
        return timeline->active_segment_first_serial == 0;
    for (i = 0; i < timeline->slot_capacity; ++i) {
        const worr_snapshot_timeline_slot_v1 *slot = &timeline->slots[i];
        if (slot->committed &&
            slot->publish_serial == timeline->latest_publish_serial) {
            return slot->segment == timeline->active_segment &&
                   timeline->active_segment_first_serial != 0 &&
                   timeline->active_segment_first_serial <=
                       slot->publish_serial;
        }
    }
    return false;
}

static bool output_disjoint(const worr_snapshot_timeline_v1 *timeline,
                            const void *output, size_t bytes)
{
    address_range owned[5];
    address_range out;
    uint32_t i;

    if (!output || !timeline_ranges(timeline, owned) ||
        !make_range(output, bytes, &out)) {
        return false;
    }
    for (i = 0; i < 5; ++i) {
        if (ranges_overlap(out, owned[i]))
            return false;
    }
    return true;
}

static bool external_ranges_disjoint(
    const worr_snapshot_timeline_v1 *timeline,
    const void *first, size_t first_bytes,
    const void *second, size_t second_bytes,
    const void *third, size_t third_bytes)
{
    address_range owned[5];
    address_range external[3];
    uint32_t i;
    uint32_t j;

    if (!timeline_ranges(timeline, owned) ||
        !make_range(first, first_bytes, &external[0]) ||
        !make_range(second, second_bytes, &external[1]) ||
        !make_range(third, third_bytes, &external[2]) ||
        !ranges_pairwise_disjoint(external, 3)) {
        return false;
    }
    for (i = 0; i < 3; ++i) {
        for (j = 0; j < 5; ++j) {
            if (ranges_overlap(external[i], owned[j]))
                return false;
        }
    }
    return true;
}

static size_t entity_offset(const worr_snapshot_timeline_v1 *timeline,
                            uint32_t slot)
{
    return (size_t)slot * timeline->entities_per_slot;
}

static size_t area_offset(const worr_snapshot_timeline_v1 *timeline,
                          uint32_t slot)
{
    return (size_t)slot * timeline->area_bytes_per_slot;
}

static size_t event_offset(const worr_snapshot_timeline_v1 *timeline,
                           uint32_t slot)
{
    return (size_t)slot * timeline->event_refs_per_slot;
}

static const worr_snapshot_entity_v2 *slot_entities(
    const worr_snapshot_timeline_v1 *timeline, uint32_t slot)
{
    return timeline->entities_per_slot == 0
               ? NULL
               : timeline->entities + entity_offset(timeline, slot);
}

static const uint8_t *slot_area(
    const worr_snapshot_timeline_v1 *timeline, uint32_t slot)
{
    return timeline->area_bytes_per_slot == 0
               ? NULL
               : timeline->area_bytes + area_offset(timeline, slot);
}

static const worr_snapshot_event_ref_v2 *slot_events(
    const worr_snapshot_timeline_v1 *timeline, uint32_t slot)
{
    return timeline->event_refs_per_slot == 0
               ? NULL
               : timeline->event_refs + event_offset(timeline, slot);
}

static bool generation_equal(worr_snapshot_entity_generation_v2 left,
                             worr_snapshot_entity_generation_v2 right)
{
    return left.identity.index == right.identity.index &&
           left.identity.generation == right.identity.generation &&
           left.provenance_flags == right.provenance_flags;
}

static uint32_t generation_primary_source(
    worr_snapshot_entity_generation_v2 generation)
{
    return generation.provenance_flags &
           (WORR_SNAPSHOT_GENERATION_AUTHORITATIVE |
            WORR_SNAPSHOT_GENERATION_LEGACY_INFERRED);
}

static const worr_snapshot_timeline_slot_v1 *find_serial_const(
    const worr_snapshot_timeline_v1 *timeline, uint64_t serial,
    uint32_t *index_out);

static bool projection_semantics_valid(
    const worr_snapshot_projection_view_v2 *view, uint32_t max_entities,
    worr_snapshot_projection_hashes_v2 *hashes_out)
{
    const uint32_t expected_source =
        (view && view->snapshot &&
         (view->snapshot->flags &
          WORR_SNAPSHOT_FLAG_AUTHORITATIVE_GENERATIONS) != 0)
            ? WORR_SNAPSHOT_GENERATION_AUTHORITATIVE
            : WORR_SNAPSHOT_GENERATION_LEGACY_INFERRED;
    uint32_t i;

    if (!view || !hashes_out ||
        !Worr_SnapshotProjectionHashesV2(view, max_entities, hashes_out) ||
        !generation_equal(view->snapshot->controlled_entity,
                          view->player->controlled_entity) ||
        generation_primary_source(view->player->controlled_entity) !=
            expected_source) {
        return false;
    }
    for (i = 0; i < view->entity_count; ++i) {
        const worr_snapshot_entity_v2 *entity = &view->entities[i];
        if (generation_primary_source(entity->generation) !=
                expected_source ||
            (entity->generation.identity.index ==
                 view->snapshot->controlled_entity.identity.index &&
             !generation_equal(entity->generation,
                               view->snapshot->controlled_entity))) {
            return false;
        }
    }
    return true;
}

static bool slot_projection_hashes(
    const worr_snapshot_timeline_v1 *timeline, uint32_t slot_index,
    worr_snapshot_projection_hashes_v2 *hashes_out)
{
    const worr_snapshot_timeline_slot_v1 *slot =
        &timeline->slots[slot_index];
    worr_snapshot_projection_view_v2 view;

    if (!hashes_out || !slot->committed)
        return false;
    memset(&view, 0, sizeof(view));
    view.struct_size = sizeof(view);
    view.schema_version = WORR_SNAPSHOT_PROJECTION_VERSION;
    view.snapshot = &slot->snapshot;
    view.player = &slot->player;
    view.entities = slot_entities(timeline, slot_index);
    view.area_bytes = slot_area(timeline, slot_index);
    view.event_refs = slot_events(timeline, slot_index);
    view.entity_count = slot->entity_count;
    view.area_byte_count = slot->area_byte_count;
    view.event_ref_count = slot->event_ref_count;
    return projection_semantics_valid(&view, timeline->max_entities,
                                      hashes_out);
}

static bool slot_content_valid(const worr_snapshot_timeline_v1 *timeline,
                               uint32_t slot_index)
{
    worr_snapshot_projection_hashes_v2 hashes;
    if (!timeline->slots[slot_index].committed)
        return true;
    return slot_projection_hashes(timeline, slot_index, &hashes);
}

static bool timeline_contents_valid(
    const worr_snapshot_timeline_v1 *timeline)
{
    const worr_snapshot_timeline_slot_v1 *previous = NULL;
    uint64_t first_serial;
    uint64_t serial;
    uint32_t i;
    if (!timeline_valid(timeline))
        return false;
    for (i = 0; i < timeline->slot_capacity; ++i) {
        if (!slot_content_valid(timeline, i))
            return false;
    }
    if (timeline->occupied != 0) {
        if (timeline->latest_publish_serial == UINT64_MAX ||
            timeline->next_publish_serial !=
                timeline->latest_publish_serial + 1u ||
            (uint64_t)(timeline->occupied - 1u) >
                timeline->latest_publish_serial - 1u) {
            return false;
        }
        first_serial = timeline->latest_publish_serial -
                       (timeline->occupied - 1u);
        for (serial = first_serial;; ++serial) {
            const worr_snapshot_timeline_slot_v1 *slot =
                find_serial_const(timeline, serial, NULL);
            if (!slot)
                return false;
            if (previous) {
                if (slot->segment < previous->segment ||
                    (slot->segment != previous->segment &&
                     (previous->segment == UINT64_MAX ||
                      slot->segment - previous->segment > 1u))) {
                    return false;
                }
                if (slot->segment == previous->segment) {
                    if (slot->snapshot.snapshot_id.epoch !=
                            previous->snapshot.snapshot_id.epoch ||
                        slot->snapshot.snapshot_id.sequence <=
                            previous->snapshot.snapshot_id.sequence ||
                        slot->snapshot.server_time_us <
                            previous->snapshot.server_time_us) {
                        return false;
                    }
                } else if (slot->snapshot.snapshot_id.epoch ==
                               previous->snapshot.snapshot_id.epoch &&
                           (slot->snapshot.discontinuity.flags &
                            TIMELINE_BOUNDARY_FLAGS) == 0) {
                    return false;
                }
            }
            previous = slot;
            if (serial == timeline->latest_publish_serial)
                break;
        }
    }
    return true;
}

static bool id_equal(worr_snapshot_id_v2 left, worr_snapshot_id_v2 right)
{
    return left.epoch == right.epoch && left.sequence == right.sequence;
}

static bool event_id_equal(worr_event_id_v1 left, worr_event_id_v1 right)
{
    return left.stream_epoch == right.stream_epoch &&
           left.sequence == right.sequence;
}

static worr_snapshot_timeline_slot_v1 *find_serial(
    worr_snapshot_timeline_v1 *timeline, uint64_t serial,
    uint32_t *index_out)
{
    uint32_t i;

    if (!timeline || serial == 0)
        return NULL;
    for (i = 0; i < timeline->slot_capacity; ++i) {
        if (timeline->slots[i].committed &&
            timeline->slots[i].publish_serial == serial) {
            if (index_out)
                *index_out = i;
            return &timeline->slots[i];
        }
    }
    return NULL;
}

static const worr_snapshot_timeline_slot_v1 *find_serial_const(
    const worr_snapshot_timeline_v1 *timeline, uint64_t serial,
    uint32_t *index_out)
{
    return find_serial((worr_snapshot_timeline_v1 *)(uintptr_t)timeline,
                       serial, index_out);
}

static const worr_snapshot_timeline_slot_v1 *latest_slot(
    const worr_snapshot_timeline_v1 *timeline, uint32_t *index_out)
{
    return find_serial_const(timeline, timeline->latest_publish_serial,
                             index_out);
}

static bool ref_valid_unchecked(const worr_snapshot_timeline_v1 *timeline,
                                worr_snapshot_timeline_ref_v1 ref)
{
    const worr_snapshot_timeline_slot_v1 *slot;

    if (ref.slot >= timeline->slot_capacity || ref.generation == 0)
        return false;
    slot = &timeline->slots[ref.slot];
    return slot->committed && slot->generation == ref.generation;
}

static bool projection_source_ranges(
    const worr_snapshot_projection_view_v2 *view,
    address_range ranges[6])
{
    size_t entity_bytes;
    size_t event_bytes;

    if (!view ||
        !checked_multiply_size(view->entity_count,
                               sizeof(*view->entities), &entity_bytes) ||
        !checked_multiply_size(view->event_ref_count,
                               sizeof(*view->event_refs), &event_bytes)) {
        return false;
    }
    return make_range(view, sizeof(*view), &ranges[0]) &&
           make_range(view->snapshot, sizeof(*view->snapshot), &ranges[1]) &&
           make_range(view->player, sizeof(*view->player), &ranges[2]) &&
           make_range(view->entities, entity_bytes, &ranges[3]) &&
           make_range(view->area_bytes, view->area_byte_count, &ranges[4]) &&
           make_range(view->event_refs, event_bytes, &ranges[5]);
}

static bool publication_disjoint(
    const worr_snapshot_timeline_v1 *timeline,
    const worr_snapshot_projection_view_v2 *view,
    const worr_snapshot_timeline_ref_v1 *ref_out)
{
    address_range owned[5];
    address_range sources[6];
    address_range output;
    uint32_t i;
    uint32_t j;

    if (!timeline_ranges(timeline, owned) ||
        !projection_source_ranges(view, sources) ||
        !make_range(ref_out, sizeof(*ref_out), &output) ||
        !ranges_pairwise_disjoint(sources, 6)) {
        return false;
    }
    for (i = 0; i < 6; ++i) {
        for (j = 0; j < 5; ++j) {
            if (ranges_overlap(sources[i], owned[j]))
                return false;
        }
        if (ranges_overlap(sources[i], output))
            return false;
    }
    for (j = 0; j < 5; ++j) {
        if (ranges_overlap(output, owned[j]))
            return false;
    }
    return true;
}

static bool authority_event_conflict(
    const worr_snapshot_event_ref_v2 *left,
    const worr_snapshot_event_ref_v2 *right)
{
    return left->provenance == WORR_SNAPSHOT_EVENT_PROVENANCE_AUTHORITY &&
           right->provenance == WORR_SNAPSHOT_EVENT_PROVENANCE_AUTHORITY &&
           event_id_equal(left->authority_id, right->authority_id) &&
           (left->semantic_version != right->semantic_version ||
            left->semantic_hash != right->semantic_hash);
}

static bool publication_event_conflict(
    const worr_snapshot_timeline_v1 *timeline,
    const worr_snapshot_projection_view_v2 *view)
{
    uint32_t i;
    uint32_t j;
    uint32_t slot_index;

    for (i = 0; i < view->event_ref_count; ++i) {
        for (j = i + 1; j < view->event_ref_count; ++j) {
            if (authority_event_conflict(&view->event_refs[i],
                                         &view->event_refs[j]))
                return true;
        }
        for (slot_index = 0; slot_index < timeline->slot_capacity;
             ++slot_index) {
            const worr_snapshot_timeline_slot_v1 *slot =
                &timeline->slots[slot_index];
            const worr_snapshot_event_ref_v2 *events;

            if (!slot->committed)
                continue;
            events = slot_events(timeline, slot_index);
            for (j = 0; j < slot->event_ref_count; ++j) {
                if (authority_event_conflict(&view->event_refs[i], &events[j]))
                    return true;
            }
        }
    }
    return false;
}

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
    uint32_t max_entities)
{
    worr_snapshot_timeline_v1 initialized;
    address_range ranges[5];
    size_t slot_bytes;
    size_t entity_bytes;
    size_t event_bytes;
    size_t required;
    uint32_t i;

    if (!timeline || !slots || slot_capacity == 0 || max_entities == 0 ||
        entities_per_slot > max_entities ||
        (entity_storage_capacity != 0 && !entities) ||
        (area_storage_capacity != 0 && !area_bytes) ||
        (event_storage_capacity != 0 && !event_refs) ||
        !pointer_aligned(slots, _Alignof(worr_snapshot_timeline_slot_v1)) ||
        !pointer_aligned(entities, _Alignof(worr_snapshot_entity_v2)) ||
        !pointer_aligned(event_refs,
                         _Alignof(worr_snapshot_event_ref_v2)) ||
        !checked_multiply_size(slot_capacity, entities_per_slot, &required) ||
        required > entity_storage_capacity ||
        !checked_multiply_size(slot_capacity, area_bytes_per_slot, &required) ||
        required > area_storage_capacity ||
        !checked_multiply_size(slot_capacity, event_refs_per_slot, &required) ||
        required > event_storage_capacity ||
        !checked_multiply_size(slot_capacity, sizeof(*slots), &slot_bytes) ||
        !checked_multiply_size(entity_storage_capacity, sizeof(*entities),
                               &entity_bytes) ||
        !checked_multiply_size(event_storage_capacity, sizeof(*event_refs),
                               &event_bytes) ||
        !make_range(timeline, sizeof(*timeline), &ranges[0]) ||
        !make_range(slots, slot_bytes, &ranges[1]) ||
        !make_range(entities, entity_bytes, &ranges[2]) ||
        !make_range(area_bytes, area_storage_capacity, &ranges[3]) ||
        !make_range(event_refs, event_bytes, &ranges[4])) {
        return WORR_SNAPSHOT_TIMELINE_INVALID_ARGUMENT;
    }
    if (!ranges_pairwise_disjoint(ranges, 5))
        return WORR_SNAPSHOT_TIMELINE_OVERLAP;

    memset(&initialized, 0, sizeof(initialized));
    initialized.struct_size = sizeof(initialized);
    initialized.schema_version = WORR_SNAPSHOT_TIMELINE_VERSION;
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
    initialized.next_publish_serial = 1;
    initialized.active_segment = 1;
    initialized.instance_generation = 1;
    initialized.segment_count = 1;
    initialized.clock.struct_size = sizeof(initialized.clock);
    initialized.clock.schema_version = WORR_SNAPSHOT_TIMELINE_VERSION;

    memset(slots, 0, slot_bytes);
    for (i = 0; i < slot_capacity; ++i)
        slots[i].generation = 1;
    if (entity_bytes != 0)
        memset(entities, 0, entity_bytes);
    if (area_storage_capacity != 0)
        memset(area_bytes, 0, area_storage_capacity);
    if (event_bytes != 0)
        memset(event_refs, 0, event_bytes);
    *timeline = initialized;
    return WORR_SNAPSHOT_TIMELINE_OK;
}

worr_snapshot_timeline_result_v1 Worr_SnapshotTimelinePublishV1(
    worr_snapshot_timeline_v1 *timeline,
    const worr_snapshot_projection_view_v2 *view,
    uint64_t receive_time_us,
    worr_snapshot_timeline_ref_v1 *ref_out)
{
    worr_snapshot_projection_hashes_v2 projection_hashes;
    const worr_snapshot_timeline_slot_v1 *latest;
    worr_snapshot_timeline_slot_v1 committed;
    worr_snapshot_timeline_slot_v1 *destination;
    worr_snapshot_timeline_ref_v1 ref;
    uint64_t segment;
    bool boundary = false;
    bool overwrite;
    uint32_t slot_index;
    uint32_t latest_slot_index = UINT32_MAX;
    uint32_t generation;

    if (!timeline_valid(timeline))
        return WORR_SNAPSHOT_TIMELINE_INVALID_TIMELINE;
    if (!view || !ref_out)
        return WORR_SNAPSHOT_TIMELINE_INVALID_ARGUMENT;
    if (view->struct_size != sizeof(*view) ||
        view->schema_version != WORR_SNAPSHOT_PROJECTION_VERSION) {
        return WORR_SNAPSHOT_TIMELINE_INVALID_PROJECTION;
    }
    if (view->entity_count > timeline->entities_per_slot ||
        view->area_byte_count > timeline->area_bytes_per_slot ||
        view->event_ref_count > timeline->event_refs_per_slot) {
        return WORR_SNAPSHOT_TIMELINE_CAPACITY;
    }
    if (!publication_disjoint(timeline, view, ref_out))
        return WORR_SNAPSHOT_TIMELINE_OVERLAP;
    if (!timeline_contents_valid(timeline))
        return WORR_SNAPSHOT_TIMELINE_CORRUPT;
    if (!projection_semantics_valid(view, timeline->max_entities,
                                    &projection_hashes)) {
        return WORR_SNAPSHOT_TIMELINE_INVALID_PROJECTION;
    }

    latest = latest_slot(timeline, &latest_slot_index);
    if (latest) {
        if (id_equal(latest->snapshot.snapshot_id,
                     view->snapshot->snapshot_id)) {
            worr_snapshot_projection_hashes_v2 retained_hashes;
            if (!slot_projection_hashes(timeline, latest_slot_index,
                                        &retained_hashes)) {
                return WORR_SNAPSHOT_TIMELINE_CORRUPT;
            }
            return latest->snapshot.snapshot_hash ==
                               view->snapshot->snapshot_hash &&
                           retained_hashes.endpoint_hash ==
                               projection_hashes.endpoint_hash
                       ? WORR_SNAPSHOT_TIMELINE_DUPLICATE
                       : WORR_SNAPSHOT_TIMELINE_CONFLICT;
        }
        boundary = latest->snapshot.snapshot_id.epoch !=
                       view->snapshot->snapshot_id.epoch ||
                   (view->snapshot->discontinuity.flags &
                    TIMELINE_BOUNDARY_FLAGS) != 0;
        if (!boundary &&
            view->snapshot->snapshot_id.sequence <=
                latest->snapshot.snapshot_id.sequence) {
            return WORR_SNAPSHOT_TIMELINE_OUT_OF_ORDER;
        }
        if (!boundary &&
            view->snapshot->server_time_us < latest->snapshot.server_time_us) {
            return WORR_SNAPSHOT_TIMELINE_TIME_ORDER;
        }
    } else {
        boundary = false;
    }
    if (publication_event_conflict(timeline, view))
        return WORR_SNAPSHOT_TIMELINE_EVENT_CONFLICT;
    if (timeline->next_publish_serial == UINT64_MAX)
        return WORR_SNAPSHOT_TIMELINE_SERIAL_EXHAUSTED;
    segment = timeline->active_segment;
    if (boundary) {
        if (segment == UINT64_MAX)
            return WORR_SNAPSHOT_TIMELINE_SEGMENT_EXHAUSTED;
        ++segment;
    }

    slot_index = timeline->next_slot;
    destination = &timeline->slots[slot_index];
    overwrite = destination->committed != 0;
    generation = destination->generation;
    if (overwrite) {
        if (generation == UINT32_MAX)
            return WORR_SNAPSHOT_TIMELINE_GENERATION_EXHAUSTED;
        ++generation;
    }

    memset(&committed, 0, sizeof(committed));
    committed.snapshot = *view->snapshot;
    committed.player = *view->player;
    committed.publish_serial = timeline->next_publish_serial;
    committed.receive_time_us = receive_time_us;
    committed.segment = segment;
    committed.generation = generation;
    committed.entity_count = view->entity_count;
    committed.area_byte_count = view->area_byte_count;
    committed.event_ref_count = view->event_ref_count;
    committed.committed = 1;

    if (view->entity_count != 0) {
        memcpy(timeline->entities + entity_offset(timeline, slot_index),
               view->entities,
               (size_t)view->entity_count * sizeof(*view->entities));
    }
    if (view->area_byte_count != 0) {
        memcpy(timeline->area_bytes + area_offset(timeline, slot_index),
               view->area_bytes, view->area_byte_count);
    }
    if (view->event_ref_count != 0) {
        memcpy(timeline->event_refs + event_offset(timeline, slot_index),
               view->event_refs,
               (size_t)view->event_ref_count * sizeof(*view->event_refs));
    }
    *destination = committed;

    timeline->latest_publish_serial = committed.publish_serial;
    ++timeline->next_publish_serial;
    timeline->next_slot = (slot_index + 1u) % timeline->slot_capacity;
    if (timeline->occupied < timeline->slot_capacity)
        ++timeline->occupied;
    if (timeline->occupied > timeline->occupied_high_water)
        timeline->occupied_high_water = timeline->occupied;
    if (view->entity_count > timeline->entity_high_water)
        timeline->entity_high_water = view->entity_count;
    if (view->area_byte_count > timeline->area_high_water)
        timeline->area_high_water = view->area_byte_count;
    if (view->event_ref_count > timeline->event_high_water)
        timeline->event_high_water = view->event_ref_count;
    if (boundary) {
        timeline->active_segment = segment;
        timeline->active_segment_first_serial = committed.publish_serial;
        saturating_increment(&timeline->segment_count);
    } else if (timeline->active_segment_first_serial == 0) {
        timeline->active_segment_first_serial = committed.publish_serial;
    }
    saturating_increment(&timeline->publish_count);
    if (overwrite)
        saturating_increment(&timeline->overwrite_count);

    ref.slot = slot_index;
    ref.generation = generation;
    *ref_out = ref;
    return WORR_SNAPSHOT_TIMELINE_OK;
}

worr_snapshot_timeline_result_v1 Worr_SnapshotTimelineResetV1(
    worr_snapshot_timeline_v1 *timeline)
{
    uint32_t i;

    if (!timeline_valid(timeline))
        return WORR_SNAPSHOT_TIMELINE_INVALID_TIMELINE;
    if (!timeline_contents_valid(timeline))
        return WORR_SNAPSHOT_TIMELINE_CORRUPT;
    if (timeline->active_segment == UINT64_MAX ||
        timeline->instance_generation == UINT64_MAX) {
        return WORR_SNAPSHOT_TIMELINE_SEGMENT_EXHAUSTED;
    }
    for (i = 0; i < timeline->slot_capacity; ++i) {
        if (timeline->slots[i].generation == UINT32_MAX)
            return WORR_SNAPSHOT_TIMELINE_GENERATION_EXHAUSTED;
    }
    for (i = 0; i < timeline->slot_capacity; ++i) {
        ++timeline->slots[i].generation;
        timeline->slots[i].committed = 0;
        timeline->slots[i].publish_serial = 0;
    }
    timeline->next_slot = 0;
    timeline->occupied = 0;
    timeline->latest_publish_serial = 0;
    timeline->active_segment_first_serial = 0;
    ++timeline->active_segment;
    ++timeline->instance_generation;
    saturating_increment(&timeline->segment_count);
    saturating_increment(&timeline->reset_count);
    return WORR_SNAPSHOT_TIMELINE_OK;
}

bool Worr_SnapshotTimelineRefValidV1(
    const worr_snapshot_timeline_v1 *timeline,
    worr_snapshot_timeline_ref_v1 ref)
{
    return timeline_valid(timeline) && ref_valid_unchecked(timeline, ref) &&
           slot_content_valid(timeline, ref.slot);
}

static worr_snapshot_timeline_result_v1 copy_fixed(
    const worr_snapshot_timeline_v1 *timeline,
    worr_snapshot_timeline_ref_v1 ref, const void *source,
    void *destination, size_t bytes)
{
    if (!timeline_valid(timeline))
        return WORR_SNAPSHOT_TIMELINE_INVALID_TIMELINE;
    if (!destination)
        return WORR_SNAPSHOT_TIMELINE_INVALID_ARGUMENT;
    if (!ref_valid_unchecked(timeline, ref))
        return WORR_SNAPSHOT_TIMELINE_STALE_REF;
    if (!output_disjoint(timeline, destination, bytes))
        return WORR_SNAPSHOT_TIMELINE_OVERLAP;
    memcpy(destination, source, bytes);
    return WORR_SNAPSHOT_TIMELINE_OK;
}

worr_snapshot_timeline_result_v1 Worr_SnapshotTimelineCopySnapshotV1(
    const worr_snapshot_timeline_v1 *timeline,
    worr_snapshot_timeline_ref_v1 ref,
    worr_snapshot_v2 *snapshot_out)
{
    const worr_snapshot_timeline_slot_v1 *slot;

    if (!timeline_valid(timeline))
        return WORR_SNAPSHOT_TIMELINE_INVALID_TIMELINE;
    if (!ref_valid_unchecked(timeline, ref))
        return WORR_SNAPSHOT_TIMELINE_STALE_REF;
    if (!slot_content_valid(timeline, ref.slot))
        return WORR_SNAPSHOT_TIMELINE_CORRUPT;
    slot = &timeline->slots[ref.slot];
    return copy_fixed(timeline, ref, &slot->snapshot,
                      snapshot_out, sizeof(*snapshot_out));
}

worr_snapshot_timeline_result_v1 Worr_SnapshotTimelineCopyPlayerV1(
    const worr_snapshot_timeline_v1 *timeline,
    worr_snapshot_timeline_ref_v1 ref,
    worr_snapshot_player_v2 *player_out)
{
    const worr_snapshot_timeline_slot_v1 *slot;

    if (!timeline_valid(timeline))
        return WORR_SNAPSHOT_TIMELINE_INVALID_TIMELINE;
    if (!ref_valid_unchecked(timeline, ref))
        return WORR_SNAPSHOT_TIMELINE_STALE_REF;
    if (!slot_content_valid(timeline, ref.slot))
        return WORR_SNAPSHOT_TIMELINE_CORRUPT;
    slot = &timeline->slots[ref.slot];
    return copy_fixed(timeline, ref, &slot->player,
                      player_out, sizeof(*player_out));
}

static worr_snapshot_timeline_result_v1 copy_range(
    const worr_snapshot_timeline_v1 *timeline,
    worr_snapshot_timeline_ref_v1 ref, const void *source,
    uint32_t count, size_t item_size, void *destination,
    uint32_t capacity, uint32_t *count_out)
{
    address_range output_ranges[2];
    size_t bytes;

    if (!timeline_valid(timeline))
        return WORR_SNAPSHOT_TIMELINE_INVALID_TIMELINE;
    if (!count_out || (count != 0 && !destination) ||
        !checked_multiply_size(count, item_size, &bytes)) {
        return WORR_SNAPSHOT_TIMELINE_INVALID_ARGUMENT;
    }
    if (!ref_valid_unchecked(timeline, ref))
        return WORR_SNAPSHOT_TIMELINE_STALE_REF;
    if (capacity < count)
        return WORR_SNAPSHOT_TIMELINE_BUFFER_TOO_SMALL;
    if (!make_range(destination, bytes, &output_ranges[0]) ||
        !make_range(count_out, sizeof(*count_out), &output_ranges[1]) ||
        !ranges_pairwise_disjoint(output_ranges, 2) ||
        !output_disjoint(timeline, count_out, sizeof(*count_out)) ||
        (bytes != 0 && !output_disjoint(timeline, destination, bytes))) {
        return WORR_SNAPSHOT_TIMELINE_OVERLAP;
    }
    if (bytes != 0)
        memcpy(destination, source, bytes);
    *count_out = count;
    return WORR_SNAPSHOT_TIMELINE_OK;
}

worr_snapshot_timeline_result_v1 Worr_SnapshotTimelineCopyEntitiesV1(
    const worr_snapshot_timeline_v1 *timeline,
    worr_snapshot_timeline_ref_v1 ref,
    worr_snapshot_entity_v2 *entities_out,
    uint32_t capacity,
    uint32_t *count_out)
{
    const worr_snapshot_timeline_slot_v1 *slot;
    const worr_snapshot_entity_v2 *source;

    if (!timeline_valid(timeline))
        return WORR_SNAPSHOT_TIMELINE_INVALID_TIMELINE;
    if (!ref_valid_unchecked(timeline, ref))
        return WORR_SNAPSHOT_TIMELINE_STALE_REF;
    if (!slot_content_valid(timeline, ref.slot))
        return WORR_SNAPSHOT_TIMELINE_CORRUPT;
    slot = &timeline->slots[ref.slot];
    source = slot_entities(timeline, ref.slot);
    return copy_range(timeline, ref, source, slot->entity_count,
                      sizeof(*entities_out),
                      entities_out, capacity, count_out);
}

worr_snapshot_timeline_result_v1 Worr_SnapshotTimelineCopyAreaV1(
    const worr_snapshot_timeline_v1 *timeline,
    worr_snapshot_timeline_ref_v1 ref,
    uint8_t *area_out,
    uint32_t capacity,
    uint32_t *count_out)
{
    const worr_snapshot_timeline_slot_v1 *slot;
    const uint8_t *source;

    if (!timeline_valid(timeline))
        return WORR_SNAPSHOT_TIMELINE_INVALID_TIMELINE;
    if (!ref_valid_unchecked(timeline, ref))
        return WORR_SNAPSHOT_TIMELINE_STALE_REF;
    if (!slot_content_valid(timeline, ref.slot))
        return WORR_SNAPSHOT_TIMELINE_CORRUPT;
    slot = &timeline->slots[ref.slot];
    source = slot_area(timeline, ref.slot);
    return copy_range(timeline, ref, source, slot->area_byte_count,
                      sizeof(*area_out),
                      area_out, capacity, count_out);
}

worr_snapshot_timeline_result_v1 Worr_SnapshotTimelineCopyEventRefsV1(
    const worr_snapshot_timeline_v1 *timeline,
    worr_snapshot_timeline_ref_v1 ref,
    worr_snapshot_event_ref_v2 *events_out,
    uint32_t capacity,
    uint32_t *count_out)
{
    const worr_snapshot_timeline_slot_v1 *slot;
    const worr_snapshot_event_ref_v2 *source;

    if (!timeline_valid(timeline))
        return WORR_SNAPSHOT_TIMELINE_INVALID_TIMELINE;
    if (!ref_valid_unchecked(timeline, ref))
        return WORR_SNAPSHOT_TIMELINE_STALE_REF;
    if (!slot_content_valid(timeline, ref.slot))
        return WORR_SNAPSHOT_TIMELINE_CORRUPT;
    slot = &timeline->slots[ref.slot];
    source = slot_events(timeline, ref.slot);
    return copy_range(timeline, ref, source, slot->event_ref_count,
                      sizeof(*events_out),
                      events_out, capacity, count_out);
}

static bool clock_rate_valid(uint32_t rate_q16)
{
    return rate_q16 != 0 &&
           rate_q16 <= WORR_SNAPSHOT_TIMELINE_RATE_MAX_Q16;
}

static bool clock_request_valid(
    const worr_snapshot_timeline_clock_request_v1 *request)
{
    if (!request || request->struct_size != sizeof(*request) ||
        request->schema_version != WORR_SNAPSHOT_TIMELINE_VERSION ||
        request->reserved0 != 0 ||
        request->operation < WORR_SNAPSHOT_TIMELINE_CLOCK_ANCHOR ||
        request->operation > WORR_SNAPSHOT_TIMELINE_CLOCK_RESET ||
        request->reset_reason > WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_USER) {
        return false;
    }
    switch (request->operation) {
    case WORR_SNAPSHOT_TIMELINE_CLOCK_ANCHOR:
        return request->reset_reason !=
                   WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_NONE &&
               clock_rate_valid(request->rate_q16);
    case WORR_SNAPSHOT_TIMELINE_CLOCK_ADVANCE:
    case WORR_SNAPSHOT_TIMELINE_CLOCK_PAUSE:
    case WORR_SNAPSHOT_TIMELINE_CLOCK_RESUME:
        return request->reset_reason ==
                   WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_NONE &&
               request->rate_q16 == 0 && request->render_time_us == 0;
    case WORR_SNAPSHOT_TIMELINE_CLOCK_SET_RATE:
        return request->reset_reason ==
                   WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_NONE &&
               request->render_time_us == 0 &&
               clock_rate_valid(request->rate_q16);
    case WORR_SNAPSHOT_TIMELINE_CLOCK_DEMO_SEEK:
        return (request->reset_reason ==
                    WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_DEMO_REWIND ||
                request->reset_reason ==
                    WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_USER) &&
               request->rate_q16 == 0;
    case WORR_SNAPSHOT_TIMELINE_CLOCK_RESET:
        return request->reset_reason !=
                   WORR_SNAPSHOT_TIMELINE_CLOCK_RESET_NONE &&
               request->rate_q16 == 0;
    default:
        return false;
    }
}

static worr_snapshot_timeline_result_v1 clock_advance_to(
    worr_snapshot_timeline_clock_state_v1 *state, uint64_t host_time_us)
{
    uint64_t delta;
    uint64_t high;
    uint64_t low_scaled;
    uint64_t whole;
    uint64_t low_whole;

    if (host_time_us < state->host_time_us)
        return WORR_SNAPSHOT_TIMELINE_CLOCK_REGRESSION;
    delta = host_time_us - state->host_time_us;
    state->host_time_us = host_time_us;
    if (state->paused || delta == 0)
        return WORR_SNAPSHOT_TIMELINE_OK;

    high = delta >> 16;
    if (high != 0 && state->rate_q16 > UINT64_MAX / high)
        return WORR_SNAPSHOT_TIMELINE_CLOCK_OVERFLOW;
    whole = high * state->rate_q16;
    low_scaled = (delta & UINT64_C(0xffff)) * state->rate_q16 +
                 state->fractional_q16;
    low_whole = low_scaled >> 16;
    if (low_whole > UINT64_MAX - whole)
        return WORR_SNAPSHOT_TIMELINE_CLOCK_OVERFLOW;
    whole += low_whole;
    if (whole > UINT64_MAX - state->render_time_us)
        return WORR_SNAPSHOT_TIMELINE_CLOCK_OVERFLOW;
    state->render_time_us += whole;
    state->fractional_q16 = (uint32_t)(low_scaled & UINT64_C(0xffff));
    return WORR_SNAPSHOT_TIMELINE_OK;
}

worr_snapshot_timeline_result_v1 Worr_SnapshotTimelineClockApplyV1(
    worr_snapshot_timeline_v1 *timeline,
    const worr_snapshot_timeline_clock_request_v1 *request,
    worr_snapshot_timeline_clock_state_v1 *state_out)
{
    worr_snapshot_timeline_clock_state_v1 next;
    worr_snapshot_timeline_result_v1 result;
    bool seek = false;

    if (!timeline_valid(timeline))
        return WORR_SNAPSHOT_TIMELINE_INVALID_TIMELINE;
    if (!request || !state_out)
        return WORR_SNAPSHOT_TIMELINE_INVALID_ARGUMENT;
    if (!external_ranges_disjoint(timeline, request, sizeof(*request),
                                  state_out, sizeof(*state_out), NULL, 0)) {
        return WORR_SNAPSHOT_TIMELINE_OVERLAP;
    }
    if (!clock_request_valid(request))
        return WORR_SNAPSHOT_TIMELINE_INVALID_ARGUMENT;

    next = timeline->clock;
    if (request->operation == WORR_SNAPSHOT_TIMELINE_CLOCK_ANCHOR) {
        if (next.initialized)
            return WORR_SNAPSHOT_TIMELINE_INVALID_ARGUMENT;
        next.epoch = 1;
        next.host_time_us = request->host_time_us;
        next.render_time_us = request->render_time_us;
        next.rate_q16 = request->rate_q16;
        next.paused = 0;
        next.initialized = 1;
        next.last_reset_reason = request->reset_reason;
        next.fractional_q16 = 0;
    } else {
        if (!next.initialized)
            return WORR_SNAPSHOT_TIMELINE_CLOCK_UNINITIALIZED;
        switch (request->operation) {
        case WORR_SNAPSHOT_TIMELINE_CLOCK_ADVANCE:
            result = clock_advance_to(&next, request->host_time_us);
            if (result != WORR_SNAPSHOT_TIMELINE_OK)
                return result;
            break;
        case WORR_SNAPSHOT_TIMELINE_CLOCK_PAUSE:
            if (next.paused)
                return WORR_SNAPSHOT_TIMELINE_INVALID_ARGUMENT;
            result = clock_advance_to(&next, request->host_time_us);
            if (result != WORR_SNAPSHOT_TIMELINE_OK)
                return result;
            next.paused = 1;
            break;
        case WORR_SNAPSHOT_TIMELINE_CLOCK_RESUME:
            if (!next.paused)
                return WORR_SNAPSHOT_TIMELINE_INVALID_ARGUMENT;
            result = clock_advance_to(&next, request->host_time_us);
            if (result != WORR_SNAPSHOT_TIMELINE_OK)
                return result;
            next.paused = 0;
            break;
        case WORR_SNAPSHOT_TIMELINE_CLOCK_SET_RATE:
            result = clock_advance_to(&next, request->host_time_us);
            if (result != WORR_SNAPSHOT_TIMELINE_OK)
                return result;
            next.rate_q16 = request->rate_q16;
            break;
        case WORR_SNAPSHOT_TIMELINE_CLOCK_DEMO_SEEK:
        case WORR_SNAPSHOT_TIMELINE_CLOCK_RESET:
            if (request->host_time_us < next.host_time_us)
                return WORR_SNAPSHOT_TIMELINE_CLOCK_REGRESSION;
            if (next.epoch == UINT64_MAX)
                return WORR_SNAPSHOT_TIMELINE_CLOCK_OVERFLOW;
            ++next.epoch;
            next.host_time_us = request->host_time_us;
            next.render_time_us = request->render_time_us;
            next.fractional_q16 = 0;
            next.last_reset_reason = request->reset_reason;
            if (request->operation ==
                WORR_SNAPSHOT_TIMELINE_CLOCK_RESET) {
                next.paused = 0;
            }
            seek = true;
            break;
        default:
            return WORR_SNAPSHOT_TIMELINE_INVALID_ARGUMENT;
        }
    }

    timeline->clock = next;
    saturating_increment(&timeline->clock_update_count);
    if (seek)
        saturating_increment(&timeline->clock_seek_count);
    *state_out = next;
    return WORR_SNAPSHOT_TIMELINE_OK;
}

static bool policy_valid(const worr_snapshot_timeline_policy_v1 *policy)
{
    return policy && policy->struct_size == sizeof(*policy) &&
           policy->schema_version == WORR_SNAPSHOT_TIMELINE_VERSION &&
           policy->reserved0 == 0 && policy->allow_extrapolation <= 1 &&
           policy->interpolation_delay_us <=
               WORR_SNAPSHOT_TIMELINE_MAX_INTERPOLATION_DELAY_US &&
           policy->max_extrapolation_us <=
               WORR_SNAPSHOT_TIMELINE_MAX_EXTRAPOLATION_US &&
           isfinite(policy->teleport_distance) &&
           policy->teleport_distance > 0.0f &&
           isfinite(policy->max_linear_velocity) &&
           policy->max_linear_velocity > 0.0f &&
           policy->max_linear_velocity <=
               WORR_SNAPSHOT_TIMELINE_MAX_LINEAR_VELOCITY &&
           isfinite(policy->max_angular_velocity) &&
           policy->max_angular_velocity > 0.0f &&
           policy->max_angular_velocity <=
               WORR_SNAPSHOT_TIMELINE_MAX_ANGULAR_VELOCITY;
}

static uint64_t policy_hash(const worr_snapshot_timeline_policy_v1 *policy)
{
    uint64_t hash = begin_hash(UINT32_C(0x504f4c31)); /* POL1 */
    hash = hash_u64(hash, policy->interpolation_delay_us);
    hash = hash_u64(hash, policy->max_extrapolation_us);
    hash = hash_float(hash, policy->teleport_distance);
    hash = hash_float(hash, policy->max_linear_velocity);
    hash = hash_float(hash, policy->max_angular_velocity);
    return hash_u32(hash, policy->allow_extrapolation);
}

static worr_snapshot_timeline_ref_v1 none_ref(void)
{
    worr_snapshot_timeline_ref_v1 ref;
    ref.slot = WORR_SNAPSHOT_TIMELINE_REF_NONE_SLOT;
    ref.generation = 0;
    return ref;
}

static worr_snapshot_timeline_ref_v1 slot_ref(
    const worr_snapshot_timeline_v1 *timeline, uint32_t slot_index)
{
    worr_snapshot_timeline_ref_v1 ref;
    ref.slot = slot_index;
    ref.generation = timeline->slots[slot_index].generation;
    return ref;
}

static uint32_t previous_active_slot(
    const worr_snapshot_timeline_v1 *timeline, uint32_t current_index)
{
    const uint64_t current_serial =
        timeline->slots[current_index].publish_serial;
    uint64_t best_serial = 0;
    uint32_t best = UINT32_MAX;
    uint32_t i;

    for (i = 0; i < timeline->slot_capacity; ++i) {
        const worr_snapshot_timeline_slot_v1 *slot = &timeline->slots[i];
        if (slot->committed && slot->segment == timeline->active_segment &&
            slot->publish_serial < current_serial &&
            slot->publish_serial > best_serial) {
            best_serial = slot->publish_serial;
            best = i;
        }
    }
    return best;
}

worr_snapshot_timeline_result_v1 Worr_SnapshotTimelineSelectPairV1(
    worr_snapshot_timeline_v1 *timeline,
    const worr_snapshot_timeline_policy_v1 *policy,
    worr_snapshot_timeline_pair_v1 *pair_out)
{
    worr_snapshot_timeline_pair_v1 pair;
    uint64_t target;
    uint64_t before_time = 0;
    uint64_t before_serial = 0;
    uint64_t after_time = UINT64_MAX;
    uint64_t after_serial = UINT64_MAX;
    uint32_t before = UINT32_MAX;
    uint32_t after = UINT32_MAX;
    uint32_t previous = UINT32_MAX;
    uint32_t i;

    if (!timeline_valid(timeline))
        return WORR_SNAPSHOT_TIMELINE_INVALID_TIMELINE;
    if (!policy || !pair_out)
        return WORR_SNAPSHOT_TIMELINE_INVALID_ARGUMENT;
    if (!external_ranges_disjoint(timeline, policy, sizeof(*policy),
                                  pair_out, sizeof(*pair_out), NULL, 0)) {
        return WORR_SNAPSHOT_TIMELINE_OVERLAP;
    }
    if (!policy_valid(policy))
        return WORR_SNAPSHOT_TIMELINE_INVALID_POLICY;
    if (!timeline->clock.initialized)
        return WORR_SNAPSHOT_TIMELINE_CLOCK_UNINITIALIZED;
    if (!timeline_contents_valid(timeline))
        return WORR_SNAPSHOT_TIMELINE_CORRUPT;

    target = timeline->clock.render_time_us >= policy->interpolation_delay_us
                 ? timeline->clock.render_time_us -
                       policy->interpolation_delay_us
                 : 0;
    for (i = 0; i < timeline->slot_capacity; ++i) {
        const worr_snapshot_timeline_slot_v1 *slot = &timeline->slots[i];
        const uint64_t time = slot->snapshot.server_time_us;
        if (!slot->committed || slot->segment != timeline->active_segment)
            continue;
        if (time <= target &&
            (before == UINT32_MAX || time > before_time ||
             (time == before_time &&
              slot->publish_serial > before_serial))) {
            before = i;
            before_time = time;
            before_serial = slot->publish_serial;
        }
        if (time > target &&
            (after == UINT32_MAX || time < after_time ||
             (time == after_time &&
              slot->publish_serial < after_serial))) {
            after = i;
            after_time = time;
            after_serial = slot->publish_serial;
        }
    }
    if (before == UINT32_MAX && after == UINT32_MAX)
        return WORR_SNAPSHOT_TIMELINE_NOT_FOUND;

    memset(&pair, 0, sizeof(pair));
    pair.struct_size = sizeof(pair);
    pair.schema_version = WORR_SNAPSHOT_TIMELINE_VERSION;
    pair.previous = none_ref();
    pair.current = none_ref();
    pair.timeline_instance = timeline->instance_generation;
    pair.clock_epoch = timeline->clock.epoch;
    pair.segment = timeline->active_segment;
    pair.policy_hash = policy_hash(policy);
    pair.target_time_us = target;

    if (before == UINT32_MAX) {
        pair.current = slot_ref(timeline, after);
        pair.current_time_us = after_time;
        pair.mode = WORR_SNAPSHOT_TIMELINE_PAIR_CLAMP_EARLIEST;
        pair.blocking_reasons =
            WORR_SNAPSHOT_TIMELINE_PAIR_BLOCK_NO_PREVIOUS;
    } else if (before_time == target) {
        previous = previous_active_slot(timeline, before);
        pair.current = slot_ref(timeline, before);
        pair.current_time_us = before_time;
        pair.mode = WORR_SNAPSHOT_TIMELINE_PAIR_EXACT;
        if (previous != UINT32_MAX) {
            pair.previous = slot_ref(timeline, previous);
            pair.previous_time_us =
                timeline->slots[previous].snapshot.server_time_us;
            pair.phase_denominator_us =
                pair.current_time_us - pair.previous_time_us;
            pair.phase_numerator_us = pair.phase_denominator_us;
        }
    } else if (after != UINT32_MAX) {
        pair.previous = slot_ref(timeline, before);
        pair.current = slot_ref(timeline, after);
        pair.previous_time_us = before_time;
        pair.current_time_us = after_time;
        pair.phase_numerator_us = target - before_time;
        pair.phase_denominator_us = after_time - before_time;
        if ((timeline->slots[after].snapshot.discontinuity.flags &
             TIMELINE_BLEND_BLOCK_FLAGS) != 0) {
            pair.mode = WORR_SNAPSHOT_TIMELINE_PAIR_HOLD;
            pair.blocking_reasons =
                WORR_SNAPSHOT_TIMELINE_PAIR_BLOCK_DISCONTINUITY;
        } else {
            pair.mode = WORR_SNAPSHOT_TIMELINE_PAIR_INTERPOLATE;
        }
    } else {
        previous = previous_active_slot(timeline, before);
        pair.current = slot_ref(timeline, before);
        pair.current_time_us = before_time;
        pair.extrapolation_us = target - before_time;
        if (previous != UINT32_MAX) {
            pair.previous = slot_ref(timeline, previous);
            pair.previous_time_us =
                timeline->slots[previous].snapshot.server_time_us;
            pair.phase_denominator_us =
                pair.current_time_us - pair.previous_time_us;
        }
        if (previous == UINT32_MAX) {
            pair.blocking_reasons |=
                WORR_SNAPSHOT_TIMELINE_PAIR_BLOCK_NO_PREVIOUS;
        }
        if (previous != UINT32_MAX && pair.phase_denominator_us == 0) {
            pair.blocking_reasons |=
                WORR_SNAPSHOT_TIMELINE_PAIR_BLOCK_ZERO_INTERVAL;
        }
        if ((timeline->slots[before].snapshot.discontinuity.flags &
             TIMELINE_BLEND_BLOCK_FLAGS) != 0) {
            pair.blocking_reasons |=
                WORR_SNAPSHOT_TIMELINE_PAIR_BLOCK_DISCONTINUITY;
        }
        if (!policy->allow_extrapolation ||
            pair.extrapolation_us > policy->max_extrapolation_us ||
            policy->max_extrapolation_us == 0) {
            pair.blocking_reasons |=
                WORR_SNAPSHOT_TIMELINE_PAIR_BLOCK_POLICY;
        }
        pair.mode = pair.blocking_reasons == 0
                        ? WORR_SNAPSHOT_TIMELINE_PAIR_EXTRAPOLATE
                        : WORR_SNAPSHOT_TIMELINE_PAIR_CLAMP_LATEST;
    }
    if (pair.current.slot != WORR_SNAPSHOT_TIMELINE_REF_NONE_SLOT) {
        pair.discontinuity_flags =
            timeline->slots[pair.current.slot].snapshot.discontinuity.flags;
    }

    saturating_increment(&timeline->selection_count);
    if (pair.mode == WORR_SNAPSHOT_TIMELINE_PAIR_INTERPOLATE)
        saturating_increment(&timeline->interpolation_pair_count);
    else if (pair.mode == WORR_SNAPSHOT_TIMELINE_PAIR_EXTRAPOLATE)
        saturating_increment(&timeline->extrapolation_pair_count);
    else if (pair.mode == WORR_SNAPSHOT_TIMELINE_PAIR_CLAMP_EARLIEST ||
             pair.mode == WORR_SNAPSHOT_TIMELINE_PAIR_CLAMP_LATEST)
        saturating_increment(&timeline->clamped_pair_count);
    *pair_out = pair;
    return WORR_SNAPSHOT_TIMELINE_OK;
}

static bool ref_is_none(worr_snapshot_timeline_ref_v1 ref)
{
    return ref.slot == WORR_SNAPSHOT_TIMELINE_REF_NONE_SLOT &&
           ref.generation == 0;
}

static bool pair_valid(const worr_snapshot_timeline_v1 *timeline,
                       const worr_snapshot_timeline_policy_v1 *policy,
                       const worr_snapshot_timeline_pair_v1 *pair)
{
    const worr_snapshot_timeline_slot_v1 *current;
    const worr_snapshot_timeline_slot_v1 *previous = NULL;
    const bool has_previous = pair && !ref_is_none(pair->previous);

    if (!pair || pair->struct_size != sizeof(*pair) ||
        pair->schema_version != WORR_SNAPSHOT_TIMELINE_VERSION ||
        pair->reserved0 != 0 ||
        pair->mode < WORR_SNAPSHOT_TIMELINE_PAIR_CLAMP_EARLIEST ||
        pair->mode > WORR_SNAPSHOT_TIMELINE_PAIR_CLAMP_LATEST ||
        (pair->blocking_reasons & ~TIMELINE_PAIR_BLOCK_FLAGS) != 0 ||
        pair->timeline_instance != timeline->instance_generation ||
        pair->clock_epoch != timeline->clock.epoch ||
        pair->segment != timeline->active_segment ||
        pair->policy_hash != policy_hash(policy) ||
        !ref_valid_unchecked(timeline, pair->current) ||
        (!has_previous && !ref_is_none(pair->previous)) ||
        (has_previous && !ref_valid_unchecked(timeline, pair->previous))) {
        return false;
    }
    current = &timeline->slots[pair->current.slot];
    if (current->segment != pair->segment ||
        current->snapshot.server_time_us != pair->current_time_us ||
        current->snapshot.discontinuity.flags !=
            pair->discontinuity_flags) {
        return false;
    }
    if (has_previous) {
        previous = &timeline->slots[pair->previous.slot];
        if (previous->segment != pair->segment ||
            previous->publish_serial >= current->publish_serial ||
            previous_active_slot(timeline, pair->current.slot) !=
                pair->previous.slot ||
            previous->snapshot.server_time_us != pair->previous_time_us ||
            pair->previous_time_us > pair->current_time_us) {
            return false;
        }
    } else if (pair->previous_time_us != 0) {
        return false;
    }

    switch (pair->mode) {
    case WORR_SNAPSHOT_TIMELINE_PAIR_CLAMP_EARLIEST:
        return !has_previous && pair->target_time_us < pair->current_time_us &&
               pair->phase_numerator_us == 0 &&
               pair->phase_denominator_us == 0 &&
               pair->extrapolation_us == 0 &&
               pair->blocking_reasons ==
                   WORR_SNAPSHOT_TIMELINE_PAIR_BLOCK_NO_PREVIOUS;
    case WORR_SNAPSHOT_TIMELINE_PAIR_HOLD:
    case WORR_SNAPSHOT_TIMELINE_PAIR_INTERPOLATE:
        if (!has_previous ||
            pair->previous_time_us >= pair->current_time_us ||
            pair->target_time_us <= pair->previous_time_us ||
            pair->target_time_us >= pair->current_time_us ||
            pair->phase_numerator_us !=
                pair->target_time_us - pair->previous_time_us ||
            pair->phase_denominator_us !=
                pair->current_time_us - pair->previous_time_us ||
            pair->extrapolation_us != 0) {
            return false;
        }
        return pair->mode == WORR_SNAPSHOT_TIMELINE_PAIR_INTERPOLATE
                   ? pair->blocking_reasons == 0 &&
                         (current->snapshot.discontinuity.flags &
                          TIMELINE_BLEND_BLOCK_FLAGS) == 0
                   : (pair->blocking_reasons &
                      WORR_SNAPSHOT_TIMELINE_PAIR_BLOCK_DISCONTINUITY) != 0 &&
                         (current->snapshot.discontinuity.flags &
                          TIMELINE_BLEND_BLOCK_FLAGS) != 0;
    case WORR_SNAPSHOT_TIMELINE_PAIR_EXACT:
        if (pair->target_time_us != pair->current_time_us ||
            pair->extrapolation_us != 0 || pair->blocking_reasons != 0)
            return false;
        if (!has_previous)
            return pair->phase_numerator_us == 0 &&
                   pair->phase_denominator_us == 0;
        return pair->phase_denominator_us ==
                   pair->current_time_us - pair->previous_time_us &&
               pair->phase_numerator_us == pair->phase_denominator_us;
    case WORR_SNAPSHOT_TIMELINE_PAIR_EXTRAPOLATE:
    case WORR_SNAPSHOT_TIMELINE_PAIR_CLAMP_LATEST:
        if (pair->target_time_us <= pair->current_time_us ||
            pair->extrapolation_us !=
                pair->target_time_us - pair->current_time_us) {
            return false;
        }
        if (has_previous &&
            pair->phase_denominator_us !=
                pair->current_time_us - pair->previous_time_us) {
            return false;
        }
        if (!has_previous && pair->phase_denominator_us != 0)
            return false;
        return pair->mode == WORR_SNAPSHOT_TIMELINE_PAIR_EXTRAPOLATE
                   ? has_previous && pair->phase_denominator_us != 0 &&
                         pair->blocking_reasons == 0 &&
                         pair->extrapolation_us <=
                             policy->max_extrapolation_us &&
                         policy->allow_extrapolation != 0 &&
                         (current->snapshot.discontinuity.flags &
                          TIMELINE_BLEND_BLOCK_FLAGS) == 0
                   : pair->blocking_reasons != 0;
    default:
        return false;
    }
}

static const worr_snapshot_entity_v2 *find_entity_in_slot(
    const worr_snapshot_timeline_v1 *timeline, uint32_t slot_index,
    uint32_t entity_index)
{
    const worr_snapshot_timeline_slot_v1 *slot =
        &timeline->slots[slot_index];
    const worr_snapshot_entity_v2 *entities =
        slot_entities(timeline, slot_index);
    uint32_t low = 0;
    uint32_t high = slot->entity_count;

    while (low < high) {
        const uint32_t middle = low + (high - low) / 2u;
        const uint32_t index =
            entities[middle].generation.identity.index;
        if (index < entity_index)
            low = middle + 1u;
        else
            high = middle;
    }
    if (low < slot->entity_count &&
        entities[low].generation.identity.index == entity_index) {
        return &entities[low];
    }
    return NULL;
}

static bool float_semantic_equal(float left, float right)
{
    return left == right;
}

static bool entity_discrete_equal(const worr_snapshot_entity_v2 *left,
                                  const worr_snapshot_entity_v2 *right)
{
    uint32_t i;
    if (left->component_mask != right->component_mask ||
        left->flags != right->flags || left->frame != right->frame ||
        left->sound != right->sound || left->skin != right->skin ||
        left->solid != right->solid || left->effects != right->effects ||
        left->renderfx != right->renderfx ||
        !float_semantic_equal(left->alpha, right->alpha) ||
        !float_semantic_equal(left->scale, right->scale) ||
        !float_semantic_equal(left->loop_volume, right->loop_volume) ||
        !float_semantic_equal(left->loop_attenuation,
                              right->loop_attenuation) ||
        left->owner.index != right->owner.index ||
        left->owner.generation != right->owner.generation ||
        left->old_frame != right->old_frame ||
        left->instance_bits != right->instance_bits) {
        return false;
    }
    for (i = 0; i < 4; ++i) {
        if (left->model_index[i] != right->model_index[i])
            return false;
    }
    return true;
}

static double shortest_angle_delta(double from, double to)
{
    double delta = fmod(to - from, 360.0);
    if (delta < -180.0)
        delta += 360.0;
    else if (delta >= 180.0)
        delta -= 360.0;
    return delta;
}

static float normalized_angle(double value)
{
    double normalized = fmod(value, 360.0);
    if (normalized < 0.0)
        normalized += 360.0;
    if (normalized == 0.0)
        normalized = 0.0;
    return (float)normalized;
}

static bool motion_parameters(
    const worr_snapshot_entity_v2 *previous,
    const worr_snapshot_entity_v2 *current,
    uint64_t interval_us,
    const worr_snapshot_timeline_policy_v1 *policy,
    float linear_velocity[3], float angular_velocity[3],
    uint32_t *blocking_reasons)
{
    const double seconds = (double)interval_us / 1000000.0;
    double distance_squared = 0.0;
    double linear_speed_squared = 0.0;
    double angular_speed_squared = 0.0;
    uint32_t i;

    if (interval_us == 0) {
        *blocking_reasons |=
            WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_SNAPSHOT;
        return false;
    }
    for (i = 0; i < 3; ++i) {
        const double displacement =
            (double)current->origin[i] - previous->origin[i];
        const double angle = shortest_angle_delta(previous->angles[i],
                                                  current->angles[i]);
        const double linear = displacement / seconds;
        const double angular = angle / seconds;
        distance_squared += displacement * displacement;
        linear_speed_squared += linear * linear;
        angular_speed_squared += angular * angular;
        linear_velocity[i] = (float)linear;
        angular_velocity[i] = (float)angular;
    }
    if (distance_squared >
        (double)policy->teleport_distance * policy->teleport_distance) {
        *blocking_reasons |=
            WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_TELEPORT;
    }
    if (linear_speed_squared >
        (double)policy->max_linear_velocity *
            policy->max_linear_velocity) {
        *blocking_reasons |=
            WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_LINEAR_SPEED;
    }
    if (angular_speed_squared >
        (double)policy->max_angular_velocity *
            policy->max_angular_velocity) {
        *blocking_reasons |=
            WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_ANGULAR_SPEED;
    }
    return (*blocking_reasons &
            (WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_TELEPORT |
             WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_LINEAR_SPEED |
             WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_ANGULAR_SPEED |
             WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_SNAPSHOT)) == 0;
}

static void sample_direct(worr_snapshot_timeline_entity_sample_v1 *sample,
                          const worr_snapshot_entity_v2 *entity,
                          uint32_t mode)
{
    sample->visible = 1;
    sample->mode = mode;
    sample->entity = *entity;
}

worr_snapshot_timeline_result_v1 Worr_SnapshotTimelineSampleEntityV1(
    worr_snapshot_timeline_v1 *timeline,
    const worr_snapshot_timeline_policy_v1 *policy,
    const worr_snapshot_timeline_pair_v1 *pair,
    uint32_t entity_index,
    worr_snapshot_timeline_entity_sample_v1 *sample_out)
{
    worr_snapshot_timeline_entity_sample_v1 sample;
    const worr_snapshot_entity_v2 *previous_entity = NULL;
    const worr_snapshot_entity_v2 *current_entity;
    bool has_previous;
    bool interpolate_time;
    uint32_t i;

    if (!timeline_valid(timeline))
        return WORR_SNAPSHOT_TIMELINE_INVALID_TIMELINE;
    if (!policy || !pair || !sample_out ||
        entity_index >= timeline->max_entities) {
        return WORR_SNAPSHOT_TIMELINE_INVALID_ARGUMENT;
    }
    if (!external_ranges_disjoint(timeline, policy, sizeof(*policy),
                                  pair, sizeof(*pair), sample_out,
                                  sizeof(*sample_out))) {
        return WORR_SNAPSHOT_TIMELINE_OVERLAP;
    }
    if (!policy_valid(policy))
        return WORR_SNAPSHOT_TIMELINE_INVALID_POLICY;
    if (ref_valid_unchecked(timeline, pair->current) &&
        !slot_content_valid(timeline, pair->current.slot)) {
        return WORR_SNAPSHOT_TIMELINE_CORRUPT;
    }
    if (!ref_is_none(pair->previous) &&
        ref_valid_unchecked(timeline, pair->previous) &&
        !slot_content_valid(timeline, pair->previous.slot)) {
        return WORR_SNAPSHOT_TIMELINE_CORRUPT;
    }
    if (!pair_valid(timeline, policy, pair))
        return WORR_SNAPSHOT_TIMELINE_STALE_REF;

    memset(&sample, 0, sizeof(sample));
    sample.struct_size = sizeof(sample);
    sample.schema_version = WORR_SNAPSHOT_TIMELINE_VERSION;
    sample.entity_index = entity_index;
    sample.visibility = WORR_SNAPSHOT_TIMELINE_VISIBILITY_ABSENT;
    current_entity = find_entity_in_slot(timeline, pair->current.slot,
                                         entity_index);
    has_previous = !ref_is_none(pair->previous);
    if (has_previous) {
        previous_entity = find_entity_in_slot(timeline, pair->previous.slot,
                                              entity_index);
    }
    interpolate_time =
        pair->mode == WORR_SNAPSHOT_TIMELINE_PAIR_INTERPOLATE ||
        pair->mode == WORR_SNAPSHOT_TIMELINE_PAIR_HOLD;

    if (!previous_entity && !current_entity) {
        sample.blocking_reasons =
            WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_MISSING;
        *sample_out = sample;
        return WORR_SNAPSHOT_TIMELINE_OK;
    }
    if (!previous_entity && current_entity) {
        sample.visibility =
            WORR_SNAPSHOT_TIMELINE_VISIBILITY_ADDED_AT_CURRENT;
        sample.blocking_reasons =
            WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_MISSING;
        if (!interpolate_time) {
            sample_direct(&sample, current_entity,
                          WORR_SNAPSHOT_TIMELINE_ENTITY_CURRENT);
        }
        *sample_out = sample;
        return WORR_SNAPSHOT_TIMELINE_OK;
    }
    if (previous_entity && !current_entity) {
        sample.visibility =
            WORR_SNAPSHOT_TIMELINE_VISIBILITY_REMOVED_AT_CURRENT;
        sample.blocking_reasons =
            WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_MISSING;
        if (interpolate_time) {
            sample_direct(&sample, previous_entity,
                          WORR_SNAPSHOT_TIMELINE_ENTITY_PREVIOUS);
        }
        *sample_out = sample;
        return WORR_SNAPSHOT_TIMELINE_OK;
    }

    if (!generation_equal(previous_entity->generation,
                          current_entity->generation)) {
        sample.visibility =
            WORR_SNAPSHOT_TIMELINE_VISIBILITY_GENERATION_REPLACED;
        sample.blocking_reasons =
            WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_GENERATION;
        sample_direct(&sample,
                      interpolate_time ? previous_entity : current_entity,
                      interpolate_time
                          ? WORR_SNAPSHOT_TIMELINE_ENTITY_PREVIOUS
                          : WORR_SNAPSHOT_TIMELINE_ENTITY_CURRENT);
        *sample_out = sample;
        return WORR_SNAPSHOT_TIMELINE_OK;
    }

    sample.compatible_component_mask =
        previous_entity->component_mask & current_entity->component_mask;
    sample.visibility = WORR_SNAPSHOT_TIMELINE_VISIBILITY_PRESENT;
    if (previous_entity->component_mask != current_entity->component_mask) {
        sample.blocking_reasons |=
            WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_COMPONENT;
    }
    if (!entity_discrete_equal(previous_entity, current_entity)) {
        sample.blocking_reasons |=
            WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_DISCRETE_TRANSITION;
    }

    if (pair->mode == WORR_SNAPSHOT_TIMELINE_PAIR_HOLD) {
        sample.blocking_reasons |=
            WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_SNAPSHOT;
        sample_direct(&sample, previous_entity,
                      WORR_SNAPSHOT_TIMELINE_ENTITY_PREVIOUS);
    } else if (pair->mode == WORR_SNAPSHOT_TIMELINE_PAIR_INTERPOLATE) {
        const double phase = (double)pair->phase_numerator_us /
                             pair->phase_denominator_us;
        uint32_t motion_blocks = 0;
        if (!motion_parameters(previous_entity, current_entity,
                               pair->phase_denominator_us, policy,
                               sample.linear_velocity,
                               sample.angular_velocity, &motion_blocks)) {
            sample.blocking_reasons |= motion_blocks;
            memset(sample.linear_velocity, 0,
                   sizeof(sample.linear_velocity));
            memset(sample.angular_velocity, 0,
                   sizeof(sample.angular_velocity));
            sample_direct(&sample, previous_entity,
                          WORR_SNAPSHOT_TIMELINE_ENTITY_PREVIOUS);
        } else {
            sample_direct(&sample, previous_entity,
                          WORR_SNAPSHOT_TIMELINE_ENTITY_INTERPOLATED);
            for (i = 0; i < 3; ++i) {
                const double origin = previous_entity->origin[i] +
                    ((double)current_entity->origin[i] -
                     previous_entity->origin[i]) * phase;
                const double angle = previous_entity->angles[i] +
                    shortest_angle_delta(previous_entity->angles[i],
                                         current_entity->angles[i]) * phase;
                sample.entity.origin[i] = (float)origin;
                sample.entity.angles[i] = normalized_angle(angle);
                if ((sample.compatible_component_mask &
                     WORR_SNAPSHOT_ENTITY_INTERPOLATION) != 0) {
                    sample.entity.old_origin[i] = (float)(
                        previous_entity->old_origin[i] +
                        ((double)current_entity->old_origin[i] -
                         previous_entity->old_origin[i]) * phase);
                }
            }
            sample.interpolated_component_mask =
                WORR_SNAPSHOT_ENTITY_TRANSFORM |
                (sample.compatible_component_mask &
                 WORR_SNAPSHOT_ENTITY_INTERPOLATION);
        }
    } else if (pair->mode == WORR_SNAPSHOT_TIMELINE_PAIR_EXTRAPOLATE) {
        const double extra_seconds =
            (double)pair->extrapolation_us / 1000000.0;
        uint32_t motion_blocks = 0;
        sample_direct(&sample, current_entity,
                      WORR_SNAPSHOT_TIMELINE_ENTITY_CURRENT);
        if (!motion_parameters(previous_entity, current_entity,
                               pair->phase_denominator_us, policy,
                               sample.linear_velocity,
                               sample.angular_velocity, &motion_blocks)) {
            sample.blocking_reasons |= motion_blocks;
            memset(sample.linear_velocity, 0,
                   sizeof(sample.linear_velocity));
            memset(sample.angular_velocity, 0,
                   sizeof(sample.angular_velocity));
        } else {
            sample.mode = WORR_SNAPSHOT_TIMELINE_ENTITY_EXTRAPOLATED;
            for (i = 0; i < 3; ++i) {
                sample.entity.origin[i] = (float)(
                    current_entity->origin[i] +
                    (double)sample.linear_velocity[i] * extra_seconds);
                sample.entity.angles[i] = normalized_angle(
                    current_entity->angles[i] +
                    (double)sample.angular_velocity[i] * extra_seconds);
                if ((sample.compatible_component_mask &
                     WORR_SNAPSHOT_ENTITY_INTERPOLATION) != 0) {
                    sample.entity.old_origin[i] = (float)(
                        current_entity->old_origin[i] +
                        (double)sample.linear_velocity[i] * extra_seconds);
                }
            }
            sample.extrapolated_component_mask =
                WORR_SNAPSHOT_ENTITY_TRANSFORM |
                (sample.compatible_component_mask &
                 WORR_SNAPSHOT_ENTITY_INTERPOLATION);
        }
    } else {
        if ((pair->blocking_reasons &
             WORR_SNAPSHOT_TIMELINE_PAIR_BLOCK_POLICY) != 0) {
            sample.blocking_reasons |=
                WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_POLICY;
        }
        sample_direct(&sample, current_entity,
                      WORR_SNAPSHOT_TIMELINE_ENTITY_CURRENT);
    }

    *sample_out = sample;
    return WORR_SNAPSHOT_TIMELINE_OK;
}

static bool event_same_identity(const worr_snapshot_event_ref_v2 *left,
                                const worr_snapshot_event_ref_v2 *right)
{
    if (left->provenance != right->provenance)
        return false;
    if (left->provenance == WORR_SNAPSHOT_EVENT_PROVENANCE_AUTHORITY)
        return event_id_equal(left->authority_id, right->authority_id);
    return left->semantic_version == right->semantic_version &&
           left->semantic_hash == right->semantic_hash;
}

static uint64_t active_retention_floor(
    const worr_snapshot_timeline_v1 *timeline)
{
    uint64_t floor = timeline->next_publish_serial;
    uint32_t i;
    for (i = 0; i < timeline->slot_capacity; ++i) {
        const worr_snapshot_timeline_slot_v1 *slot = &timeline->slots[i];
        if (slot->committed && slot->segment == timeline->active_segment &&
            slot->publish_serial < floor) {
            floor = slot->publish_serial;
        }
    }
    return floor;
}

static uint64_t event_dedup_hash(
    const worr_snapshot_event_ref_v2 *event)
{
    uint64_t hash = begin_hash(UINT32_C(0x45564431)); /* EVD1 */
    hash = hash_u16(hash, event->provenance);
    if (event->provenance == WORR_SNAPSHOT_EVENT_PROVENANCE_AUTHORITY) {
        hash = hash_u32(hash, event->authority_id.stream_epoch);
        return hash_u32(hash, event->authority_id.sequence);
    }
    hash = hash_u32(hash, event->semantic_version);
    return hash_u64(hash, event->semantic_hash);
}

worr_snapshot_timeline_result_v1 Worr_SnapshotTimelineEventCursorBeginV1(
    const worr_snapshot_timeline_v1 *timeline,
    worr_snapshot_timeline_event_cursor_v1 *cursor_out)
{
    worr_snapshot_timeline_event_cursor_v1 cursor;

    if (!timeline_valid(timeline))
        return WORR_SNAPSHOT_TIMELINE_INVALID_TIMELINE;
    if (!cursor_out)
        return WORR_SNAPSHOT_TIMELINE_INVALID_ARGUMENT;
    if (!output_disjoint(timeline, cursor_out, sizeof(*cursor_out)))
        return WORR_SNAPSHOT_TIMELINE_OVERLAP;

    memset(&cursor, 0, sizeof(cursor));
    cursor.struct_size = sizeof(cursor);
    cursor.schema_version = WORR_SNAPSHOT_TIMELINE_VERSION;
    cursor.timeline_instance = timeline->instance_generation;
    cursor.segment = timeline->active_segment;
    cursor.retention_floor_serial = active_retention_floor(timeline);
    cursor.publish_serial = cursor.retention_floor_serial;
    *cursor_out = cursor;
    return WORR_SNAPSHOT_TIMELINE_OK;
}

worr_snapshot_timeline_result_v1 Worr_SnapshotTimelineEventNextV1(
    worr_snapshot_timeline_v1 *timeline,
    const worr_snapshot_timeline_event_cursor_v1 *cursor,
    worr_snapshot_timeline_event_cursor_v1 *next_cursor_out,
    worr_snapshot_timeline_event_observation_v1 *observation_out)
{
    worr_snapshot_timeline_event_cursor_v1 next;
    worr_snapshot_timeline_event_observation_v1 observation;
    const worr_snapshot_timeline_slot_v1 *slot;
    const worr_snapshot_event_ref_v2 *events;
    const worr_snapshot_event_ref_v2 *event;
    uint64_t floor;
    uint64_t serial;
    uint64_t first_match = 0;
    uint32_t event_index;
    uint32_t slot_index;
    uint32_t retained_matches = 0;
    uint32_t i;

    if (!timeline_valid(timeline))
        return WORR_SNAPSHOT_TIMELINE_INVALID_TIMELINE;
    if (!cursor || !next_cursor_out || !observation_out)
        return WORR_SNAPSHOT_TIMELINE_INVALID_ARGUMENT;
    if (!external_ranges_disjoint(
            timeline, cursor, sizeof(*cursor), next_cursor_out,
            sizeof(*next_cursor_out), observation_out,
            sizeof(*observation_out))) {
        return WORR_SNAPSHOT_TIMELINE_OVERLAP;
    }
    if (cursor->struct_size != sizeof(*cursor) ||
        cursor->schema_version != WORR_SNAPSHOT_TIMELINE_VERSION ||
        cursor->reserved0 != 0 || cursor->retention_floor_serial == 0 ||
        cursor->publish_serial < cursor->retention_floor_serial) {
        return WORR_SNAPSHOT_TIMELINE_INVALID_ARGUMENT;
    }
    if (!timeline_contents_valid(timeline))
        return WORR_SNAPSHOT_TIMELINE_CORRUPT;
    if (cursor->timeline_instance != timeline->instance_generation ||
        cursor->segment != timeline->active_segment) {
        return WORR_SNAPSHOT_TIMELINE_CURSOR_STALE;
    }

    floor = active_retention_floor(timeline);
    if (cursor->retention_floor_serial > floor)
        return WORR_SNAPSHOT_TIMELINE_CURSOR_STALE;
    if (cursor->publish_serial < floor)
        return WORR_SNAPSHOT_TIMELINE_CURSOR_OVERRUN;
    if (cursor->publish_serial > timeline->next_publish_serial)
        return WORR_SNAPSHOT_TIMELINE_CURSOR_STALE;
    if (cursor->publish_serial == timeline->next_publish_serial &&
        cursor->event_index != 0) {
        return WORR_SNAPSHOT_TIMELINE_CURSOR_STALE;
    }
    serial = cursor->publish_serial;
    event_index = cursor->event_index;
    for (;;) {
        if (serial == timeline->next_publish_serial)
            return WORR_SNAPSHOT_TIMELINE_NOT_FOUND;
        slot = find_serial_const(timeline, serial, &slot_index);
        if (!slot)
            return serial < floor
                       ? WORR_SNAPSHOT_TIMELINE_CURSOR_OVERRUN
                       : WORR_SNAPSHOT_TIMELINE_CURSOR_STALE;
        if (slot->segment != timeline->active_segment)
            return WORR_SNAPSHOT_TIMELINE_CURSOR_STALE;
        if (event_index > slot->event_ref_count)
            return WORR_SNAPSHOT_TIMELINE_CURSOR_STALE;
        if (event_index < slot->event_ref_count)
            break;
        if (serial == UINT64_MAX)
            return WORR_SNAPSHOT_TIMELINE_SERIAL_EXHAUSTED;
        ++serial;
        event_index = 0;
    }

    events = slot_events(timeline, slot_index);
    event = &events[event_index];
    for (i = 0; i < timeline->slot_capacity; ++i) {
        const worr_snapshot_timeline_slot_v1 *candidate =
            &timeline->slots[i];
        const worr_snapshot_event_ref_v2 *candidate_events;
        uint32_t candidate_index;
        if (!candidate->committed ||
            candidate->segment != timeline->active_segment ||
            candidate->publish_serial > serial) {
            continue;
        }
        candidate_events = slot_events(timeline, i);
        for (candidate_index = 0;
             candidate_index < candidate->event_ref_count;
             ++candidate_index) {
            if (candidate->publish_serial == serial &&
                candidate_index >= event_index) {
                break;
            }
            if (event_same_identity(&candidate_events[candidate_index],
                                    event)) {
                if (first_match == 0 ||
                    candidate->publish_serial < first_match) {
                    first_match = candidate->publish_serial;
                }
                if (retained_matches != UINT32_MAX)
                    ++retained_matches;
            }
        }
    }

    memset(&observation, 0, sizeof(observation));
    observation.struct_size = sizeof(observation);
    observation.schema_version = WORR_SNAPSHOT_TIMELINE_VERSION;
    observation.snapshot_ref = slot_ref(timeline, slot_index);
    observation.snapshot_id = slot->snapshot.snapshot_id;
    observation.publish_serial = serial;
    observation.dedup_key_hash = event_dedup_hash(event);
    observation.first_match_publish_serial =
        first_match != 0 ? first_match : serial;
    observation.event_index = event_index;
    observation.dedup_kind =
        event->provenance == WORR_SNAPSHOT_EVENT_PROVENANCE_AUTHORITY
            ? WORR_SNAPSHOT_TIMELINE_EVENT_DEDUP_AUTHORITY_ID
            : WORR_SNAPSHOT_TIMELINE_EVENT_DEDUP_LEGACY_SEMANTIC;
    observation.retained_match_count = retained_matches;
    if (retained_matches != 0)
        observation.flags |= WORR_SNAPSHOT_TIMELINE_EVENT_RETAINED_MATCH;
    if (timeline->active_segment_first_serial != 0 &&
        floor == timeline->active_segment_first_serial) {
        observation.flags |= WORR_SNAPSHOT_TIMELINE_EVENT_HISTORY_COMPLETE;
    }
    observation.event_ref = *event;

    next = *cursor;
    next.publish_serial = serial;
    next.event_index = event_index + 1u;
    if (next.event_index == slot->event_ref_count) {
        if (next.publish_serial == UINT64_MAX)
            return WORR_SNAPSHOT_TIMELINE_SERIAL_EXHAUSTED;
        ++next.publish_serial;
        next.event_index = 0;
    }

    saturating_increment(&timeline->event_observation_count);
    saturating_add(&timeline->event_retained_match_count,
                   retained_matches);
    *next_cursor_out = next;
    *observation_out = observation;
    return WORR_SNAPSHOT_TIMELINE_OK;
}

worr_snapshot_timeline_result_v1 Worr_SnapshotTimelineGetStatsV1(
    const worr_snapshot_timeline_v1 *timeline,
    worr_snapshot_timeline_stats_v1 *stats_out)
{
    worr_snapshot_timeline_stats_v1 stats;

    if (!timeline_valid(timeline))
        return WORR_SNAPSHOT_TIMELINE_INVALID_TIMELINE;
    if (!stats_out)
        return WORR_SNAPSHOT_TIMELINE_INVALID_ARGUMENT;
    if (!output_disjoint(timeline, stats_out, sizeof(*stats_out)))
        return WORR_SNAPSHOT_TIMELINE_OVERLAP;

    memset(&stats, 0, sizeof(stats));
    stats.struct_size = sizeof(stats);
    stats.schema_version = WORR_SNAPSHOT_TIMELINE_VERSION;
    stats.slot_capacity = timeline->slot_capacity;
    stats.occupied = timeline->occupied;
    stats.occupied_high_water = timeline->occupied_high_water;
    stats.entity_high_water = timeline->entity_high_water;
    stats.area_high_water = timeline->area_high_water;
    stats.event_high_water = timeline->event_high_water;
    stats.publish_count = timeline->publish_count;
    stats.overwrite_count = timeline->overwrite_count;
    stats.segment_count = timeline->segment_count;
    stats.reset_count = timeline->reset_count;
    stats.clock_update_count = timeline->clock_update_count;
    stats.clock_seek_count = timeline->clock_seek_count;
    stats.selection_count = timeline->selection_count;
    stats.interpolation_pair_count =
        timeline->interpolation_pair_count;
    stats.extrapolation_pair_count =
        timeline->extrapolation_pair_count;
    stats.clamped_pair_count = timeline->clamped_pair_count;
    stats.event_observation_count = timeline->event_observation_count;
    stats.event_retained_match_count =
        timeline->event_retained_match_count;
    *stats_out = stats;
    return WORR_SNAPSHOT_TIMELINE_OK;
}

worr_snapshot_timeline_result_v1 Worr_SnapshotTimelineHashesV1(
    const worr_snapshot_timeline_v1 *timeline,
    worr_snapshot_timeline_hashes_v1 *hashes_out)
{
    worr_snapshot_timeline_hashes_v1 hashes;
    uint64_t retained;
    uint64_t clock;
    uint64_t telemetry;
    uint64_t last_serial = 0;
    uint32_t ordinal;

    if (!timeline_valid(timeline))
        return WORR_SNAPSHOT_TIMELINE_INVALID_TIMELINE;
    if (!hashes_out)
        return WORR_SNAPSHOT_TIMELINE_INVALID_ARGUMENT;
    if (!output_disjoint(timeline, hashes_out, sizeof(*hashes_out)))
        return WORR_SNAPSHOT_TIMELINE_OVERLAP;
    if (!timeline_contents_valid(timeline))
        return WORR_SNAPSHOT_TIMELINE_CORRUPT;

    retained = begin_hash(UINT32_C(0x52455431)); /* RET1 */
    retained = hash_u32(retained, timeline->occupied);
    for (ordinal = 0; ordinal < timeline->occupied; ++ordinal) {
        const worr_snapshot_timeline_slot_v1 *selected = NULL;
        uint64_t selected_serial = UINT64_MAX;
        uint32_t selected_index = UINT32_MAX;
        uint32_t i;
        worr_snapshot_projection_view_v2 view;
        worr_snapshot_projection_hashes_v2 projection;

        for (i = 0; i < timeline->slot_capacity; ++i) {
            const worr_snapshot_timeline_slot_v1 *slot =
                &timeline->slots[i];
            if (slot->committed && slot->publish_serial > last_serial &&
                slot->publish_serial < selected_serial) {
                selected = slot;
                selected_serial = slot->publish_serial;
                selected_index = i;
            }
        }
        if (!selected)
            return WORR_SNAPSHOT_TIMELINE_CORRUPT;
        memset(&view, 0, sizeof(view));
        view.struct_size = sizeof(view);
        view.schema_version = WORR_SNAPSHOT_PROJECTION_VERSION;
        view.snapshot = &selected->snapshot;
        view.player = &selected->player;
        view.entities = slot_entities(timeline, selected_index);
        view.area_bytes = slot_area(timeline, selected_index);
        view.event_refs = slot_events(timeline, selected_index);
        view.entity_count = selected->entity_count;
        view.area_byte_count = selected->area_byte_count;
        view.event_ref_count = selected->event_ref_count;
        if (!projection_semantics_valid(&view, timeline->max_entities,
                                        &projection)) {
            return WORR_SNAPSHOT_TIMELINE_CORRUPT;
        }
        retained = hash_u64(retained, selected->publish_serial);
        retained = hash_u64(retained, selected->receive_time_us);
        retained = hash_u64(retained, selected->segment);
        retained = hash_u64(retained, projection.endpoint_hash);
        retained = hash_u32(retained, selected->entity_count);
        retained = hash_u32(retained, selected->area_byte_count);
        retained = hash_u32(retained, selected->event_ref_count);
        last_serial = selected_serial;
    }

    clock = begin_hash(UINT32_C(0x434c4b31)); /* CLK1 */
    clock = hash_u64(clock, timeline->clock.epoch);
    clock = hash_u64(clock, timeline->clock.host_time_us);
    clock = hash_u64(clock, timeline->clock.render_time_us);
    clock = hash_u32(clock, timeline->clock.rate_q16);
    clock = hash_u16(clock, timeline->clock.paused);
    clock = hash_u16(clock, timeline->clock.initialized);
    clock = hash_u32(clock, timeline->clock.last_reset_reason);
    clock = hash_u32(clock, timeline->clock.fractional_q16);

    telemetry = begin_hash(UINT32_C(0x54454c31)); /* TEL1 */
    telemetry = hash_u32(telemetry, timeline->slot_capacity);
    telemetry = hash_u32(telemetry, timeline->occupied);
    telemetry = hash_u64(telemetry, timeline->next_publish_serial);
    telemetry = hash_u64(telemetry, timeline->latest_publish_serial);
    telemetry = hash_u64(telemetry, timeline->instance_generation);
    telemetry = hash_u64(telemetry, timeline->active_segment);
    telemetry = hash_u64(telemetry, timeline->active_segment_first_serial);
    telemetry = hash_u32(telemetry, timeline->occupied_high_water);
    telemetry = hash_u32(telemetry, timeline->entity_high_water);
    telemetry = hash_u32(telemetry, timeline->area_high_water);
    telemetry = hash_u32(telemetry, timeline->event_high_water);
    telemetry = hash_u64(telemetry, timeline->publish_count);
    telemetry = hash_u64(telemetry, timeline->overwrite_count);
    telemetry = hash_u64(telemetry, timeline->segment_count);
    telemetry = hash_u64(telemetry, timeline->reset_count);
    telemetry = hash_u64(telemetry, timeline->clock_update_count);
    telemetry = hash_u64(telemetry, timeline->clock_seek_count);
    telemetry = hash_u64(telemetry, timeline->selection_count);
    telemetry = hash_u64(telemetry,
                         timeline->interpolation_pair_count);
    telemetry = hash_u64(telemetry,
                         timeline->extrapolation_pair_count);
    telemetry = hash_u64(telemetry, timeline->clamped_pair_count);
    telemetry = hash_u64(telemetry, timeline->event_observation_count);
    telemetry = hash_u64(telemetry,
                         timeline->event_retained_match_count);

    memset(&hashes, 0, sizeof(hashes));
    hashes.struct_size = sizeof(hashes);
    hashes.schema_version = WORR_SNAPSHOT_TIMELINE_VERSION;
    hashes.retained_content_hash = retained;
    hashes.clock_hash = clock;
    hashes.telemetry_hash = telemetry;
    *hashes_out = hashes;
    return WORR_SNAPSHOT_TIMELINE_OK;
}
