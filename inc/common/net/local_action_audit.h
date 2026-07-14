/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "shared/local_action_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WORR_LOCAL_ACTION_AUDIT_VERSION 1u
#define WORR_LOCAL_ACTION_AUDIT_OPERATIONAL_API_VERSION 2u
#define WORR_LOCAL_ACTION_AUDIT_MAX_CAPACITY 512u

enum {
  WORR_LOCAL_ACTION_AUDIT_SLOT_HAS_PREDICTED = 1u << 0,
  WORR_LOCAL_ACTION_AUDIT_SLOT_HAS_AUTHORITATIVE = 1u << 1,
  WORR_LOCAL_ACTION_AUDIT_SLOT_CLASSIFIED = 1u << 2,
};

/*
 * Pointer-free retained evidence for one canonical command.  Once both
 * producer roles are present, the slot and its correction are immutable until
 * an explicit prune or connection-epoch reset removes the slot.
 */
typedef struct worr_local_action_audit_slot_v1_s {
  uint32_t struct_size;
  uint32_t schema_version;
  uint32_t flags;
  uint32_t reserved0;
  worr_command_id_v1 command_id;
  uint64_t first_arrival_serial;
  worr_local_action_transaction_v2 predicted;
  worr_local_action_transaction_v2 authoritative;
  worr_local_action_correction_v2 correction;
} worr_local_action_audit_slot_v1;

/*
 * Monotonic counters saturate at UINT64_MAX.  unmatched_* are retained-slot
 * gauges and therefore remain exact rather than saturating history counters.
 */
typedef struct worr_local_action_audit_telemetry_v1_s {
  uint64_t submit_attempts;
  uint64_t predicted_submissions;
  uint64_t authoritative_submissions;
  uint64_t slots_created;
  uint64_t pairs_classified;
  uint64_t exact_pairs;
  uint64_t presentation_corrections;
  uint64_t gameplay_corrections;
  uint64_t hard_resync_corrections;
  uint64_t duplicates;
  uint64_t conflicts;
  uint64_t wrong_connection_epoch;
  uint64_t stale;
  uint64_t invalid_transactions;
  uint64_t invalid_roles;
  uint64_t capacity_stalls;
  uint64_t serial_exhaustions;
  uint64_t classification_failures;
  uint64_t copy_attempts;
  uint64_t copies;
  uint64_t not_found;
  uint64_t prune_attempts;
  uint64_t prunes;
  uint64_t prune_blocks;
  uint64_t pruned_slots;
  uint64_t reset_attempts;
  uint64_t resets;
  uint64_t reset_rejections;
  uint64_t invalid_arguments;
  uint64_t unmatched_predicted;
  uint64_t unmatched_authoritative;
} worr_local_action_audit_telemetry_v1;

/* Runtime-only caller-owned envelope; it is never serialized or hashed. */
typedef struct worr_local_action_audit_v1_s {
  uint32_t struct_size;
  uint32_t schema_version;
  worr_local_action_audit_slot_v1 *slots;
  uint32_t capacity;
  uint32_t head;
  uint32_t count;
  uint32_t connection_epoch;
  uint32_t generation;
  uint32_t reserved0;
  worr_command_id_v1 pruned_through;
  uint64_t next_arrival_serial;
  worr_local_action_audit_telemetry_v1 telemetry;
} worr_local_action_audit_v1;

typedef enum worr_local_action_audit_result_v1_e {
  WORR_LOCAL_ACTION_AUDIT_INSERTED = 0,
  WORR_LOCAL_ACTION_AUDIT_PAIRED = 1,
  WORR_LOCAL_ACTION_AUDIT_DUPLICATE = 2,
  WORR_LOCAL_ACTION_AUDIT_COPIED = 3,
  WORR_LOCAL_ACTION_AUDIT_PRUNED = 4,
  WORR_LOCAL_ACTION_AUDIT_RESET = 5,
  WORR_LOCAL_ACTION_AUDIT_NOT_FOUND = 6,
  WORR_LOCAL_ACTION_AUDIT_STALE = 7,
  WORR_LOCAL_ACTION_AUDIT_WRONG_CONNECTION_EPOCH = 8,
  WORR_LOCAL_ACTION_AUDIT_CAPACITY = 9,
  WORR_LOCAL_ACTION_AUDIT_CONFLICT = 10,
  WORR_LOCAL_ACTION_AUDIT_INVALID_TRANSACTION = 11,
  WORR_LOCAL_ACTION_AUDIT_INVALID_ROLE = 12,
  WORR_LOCAL_ACTION_AUDIT_PRUNE_REGRESSION = 13,
  WORR_LOCAL_ACTION_AUDIT_PRUNE_BLOCKED = 14,
  WORR_LOCAL_ACTION_AUDIT_RESET_REGRESSION = 15,
  WORR_LOCAL_ACTION_AUDIT_GENERATION_EXHAUSTED = 16,
  WORR_LOCAL_ACTION_AUDIT_SERIAL_EXHAUSTED = 17,
  WORR_LOCAL_ACTION_AUDIT_INVALID_ARGUMENT = 18,
  WORR_LOCAL_ACTION_AUDIT_INVALID_STATE = 19,
} worr_local_action_audit_result_v1;

/* Invalid initialization leaves both the envelope and storage untouched. */
bool Worr_LocalActionAuditInitV1(worr_local_action_audit_v1 *audit,
                                 worr_local_action_audit_slot_v1 *storage,
                                 uint32_t capacity, uint32_t connection_epoch);

/* Revalidates every retained transaction and reproduces every correction. */
bool Worr_LocalActionAuditValidateV1(const worr_local_action_audit_v1 *audit);

/*
 * Pair by transaction.command.command_id, in either arrival order.  A second
 * byte-identical transaction from a retained role is idempotent; any other
 * transaction for that retained role conflicts.  The explicit connection
 * epoch is transport/session provenance and is deliberately independent of
 * command_id.epoch.  Full rings never evict.
 */
worr_local_action_audit_result_v1 Worr_LocalActionAuditSubmitV1(
    worr_local_action_audit_v1 *audit, uint32_t submission_connection_epoch,
    const worr_local_action_transaction_v2 *transaction);

/* slot_out is untouched on failure and must not alias the audit or storage. */
worr_local_action_audit_result_v1
Worr_LocalActionAuditCopyV1(worr_local_action_audit_v1 *audit,
                            worr_command_id_v1 command_id,
                            worr_local_action_audit_slot_v1 *slot_out);

/*
 * Explicitly discard every classified command at or below command_id and move
 * the stale watermark forward.  The watermark cannot regress.  Any unmatched
 * slot at or below the requested ID blocks the complete operation.
 */
worr_local_action_audit_result_v1
Worr_LocalActionAuditPruneThroughV1(worr_local_action_audit_v1 *audit,
                                    worr_command_id_v1 command_id);

/* Explicit discontinuity; new_connection_epoch must strictly increase. */
worr_local_action_audit_result_v1
Worr_LocalActionAuditResetV1(worr_local_action_audit_v1 *audit,
                             uint32_t new_connection_epoch);

/* Pure snapshot; telemetry_out must not alias the audit or storage. */
bool Worr_LocalActionAuditGetTelemetryV1(
    const worr_local_action_audit_v1 *audit,
    worr_local_action_audit_telemetry_v1 *telemetry_out);

/*
 * Operational API v2 deliberately retains the pointer-free v1 storage ABI.
 * V1 operations keep their original whole-ring deep-validation contract.
 * V2 operations use a bounded O(n) structural pass and fully validate every
 * retained slot whose transaction/correction bytes they consume.  They never
 * return or classify unvalidated evidence.
 *
 * Operational validation detects envelope, ring, slot-header, identity,
 * ordering, role, correction-header, gauge, and telemetry corruption.  Deep
 * validation additionally reconstructs every transaction and correction and
 * verifies every unused byte.  Run the explicit deep audit at trust-boundary
 * admission and on a caller-selected diagnostic cadence.
 */
bool Worr_LocalActionAuditInitV2(worr_local_action_audit_v1 *audit,
                                 worr_local_action_audit_slot_v1 *storage,
                                 uint32_t capacity, uint32_t connection_epoch);
bool Worr_LocalActionAuditValidateOperationalV2(
    const worr_local_action_audit_v1 *audit);
bool Worr_LocalActionAuditValidateDeepV2(
    const worr_local_action_audit_v1 *audit);

worr_local_action_audit_result_v1 Worr_LocalActionAuditSubmitV2(
    worr_local_action_audit_v1 *audit, uint32_t submission_connection_epoch,
    const worr_local_action_transaction_v2 *transaction);
worr_local_action_audit_result_v1
Worr_LocalActionAuditCopyV2(worr_local_action_audit_v1 *audit,
                            worr_command_id_v1 command_id,
                            worr_local_action_audit_slot_v1 *slot_out);
worr_local_action_audit_result_v1
Worr_LocalActionAuditPruneThroughV2(worr_local_action_audit_v1 *audit,
                                    worr_command_id_v1 command_id);
worr_local_action_audit_result_v1
Worr_LocalActionAuditResetV2(worr_local_action_audit_v1 *audit,
                             uint32_t new_connection_epoch);
bool Worr_LocalActionAuditGetTelemetryV2(
    const worr_local_action_audit_v1 *audit,
    worr_local_action_audit_telemetry_v1 *telemetry_out);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_LOCAL_ACTION_AUDIT_STATIC_ASSERT(condition, message)              \
  static_assert((condition), message)
#else
#define WORR_LOCAL_ACTION_AUDIT_STATIC_ASSERT(condition, message)              \
  _Static_assert((condition), message)
#endif

WORR_LOCAL_ACTION_AUDIT_STATIC_ASSERT(
    sizeof(worr_local_action_audit_slot_v1) == 2528,
    "local action audit slot v1 layout changed");
WORR_LOCAL_ACTION_AUDIT_STATIC_ASSERT(
    offsetof(worr_local_action_audit_slot_v1, command_id) == 16,
    "local action audit command-id offset changed");
WORR_LOCAL_ACTION_AUDIT_STATIC_ASSERT(
    offsetof(worr_local_action_audit_slot_v1, first_arrival_serial) == 24,
    "local action audit arrival-serial offset changed");
WORR_LOCAL_ACTION_AUDIT_STATIC_ASSERT(
    offsetof(worr_local_action_audit_slot_v1, predicted) == 32,
    "local action audit predicted offset changed");
WORR_LOCAL_ACTION_AUDIT_STATIC_ASSERT(
    offsetof(worr_local_action_audit_slot_v1, authoritative) == 1248,
    "local action audit authoritative offset changed");
WORR_LOCAL_ACTION_AUDIT_STATIC_ASSERT(
    offsetof(worr_local_action_audit_slot_v1, correction) == 2464,
    "local action audit correction offset changed");
WORR_LOCAL_ACTION_AUDIT_STATIC_ASSERT(
    sizeof(worr_local_action_audit_telemetry_v1) == 248,
    "local action audit telemetry v1 layout changed");

#undef WORR_LOCAL_ACTION_AUDIT_STATIC_ASSERT
