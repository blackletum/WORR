/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/native_event_admission.h"
#include "shared/cgame_event_runtime.h"
#include "shared/event_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

bool CL_CGameEventRuntimeSetConsumer(
    const worr_cgame_event_runtime_export_v1 *consumer);
worr_cgame_event_runtime_result_v1
CL_CGameEventRuntimeResetAuthority(uint32_t stream_epoch,
                                   uint32_t first_sequence);
worr_cgame_event_runtime_result_v1
CL_CGameEventRuntimeObserveDescriptor(
    const worr_event_stream_descriptor_v1 *descriptor);

/*
 * Map/serverdata quiesce scrubs attached cgame authority but deliberately
 * retains the connection-lifetime epoch high-water and establishes a resync
 * barrier.  Full disconnect is the sole high-water reset boundary.
 */
worr_cgame_event_runtime_result_v1
CL_CGameEventRuntimeQuiesceAuthority(void);
worr_cgame_event_runtime_result_v1
CL_CGameEventRuntimeResetConnection(void);
worr_cgame_event_runtime_result_v1
CL_CGameEventRuntimeSubmitAuthoritativeBatch(
    const worr_event_record_v1 *records, uint32_t count);
bool CL_CGameEventRuntimeGetStatus(
    worr_cgame_event_runtime_status_v1 *status_out);
bool CL_CGameEventRuntimeRequiresResync(void);

typedef struct cl_cgame_event_runtime_diagnostic_v1_s {
    uint32_t reset_epoch;
    uint32_t reset_sequence;
    uint32_t reset_result;
    uint32_t reset_consumer_attached;
    uint32_t status_valid;
    uint32_t status_consumer_attached;
    uint32_t status_owner_failure;
    worr_cgame_event_runtime_status_v1 status;
} cl_cgame_event_runtime_diagnostic_v1;

bool CL_CGameEventRuntimeGetDiagnosticV1(
    cl_cgame_event_runtime_diagnostic_v1 *diagnostic_out);

/* Builds the callback table consumed by the transport-neutral admission core. */
bool CL_CGameEventRuntimeGetNativeConsumerV1(
    worr_native_event_consumer_v1 *consumer_out);

#ifdef __cplusplus
}
#endif
