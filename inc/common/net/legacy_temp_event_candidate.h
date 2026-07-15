/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "shared/event_abi.h"
#include "q2proto/q2proto.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Converts one decoded legacy temporary-entity carrier into an ID-less T05
 * candidate. The returned record deliberately keeps source/subject references
 * absent: the caller resolves the returned raw entity indices against its own
 * authoritative or observed entity lineage before publication.
 *
 * All output pointers are required and are left untouched on failure. This is
 * shared by client decode shadowing and server final-emission capture. It does
 * not modify q2proto.
 */
typedef enum worr_legacy_temp_event_candidate_result_v1_e {
    WORR_LEGACY_TEMP_EVENT_CANDIDATE_OK = 0,
    WORR_LEGACY_TEMP_EVENT_CANDIDATE_INVALID_ARGUMENT = 1,
    WORR_LEGACY_TEMP_EVENT_CANDIDATE_UNSUPPORTED_SUBTYPE = 2,
    WORR_LEGACY_TEMP_EVENT_CANDIDATE_INVALID_SHAPE = 3,
    WORR_LEGACY_TEMP_EVENT_CANDIDATE_ENTITY_OUT_OF_RANGE = 4,
    WORR_LEGACY_TEMP_EVENT_CANDIDATE_INVALID_RECORD = 5,
    WORR_LEGACY_TEMP_EVENT_CANDIDATE_CAPACITY = 6,
} worr_legacy_temp_event_candidate_result_v1;

/* The final-emission adapter accepts a deliberately small homogeneous batch.
 * This bounds stack use and preserves an all-or-nothing native observation. */
#define WORR_LEGACY_TEMP_EVENT_CANDIDATE_SEQUENCE_MAX 16u

/*
 * Decodes one complete engine-game temporary-entity carrier that uses WORR's
 * legacy game-import layout: opcode/type, IEEE-754 little-endian positions,
 * signed little-endian entity/scalar fields, and byte-normal directions.
 * Only a standalone complete carrier is accepted; combined/truncated data and
 * unsupported shapes reject without modifying temp_entity_out.
 */
worr_legacy_temp_event_candidate_result_v1
Worr_LegacyTempEventDecodeRawV1(const uint8_t *raw_message,
                                 size_t raw_message_size,
                                 q2proto_svc_temp_entity_t *temp_entity_out);

/* Decodes one carrier at the front of a larger game message and reports its
 * exact byte length. Unlike the standalone API, trailing bytes are allowed. */
worr_legacy_temp_event_candidate_result_v1
Worr_LegacyTempEventDecodeRawPrefixV1(
    const uint8_t *raw_message, size_t raw_message_size,
    q2proto_svc_temp_entity_t *temp_entity_out, size_t *bytes_consumed_out);

/*
 * Decodes a complete sequence of standalone svc_temp_entity carriers from one
 * game-DLL multicast message. Every carrier must use the legacy layout above.
 * The sequence remains ordered, is bounded by
 * WORR_LEGACY_TEMP_EVENT_CANDIDATE_SEQUENCE_MAX, and is failure-atomic: an
 * invalid carrier or insufficient capacity leaves both outputs untouched.
 */
worr_legacy_temp_event_candidate_result_v1
Worr_LegacyTempEventDecodeRawSequenceV1(
    const uint8_t *raw_message, size_t raw_message_size,
    q2proto_svc_temp_entity_t *temp_entities_out, uint32_t capacity,
    uint32_t *count_out);

worr_legacy_temp_event_candidate_result_v1
Worr_LegacyTempEventCandidateBuildV1(
    const q2proto_svc_temp_entity_t *temp_entity, uint32_t source_tick,
    uint64_t source_time_us, uint32_t max_entities,
    worr_event_record_v1 *candidate_out, uint32_t *source_entity_index_out,
    uint32_t *subject_entity_index_out);

#ifdef __cplusplus
}
#endif
