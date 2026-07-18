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

#include "shared/command_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WORR_LOCAL_ACTION_COMMAND_LEASE_ABI_VERSION 1u
#define WORR_LOCAL_ACTION_COMMAND_LEASE_MODEL_REVISION 1u
#define WORR_LOCAL_ACTION_COMMAND_LEASE_MAX_CLIENTS 32u

typedef enum worr_local_action_command_lease_phase_v1_e {
  WORR_LOCAL_ACTION_COMMAND_LEASE_EMPTY = 0,
  WORR_LOCAL_ACTION_COMMAND_LEASE_PENDING = 1,
  WORR_LOCAL_ACTION_COMMAND_LEASE_FRAME_ACTIVE = 2,
  WORR_LOCAL_ACTION_COMMAND_LEASE_CLAIMED = 3,
} worr_local_action_command_lease_phase_v1;

typedef enum worr_local_action_command_lease_offer_result_v1_e {
  WORR_LOCAL_ACTION_COMMAND_LEASE_OFFER_ACCEPTED = 1,
  WORR_LOCAL_ACTION_COMMAND_LEASE_OFFER_SUPERSEDED = 2,
  WORR_LOCAL_ACTION_COMMAND_LEASE_OFFER_DUPLICATE = 3,
  WORR_LOCAL_ACTION_COMMAND_LEASE_OFFER_REBASED = 4,
} worr_local_action_command_lease_offer_result_v1;

typedef enum worr_local_action_command_lease_end_result_v1_e {
  WORR_LOCAL_ACTION_COMMAND_LEASE_END_EXPIRED = 1,
  WORR_LOCAL_ACTION_COMMAND_LEASE_END_CLAIMED = 2,
} worr_local_action_command_lease_end_result_v1;

/*
 * Observation-only ownership for one post-command server-frame advance.
 * This contract cannot extend command authority, run weapon code, or alter
 * gameplay time. A pending exact record is active for one frame and claimable
 * once; frame end always returns the lease to its canonical empty state.
 */
typedef struct worr_local_action_command_lease_v1_s {
  uint32_t struct_size;
  uint32_t schema_version;
  uint32_t model_revision;
  uint32_t client_index;
  uint32_t phase;
  uint32_t reserved0;
  uint64_t command_hash;
  worr_command_record_v1 command;
  uint32_t reserved1;
  uint32_t reserved2;
} worr_local_action_command_lease_v1;

bool Worr_LocalActionCommandLeaseInitV1(
    uint32_t client_index, worr_local_action_command_lease_v1 *lease_out);
bool Worr_LocalActionCommandLeaseValidateV1(
    const worr_local_action_command_lease_v1 *lease);

/* Inputs and outputs are transactional; result outputs must point to zero. */
bool Worr_LocalActionCommandLeaseOfferV1(
    worr_local_action_command_lease_v1 *lease,
    const worr_command_record_v1 *command, uint32_t *offer_result_out);
bool Worr_LocalActionCommandLeaseBeginFrameV1(
    worr_local_action_command_lease_v1 *lease);
bool Worr_LocalActionCommandLeaseClaimV1(
    worr_local_action_command_lease_v1 *lease,
    worr_command_record_v1 *command_out);
bool Worr_LocalActionCommandLeaseEndFrameV1(
    worr_local_action_command_lease_v1 *lease, uint32_t *end_result_out);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_LOCAL_ACTION_COMMAND_LEASE_STATIC_ASSERT(condition, message)      \
  static_assert((condition), message)
#else
#define WORR_LOCAL_ACTION_COMMAND_LEASE_STATIC_ASSERT(condition, message)      \
  _Static_assert((condition), message)
#endif

WORR_LOCAL_ACTION_COMMAND_LEASE_STATIC_ASSERT(
    sizeof(worr_local_action_command_lease_v1) == 144,
    "local-action command lease v1 layout changed");
WORR_LOCAL_ACTION_COMMAND_LEASE_STATIC_ASSERT(
    offsetof(worr_local_action_command_lease_v1, command_hash) == 24,
    "local-action command lease hash offset changed");
WORR_LOCAL_ACTION_COMMAND_LEASE_STATIC_ASSERT(
    offsetof(worr_local_action_command_lease_v1, command) == 32,
    "local-action command lease command offset changed");

#undef WORR_LOCAL_ACTION_COMMAND_LEASE_STATIC_ASSERT
