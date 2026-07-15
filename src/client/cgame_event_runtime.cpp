/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "client/cgame_event_runtime.h"

namespace {

const worr_cgame_event_runtime_export_v1 *event_runtime_consumer;
uint32_t authority_epoch;
uint32_t authority_first_sequence;
uint32_t authority_epoch_high_water;
bool authority_requires_resync;
uint8_t native_consumer_cookie;

enum class status_check_t {
    valid,
    requires_resync,
    invalid,
};

bool result_valid(worr_cgame_event_runtime_result_v1 result)
{
    return result <= WORR_CGAME_EVENT_RUNTIME_REENTRANT;
}

bool consumer_valid(const worr_cgame_event_runtime_export_v1 *consumer)
{
    return consumer && consumer->struct_size == sizeof(*consumer) &&
           consumer->api_version == WORR_CGAME_EVENT_RUNTIME_API_VERSION &&
           consumer->ResetAuthority && consumer->SubmitAuthoritativeBatch &&
           consumer->GetStatus;
}

bool status_structurally_valid(
    const worr_cgame_event_runtime_status_v1 &status)
{
    constexpr uint32_t known_flags =
        WORR_CGAME_EVENT_RUNTIME_STATE_ACTIVE |
        WORR_CGAME_EVENT_RUNTIME_STATE_DEGRADED |
        WORR_CGAME_EVENT_RUNTIME_STATE_AUDIT_ENABLED |
        WORR_CGAME_EVENT_RUNTIME_STATE_REQUIRES_RESYNC;
    if (status.struct_size != sizeof(status) ||
        status.api_version != WORR_CGAME_EVENT_RUNTIME_API_VERSION ||
        (status.state_flags & ~known_flags) != 0) {
        return false;
    }
    if ((status.state_flags & WORR_CGAME_EVENT_RUNTIME_STATE_ACTIVE) == 0) {
        return (status.state_flags &
                (WORR_CGAME_EVENT_RUNTIME_STATE_DEGRADED |
                 WORR_CGAME_EVENT_RUNTIME_STATE_REQUIRES_RESYNC)) == 0 &&
               status.authority_epoch == 0 &&
               status.next_presentation_sequence == 0 &&
               status.authority_count == 0 &&
               status.receipt.struct_size == 0 &&
               status.receipt.schema_version == 0 &&
               status.receipt.stream_epoch == 0 &&
               status.receipt.highest_contiguous == 0 &&
               status.receipt.selective_mask == 0;
    }
    return status.authority_epoch != 0 &&
           status.next_presentation_sequence != 0 &&
           status.receipt.struct_size == sizeof(status.receipt) &&
           status.receipt.schema_version == WORR_EVENT_ABI_VERSION &&
           status.receipt.stream_epoch == status.authority_epoch;
}

bool read_status(worr_cgame_event_runtime_status_v1 &status)
{
    status = {};
    return event_runtime_consumer &&
           event_runtime_consumer->GetStatus(&status) &&
           status_structurally_valid(status);
}

bool inactive_status(const worr_cgame_event_runtime_status_v1 &status)
{
    return (status.state_flags & WORR_CGAME_EVENT_RUNTIME_STATE_ACTIVE) == 0;
}

bool active_status_matches(
    const worr_cgame_event_runtime_status_v1 &status,
    uint32_t expected_epoch, uint32_t first_sequence, bool fresh)
{
    if ((status.state_flags & WORR_CGAME_EVENT_RUNTIME_STATE_ACTIVE) == 0 ||
        status.authority_epoch != expected_epoch ||
        status.next_presentation_sequence < first_sequence ||
        status.receipt.highest_contiguous < first_sequence - 1u ||
        static_cast<uint64_t>(status.next_presentation_sequence) >
            static_cast<uint64_t>(status.receipt.highest_contiguous) + 1u) {
        return false;
    }
    if (!fresh)
        return true;
    return (status.state_flags &
            (WORR_CGAME_EVENT_RUNTIME_STATE_DEGRADED |
             WORR_CGAME_EVENT_RUNTIME_STATE_REQUIRES_RESYNC)) == 0 &&
           status.next_presentation_sequence == first_sequence &&
           status.authority_count == 0 &&
           status.receipt.highest_contiguous == first_sequence - 1u &&
           status.receipt.selective_mask == 0;
}

void remember_active_loss()
{
    if (authority_epoch != 0) {
        authority_requires_resync = true;
    }
    authority_epoch = 0;
    authority_first_sequence = 0;
}

void quarantine_consumer()
{
    const auto *doomed = event_runtime_consumer;
    remember_active_loss();
    event_runtime_consumer = nullptr;
    if (doomed)
        (void)doomed->ResetAuthority(0, 0);
}

void quarantine_consumer_after_reset_attempt(uint32_t attempted_epoch)
{
    const auto *doomed = event_runtime_consumer;
    if (attempted_epoch > authority_epoch_high_water)
        authority_epoch_high_water = attempted_epoch;
    authority_requires_resync = true;
    authority_epoch = 0;
    authority_first_sequence = 0;
    event_runtime_consumer = nullptr;
    if (doomed)
        (void)doomed->ResetAuthority(0, 0);
}

bool reset_attached_consumer(uint32_t stream_epoch,
                             uint32_t first_sequence)
{
    const auto result = event_runtime_consumer->ResetAuthority(
        stream_epoch, first_sequence);
    if (!result_valid(result) || result != WORR_CGAME_EVENT_RUNTIME_OK)
        return false;

    worr_cgame_event_runtime_status_v1 status{};
    if (!read_status(status))
        return false;
    return stream_epoch == 0
               ? inactive_status(status)
               : active_status_matches(status, stream_epoch,
                                       first_sequence, true);
}

status_check_t check_status_against_owner(
    const worr_cgame_event_runtime_status_v1 &status)
{
    if (authority_epoch == 0)
        return inactive_status(status) ? status_check_t::valid
                                       : status_check_t::invalid;
    if (!active_status_matches(status, authority_epoch,
                               authority_first_sequence, false)) {
        return status_check_t::invalid;
    }
    if ((status.state_flags &
         WORR_CGAME_EVENT_RUNTIME_STATE_REQUIRES_RESYNC) != 0) {
        return status_check_t::requires_resync;
    }
    return status_check_t::valid;
}

void enter_runtime_requested_resync()
{
    remember_active_loss();
    if (!event_runtime_consumer)
        return;

    const auto result = event_runtime_consumer->ResetAuthority(0, 0);
    worr_cgame_event_runtime_status_v1 status{};
    if (!result_valid(result) || result != WORR_CGAME_EVENT_RUNTIME_OK ||
        !read_status(status) || !inactive_status(status)) {
        event_runtime_consumer = nullptr;
    }
}

} // namespace

namespace {

worr_cgame_event_runtime_result_v1 native_reset_authority(
    void *opaque, uint32_t stream_epoch, uint32_t first_sequence)
{
    if (opaque != &native_consumer_cookie)
        return WORR_CGAME_EVENT_RUNTIME_INVALID_ARGUMENT;
    /* Admission recovery uses {0,0} as a semantic scrub, not a physical
     * disconnect. Preserve the engine owner's connection-lifetime high-water
     * so a stale descriptor cannot be accepted through the direct API. */
    if (stream_epoch == 0 && first_sequence == 0)
        return CL_CGameEventRuntimeQuiesceAuthority();
    return CL_CGameEventRuntimeResetAuthority(
        stream_epoch, first_sequence);
}

worr_cgame_event_runtime_result_v1 native_submit_authority(
    void *opaque, const worr_event_record_v1 *records, uint32_t count)
{
    if (opaque != &native_consumer_cookie)
        return WORR_CGAME_EVENT_RUNTIME_INVALID_ARGUMENT;
    return CL_CGameEventRuntimeSubmitAuthoritativeBatch(records, count);
}

bool native_get_status(
    void *opaque, worr_cgame_event_runtime_status_v1 *status_out)
{
    return opaque == &native_consumer_cookie &&
           CL_CGameEventRuntimeGetStatus(status_out);
}

} // namespace

bool CL_CGameEventRuntimeSetConsumer(
    const worr_cgame_event_runtime_export_v1 *consumer)
{
    if (consumer && !consumer_valid(consumer))
        return false;
    if (consumer == event_runtime_consumer)
        return true;

    if (event_runtime_consumer) {
        const auto *old_consumer = event_runtime_consumer;
        event_runtime_consumer = nullptr;
        const bool active_was_lost = authority_epoch != 0;
        (void)old_consumer->ResetAuthority(0, 0);
        if (active_was_lost)
            remember_active_loss();
    }

    event_runtime_consumer = consumer;
    if (!event_runtime_consumer)
        return true;

    const uint32_t replay_epoch = authority_requires_resync
                                      ? 0u
                                      : authority_epoch;
    const uint32_t replay_sequence = replay_epoch
                                         ? authority_first_sequence
                                         : 0u;
    if (reset_attached_consumer(replay_epoch, replay_sequence))
        return true;

    (void)event_runtime_consumer->ResetAuthority(0, 0);
    event_runtime_consumer = nullptr;
    if (replay_epoch != 0)
        remember_active_loss();
    return false;
}

worr_cgame_event_runtime_result_v1
CL_CGameEventRuntimeResetAuthority(uint32_t stream_epoch,
                                   uint32_t first_sequence)
{
    if ((stream_epoch == 0) != (first_sequence == 0))
        return WORR_CGAME_EVENT_RUNTIME_INVALID_ARGUMENT;

    if (stream_epoch == 0) {
        worr_cgame_event_runtime_result_v1 result =
            WORR_CGAME_EVENT_RUNTIME_OK;
        bool consumer_healthy = true;
        if (event_runtime_consumer) {
            result = event_runtime_consumer->ResetAuthority(0, 0);
            consumer_healthy = result_valid(result) &&
                               result == WORR_CGAME_EVENT_RUNTIME_OK;
            worr_cgame_event_runtime_status_v1 status{};
            if (consumer_healthy &&
                (!read_status(status) || !inactive_status(status))) {
                consumer_healthy = false;
                result = WORR_CGAME_EVENT_RUNTIME_DEGRADED;
            }
            if (!result_valid(result))
                result = WORR_CGAME_EVENT_RUNTIME_DEGRADED;
            if (!consumer_healthy)
                event_runtime_consumer = nullptr;
        }
        authority_epoch = 0;
        authority_first_sequence = 0;
        authority_epoch_high_water = 0;
        authority_requires_resync = false;
        return result;
    }

    if (authority_epoch == stream_epoch) {
        return authority_first_sequence == first_sequence
                   ? WORR_CGAME_EVENT_RUNTIME_OK
                   : WORR_CGAME_EVENT_RUNTIME_WRONG_EPOCH;
    }
    if (stream_epoch <= authority_epoch_high_water) {
        return WORR_CGAME_EVENT_RUNTIME_WRONG_EPOCH;
    }

    if (event_runtime_consumer) {
        const auto result = event_runtime_consumer->ResetAuthority(
            stream_epoch, first_sequence);
        if (!result_valid(result) || result != WORR_CGAME_EVENT_RUNTIME_OK) {
            const auto reported = result_valid(result)
                                      ? result
                                      : WORR_CGAME_EVENT_RUNTIME_DEGRADED;
            /* The callback was invoked and may have partially installed the
             * attempted authority. Burn that epoch even though publication
             * failed so direct and native owners remain fail-closed. */
            quarantine_consumer_after_reset_attempt(stream_epoch);
            return reported;
        }
        worr_cgame_event_runtime_status_v1 status{};
        if (!read_status(status) ||
            !active_status_matches(status, stream_epoch,
                                   first_sequence, true)) {
            quarantine_consumer_after_reset_attempt(stream_epoch);
            return WORR_CGAME_EVENT_RUNTIME_DEGRADED;
        }
    }

    authority_epoch = stream_epoch;
    authority_first_sequence = first_sequence;
    authority_epoch_high_water = stream_epoch;
    authority_requires_resync = false;
    return WORR_CGAME_EVENT_RUNTIME_OK;
}

worr_cgame_event_runtime_result_v1
CL_CGameEventRuntimeObserveDescriptor(
    const worr_event_stream_descriptor_v1 *descriptor)
{
    if (!Worr_EventStreamDescriptorValidateV1(descriptor))
        return WORR_CGAME_EVENT_RUNTIME_INVALID_ARGUMENT;
    return CL_CGameEventRuntimeResetAuthority(
        descriptor->stream_epoch, descriptor->first_sequence);
}

worr_cgame_event_runtime_result_v1
CL_CGameEventRuntimeQuiesceAuthority(void)
{
    worr_cgame_event_runtime_result_v1 result =
        WORR_CGAME_EVENT_RUNTIME_OK;
    bool consumer_healthy = true;

    if (event_runtime_consumer) {
        result = event_runtime_consumer->ResetAuthority(0, 0);
        consumer_healthy = result_valid(result) &&
                           result == WORR_CGAME_EVENT_RUNTIME_OK;
        worr_cgame_event_runtime_status_v1 status{};
        if (consumer_healthy &&
            (!read_status(status) || !inactive_status(status))) {
            consumer_healthy = false;
            result = WORR_CGAME_EVENT_RUNTIME_DEGRADED;
        }
        if (!result_valid(result))
            result = WORR_CGAME_EVENT_RUNTIME_DEGRADED;
        if (!consumer_healthy)
            event_runtime_consumer = nullptr;
    }

    if (authority_epoch != 0)
        authority_requires_resync = true;
    authority_epoch = 0;
    authority_first_sequence = 0;
    return result;
}

worr_cgame_event_runtime_result_v1
CL_CGameEventRuntimeResetConnection(void)
{
    return CL_CGameEventRuntimeResetAuthority(0, 0);
}

worr_cgame_event_runtime_result_v1
CL_CGameEventRuntimeSubmitAuthoritativeBatch(
    const worr_event_record_v1 *records, uint32_t count)
{
    if (!records || count == 0 ||
        count > WORR_CGAME_EVENT_RUNTIME_MAX_BATCH) {
        return WORR_CGAME_EVENT_RUNTIME_INVALID_ARGUMENT;
    }
    if (authority_requires_resync)
        return WORR_CGAME_EVENT_RUNTIME_NOT_READY;
    if (!event_runtime_consumer || authority_epoch == 0)
        return WORR_CGAME_EVENT_RUNTIME_UNINITIALIZED;

    const auto result =
        event_runtime_consumer->SubmitAuthoritativeBatch(records, count);
    if (!result_valid(result)) {
        quarantine_consumer();
        return WORR_CGAME_EVENT_RUNTIME_DEGRADED;
    }

    worr_cgame_event_runtime_status_v1 status{};
    if (!read_status(status)) {
        quarantine_consumer();
        return WORR_CGAME_EVENT_RUNTIME_DEGRADED;
    }
    const auto status_check = check_status_against_owner(status);
    if (status_check == status_check_t::invalid) {
        quarantine_consumer();
        return WORR_CGAME_EVENT_RUNTIME_DEGRADED;
    }
    if (status_check == status_check_t::requires_resync) {
        enter_runtime_requested_resync();
        return WORR_CGAME_EVENT_RUNTIME_NOT_READY;
    }
    return result;
}

bool CL_CGameEventRuntimeGetStatus(
    worr_cgame_event_runtime_status_v1 *status_out)
{
    if (!status_out || !event_runtime_consumer)
        return false;
    worr_cgame_event_runtime_status_v1 status{};
    if (!read_status(status)) {
        quarantine_consumer();
        return false;
    }
    const auto status_check = check_status_against_owner(status);
    if (status_check == status_check_t::invalid) {
        quarantine_consumer();
        return false;
    }
    if (status_check == status_check_t::requires_resync) {
        enter_runtime_requested_resync();
        return false;
    }
    *status_out = status;
    return true;
}

bool CL_CGameEventRuntimeRequiresResync(void)
{
    return authority_requires_resync;
}

bool CL_CGameEventRuntimeGetNativeConsumerV1(
    worr_native_event_consumer_v1 *consumer_out)
{
    if (!consumer_out)
        return false;
    worr_native_event_consumer_v1 consumer{};
    consumer.struct_size = sizeof(consumer);
    consumer.schema_version = WORR_NATIVE_EVENT_ADMISSION_ABI_VERSION;
    consumer.opaque = &native_consumer_cookie;
    consumer.ResetAuthority = native_reset_authority;
    consumer.SubmitAuthoritativeBatch = native_submit_authority;
    consumer.GetStatus = native_get_status;
    *consumer_out = consumer;
    return true;
}
