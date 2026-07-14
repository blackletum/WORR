/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "client.h"

#include "client/snapshot_recovery.h"
#include "client/snapshot_shadow.h"

#include <cinttypes>
#include <cstring>

namespace {

struct snapshot_recovery_runtime_t {
    worr_snapshot_recovery_config_v1 config{};
    worr_snapshot_recovery_state_v1 state{};
    worr_snapshot_recovery_decision_v1 last_decision{};
    cvar_t *enabled{};
    cvar_t *debug{};
    uint32_t last_result{};
    uint64_t observation_failures{};
    uint64_t decision_failures{};
    uint64_t disabled_request_blocks{};
    uint64_t applied_full_overrides{};
    uint64_t inherited_legacy_full_requests{};
    uint64_t ignored_nontransport_rejections{};
};

snapshot_recovery_runtime_t recovery;

void increment_saturating(uint64_t &value)
{
    if (value != UINT64_MAX)
        ++value;
}

uint32_t snapshot_number_or_zero(int32_t snapshot_number)
{
    return snapshot_number > 0
        ? static_cast<uint32_t>(snapshot_number)
        : 0;
}

void initialize_observation(
    worr_snapshot_recovery_observation_v1 &observation,
    uint32_t type, uint32_t reason_mask, int32_t snapshot_number,
    int32_t base_snapshot_number)
{
    observation = {};
    observation.struct_size = sizeof(observation);
    observation.schema_version = WORR_SNAPSHOT_RECOVERY_VERSION;
    observation.type = type;
    observation.reason_mask = reason_mask;
    observation.snapshot_number = snapshot_number_or_zero(snapshot_number);
    observation.base_snapshot_number = base_snapshot_number;
}

void observe(const worr_snapshot_recovery_observation_v1 &observation)
{
    const bool was_active = recovery.state.active != 0;
    recovery.last_result = Worr_SnapshotRecoveryObserveV1(
        &recovery.state, &recovery.config, &observation);
    if (recovery.last_result != WORR_SNAPSHOT_RECOVERY_OK) {
        increment_saturating(recovery.observation_failures);
        if (recovery.debug && recovery.debug->integer) {
            Com_DPrintf("snapshot recovery: observation rejected (%u)\n",
                        recovery.last_result);
        }
        return;
    }

    if (!was_active && recovery.state.active && recovery.debug &&
        recovery.debug->integer) {
        Com_DPrintf(
            "snapshot recovery: armed generation %u reasons 0x%x\n",
            recovery.state.request_generation,
            recovery.state.reason_mask);
    }
}

uint32_t legacy_failure_reasons(uint32_t frame_flags)
{
    uint32_t reasons = 0;

    if ((frame_flags & FF_OLDFRAME) != 0)
        reasons |= WORR_SNAPSHOT_RECOVERY_REASON_INVALID_BASE;
    if ((frame_flags & FF_BADFRAME) != 0)
        reasons |= WORR_SNAPSHOT_RECOVERY_REASON_INVALID_FRAME;
    if ((frame_flags & FF_OLDENT) != 0)
        reasons |= WORR_SNAPSHOT_RECOVERY_REASON_ENTITY_HISTORY;
    if ((frame_flags & FF_SERVERDROP) != 0)
        reasons |= WORR_SNAPSHOT_RECOVERY_REASON_SEQUENCE_GAP;
    if (reasons == 0)
        reasons = WORR_SNAPSHOT_RECOVERY_REASON_INVALID_FRAME;
    return reasons;
}

} // namespace

extern "C" void CL_SnapshotRecoveryInit(void)
{
    recovery.enabled =
        Cvar_Get("cl_snapshot_recovery", "0", CVAR_ARCHIVE);
    recovery.debug = Cvar_Get("cl_snapshot_recovery_debug", "0", 0);
    CL_SnapshotRecoveryReset();
}

extern "C" void CL_SnapshotRecoveryBeginConnection(void)
{
    if (!recovery.enabled || !recovery.debug)
        CL_SnapshotRecoveryInit();
    else
        CL_SnapshotRecoveryReset();
}

extern "C" void CL_SnapshotRecoveryReset(void)
{
    cvar_t *enabled = recovery.enabled;
    cvar_t *debug = recovery.debug;

    recovery = {};
    recovery.enabled = enabled;
    recovery.debug = debug;
    Worr_SnapshotRecoveryDefaultConfigV1(&recovery.config);
    recovery.last_result =
        Worr_SnapshotRecoveryResetV1(&recovery.state);
}

extern "C" void CL_SnapshotRecoveryObserveLegacyFrame(
    int32_t snapshot_number, int32_t base_snapshot_number,
    bool valid, uint32_t legacy_frame_flags)
{
    worr_snapshot_recovery_observation_v1 observation{};

    if (valid) {
        initialize_observation(
            observation,
            base_snapshot_number <= 0
                ? WORR_SNAPSHOT_RECOVERY_OBSERVE_ACCEPTED_KEYFRAME
                : WORR_SNAPSHOT_RECOVERY_OBSERVE_ACCEPTED_DELTA,
            0, snapshot_number, base_snapshot_number);
    } else {
        initialize_observation(
            observation,
            WORR_SNAPSHOT_RECOVERY_OBSERVE_LEGACY_FAILURE,
            legacy_failure_reasons(legacy_frame_flags), snapshot_number,
            base_snapshot_number);
    }
    observe(observation);
}

extern "C" void CL_SnapshotRecoveryObserveCanonicalFrame(
    int32_t snapshot_number, int32_t base_snapshot_number,
    bool canonical_ok, bool shadow_active, bool controlled_entity_valid,
    uint32_t projector_result, uint32_t parity_mismatch)
{
    worr_snapshot_recovery_observation_v1 observation{};
    uint32_t reason_mask = 0;

    if (!shadow_active || !controlled_entity_valid)
        return;
    if (canonical_ok) {
        initialize_observation(
            observation,
            WORR_SNAPSHOT_RECOVERY_OBSERVE_CANONICAL_SUCCESS, 0,
            snapshot_number, base_snapshot_number);
        observe(observation);
        return;
    }

    if (parity_mismatch != 0)
        reason_mask |= WORR_SNAPSHOT_RECOVERY_REASON_CANONICAL_PARITY;
    if (projector_result != WORR_SNAPSHOT_Q2PROTO_OK)
        reason_mask |= WORR_SNAPSHOT_RECOVERY_REASON_CANONICAL_PROJECTOR;
    if (reason_mask == 0) {
        increment_saturating(recovery.ignored_nontransport_rejections);
        return;
    }

    initialize_observation(
        observation,
        WORR_SNAPSHOT_RECOVERY_OBSERVE_CANONICAL_FAILURE,
        reason_mask, snapshot_number, base_snapshot_number);
    observe(observation);
}

extern "C" int32_t CL_SnapshotRecoverySelectLastFrame(
    int32_t legacy_lastframe)
{
    if (!recovery.state.active)
        return legacy_lastframe;

    /* Legacy invalid-frame recovery is already authoritative and must never
     * be weakened by the optional policy's retry budget. */
    if (legacy_lastframe <= 0) {
        increment_saturating(recovery.inherited_legacy_full_requests);
        return legacy_lastframe;
    }

    if (!recovery.enabled || !recovery.enabled->integer) {
        increment_saturating(recovery.disabled_request_blocks);
        return legacy_lastframe;
    }

    recovery.last_result = Worr_SnapshotRecoveryDecideV1(
        &recovery.state, &recovery.config, &recovery.last_decision);
    if (recovery.last_result != WORR_SNAPSHOT_RECOVERY_OK) {
        increment_saturating(recovery.decision_failures);
        return legacy_lastframe;
    }
    if ((recovery.last_decision.flags &
         WORR_SNAPSHOT_RECOVERY_DECISION_REQUEST_FULL) == 0) {
        return legacy_lastframe;
    }

    increment_saturating(recovery.applied_full_overrides);
    if (recovery.debug && recovery.debug->integer) {
        Com_DPrintf(
            "snapshot recovery: requesting full snapshot generation %u "
            "attempt %u reasons 0x%x\n",
            recovery.last_decision.request_generation,
            recovery.last_decision.attempt_in_burst,
            recovery.last_decision.reason_mask);
    }
    return -1;
}

extern "C" bool CL_SnapshotRecoveryGetStatus(
    cl_snapshot_recovery_status_v1 *status_out)
{
    if (!status_out)
        return false;
    *status_out = {};
    status_out->struct_size = sizeof(*status_out);
    status_out->schema_version = CL_SNAPSHOT_RECOVERY_STATUS_VERSION;
    status_out->enabled =
        recovery.enabled && recovery.enabled->integer ? 1u : 0u;
    status_out->last_decision_flags = recovery.last_decision.flags;
    status_out->last_result = recovery.last_result;
    status_out->observation_failures = recovery.observation_failures;
    status_out->decision_failures = recovery.decision_failures;
    status_out->disabled_request_blocks =
        recovery.disabled_request_blocks;
    status_out->applied_full_overrides = recovery.applied_full_overrides;
    status_out->inherited_legacy_full_requests =
        recovery.inherited_legacy_full_requests;
    status_out->ignored_nontransport_rejections =
        recovery.ignored_nontransport_rejections;
    status_out->core = recovery.state;
    return true;
}

extern "C" void CL_SnapshotRecoveryStatus_f(void)
{
    cl_snapshot_recovery_status_v1 status{};

    if (!CL_SnapshotRecoveryGetStatus(&status))
        return;
    Com_Printf(
        "snapshot recovery: enabled=%u active=%u exhausted=%u generation=%u "
        "reasons=0x%x legacy_streak=%u canonical_streak=%u attempts=%u "
        "cooldown=%u arms=%" PRIu64 " decisions=%" PRIu64
        " recoveries=%" PRIu64 " overrides=%" PRIu64
        " inherited=%" PRIu64 " disabled=%" PRIu64
        " ignored_nontransport=%" PRIu64 " last_result=%u\n",
        status.enabled, status.core.active, status.core.exhausted,
        status.core.request_generation, status.core.reason_mask,
        status.core.legacy_failure_streak,
        status.core.canonical_failure_streak,
        status.core.attempts_in_burst,
        status.core.cooldown_remaining, status.core.request_arms,
        status.core.request_decisions, status.core.recoveries,
        status.applied_full_overrides,
        status.inherited_legacy_full_requests,
        status.disabled_request_blocks,
        status.ignored_nontransport_rejections, status.last_result);
}
