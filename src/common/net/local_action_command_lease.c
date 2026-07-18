/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "shared/local_action_command_lease.h"

#include <string.h>

static bool output_is_zero(const void *output, size_t size) {
  const uint8_t *bytes = (const uint8_t *)output;
  size_t index;

  if (!output)
    return false;
  for (index = 0; index != size; ++index) {
    if (bytes[index] != 0)
      return false;
  }
  return true;
}

static worr_local_action_command_lease_v1 empty_lease(uint32_t client_index) {
  worr_local_action_command_lease_v1 lease;

  memset(&lease, 0, sizeof(lease));
  lease.struct_size = sizeof(lease);
  lease.schema_version = WORR_LOCAL_ACTION_COMMAND_LEASE_ABI_VERSION;
  lease.model_revision = WORR_LOCAL_ACTION_COMMAND_LEASE_MODEL_REVISION;
  lease.client_index = client_index;
  return lease;
}

bool Worr_LocalActionCommandLeaseInitV1(
    uint32_t client_index, worr_local_action_command_lease_v1 *lease_out) {
  if (!output_is_zero(lease_out, sizeof(*lease_out)) ||
      client_index >= WORR_LOCAL_ACTION_COMMAND_LEASE_MAX_CLIENTS) {
    return false;
  }
  *lease_out = empty_lease(client_index);
  return true;
}

bool Worr_LocalActionCommandLeaseValidateV1(
    const worr_local_action_command_lease_v1 *lease) {
  uint64_t command_hash = 0;

  if (!lease || lease->struct_size != sizeof(*lease) ||
      lease->schema_version != WORR_LOCAL_ACTION_COMMAND_LEASE_ABI_VERSION ||
      lease->model_revision != WORR_LOCAL_ACTION_COMMAND_LEASE_MODEL_REVISION ||
      lease->client_index >= WORR_LOCAL_ACTION_COMMAND_LEASE_MAX_CLIENTS ||
      lease->phase > WORR_LOCAL_ACTION_COMMAND_LEASE_CLAIMED ||
      lease->reserved0 != 0 || lease->reserved1 != 0 || lease->reserved2 != 0) {
    return false;
  }
  if (lease->phase == WORR_LOCAL_ACTION_COMMAND_LEASE_EMPTY) {
    return lease->command_hash == 0 &&
           output_is_zero(&lease->command, sizeof(lease->command));
  }
  return Worr_CommandRecordSemanticHashV1(
             &lease->command, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS,
             &command_hash) &&
         command_hash == lease->command_hash;
}

bool Worr_LocalActionCommandLeaseOfferV1(
    worr_local_action_command_lease_v1 *lease,
    const worr_command_record_v1 *command, uint32_t *offer_result_out) {
  worr_local_action_command_lease_v1 candidate;
  uint64_t command_hash = 0;
  uint32_t result;

  if (!lease || !offer_result_out || *offer_result_out != 0 ||
      !Worr_LocalActionCommandLeaseValidateV1(lease) ||
      !Worr_CommandRecordSemanticHashV1(
          command, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS, &command_hash) ||
      (lease->phase != WORR_LOCAL_ACTION_COMMAND_LEASE_EMPTY &&
       lease->phase != WORR_LOCAL_ACTION_COMMAND_LEASE_PENDING)) {
    return false;
  }

  candidate = *lease;
  if (lease->phase == WORR_LOCAL_ACTION_COMMAND_LEASE_PENDING) {
    worr_command_id_v1 next_id;
    if (lease->command.command_id.epoch == command->command_id.epoch &&
        lease->command.command_id.sequence == command->command_id.sequence) {
      if (!Worr_CommandRecordSemanticallyEqualV1(
              &lease->command, command,
              WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS)) {
        return false;
      }
      result = WORR_LOCAL_ACTION_COMMAND_LEASE_OFFER_DUPLICATE;
    } else {
      if (Worr_CommandIdNextV1(lease->command.command_id, &next_id) &&
          next_id.epoch == command->command_id.epoch &&
          next_id.sequence == command->command_id.sequence) {
        result = WORR_LOCAL_ACTION_COMMAND_LEASE_OFFER_SUPERSEDED;
      } else if (lease->command.command_id.epoch != command->command_id.epoch) {
        result = WORR_LOCAL_ACTION_COMMAND_LEASE_OFFER_REBASED;
      } else {
        return false;
      }
      candidate.command = *command;
      candidate.command_hash = command_hash;
    }
  } else {
    candidate.phase = WORR_LOCAL_ACTION_COMMAND_LEASE_PENDING;
    candidate.command = *command;
    candidate.command_hash = command_hash;
    result = WORR_LOCAL_ACTION_COMMAND_LEASE_OFFER_ACCEPTED;
  }

  if (!Worr_LocalActionCommandLeaseValidateV1(&candidate))
    return false;
  *lease = candidate;
  *offer_result_out = result;
  return true;
}

bool Worr_LocalActionCommandLeaseBeginFrameV1(
    worr_local_action_command_lease_v1 *lease) {
  worr_local_action_command_lease_v1 candidate;

  if (!lease || !Worr_LocalActionCommandLeaseValidateV1(lease) ||
      lease->phase != WORR_LOCAL_ACTION_COMMAND_LEASE_PENDING) {
    return false;
  }
  candidate = *lease;
  candidate.phase = WORR_LOCAL_ACTION_COMMAND_LEASE_FRAME_ACTIVE;
  if (!Worr_LocalActionCommandLeaseValidateV1(&candidate))
    return false;
  *lease = candidate;
  return true;
}

bool Worr_LocalActionCommandLeaseClaimV1(
    worr_local_action_command_lease_v1 *lease,
    worr_command_record_v1 *command_out) {
  worr_local_action_command_lease_v1 candidate;

  if (!lease || !output_is_zero(command_out, sizeof(*command_out)) ||
      !Worr_LocalActionCommandLeaseValidateV1(lease) ||
      lease->phase != WORR_LOCAL_ACTION_COMMAND_LEASE_FRAME_ACTIVE) {
    return false;
  }
  candidate = *lease;
  candidate.phase = WORR_LOCAL_ACTION_COMMAND_LEASE_CLAIMED;
  if (!Worr_LocalActionCommandLeaseValidateV1(&candidate))
    return false;
  *lease = candidate;
  *command_out = candidate.command;
  return true;
}

bool Worr_LocalActionCommandLeaseEndFrameV1(
    worr_local_action_command_lease_v1 *lease, uint32_t *end_result_out) {
  worr_local_action_command_lease_v1 candidate;
  uint32_t result;

  if (!lease || !end_result_out || *end_result_out != 0 ||
      !Worr_LocalActionCommandLeaseValidateV1(lease) ||
      (lease->phase != WORR_LOCAL_ACTION_COMMAND_LEASE_FRAME_ACTIVE &&
       lease->phase != WORR_LOCAL_ACTION_COMMAND_LEASE_CLAIMED)) {
    return false;
  }
  result = lease->phase == WORR_LOCAL_ACTION_COMMAND_LEASE_CLAIMED
               ? WORR_LOCAL_ACTION_COMMAND_LEASE_END_CLAIMED
               : WORR_LOCAL_ACTION_COMMAND_LEASE_END_EXPIRED;
  candidate = empty_lease(lease->client_index);
  if (!Worr_LocalActionCommandLeaseValidateV1(&candidate))
    return false;
  *lease = candidate;
  *end_result_out = result;
  return true;
}
