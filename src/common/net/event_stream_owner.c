/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/event_stream_owner.h"

#include <string.h>

static bool ranges_overlap(const void *left, size_t left_bytes,
                           const void *right, size_t right_bytes)
{
    uintptr_t left_begin;
    uintptr_t right_begin;
    uintptr_t left_end;
    uintptr_t right_end;

    if (left == NULL || right == NULL || left_bytes == 0 ||
        right_bytes == 0) {
        return false;
    }
    left_begin = (uintptr_t)left;
    right_begin = (uintptr_t)right;
    if (left_bytes > UINTPTR_MAX - left_begin ||
        right_bytes > UINTPTR_MAX - right_begin) {
        return true;
    }
    left_end = left_begin + left_bytes;
    right_end = right_begin + right_bytes;
    return left_begin < right_end && right_begin < left_end;
}

static bool descriptor_zero(
    const worr_event_stream_descriptor_v1 *descriptor)
{
    const worr_event_stream_descriptor_v1 zero = { 0 };
    return memcmp(descriptor, &zero, sizeof(zero)) == 0;
}

static bool generation_advance(worr_event_stream_owner_v1 *owner)
{
    if (owner->mutation_generation == UINT64_MAX)
        return false;
    ++owner->mutation_generation;
    if (owner->mutation_generation == UINT64_MAX) {
        owner->state_flags |=
            WORR_EVENT_STREAM_OWNER_GENERATION_EXHAUSTED;
    }
    return true;
}

bool Worr_EventStreamOwnerInitV1(
    worr_event_stream_owner_v1 *owner_out,
    uint64_t connection_owner_id)
{
    worr_event_stream_owner_v1 owner;

    if (owner_out == NULL || connection_owner_id == 0)
        return false;
    memset(&owner, 0, sizeof(owner));
    owner.struct_size = sizeof(owner);
    owner.schema_version = WORR_EVENT_STREAM_OWNER_ABI_VERSION;
    owner.state_flags = WORR_EVENT_STREAM_OWNER_INITIALIZED;
    owner.mutation_generation = 1;
    owner.connection_owner_id = connection_owner_id;
    *owner_out = owner;
    return true;
}

bool Worr_EventStreamOwnerValidateV1(
    const worr_event_stream_owner_v1 *owner)
{
    const uint16_t known_flags =
        WORR_EVENT_STREAM_OWNER_INITIALIZED |
        WORR_EVENT_STREAM_OWNER_ACTIVE |
        WORR_EVENT_STREAM_OWNER_REQUIRES_RESYNC |
        WORR_EVENT_STREAM_OWNER_GENERATION_EXHAUSTED;
    const bool active = owner != NULL &&
        (owner->state_flags & WORR_EVENT_STREAM_OWNER_ACTIVE) != 0;
    const bool resync = owner != NULL &&
        (owner->state_flags &
         WORR_EVENT_STREAM_OWNER_REQUIRES_RESYNC) != 0;
    const bool exhausted = owner != NULL &&
        (owner->state_flags &
         WORR_EVENT_STREAM_OWNER_GENERATION_EXHAUSTED) != 0;

    if (owner == NULL || owner->struct_size != sizeof(*owner) ||
        owner->schema_version != WORR_EVENT_STREAM_OWNER_ABI_VERSION ||
        (owner->state_flags & ~known_flags) != 0 ||
        (owner->state_flags & WORR_EVENT_STREAM_OWNER_INITIALIZED) == 0 ||
        owner->reserved0 != 0 || owner->mutation_generation == 0 ||
        owner->connection_owner_id == 0 ||
        exhausted != (owner->mutation_generation == UINT64_MAX) ||
        (active && resync)) {
        return false;
    }
    if (active) {
        return Worr_EventStreamDescriptorValidateV1(&owner->descriptor) &&
               owner->epoch_high_water == owner->descriptor.stream_epoch;
    }
    if (!descriptor_zero(&owner->descriptor))
        return false;
    if (owner->epoch_high_water == 0)
        return !resync;
    return resync;
}

worr_event_stream_owner_result_v1 Worr_EventStreamOwnerObserveV1(
    worr_event_stream_owner_v1 *owner,
    const worr_event_stream_descriptor_v1 *descriptor)
{
    worr_event_stream_owner_v1 staged;

    if (owner == NULL || descriptor == NULL ||
        ranges_overlap(owner, sizeof(*owner), descriptor,
                       sizeof(*descriptor))) {
        return WORR_EVENT_STREAM_OWNER_INVALID_ARGUMENT;
    }
    if (!Worr_EventStreamOwnerValidateV1(owner))
        return WORR_EVENT_STREAM_OWNER_INVALID_STATE;
    if (!Worr_EventStreamDescriptorValidateV1(descriptor))
        return WORR_EVENT_STREAM_OWNER_INVALID_DESCRIPTOR;
    if ((owner->state_flags & WORR_EVENT_STREAM_OWNER_ACTIVE) != 0 &&
        Worr_EventStreamDescriptorEqualV1(
            &owner->descriptor, descriptor)) {
        return WORR_EVENT_STREAM_OWNER_EXACT_DUPLICATE;
    }
    if (descriptor->stream_epoch == owner->epoch_high_water)
        return WORR_EVENT_STREAM_OWNER_CONFLICT;
    if (descriptor->stream_epoch < owner->epoch_high_water)
        return WORR_EVENT_STREAM_OWNER_WRONG_EPOCH;
    /* Reserve the final generation for a fail-closed resync transition after
     * an irreversible consumer callback. */
    if ((owner->state_flags &
         WORR_EVENT_STREAM_OWNER_GENERATION_EXHAUSTED) != 0 ||
        owner->mutation_generation >= UINT64_MAX - 1u) {
        return WORR_EVENT_STREAM_OWNER_GENERATION_LIMIT;
    }

    staged = *owner;
    if (!generation_advance(&staged))
        return WORR_EVENT_STREAM_OWNER_GENERATION_LIMIT;
    staged.descriptor = *descriptor;
    staged.epoch_high_water = descriptor->stream_epoch;
    staged.state_flags |= WORR_EVENT_STREAM_OWNER_ACTIVE;
    staged.state_flags &=
        (uint16_t)~WORR_EVENT_STREAM_OWNER_REQUIRES_RESYNC;
    *owner = staged;
    return WORR_EVENT_STREAM_OWNER_ACTIVATED;
}

bool Worr_EventStreamOwnerRequireResyncV1(
    worr_event_stream_owner_v1 *owner)
{
    worr_event_stream_owner_v1 staged;

    if (!Worr_EventStreamOwnerValidateV1(owner) ||
        owner->epoch_high_water == 0) {
        return false;
    }
    if ((owner->state_flags &
         WORR_EVENT_STREAM_OWNER_REQUIRES_RESYNC) != 0) {
        return true;
    }
    if ((owner->state_flags &
         WORR_EVENT_STREAM_OWNER_GENERATION_EXHAUSTED) != 0) {
        return false;
    }

    staged = *owner;
    if (!generation_advance(&staged))
        return false;
    memset(&staged.descriptor, 0, sizeof(staged.descriptor));
    staged.state_flags &=
        (uint16_t)~WORR_EVENT_STREAM_OWNER_ACTIVE;
    staged.state_flags |= WORR_EVENT_STREAM_OWNER_REQUIRES_RESYNC;
    *owner = staged;
    return true;
}
