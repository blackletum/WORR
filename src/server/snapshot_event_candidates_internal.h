/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "server/snapshot_event_candidates.h"

typedef struct sv_snapshot_event_candidate_source_v1_s {
  uint32_t source_ordinal;
  worr_event_entity_ref_v1 source_entity;
  uint16_t raw_event;
  uint16_t reserved0;
} sv_snapshot_event_candidate_source_v1;

#if defined(__cplusplus)
static_assert(sizeof(sv_snapshot_event_candidate_source_v1) == 16,
              "snapshot event candidate source layout changed");
#else
_Static_assert(sizeof(sv_snapshot_event_candidate_source_v1) == 16,
               "snapshot event candidate source layout changed");
#endif

/*
 * Private final-emission adapter.  sources_out must not overlap the view
 * or carrier inputs.  The output array is assigned only after every inferred
 * event reference has an exact typed semantic match.  Compact sources are
 * enough to rebuild typed candidates because tick/time live in the retained
 * projection and the remaining fields are fixed by the canonical mapper.
 */
sv_snapshot_event_candidates_result_v1
SV_SnapshotEventCandidatesBuildFinalEmissionInternalV1(
    const worr_snapshot_projection_view_v2 *view,
    const q2proto_svc_frame_entity_delta_t *entity_deltas,
    uint32_t entity_delta_count, uint32_t max_entities,
    sv_snapshot_event_candidate_source_v1 *sources_out,
    uint32_t source_capacity, uint32_t *source_count_out);
