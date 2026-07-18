/* Deterministic hostile checks for the observation-only command lease. */
#include "shared/local_action_command_lease.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,         \
              #condition);                                                     \
      return 1;                                                                \
    }                                                                          \
  } while (0)

static worr_command_record_v1 make_command(uint32_t sequence) {
  worr_command_record_v1 command;
  memset(&command, 0, sizeof(command));
  command.struct_size = sizeof(command);
  command.schema_version = WORR_COMMAND_ABI_VERSION;
  command.command_id.epoch = 7;
  command.command_id.sequence = sequence;
  command.sample_time_us = UINT64_C(100000) + sequence * UINT64_C(16000);
  command.movement_model_revision = WORR_PREDICTION_MODEL_REVISION;
  command.command.struct_size = sizeof(command.command);
  command.command.schema_version = WORR_PREDICTION_ABI_VERSION;
  command.command.duration_ms = 16;
  command.render_watermark.struct_size = sizeof(command.render_watermark);
  command.render_watermark.schema_version = WORR_COMMAND_ABI_VERSION;
  return command;
}

static int test_offer_claim_lifecycle(void) {
  worr_local_action_command_lease_v1 lease;
  worr_command_record_v1 first = make_command(11);
  worr_command_record_v1 second = make_command(12);
  worr_command_record_v1 claimed;
  worr_command_record_v1 untouched;
  uint32_t result = 0;

  memset(&lease, 0, sizeof(lease));
  CHECK(Worr_LocalActionCommandLeaseInitV1(2, &lease));
  CHECK(Worr_LocalActionCommandLeaseValidateV1(&lease));
  CHECK(lease.phase == WORR_LOCAL_ACTION_COMMAND_LEASE_EMPTY);

  CHECK(Worr_LocalActionCommandLeaseOfferV1(&lease, &first, &result));
  CHECK(result == WORR_LOCAL_ACTION_COMMAND_LEASE_OFFER_ACCEPTED);
  CHECK(lease.phase == WORR_LOCAL_ACTION_COMMAND_LEASE_PENDING);

  result = 0;
  CHECK(Worr_LocalActionCommandLeaseOfferV1(&lease, &first, &result));
  CHECK(result == WORR_LOCAL_ACTION_COMMAND_LEASE_OFFER_DUPLICATE);
  result = 0;
  CHECK(Worr_LocalActionCommandLeaseOfferV1(&lease, &second, &result));
  CHECK(result == WORR_LOCAL_ACTION_COMMAND_LEASE_OFFER_SUPERSEDED);
  CHECK(lease.command.command_id.sequence == 12);

  CHECK(Worr_LocalActionCommandLeaseBeginFrameV1(&lease));
  CHECK(lease.phase == WORR_LOCAL_ACTION_COMMAND_LEASE_FRAME_ACTIVE);
  memset(&claimed, 0, sizeof(claimed));
  CHECK(Worr_LocalActionCommandLeaseClaimV1(&lease, &claimed));
  CHECK(Worr_CommandRecordSemanticallyEqualV1(
      &claimed, &second, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS));
  CHECK(lease.phase == WORR_LOCAL_ACTION_COMMAND_LEASE_CLAIMED);

  memset(&untouched, 0xa5, sizeof(untouched));
  claimed = untouched;
  CHECK(!Worr_LocalActionCommandLeaseClaimV1(&lease, &claimed));
  CHECK(memcmp(&claimed, &untouched, sizeof(claimed)) == 0);

  result = 0;
  CHECK(Worr_LocalActionCommandLeaseEndFrameV1(&lease, &result));
  CHECK(result == WORR_LOCAL_ACTION_COMMAND_LEASE_END_CLAIMED);
  CHECK(lease.phase == WORR_LOCAL_ACTION_COMMAND_LEASE_EMPTY);
  CHECK(Worr_LocalActionCommandLeaseValidateV1(&lease));
  return 0;
}

static int test_expiry_and_fail_closed(void) {
  worr_local_action_command_lease_v1 lease;
  worr_local_action_command_lease_v1 before;
  worr_command_record_v1 command = make_command(20);
  worr_command_record_v1 gap = make_command(22);
  uint32_t result = 0;

  memset(&lease, 0, sizeof(lease));
  CHECK(Worr_LocalActionCommandLeaseInitV1(0, &lease));
  CHECK(Worr_LocalActionCommandLeaseOfferV1(&lease, &command, &result));
  before = lease;
  result = 0;
  CHECK(!Worr_LocalActionCommandLeaseOfferV1(&lease, &gap, &result));
  CHECK(result == 0);
  CHECK(memcmp(&lease, &before, sizeof(lease)) == 0);

  CHECK(Worr_LocalActionCommandLeaseBeginFrameV1(&lease));
  before = lease;
  result = UINT32_C(0xa5a5a5a5);
  CHECK(!Worr_LocalActionCommandLeaseEndFrameV1(&lease, &result));
  CHECK(result == UINT32_C(0xa5a5a5a5));
  CHECK(memcmp(&lease, &before, sizeof(lease)) == 0);
  result = 0;
  CHECK(Worr_LocalActionCommandLeaseEndFrameV1(&lease, &result));
  CHECK(result == WORR_LOCAL_ACTION_COMMAND_LEASE_END_EXPIRED);
  CHECK(lease.phase == WORR_LOCAL_ACTION_COMMAND_LEASE_EMPTY);

  memset(&lease, 0, sizeof(lease));
  CHECK(!Worr_LocalActionCommandLeaseInitV1(
      WORR_LOCAL_ACTION_COMMAND_LEASE_MAX_CLIENTS, &lease));
  CHECK(lease.struct_size == 0);
  memset(&lease, 0xa5, sizeof(lease));
  before = lease;
  CHECK(!Worr_LocalActionCommandLeaseInitV1(0, &lease));
  CHECK(memcmp(&lease, &before, sizeof(lease)) == 0);
  return 0;
}

static int test_epoch_rebase(void) {
  worr_local_action_command_lease_v1 lease;
  worr_command_record_v1 old_command = make_command(30);
  worr_command_record_v1 new_command = make_command(1);
  uint32_t result = 0;

  new_command.command_id.epoch = 8;
  memset(&lease, 0, sizeof(lease));
  CHECK(Worr_LocalActionCommandLeaseInitV1(1, &lease));
  CHECK(Worr_LocalActionCommandLeaseOfferV1(&lease, &old_command, &result));
  result = 0;
  CHECK(Worr_LocalActionCommandLeaseOfferV1(&lease, &new_command, &result));
  CHECK(result == WORR_LOCAL_ACTION_COMMAND_LEASE_OFFER_REBASED);
  CHECK(lease.command.command_id.epoch == 8);
  CHECK(lease.command.command_id.sequence == 1);
  return 0;
}

int main(void) {
  CHECK(sizeof(worr_local_action_command_lease_v1) == 144);
  if (test_offer_claim_lifecycle() != 0 || test_expiry_and_fail_closed() != 0 ||
      test_epoch_rebase() != 0) {
    return 1;
  }
  puts("local_action_command_lease latest=exact claim=once "
       "epoch=rebase frame_end=empty");
  return 0;
}
