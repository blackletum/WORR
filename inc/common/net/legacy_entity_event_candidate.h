/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "shared/event_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Constructs the ID-less canonical T05 candidate represented by one legacy
 * entity impulse.  Outputs are assigned only after both candidate validation
 * and fieldwise semantic hashing succeed.
 */
bool Worr_LegacyEntityEventCandidateBuildV1(
    uint32_t source_tick, uint64_t source_time_us, uint32_t source_ordinal,
    worr_event_entity_ref_v1 source_entity, uint16_t raw_event,
    uint32_t max_entities, worr_event_record_v1 *candidate_out,
    uint64_t *semantic_hash_out);

#ifdef __cplusplus
}
#endif
