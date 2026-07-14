/*
Copyright (C) 2026 WORR contributors

Deterministic legacy entity-event shadow mapping and allocator tests.
*/

#include "server/event_shadow.h"

#include "common/net/event_journal.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "event shadow check failed at %s:%d: %s\n",    \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static worr_event_shadow_map_result_v1 map_event(
    worr_event_shadow_source_state_v1 *state,
    uint32_t tick,
    uint32_t raw_event,
    uint32_t entity_index,
    uint32_t generation,
    worr_event_record_v1 *candidate)
{
    worr_event_shadow_legacy_input_v1 input;
    memset(&input, 0, sizeof(input));
    input.struct_size = sizeof(input);
    input.schema_version = WORR_EVENT_SHADOW_API_VERSION;
    input.source_tick = tick;
    input.raw_event = raw_event;
    input.source_time_us = (uint64_t)tick * UINT64_C(16667);
    input.source_entity.index = entity_index;
    input.source_entity.generation = generation;
    input.max_entities = WORR_EVENT_SHADOW_MAX_ENTITIES;
    return Worr_EventShadowMapLegacyEntityV1(state, &input, candidate);
}

static uint32_t payload_value(const worr_event_record_v1 *record,
                              uint32_t index)
{
    worr_event_payload_u32x4_v1 payload;
    memcpy(&payload, record->payload, sizeof(payload));
    return payload.value[index];
}

static int test_mapper(void)
{
    worr_event_shadow_source_state_v1 state;
    worr_event_shadow_source_state_v1 before;
    worr_event_record_v1 candidate;
    worr_event_record_v1 authority;

    memset(&state, 0, sizeof(state));
    CHECK(map_event(&state, 10, 2, 4, 1, &candidate) ==
          WORR_EVENT_SHADOW_MAP_MAPPED);
    CHECK(candidate.struct_size == sizeof(candidate));
    CHECK(candidate.schema_version == WORR_EVENT_ABI_VERSION);
    CHECK(candidate.model_revision == WORR_EVENT_MODEL_REVISION);
    CHECK((candidate.flags & WORR_EVENT_FLAG_HAS_AUTHORITY_ID) == 0);
    CHECK(candidate.event_id.stream_epoch == 0 &&
          candidate.event_id.sequence == 0);
    CHECK(candidate.source_tick == 10 && candidate.source_ordinal == 0);
    CHECK(candidate.source_entity.index == 4 &&
          candidate.source_entity.generation == 1);
    CHECK(candidate.subject_entity.index == WORR_EVENT_NO_ENTITY &&
          candidate.subject_entity.generation == 0);
    CHECK(candidate.event_type == WORR_EVENT_TYPE_LEGACY_BRIDGE);
    CHECK(candidate.delivery_class == WORR_EVENT_DELIVERY_TRANSIENT);
    CHECK(candidate.prediction_class ==
          WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    CHECK(candidate.expiry_tick == 11);
    CHECK(candidate.payload_kind == WORR_EVENT_PAYLOAD_U32X4 &&
          candidate.payload_size == sizeof(worr_event_payload_u32x4_v1));
    CHECK(payload_value(&candidate, 0) == 2 &&
          payload_value(&candidate, 1) == 4 &&
          payload_value(&candidate, 2) == 1 &&
          payload_value(&candidate, 3) == 0);

    before = state;
    memset(&candidate, 0xa5, sizeof(candidate));
    CHECK(map_event(&state, 10, 2, 4, 1, &candidate) ==
          WORR_EVENT_SHADOW_MAP_DUPLICATE);
    CHECK(memcmp(&state, &before, sizeof(state)) == 0);
    CHECK(candidate.struct_size == UINT32_C(0xa5a5a5a5));

    CHECK(map_event(&state, 10, 3, 4, 1, &candidate) ==
          WORR_EVENT_SHADOW_MAP_MAPPED);
    CHECK(candidate.source_ordinal == 1 &&
          payload_value(&candidate, 3) == 1);
    CHECK(map_event(&state, 10, 2, 4, 1, &candidate) ==
          WORR_EVENT_SHADOW_MAP_DUPLICATE);

    CHECK(map_event(&state, 10, 2, 4, 2, &candidate) ==
          WORR_EVENT_SHADOW_MAP_MAPPED);
    CHECK(candidate.source_ordinal == 0 &&
          candidate.source_entity.generation == 2);
    CHECK(map_event(&state, 11, 2, 4, 2, &candidate) ==
          WORR_EVENT_SHADOW_MAP_MAPPED);
    CHECK(candidate.source_ordinal == 0);

    CHECK(map_event(&state, UINT32_MAX, 2, 4, 2, &candidate) ==
          WORR_EVENT_SHADOW_MAP_MAPPED);
    CHECK(candidate.expiry_tick == 0);
    authority = candidate;
    authority.flags |= WORR_EVENT_FLAG_HAS_AUTHORITY_ID;
    authority.event_id.stream_epoch = 1;
    authority.event_id.sequence = 1;
    CHECK(Worr_EventRecordValidateV1(
        &authority, WORR_EVENT_SHADOW_MAX_ENTITIES));

    before = state;
    CHECK(map_event(&state, 12, 0, 4, 2, &candidate) ==
          WORR_EVENT_SHADOW_MAP_INVALID);
    CHECK(memcmp(&state, &before, sizeof(state)) == 0);
    CHECK(map_event(&state, 12,
                    WORR_EVENT_SHADOW_MAX_LEGACY_EVENT + 1, 4, 2,
                    &candidate) == WORR_EVENT_SHADOW_MAP_INVALID);
    CHECK(map_event(&state, 12, 2,
                    WORR_EVENT_SHADOW_MAX_ENTITIES, 2,
                    &candidate) == WORR_EVENT_SHADOW_MAP_INVALID);
    CHECK(map_event(&state, 12, 2, 4, 0, &candidate) ==
          WORR_EVENT_SHADOW_MAP_INVALID);
    return 0;
}

static int test_allocator_and_telemetry(void)
{
    const worr_event_shadow_import_v1 *api = SV_EventShadowImportV1();
    worr_event_shadow_status_v1 status;
    worr_event_shadow_source_state_v1 state;
    worr_event_record_v1 first;
    worr_event_record_v1 second;
    worr_event_record_v1 queried;
    uint32_t slot_state;
    uint64_t hash;
    uint32_t first_epoch;
    uint64_t first_reset_count;

    CHECK(api && api->struct_size == sizeof(*api) &&
          api->api_version == WORR_EVENT_SHADOW_API_VERSION);
    CHECK(api->SubmitCandidate && api->GetStatus &&
          api->GetRecordFromNewest);

    SV_EventShadowResetMap();
    CHECK(api->GetStatus(&status));
    CHECK(status.struct_size == sizeof(status) &&
          status.schema_version == WORR_EVENT_SHADOW_API_VERSION);
    CHECK(status.stream_epoch != 0 &&
          status.capacity == WORR_EVENT_SHADOW_CAPACITY &&
          status.occupied == 0 && status.last_sequence == 0);
    first_epoch = status.stream_epoch;
    first_reset_count = status.reset_count;

    memset(&state, 0, sizeof(state));
    CHECK(map_event(&state, 20, 2, 4, 1, &first) ==
          WORR_EVENT_SHADOW_MAP_MAPPED);
    CHECK(api->SubmitCandidate(&first) == WORR_EVENT_SHADOW_ACCEPTED);
    CHECK(api->GetStatus(&status));
    CHECK(status.accepted == 1 && status.submit_attempts == 1 &&
          status.occupied == 1 && status.last_sequence == 1 &&
          status.last_record_hash != 0);
    CHECK(api->SubmitCandidate(&first) == WORR_EVENT_SHADOW_DUPLICATE);
    CHECK(api->GetStatus(&status));
    CHECK(status.accepted == 1 && status.duplicates == 1 &&
          status.submit_attempts == 2 && status.last_sequence == 1 &&
          status.occupied == 1);

    CHECK(map_event(&state, 20, 3, 4, 1, &second) ==
          WORR_EVENT_SHADOW_MAP_MAPPED);
    CHECK(api->SubmitCandidate(&second) == WORR_EVENT_SHADOW_ACCEPTED);
    CHECK(api->GetRecordFromNewest(0, &queried, &slot_state, &hash));
    CHECK(queried.event_id.stream_epoch == first_epoch &&
          queried.event_id.sequence == 2);
    CHECK(payload_value(&queried, 0) == 3 &&
          queried.source_ordinal == 1);
    CHECK((slot_state & WORR_EVENT_SLOT_RECEIVED) != 0 && hash != 0);
    CHECK(api->GetRecordFromNewest(1, &queried, NULL, NULL));
    CHECK(queried.event_id.sequence == 1 &&
          payload_value(&queried, 0) == 2);
    CHECK(!api->GetRecordFromNewest(2, &queried, NULL, NULL));

    memset(&state, 0, sizeof(state));
    CHECK(map_event(&state, 20, 2, 4, 2, &second) ==
          WORR_EVENT_SHADOW_MAP_MAPPED);
    CHECK(api->SubmitCandidate(&second) == WORR_EVENT_SHADOW_ACCEPTED);
    CHECK(api->GetRecordFromNewest(0, &queried, NULL, NULL));
    CHECK(queried.event_id.sequence == 3 &&
          queried.source_entity.index == 4 &&
          queried.source_entity.generation == 2);

    second.reserved0 = 1;
    CHECK(api->SubmitCandidate(&second) == WORR_EVENT_SHADOW_INVALID);
    CHECK(api->GetStatus(&status));
    CHECK(status.invalid == 1 && status.accepted == 3 &&
          status.last_sequence == 3);

    SV_EventShadowResetMap();
    CHECK(api->GetStatus(&status));
    CHECK(status.stream_epoch == first_epoch + 1 && status.occupied == 0 &&
          status.last_sequence == 0 && status.accepted == 0 &&
          status.reset_count == first_reset_count + 1);
    return 0;
}

static int test_capacity_and_wrap(void)
{
    const worr_event_shadow_import_v1 *api = SV_EventShadowImportV1();
    worr_event_shadow_source_state_v1 state;
    worr_event_shadow_status_v1 status;
    worr_event_record_v1 candidate;
    worr_event_record_v1 queried;
    uint32_t index;
    uint32_t wrap_epoch;

    SV_EventShadowResetMap();
    for (index = 0; index < WORR_EVENT_SHADOW_CAPACITY; ++index) {
        memset(&state, 0, sizeof(state));
        CHECK(map_event(&state, index + 1, 2,
                        index % WORR_EVENT_SHADOW_MAX_ENTITIES, 1,
                        &candidate) == WORR_EVENT_SHADOW_MAP_MAPPED);
        CHECK(api->SubmitCandidate(&candidate) ==
              WORR_EVENT_SHADOW_ACCEPTED);
    }
    CHECK(api->GetStatus(&status));
    CHECK(status.accepted == WORR_EVENT_SHADOW_CAPACITY &&
          status.occupied == WORR_EVENT_SHADOW_CAPACITY &&
          status.last_sequence == WORR_EVENT_SHADOW_CAPACITY);
    memset(&state, 0, sizeof(state));
    CHECK(map_event(&state, WORR_EVENT_SHADOW_CAPACITY + 1, 2, 7, 1,
                    &candidate) == WORR_EVENT_SHADOW_MAP_MAPPED);
    CHECK(api->SubmitCandidate(&candidate) ==
          WORR_EVENT_SHADOW_CAPACITY_EXHAUSTED);
    CHECK(api->GetStatus(&status));
    CHECK(status.capacity_failures == 1 &&
          status.accepted == WORR_EVENT_SHADOW_CAPACITY &&
          status.last_sequence == WORR_EVENT_SHADOW_CAPACITY &&
          status.last_result == WORR_EVENT_SHADOW_CAPACITY_EXHAUSTED);

    SV_EventShadowResetMap();
    CHECK(api->GetStatus(&status));
    wrap_epoch = status.stream_epoch + 10;
    CHECK(wrap_epoch > status.stream_epoch && wrap_epoch < UINT32_MAX);
    CHECK(SV_EventShadowTestSetCursor(wrap_epoch, UINT32_MAX - 1));
    memset(&state, 0, sizeof(state));
    CHECK(map_event(&state, 100, 2, 1, 1, &candidate) ==
          WORR_EVENT_SHADOW_MAP_MAPPED);
    CHECK(api->SubmitCandidate(&candidate) == WORR_EVENT_SHADOW_ACCEPTED);
    CHECK(api->GetRecordFromNewest(0, &queried, NULL, NULL));
    CHECK(queried.event_id.stream_epoch == wrap_epoch &&
          queried.event_id.sequence == UINT32_MAX);

    memset(&state, 0, sizeof(state));
    CHECK(map_event(&state, 101, 3, 2, 1, &candidate) ==
          WORR_EVENT_SHADOW_MAP_MAPPED);
    CHECK(api->SubmitCandidate(&candidate) == WORR_EVENT_SHADOW_ACCEPTED);
    CHECK(api->GetStatus(&status));
    CHECK(status.stream_epoch == wrap_epoch + 1 &&
          status.last_sequence == 1 && status.occupied == 1 &&
          status.sequence_wraps == 1);
    CHECK(api->GetRecordFromNewest(0, &queried, NULL, NULL));
    CHECK(queried.event_id.stream_epoch == wrap_epoch + 1 &&
          queried.event_id.sequence == 1);

    CHECK(SV_EventShadowTestSetCursor(UINT32_MAX, UINT32_MAX));
    memset(&state, 0, sizeof(state));
    CHECK(map_event(&state, 102, 4, 3, 1, &candidate) ==
          WORR_EVENT_SHADOW_MAP_MAPPED);
    CHECK(api->SubmitCandidate(&candidate) ==
          WORR_EVENT_SHADOW_ID_EXHAUSTED);
    CHECK(api->GetStatus(&status));
    CHECK(status.id_exhaustions == 1 &&
          status.last_result == WORR_EVENT_SHADOW_ID_EXHAUSTED);
    SV_EventShadowResetMap();
    CHECK(api->GetStatus(&status));
    CHECK(status.stream_epoch == UINT32_MAX && status.occupied == 0 &&
          status.id_exhaustions == 1 &&
          status.last_result == WORR_EVENT_SHADOW_ID_EXHAUSTED);
    CHECK(api->SubmitCandidate(&candidate) ==
          WORR_EVENT_SHADOW_UNAVAILABLE);
    return 0;
}

int main(void)
{
    CHECK(test_mapper() == 0);
    CHECK(test_allocator_and_telemetry() == 0);
    CHECK(test_capacity_and_wrap() == 0);
    printf("event shadow: mapper/allocator/telemetry checks passed\n");
    return 0;
}

