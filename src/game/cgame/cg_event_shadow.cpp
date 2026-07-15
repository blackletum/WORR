/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "cg_event_shadow.hpp"
#include "cg_event_runtime.hpp"

#include <array>
#include <cstring>

namespace {

worr_cgame_event_shadow_audit_v1 audit_v1;
worr_cgame_event_range_audit_v2 audit_v2;

struct canonical_event_presentation_state_t {
    std::array<cg_canonical_event_presentation_entry_v1,
               CG_CANONICAL_EVENT_PRESENTATION_CAPACITY> entries;
    std::uint32_t stream_epoch;
    std::uint32_t journal_generation;
    std::uint32_t retained_count;
    std::uint64_t next_serial;
    std::uint64_t reset_count;
    std::uint64_t accepted_ranges;
    std::uint64_t accepted_records;
    std::uint64_t rejected_ranges;
    std::uint64_t empty_carriers;
    std::uint64_t adapter_rejections;
    std::uint64_t overwritten_records;
    cg_canonical_event_presentation_cursor_v1 audit_cursor;
    std::uint64_t audit_ready_records;
    std::uint64_t audit_future_stalls;
    std::uint64_t audit_overrun_recoveries;
    std::uint64_t audit_last_ready_serial;
    std::uint64_t audit_last_render_time_us;
    bool initialized;
};

canonical_event_presentation_state_t presentation;

void increment_saturated(std::uint64_t &value)
{
    if (value != UINT64_MAX)
        ++value;
}

std::uint64_t oldest_serial()
{
    if (!presentation.retained_count)
        return presentation.next_serial;
    return presentation.next_serial - presentation.retained_count;
}

cg_canonical_event_presentation_cursor_v1 make_cursor(
    std::uint64_t next_serial);

void reset_presentation(std::uint32_t stream_epoch)
{
    const std::uint64_t reset_count = presentation.reset_count;
    std::uint32_t generation = presentation.journal_generation + 1u;
    if (!generation)
        generation = 1u;
    presentation = {};
    presentation.initialized = stream_epoch != 0;
    presentation.stream_epoch = stream_epoch;
    presentation.journal_generation = generation;
    presentation.next_serial = 1;
    presentation.reset_count = reset_count;
    increment_saturated(presentation.reset_count);
    if (presentation.initialized)
        presentation.audit_cursor = make_cursor(1);
}

bool cursor_shape_valid(
    const cg_canonical_event_presentation_cursor_v1 *cursor)
{
    return cursor && cursor->struct_size == sizeof(*cursor) &&
           cursor->schema_version ==
               CG_CANONICAL_EVENT_PRESENTATION_VERSION &&
           cursor->stream_epoch != 0 &&
           cursor->journal_generation != 0 && cursor->next_serial != 0;
}

cg_canonical_event_presentation_cursor_v1 make_cursor(
    std::uint64_t next_serial)
{
    cg_canonical_event_presentation_cursor_v1 cursor{};
    cursor.struct_size = sizeof(cursor);
    cursor.schema_version = CG_CANONICAL_EVENT_PRESENTATION_VERSION;
    cursor.stream_epoch = presentation.stream_epoch;
    cursor.journal_generation = presentation.journal_generation;
    cursor.next_serial = next_serial;
    return cursor;
}

void ensure_serial_capacity(std::uint32_t incoming_count)
{
    if (incoming_count == 0 ||
        presentation.next_serial <= UINT64_MAX - incoming_count) {
        return;
    }

    /* A serial space reset is local presentation lifetime management. It does
     * not reset the validated producer stream or its ordering audit. */
    const std::uint64_t resets = presentation.reset_count;
    const std::uint64_t accepted_ranges = presentation.accepted_ranges;
    const std::uint64_t accepted_records = presentation.accepted_records;
    const std::uint64_t rejected_ranges = presentation.rejected_ranges;
    const std::uint64_t empty_carriers = presentation.empty_carriers;
    const std::uint64_t adapter_rejections =
        presentation.adapter_rejections;
    const std::uint64_t overwritten_records =
        presentation.overwritten_records >
                UINT64_MAX - presentation.retained_count
            ? UINT64_MAX
            : presentation.overwritten_records +
                  presentation.retained_count;
    const std::uint64_t audit_ready_records =
        presentation.audit_ready_records;
    const std::uint64_t audit_future_stalls =
        presentation.audit_future_stalls;
    const std::uint64_t audit_overrun_recoveries =
        presentation.audit_overrun_recoveries;
    const std::uint32_t stream_epoch = presentation.stream_epoch;
    std::uint32_t generation = presentation.journal_generation + 1u;
    if (!generation)
        generation = 1u;

    presentation = {};
    presentation.initialized = true;
    presentation.stream_epoch = stream_epoch;
    presentation.journal_generation = generation;
    presentation.next_serial = 1;
    presentation.reset_count = resets;
    presentation.accepted_ranges = accepted_ranges;
    presentation.accepted_records = accepted_records;
    presentation.rejected_ranges = rejected_ranges;
    presentation.empty_carriers = empty_carriers;
    presentation.adapter_rejections = adapter_rejections;
    presentation.overwritten_records = overwritten_records;
    presentation.audit_cursor = make_cursor(1);
    presentation.audit_ready_records = audit_ready_records;
    presentation.audit_future_stalls = audit_future_stalls;
    presentation.audit_overrun_recoveries = audit_overrun_recoveries;
    (void)CG_EventRuntimeResetLegacy(stream_epoch);
}

void reset_v1(uint32_t stream_epoch, uint32_t)
{
    Worr_CGameEventShadowAuditResetV1(&audit_v1, stream_epoch);
}

void consume_v1(const worr_cgame_event_range_v1 *range)
{
    /* Audit only.  No record pointer or record copy survives this callback,
     * and legacy parse_entity_event remains the sole presenter. */
    (void)Worr_CGameEventShadowAuditConsumeV1(&audit_v1, range);
}

bool get_status_v1(worr_cgame_event_shadow_audit_status_v1 *status_out)
{
    return Worr_CGameEventShadowAuditStatusV1(&audit_v1, status_out);
}

const worr_cgame_event_shadow_export_v1 event_shadow_api = {
    sizeof(event_shadow_api),
    WORR_CGAME_EVENT_SHADOW_API_VERSION,
    reset_v1,
    consume_v1,
    get_status_v1,
};

void reset_v2(uint32_t stream_epoch, uint32_t)
{
    Worr_CGameEventRangeAuditResetV2(&audit_v2, stream_epoch);
    reset_presentation(stream_epoch);
    (void)CG_EventRuntimeResetLegacy(stream_epoch);
}

void consume_v2(const worr_cgame_event_range_v2 *range)
{
    /* Validate the complete carrier/chunk ordering contract before copying any
     * record. Decode-time and entity-frame legacy presenters remain
     * authoritative until the durable journal's parity gate is promoted. */
    if (!Worr_CGameEventRangeAuditConsumeV2(&audit_v2, range)) {
        if (presentation.initialized)
            increment_saturated(presentation.rejected_ranges);
        return;
    }

    if (!presentation.initialized || !range ||
        range->stream_epoch != presentation.stream_epoch) {
        increment_saturated(presentation.rejected_ranges);
        return;
    }

    std::array<std::uint64_t, WORR_CGAME_EVENT_RANGE_MAX_RECORDS_V2>
        semantic_hashes{};
    for (std::uint32_t index = 0; index < range->count; ++index) {
        if (!Worr_EventRecordSemanticHashV1(
                &range->records[index],
                WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2,
                &semantic_hashes[index])) {
            increment_saturated(presentation.rejected_ranges);
            return;
        }
    }

    increment_saturated(presentation.accepted_ranges);
    if (range->adapter_status != WORR_CGAME_EVENT_ADAPTER_OK_V2)
        increment_saturated(presentation.adapter_rejections);
    if (range->count == 0) {
        increment_saturated(presentation.empty_carriers);
        return;
    }

    ensure_serial_capacity(range->count);
    for (std::uint32_t index = 0; index < range->count; ++index) {
        const std::uint64_t serial = presentation.next_serial++;
        const std::uint32_t slot = static_cast<std::uint32_t>(
            (serial - 1u) % CG_CANONICAL_EVENT_PRESENTATION_CAPACITY);
        auto &entry = presentation.entries[slot];
        entry = {};
        entry.struct_size = sizeof(entry);
        entry.schema_version = CG_CANONICAL_EVENT_PRESENTATION_VERSION;
        entry.stream_epoch = range->stream_epoch;
        entry.batch_generation = range->batch_generation;
        entry.journal_serial = serial;
        entry.carrier_sequence = range->carrier_sequence;
        entry.arrival_ordinal = range->first_arrival_ordinal + index;
        entry.carrier_time_us = range->carrier_time_us;
        entry.semantic_hash = semantic_hashes[index];
        entry.carrier_tick = range->carrier_tick;
        entry.phase = range->phase;
        entry.range_flags = range->flags;
        entry.carrier_kind = range->carrier_kind;
        entry.adapter_status = range->adapter_status;
        entry.record = range->records[index];

        if (presentation.retained_count ==
            CG_CANONICAL_EVENT_PRESENTATION_CAPACITY) {
            increment_saturated(presentation.overwritten_records);
        } else {
            ++presentation.retained_count;
        }
        increment_saturated(presentation.accepted_records);
        /* The runtime retains only this entry's join metadata. The value-owned
         * body remains solely in the presentation history above. */
        (void)CG_EventRuntimeObserveLegacyEntry(&entry);
    }
}

bool get_status_v2(worr_cgame_event_range_audit_status_v2 *status_out)
{
    return Worr_CGameEventRangeAuditStatusV2(&audit_v2, status_out);
}

const worr_cgame_event_range_export_v2 event_range_api_v2 = {
    sizeof(event_range_api_v2),
    WORR_CGAME_EVENT_RANGE_API_VERSION_V2,
    reset_v2,
    consume_v2,
    get_status_v2,
};

} // namespace

const worr_cgame_event_shadow_export_v1 *CG_GetEventShadowAPI()
{
    return &event_shadow_api;
}

const worr_cgame_event_range_export_v2 *CG_GetEventRangeAPIv2()
{
    return &event_range_api_v2;
}

cg_canonical_event_presentation_result_v1
CG_CanonicalEventPresentationBegin(
    cg_canonical_event_presentation_cursor_v1 *cursor_out)
{
    if (!cursor_out)
        return CG_CANONICAL_EVENT_PRESENTATION_INVALID_ARGUMENT;
    if (!presentation.initialized)
        return CG_CANONICAL_EVENT_PRESENTATION_UNINITIALIZED;
    *cursor_out = make_cursor(oldest_serial());
    return CG_CANONICAL_EVENT_PRESENTATION_OK;
}

cg_canonical_event_presentation_result_v1
CG_CanonicalEventPresentationTail(
    cg_canonical_event_presentation_cursor_v1 *cursor_out)
{
    if (!cursor_out)
        return CG_CANONICAL_EVENT_PRESENTATION_INVALID_ARGUMENT;
    if (!presentation.initialized)
        return CG_CANONICAL_EVENT_PRESENTATION_UNINITIALIZED;
    *cursor_out = make_cursor(presentation.next_serial);
    return CG_CANONICAL_EVENT_PRESENTATION_OK;
}

cg_canonical_event_presentation_result_v1
CG_CanonicalEventPresentationNext(
    const cg_canonical_event_presentation_cursor_v1 *cursor,
    cg_canonical_event_presentation_cursor_v1 *next_cursor_out,
    cg_canonical_event_presentation_entry_v1 *entry_out)
{
    if (!cursor_shape_valid(cursor) || !next_cursor_out || !entry_out)
        return CG_CANONICAL_EVENT_PRESENTATION_INVALID_ARGUMENT;
    if (!presentation.initialized)
        return CG_CANONICAL_EVENT_PRESENTATION_UNINITIALIZED;
    if (cursor->stream_epoch != presentation.stream_epoch ||
        cursor->journal_generation != presentation.journal_generation) {
        return CG_CANONICAL_EVENT_PRESENTATION_STALE_CURSOR;
    }

    const std::uint64_t oldest = oldest_serial();
    if (cursor->next_serial < oldest)
        return CG_CANONICAL_EVENT_PRESENTATION_CURSOR_OVERRUN;
    if (cursor->next_serial == presentation.next_serial)
        return CG_CANONICAL_EVENT_PRESENTATION_EMPTY;
    if (cursor->next_serial > presentation.next_serial)
        return CG_CANONICAL_EVENT_PRESENTATION_CORRUPT;

    const std::uint32_t slot = static_cast<std::uint32_t>(
        (cursor->next_serial - 1u) %
        CG_CANONICAL_EVENT_PRESENTATION_CAPACITY);
    const auto &entry = presentation.entries[slot];
    if (entry.struct_size != sizeof(entry) ||
        entry.schema_version != CG_CANONICAL_EVENT_PRESENTATION_VERSION ||
        entry.stream_epoch != presentation.stream_epoch ||
        entry.journal_serial != cursor->next_serial) {
        return CG_CANONICAL_EVENT_PRESENTATION_CORRUPT;
    }

    *entry_out = entry;
    *next_cursor_out = make_cursor(cursor->next_serial + 1u);
    return CG_CANONICAL_EVENT_PRESENTATION_OK;
}

bool CG_CanonicalEventPresentationGetStatus(
    cg_canonical_event_presentation_status_v1 *status_out)
{
    if (!status_out || !presentation.initialized)
        return false;

    cg_canonical_event_presentation_status_v1 status{};
    status.struct_size = sizeof(status);
    status.schema_version = CG_CANONICAL_EVENT_PRESENTATION_VERSION;
    status.stream_epoch = presentation.stream_epoch;
    status.journal_generation = presentation.journal_generation;
    status.capacity = CG_CANONICAL_EVENT_PRESENTATION_CAPACITY;
    status.retained_count = presentation.retained_count;
    status.oldest_serial = oldest_serial();
    status.next_serial = presentation.next_serial;
    status.reset_count = presentation.reset_count;
    status.accepted_ranges = presentation.accepted_ranges;
    status.accepted_records = presentation.accepted_records;
    status.rejected_ranges = presentation.rejected_ranges;
    status.empty_carriers = presentation.empty_carriers;
    status.adapter_rejections = presentation.adapter_rejections;
    status.overwritten_records = presentation.overwritten_records;
    status.audit_ready_records = presentation.audit_ready_records;
    status.audit_future_stalls = presentation.audit_future_stalls;
    status.audit_overrun_recoveries =
        presentation.audit_overrun_recoveries;
    status.audit_last_ready_serial = presentation.audit_last_ready_serial;
    status.audit_last_render_time_us =
        presentation.audit_last_render_time_us;
    if (!Worr_CGameEventRangeAuditStatusV2(&audit_v2,
                                            &status.range_audit)) {
        return false;
    }
    *status_out = status;
    return true;
}

cg_canonical_event_presentation_result_v1
CG_CanonicalEventPresentationAdvanceAudit(
    std::uint64_t render_time_us,
    std::uint32_t max_records,
    std::uint32_t *advanced_out)
{
    if (!advanced_out || max_records == 0)
        return CG_CANONICAL_EVENT_PRESENTATION_INVALID_ARGUMENT;
    if (!presentation.initialized)
        return CG_CANONICAL_EVENT_PRESENTATION_UNINITIALIZED;

    std::uint32_t advanced = 0;
    while (advanced < max_records) {
        cg_canonical_event_presentation_cursor_v1 next{};
        cg_canonical_event_presentation_entry_v1 entry{};
        const auto result = CG_CanonicalEventPresentationNext(
            &presentation.audit_cursor, &next, &entry);
        if (result == CG_CANONICAL_EVENT_PRESENTATION_EMPTY) {
            presentation.audit_last_render_time_us = render_time_us;
            *advanced_out = advanced;
            return CG_CANONICAL_EVENT_PRESENTATION_OK;
        }
        if (result == CG_CANONICAL_EVENT_PRESENTATION_CURSOR_OVERRUN) {
            increment_saturated(presentation.audit_overrun_recoveries);
            presentation.audit_cursor = make_cursor(oldest_serial());
            presentation.audit_last_render_time_us = render_time_us;
            *advanced_out = advanced;
            return result;
        }
        if (result != CG_CANONICAL_EVENT_PRESENTATION_OK) {
            *advanced_out = advanced;
            return result;
        }
        if (entry.record.source_time_us > render_time_us) {
            increment_saturated(presentation.audit_future_stalls);
            presentation.audit_last_render_time_us = render_time_us;
            *advanced_out = advanced;
            return CG_CANONICAL_EVENT_PRESENTATION_NOT_READY;
        }

        presentation.audit_cursor = next;
        presentation.audit_last_ready_serial = entry.journal_serial;
        increment_saturated(presentation.audit_ready_records);
        ++advanced;
    }

    presentation.audit_last_render_time_us = render_time_us;
    *advanced_out = advanced;
    return CG_CANONICAL_EVENT_PRESENTATION_OK;
}
