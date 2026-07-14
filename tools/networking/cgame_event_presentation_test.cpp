/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "cg_event_shadow.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__,    \
                         __LINE__, #condition);                                 \
            std::exit(1);                                                       \
        }                                                                       \
    } while (0)

constexpr std::uint32_t max_entities = 16;
constexpr std::uint32_t scratch_capacity = 4;

struct builder_storage_t {
    worr_cgame_event_range_builder_v2 builder{};
    std::array<worr_cgame_event_observed_v2, max_entities> observed{};
    std::array<std::uint32_t, max_entities> seen{};
    std::array<worr_event_record_v1, scratch_capacity> scratch{};
};

const worr_cgame_event_range_export_v2 *consumer;

void consume_range(void *, const worr_cgame_event_range_v2 *range)
{
    CHECK(consumer != nullptr);
    consumer->ConsumeCanonicalEventRange(range);
}

void initialize(builder_storage_t &storage, std::uint32_t epoch)
{
    storage = {};
    CHECK(Worr_CGameEventRangeBuilderInitV2(
        &storage.builder, storage.observed.data(), storage.seen.data(),
        static_cast<std::uint32_t>(storage.observed.size()),
        storage.scratch.data(),
        static_cast<std::uint32_t>(storage.scratch.size()), epoch));
    consumer->Reset(epoch, WORR_CGAME_EVENT_SHADOW_RESET_CLIENT_STATE);
}

worr_cgame_event_range_build_result_v2 deliver_frame(
    builder_storage_t &storage, std::uint32_t tick, std::uint64_t time_us,
    const worr_cgame_event_carrier_v2 *carriers, std::uint32_t count)
{
    return Worr_CGameEventRangeDeliverFrameV2(
        &storage.builder, tick, time_us, carriers, count, 0,
        consume_range, nullptr);
}

void test_order_copy_reset_and_empty()
{
    builder_storage_t storage{};
    initialize(storage, 7);

    cg_canonical_event_presentation_cursor_v1 empty_cursor{};
    CHECK(CG_CanonicalEventPresentationBegin(&empty_cursor) ==
          CG_CANONICAL_EVENT_PRESENTATION_OK);

    cg_canonical_event_presentation_cursor_v1 next{};
    cg_canonical_event_presentation_entry_v1 entry{};
    CHECK(CG_CanonicalEventPresentationNext(&empty_cursor, &next, &entry) ==
          CG_CANONICAL_EVENT_PRESENTATION_EMPTY);

    const worr_cgame_event_carrier_v2 first[] = {
        {1, WORR_EVENT_LEGACY_ENTITY_FOOTSTEP, 0},
        {2, 0, 1},
    };
    CHECK(deliver_frame(storage, 100, UINT64_C(1000000), first, 2) ==
          WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);

    std::uint32_t audit_advanced = UINT32_MAX;
    CHECK(CG_CanonicalEventPresentationAdvanceAudit(
              UINT64_C(999999), 16, &audit_advanced) ==
          CG_CANONICAL_EVENT_PRESENTATION_NOT_READY);
    CHECK(audit_advanced == 0);
    CHECK(CG_CanonicalEventPresentationAdvanceAudit(
              UINT64_C(1000000), 16, &audit_advanced) ==
          CG_CANONICAL_EVENT_PRESENTATION_OK);
    CHECK(audit_advanced == 1);

    cg_canonical_event_presentation_cursor_v1 cursor{};
    CHECK(CG_CanonicalEventPresentationBegin(&cursor) ==
          CG_CANONICAL_EVENT_PRESENTATION_OK);
    CHECK(CG_CanonicalEventPresentationNext(&cursor, &next, &entry) ==
          CG_CANONICAL_EVENT_PRESENTATION_OK);
    CHECK(entry.journal_serial == 1);
    CHECK(entry.arrival_ordinal == 1);
    CHECK(entry.carrier_tick == 100);
    CHECK(entry.carrier_kind == WORR_CGAME_EVENT_CARRIER_ENTITY_FRAME_V2);
    CHECK(entry.record.source_entity.index == 1);
    CHECK(entry.record.source_entity.generation == 1);
    CHECK(entry.semantic_hash != 0);

    worr_event_payload_legacy_entity_v1 payload{};
    std::memcpy(&payload, entry.record.payload, sizeof(payload));
    CHECK(payload.raw_event == WORR_EVENT_LEGACY_ENTITY_FOOTSTEP);

    /* Prove the cgame journal owns a copy rather than the builder scratch. */
    std::memset(storage.scratch.data(), 0xA5,
                sizeof(storage.scratch));
    cg_canonical_event_presentation_entry_v1 copied_again{};
    cg_canonical_event_presentation_cursor_v1 ignored{};
    CHECK(CG_CanonicalEventPresentationNext(&cursor, &ignored,
                                             &copied_again) ==
          CG_CANONICAL_EVENT_PRESENTATION_OK);
    CHECK(copied_again.semantic_hash == entry.semantic_hash);
    CHECK(std::memcmp(&copied_again.record, &entry.record,
                      sizeof(entry.record)) == 0);

    cursor = next;
    CHECK(CG_CanonicalEventPresentationNext(&cursor, &next, &entry) ==
          CG_CANONICAL_EVENT_PRESENTATION_EMPTY);
    CHECK(deliver_frame(storage, 100, UINT64_C(1000000), first, 2) ==
          WORR_CGAME_EVENT_RANGE_BUILD_DUPLICATE_FRAME_V2);

    const worr_cgame_event_carrier_v2 second[] = {
        {1, WORR_EVENT_LEGACY_ENTITY_ITEM_RESPAWN, 0},
        {2, WORR_EVENT_LEGACY_ENTITY_OTHER_FOOTSTEP, 1},
    };
    CHECK(deliver_frame(storage, 101, UINT64_C(1010000), second, 2) ==
          WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);
    CHECK(CG_CanonicalEventPresentationNext(&cursor, &next, &entry) ==
          CG_CANONICAL_EVENT_PRESENTATION_OK);
    CHECK(entry.journal_serial == 2 && entry.arrival_ordinal == 2);
    cursor = next;
    CHECK(CG_CanonicalEventPresentationNext(&cursor, &next, &entry) ==
          CG_CANONICAL_EVENT_PRESENTATION_OK);
    CHECK(entry.journal_serial == 3 && entry.arrival_ordinal == 3);
    CHECK(entry.record.source_ordinal == 1);

    const worr_cgame_event_carrier_v2 empty[] = {
        {1, 0, 0},
        {2, 0, 1},
    };
    CHECK(deliver_frame(storage, 102, UINT64_C(1020000), empty, 2) ==
          WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);
    CHECK(CG_CanonicalEventPresentationAdvanceAudit(
              UINT64_C(1020000), 16, &audit_advanced) ==
          CG_CANONICAL_EVENT_PRESENTATION_OK);
    CHECK(audit_advanced == 2);

    cg_canonical_event_presentation_status_v1 status{};
    CHECK(CG_CanonicalEventPresentationGetStatus(&status));
    CHECK(status.accepted_records == 3);
    CHECK(status.retained_count == 3);
    CHECK(status.empty_carriers == 1);
    CHECK(status.audit_ready_records == 3);
    CHECK(status.audit_future_stalls == 1);
    CHECK(status.audit_last_ready_serial == 3);
    CHECK(status.range_audit.accepted_records == 3);

    CHECK(Worr_CGameEventRangeBuilderResetV2(&storage.builder, 8));
    consumer->Reset(8, WORR_CGAME_EVENT_SHADOW_RESET_DEMO_RESTART);
    CHECK(CG_CanonicalEventPresentationNext(&cursor, &next, &entry) ==
          CG_CANONICAL_EVENT_PRESENTATION_STALE_CURSOR);
    CHECK(CG_CanonicalEventPresentationGetStatus(&status));
    CHECK(status.stream_epoch == 8);
    CHECK(status.retained_count == 0);
    CHECK(status.reset_count == 2);
}

void test_invalid_range_and_overrun()
{
    builder_storage_t storage{};
    initialize(storage, 20);

    worr_cgame_event_range_v2 invalid{};
    invalid.struct_size = sizeof(invalid);
    invalid.api_version = WORR_CGAME_EVENT_RANGE_API_VERSION_V2;
    invalid.stream_epoch = 20;
    consumer->ConsumeCanonicalEventRange(&invalid);

    cg_canonical_event_presentation_status_v1 status{};
    CHECK(CG_CanonicalEventPresentationGetStatus(&status));
    CHECK(status.rejected_ranges == 1);
    CHECK(status.retained_count == 0);

    cg_canonical_event_presentation_cursor_v1 unread{};
    CHECK(CG_CanonicalEventPresentationTail(&unread) ==
          CG_CANONICAL_EVENT_PRESENTATION_OK);

    worr_cgame_event_carrier_v2 carrier{
        1, WORR_EVENT_LEGACY_ENTITY_FOOTSTEP, 0};
    for (std::uint32_t index = 0;
         index < CG_CANONICAL_EVENT_PRESENTATION_CAPACITY + 1u; ++index) {
        const std::uint32_t tick = 1000u + index;
        CHECK(deliver_frame(
                  storage, tick,
                  UINT64_C(5000000) + static_cast<std::uint64_t>(index) * 1000u,
                  &carrier, 1) ==
              WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2);
    }

    cg_canonical_event_presentation_cursor_v1 next{};
    cg_canonical_event_presentation_entry_v1 entry{};
    CHECK(CG_CanonicalEventPresentationNext(&unread, &next, &entry) ==
          CG_CANONICAL_EVENT_PRESENTATION_CURSOR_OVERRUN);
    CHECK(CG_CanonicalEventPresentationGetStatus(&status));
    CHECK(status.retained_count == CG_CANONICAL_EVENT_PRESENTATION_CAPACITY);
    CHECK(status.overwritten_records == 1);
    CHECK(status.oldest_serial == 2);

    std::uint32_t audit_advanced = UINT32_MAX;
    CHECK(CG_CanonicalEventPresentationAdvanceAudit(
              UINT64_MAX, CG_CANONICAL_EVENT_PRESENTATION_CAPACITY,
              &audit_advanced) ==
          CG_CANONICAL_EVENT_PRESENTATION_CURSOR_OVERRUN);
    CHECK(audit_advanced == 0);
    CHECK(CG_CanonicalEventPresentationAdvanceAudit(
              UINT64_MAX, CG_CANONICAL_EVENT_PRESENTATION_CAPACITY,
              &audit_advanced) ==
          CG_CANONICAL_EVENT_PRESENTATION_OK);
    CHECK(audit_advanced == CG_CANONICAL_EVENT_PRESENTATION_CAPACITY);
    CHECK(CG_CanonicalEventPresentationGetStatus(&status));
    CHECK(status.audit_overrun_recoveries == 1);
    CHECK(status.audit_ready_records ==
          CG_CANONICAL_EVENT_PRESENTATION_CAPACITY);

    CHECK(CG_CanonicalEventPresentationBegin(&unread) ==
          CG_CANONICAL_EVENT_PRESENTATION_OK);
    CHECK(CG_CanonicalEventPresentationNext(&unread, &next, &entry) ==
          CG_CANONICAL_EVENT_PRESENTATION_OK);
    CHECK(entry.journal_serial == 2);
    CHECK(entry.carrier_tick == 1001);
}

} // namespace

int main()
{
    consumer = CG_GetEventRangeAPIv2();
    CHECK(consumer != nullptr);
    CHECK(consumer->struct_size == sizeof(*consumer));
    CHECK(consumer->api_version == WORR_CGAME_EVENT_RANGE_API_VERSION_V2);

    test_order_copy_reset_and_empty();
    test_invalid_range_and_overrun();
    std::puts("cgame canonical event presentation tests passed");
    return 0;
}
