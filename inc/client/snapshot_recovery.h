/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/snapshot_recovery.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CL_SNAPSHOT_RECOVERY_STATUS_VERSION UINT32_C(1)

typedef struct cl_snapshot_recovery_status_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t enabled;
    uint32_t last_decision_flags;
    uint32_t last_result;
    uint32_t reserved[3];
    uint64_t observation_failures;
    uint64_t decision_failures;
    uint64_t disabled_request_blocks;
    uint64_t applied_full_overrides;
    uint64_t inherited_legacy_full_requests;
    uint64_t ignored_nontransport_rejections;
    worr_snapshot_recovery_state_v1 core;
} cl_snapshot_recovery_status_v1;

void CL_SnapshotRecoveryInit(void);
void CL_SnapshotRecoveryBeginConnection(void);
void CL_SnapshotRecoveryReset(void);

void CL_SnapshotRecoveryObserveLegacyFrame(
    int32_t snapshot_number, int32_t base_snapshot_number,
    bool valid, uint32_t legacy_frame_flags);

/*
 * canonical_ok is the complete shadow/promotion result.  A false result only
 * counts as recoverable when parity_mismatch or projector_result identifies a
 * projector-side problem. Promotion-policy and downstream cgame consumer
 * rejections never become reasons to spend transport bandwidth.
 */
void CL_SnapshotRecoveryObserveCanonicalFrame(
    int32_t snapshot_number, int32_t base_snapshot_number,
    bool canonical_ok, bool shadow_active, bool controlled_entity_valid,
    uint32_t projector_result, uint32_t parity_mismatch);

/* Apply the default-off policy to the protocol's already selected base. */
int32_t CL_SnapshotRecoverySelectLastFrame(int32_t legacy_lastframe);

bool CL_SnapshotRecoveryGetStatus(
    cl_snapshot_recovery_status_v1 *status_out);
void CL_SnapshotRecoveryStatus_f(void);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
static_assert(offsetof(cl_snapshot_recovery_status_v1, core) == 80,
              "client snapshot recovery status header changed");
static_assert(sizeof(cl_snapshot_recovery_status_v1) == 200,
              "client snapshot recovery status layout changed");
#else
_Static_assert(offsetof(cl_snapshot_recovery_status_v1, core) == 80,
               "client snapshot recovery status header changed");
_Static_assert(sizeof(cl_snapshot_recovery_status_v1) == 200,
               "client snapshot recovery status layout changed");
#endif
