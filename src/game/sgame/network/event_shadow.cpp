/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "event_shadow.hpp"

#include "../g_local.hpp"
#include "../../../../inc/shared/event_shadow.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>

namespace {

static_assert(static_cast<uint32_t>(EV_NONE) == 0);
static_assert(static_cast<uint32_t>(EV_LADDER_STEP) <=
              WORR_EVENT_SHADOW_MAX_LEGACY_EVENT);

const worr_event_shadow_import_v1 *shadow_import;
std::array<worr_event_shadow_source_state_v1, MAX_ENTITIES> source_states;

uint64_t source_time_us()
{
    const int64_t milliseconds = level.time.milliseconds();
    if (milliseconds <= 0)
        return 0;
    constexpr uint64_t microseconds_per_millisecond = 1000;
    const uint64_t value = static_cast<uint64_t>(milliseconds);
    if (value > std::numeric_limits<uint64_t>::max() /
                    microseconds_per_millisecond) {
        return std::numeric_limits<uint64_t>::max();
    }
    return value * microseconds_per_millisecond;
}

} // namespace

void SG_EventShadowInitialize()
{
    shadow_import = nullptr;
    if (!gi.GetExtension)
        return;

    const auto *candidate = static_cast<const worr_event_shadow_import_v1 *>(
        gi.GetExtension(WORR_EVENT_SHADOW_IMPORT_V1));
    if (!candidate || candidate->struct_size != sizeof(*candidate) ||
        candidate->api_version != WORR_EVENT_SHADOW_API_VERSION ||
        !candidate->SubmitCandidate || !candidate->GetStatus ||
        !candidate->GetRecordFromNewest) {
        return;
    }
    shadow_import = candidate;
}

void SG_EventShadowResetMap()
{
    std::memset(source_states.data(), 0,
                source_states.size() * sizeof(source_states[0]));
}

void SG_EventShadowCaptureAuthoritativeTick()
{
    if (!shadow_import)
        return;

    const uint32_t source_tick = gi.ServerFrame();
    const uint64_t time_us = source_time_us();
    const size_t entity_count =
        std::min(static_cast<size_t>(globals.numEntities), MAX_ENTITIES);

    for (size_t index = 0; index < entity_count; ++index) {
        const gentity_t &entity = g_entities[index];
        if (!entity.inUse || entity.s.event == EV_NONE)
            continue;

        /* spawn_count starts at zero and increments when a slot is freed.
         * Canonical references reserve generation zero, so store count + 1;
         * reject the single wrapping value instead of aliasing generation 0. */
        const uint32_t generation =
            static_cast<uint32_t>(entity.spawn_count) + 1u;
        if (generation == 0)
            continue;

        worr_event_shadow_legacy_input_v1 input{};
        input.struct_size = sizeof(input);
        input.schema_version = WORR_EVENT_SHADOW_API_VERSION;
        input.source_tick = source_tick;
        input.raw_event = static_cast<uint32_t>(entity.s.event);
        input.source_time_us = time_us;
        input.source_entity.index = static_cast<uint32_t>(index);
        input.source_entity.generation = generation;
        input.max_entities = WORR_EVENT_SHADOW_MAX_ENTITIES;

        worr_event_record_v1 candidate{};
        if (Worr_EventShadowMapLegacyEntityV1(
                &source_states[index], &input, &candidate) !=
            WORR_EVENT_SHADOW_MAP_MAPPED) {
            continue;
        }

        /* Shadow submission is observational.  Its result must never change
         * the legacy entity event or any packet-building decision. */
        (void)shadow_import->SubmitCandidate(&candidate);
    }
}
