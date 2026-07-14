/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <stddef.h>

#include "common/net/capability.h"
#include "common/net/chan.h"
#include "shared/command_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Default-off FR-10-T04 client readiness pilot.  The owner is process-static
 * and deliberately independent of client_state_t, which is wiped by
 * CL_ClearState during serverdata/map transitions.
 */
void CL_NativeReadinessPilotRegisterCvar(void);

/* Called only after Netchan_Setup and q2proto client-context initialization. */
bool CL_NativeReadinessPilotBeginConnection(netchan_t *channel);

/* Must precede every close, replacement, or final disconnect transmit. */
void CL_NativeReadinessPilotBeforeNetchanClose(netchan_t *channel);

/* Map-local lifecycle.  Neither operation resamples the opt-in cvar. */
void CL_NativeReadinessPilotQuiesceMap(void);
void CL_NativeReadinessPilotServerDataReset(void);

/* Canonical packet transaction hooks for the SVC setting carrier. */
void CL_NativeReadinessPilotPacketBegin(void);
void CL_NativeReadinessPilotPacketEnd(void);
bool CL_NativeReadinessPilotObserveSetting(int32_t index, int32_t value);
void CL_NativeReadinessPilotObserveInterveningService(void);

/* Called only after the ordinary public capability core confirms a tuple. */
void CL_NativeReadinessPilotCapabilityConfirmed(
    const worr_net_capability_state_v1 *capability_state);

/*
 * Observational command-shadow input.  Finalized commands are supplied in
 * canonical sequence order; an encoded range is reported only after its
 * legacy MOVE/BATCH bytes have been committed to the outgoing message.  The
 * pilot never changes which legacy commands are authoritative or transmitted.
 */
void CL_NativeReadinessPilotObserveFinalizedCommand(
    uint32_t legacy_command_number,
    const worr_command_id_v1 *command_id,
    const worr_prediction_command_v1 *command);
void CL_NativeReadinessPilotObserveEncodedCommandRange(
    uint32_t first_legacy_command_number,
    uint32_t command_count);

/* Private readiness carrier settings must not enter demos or GTV streams. */
bool CL_NativeReadinessPilotIsCarrierSetting(int32_t index);

/*
 * Pointer-free production observability for the default-off pilot.  Counters
 * saturate at UINT64_MAX and cover the current connection owner.  The getter
 * is read-only and remains available while the pilot is disabled so runtime
 * acceptance tools can distinguish an inactive pilot from a missing row.
 */
enum {
    CL_NATIVE_READINESS_PILOT_STATUS_ABI_V1 = 1,
};

typedef struct cl_native_readiness_pilot_status_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t reserved0;
    uint32_t enabled;
    uint32_t mode;
    uint32_t hooks;
    uint32_t capability_confirmed;
    uint32_t readiness_phase;
    uint32_t official_epoch;
    uint32_t transport_epoch;
    uint32_t protocol;
    uint32_t public_mask;
    uint32_t private_mask;
    uint32_t probe_hold;
    uint32_t reserved_counters;
    uint64_t challenges;
    uint64_t client_ready_queued;
    uint64_t server_active;
    uint64_t proof_enqueued;
    uint64_t retained;
    uint64_t retained_highwater;
    uint64_t retained_releases;
    uint64_t tx_first_sends;
    uint64_t tx_retries;
    uint64_t tx_handoffs;
    uint64_t ack_carriers;
    uint64_t acknowledged_reliable;
    uint64_t drains;
    uint64_t failures;
    uint32_t last_failure;
    uint32_t reserved1;
} cl_native_readiness_pilot_status_v1;

#if defined(__cplusplus)
static_assert(sizeof(cl_native_readiness_pilot_status_v1) == 176,
              "client native readiness status V1 layout changed");
static_assert(offsetof(cl_native_readiness_pilot_status_v1, challenges) == 56,
              "client native readiness status V1 counter offset changed");
static_assert(offsetof(cl_native_readiness_pilot_status_v1, last_failure) == 168,
              "client native readiness status V1 tail offset changed");
#else
_Static_assert(sizeof(cl_native_readiness_pilot_status_v1) == 176,
               "client native readiness status V1 layout changed");
_Static_assert(offsetof(cl_native_readiness_pilot_status_v1, challenges) == 56,
               "client native readiness status V1 counter offset changed");
_Static_assert(offsetof(cl_native_readiness_pilot_status_v1, last_failure) == 168,
               "client native readiness status V1 tail offset changed");
#endif

bool CL_NativeReadinessPilotGetStatusV1(
    cl_native_readiness_pilot_status_v1 *status_out);

#if defined(WORR_NATIVE_READINESS_PILOT_TESTING)
typedef struct cl_native_readiness_pilot_test_state_s {
    uint32_t transport_epoch;
    uint32_t retained_messages;
    uint32_t retained_payloads;
    uint32_t retired_transport_epoch;
    uint32_t retired_messages;
    uint32_t retired_payloads;
    uint32_t message_sequence_highwater;
    uint32_t selected_send_attempts;
    uint32_t mode;
    bool hooks_installed;
    bool readiness_committed;
    bool proof_enqueued_once;
    bool carrier_traffic_seen;
} cl_native_readiness_pilot_test_state_t;

bool CL_NativeReadinessPilotGetTestState(
    cl_native_readiness_pilot_test_state_t *state_out);
#endif

#ifdef __cplusplus
}
#endif
