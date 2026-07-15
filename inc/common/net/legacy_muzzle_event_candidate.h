/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <stddef.h>

#include "shared/event_abi.h"
#include "q2proto/q2proto.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Converts a decoded player or monster muzzleflash into an ID-less T05 action
 * template. The source identity deliberately remains absent; the caller must
 * resolve source_entity_index_out through its observed or final-emission
 * lineage before publication. All output pointers are required and remain
 * unchanged on failure.
 */
typedef enum worr_legacy_muzzle_event_candidate_result_v1_e {
    WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_OK = 0,
    WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_INVALID_ARGUMENT = 1,
    WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_INVALID_SHAPE = 2,
    WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_ENTITY_OUT_OF_RANGE = 3,
    WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_INVALID_RECORD = 4,
    WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_CAPACITY = 5,
} worr_legacy_muzzle_event_candidate_result_v1;

#define WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_SEQUENCE_MAX 16u

/*
 * Decodes only one complete engine-game legacy muzzle carrier. It never
 * accepts combined messages: the caller must pass exactly a four-byte player
 * or monster carrier, or the five-byte rerelease monster carrier. Outputs are
 * required and unchanged on failure.
 */
worr_legacy_muzzle_event_candidate_result_v1
Worr_LegacyMuzzleEventDecodeRawV1(
    const uint8_t *raw_message, size_t raw_message_size,
    q2proto_svc_muzzleflash_t *muzzleflash_out, uint32_t *family_out);

/* Decodes one carrier at the front of a larger game message and reports its
 * exact byte length. Trailing bytes remain for a caller-owned sequence parser. */
worr_legacy_muzzle_event_candidate_result_v1
Worr_LegacyMuzzleEventDecodeRawPrefixV1(
    const uint8_t *raw_message, size_t raw_message_size,
    q2proto_svc_muzzleflash_t *muzzleflash_out, uint32_t *family_out,
    size_t *bytes_consumed_out);

/* Decodes one bounded ordered sequence of consecutive legacy muzzle carriers.
 * Output arrays and count are unchanged unless every carrier is valid. */
worr_legacy_muzzle_event_candidate_result_v1
Worr_LegacyMuzzleEventDecodeRawSequenceV1(
    const uint8_t *raw_message, size_t raw_message_size,
    q2proto_svc_muzzleflash_t *muzzleflashes_out, uint32_t *families_out,
    uint32_t capacity, uint32_t *count_out);

worr_legacy_muzzle_event_candidate_result_v1
Worr_LegacyMuzzleEventCandidateBuildV1(
    const q2proto_svc_muzzleflash_t *muzzleflash, uint32_t family,
    uint32_t source_tick, uint64_t source_time_us, uint32_t max_entities,
    worr_event_record_v1 *candidate_out, uint32_t *source_entity_index_out);

#ifdef __cplusplus
}
#endif
