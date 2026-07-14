/* Hostile deterministic checks for the FR-10-T08/T16 local-action audit. */
#include "common/net/local_action_audit.h"

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

static bool bytes_are_zero(const void *data, size_t size) {
  const uint8_t *bytes = (const uint8_t *)data;
  size_t index;
  for (index = 0; index < size; ++index) {
    if (bytes[index] != 0)
      return false;
  }
  return true;
}

static worr_local_action_weapon_rule_v2 absent_rule(void) {
  worr_local_action_weapon_rule_v2 rule;
  memset(&rule, 0, sizeof(rule));
  rule.struct_size = sizeof(rule);
  rule.schema_version = WORR_LOCAL_ACTION_ABI_VERSION;
  rule.model_revision = WORR_LOCAL_ACTION_MODEL_REVISION;
  return rule;
}

static worr_local_action_weapon_rule_v2 make_rule(void) {
  worr_local_action_weapon_rule_v2 rule = absent_rule();
  rule.flags = WORR_LOCAL_ACTION_WEAPON_USES_AMMO;
  rule.weapon_id = 1;
  rule.ammo_per_shot = 1;
  rule.refire_duration_ms = 20;
  rule.ready_frame = 11;
  rule.fire_frame = 12;
  return rule;
}

static worr_local_action_intent_v2 idle_intent(void) {
  worr_local_action_intent_v2 intent;
  memset(&intent, 0, sizeof(intent));
  intent.struct_size = sizeof(intent);
  intent.schema_version = WORR_LOCAL_ACTION_ABI_VERSION;
  return intent;
}

static worr_command_record_v1
command_for_state(const worr_local_action_state_v2 *state,
                  uint8_t duration_ms) {
  worr_command_record_v1 command;
  memset(&command, 0, sizeof(command));
  command.struct_size = sizeof(command);
  command.schema_version = WORR_COMMAND_ABI_VERSION;
  (void)Worr_CommandCursorNextIdV1(state->applied_cursor, &command.command_id);
  command.sample_time_us =
      state->sample_time_us + (uint64_t)duration_ms * UINT64_C(1000);
  command.movement_model_revision = WORR_PREDICTION_MODEL_REVISION;
  command.command.struct_size = sizeof(command.command);
  command.command.schema_version = WORR_PREDICTION_ABI_VERSION;
  command.command.duration_ms = duration_ms;
  command.render_watermark.struct_size = sizeof(command.render_watermark);
  command.render_watermark.schema_version = WORR_COMMAND_ABI_VERSION;
  return command;
}

static bool build_from_state(const worr_local_action_state_v2 *state,
                             uint32_t role,
                             worr_local_action_transaction_v2 *transaction) {
  const worr_local_action_weapon_rule_v2 active = make_rule();
  const worr_local_action_weapon_rule_v2 pending = absent_rule();
  const worr_local_action_intent_v2 intent = idle_intent();
  const worr_command_record_v1 command = command_for_state(state, 1);
  memset(transaction, 0, sizeof(*transaction));
  return Worr_LocalActionBuildTransactionV2(state, &command, &intent, &active,
                                            &pending, role, transaction);
}

static bool make_transaction(uint32_t command_epoch, uint32_t command_sequence,
                             uint32_t role, uint32_t ammo,
                             uint32_t presentation_frame,
                             uint64_t sample_bias_us,
                             worr_local_action_transaction_v2 *transaction) {
  worr_local_action_state_v2 state;
  if (command_epoch == 0 || command_sequence == 0 ||
      !Worr_LocalActionStateInitV2(&state, command_epoch, 1, ammo,
                                   presentation_frame)) {
    return false;
  }
  state.applied_cursor.contiguous_sequence = command_sequence - 1;
  state.sample_time_us =
      (uint64_t)(command_sequence - 1) * UINT64_C(1000) + sample_bias_us;
  return build_from_state(&state, role, transaction);
}

static int test_init_layout_and_validation(void) {
  worr_local_action_audit_v1 audit;
  worr_local_action_audit_v1 before;
  worr_local_action_audit_slot_v1 slots[3];
  worr_local_action_audit_slot_v1 slots_before[3];
  worr_local_action_audit_telemetry_v1 telemetry;
  uint8_t unaligned[sizeof(worr_local_action_audit_v1) + 1];

  CHECK(sizeof(worr_local_action_audit_slot_v1) == 2528);
  CHECK(sizeof(worr_local_action_audit_telemetry_v1) == 248);
  memset(&audit, 0xa5, sizeof(audit));
  memset(slots, 0x5a, sizeof(slots));
  before = audit;
  memcpy(slots_before, slots, sizeof(slots));
  CHECK(!Worr_LocalActionAuditInitV1(&audit, slots, 0, 9));
  CHECK(memcmp(&audit, &before, sizeof(audit)) == 0);
  CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);
  CHECK(!Worr_LocalActionAuditInitV1(&audit, slots, 3, 0));
  CHECK(memcmp(&audit, &before, sizeof(audit)) == 0);
  CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);
  CHECK(!Worr_LocalActionAuditInitV1(
      &audit, slots, WORR_LOCAL_ACTION_AUDIT_MAX_CAPACITY + 1, 9));
  CHECK(memcmp(&audit, &before, sizeof(audit)) == 0);
  CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);
  CHECK(!Worr_LocalActionAuditInitV1(
      (worr_local_action_audit_v1 *)(void *)(unaligned + 1), slots, 3, 9));
  CHECK(!Worr_LocalActionAuditInitV1(
      &audit, (worr_local_action_audit_slot_v1 *)(void *)&audit, 1, 9));
  CHECK(memcmp(&audit, &before, sizeof(audit)) == 0);

  CHECK(Worr_LocalActionAuditInitV1(&audit, slots, 3, 9));
  CHECK(Worr_LocalActionAuditValidateV1(&audit));
  CHECK(audit.connection_epoch == 9);
  CHECK(audit.generation == 1);
  CHECK(audit.pruned_through.epoch == 0);
  CHECK(audit.pruned_through.sequence == 0);
  CHECK(audit.next_arrival_serial == 1);
  CHECK(audit.count == 0 && audit.head == 0);
  CHECK(bytes_are_zero(slots, sizeof(slots)));
  memset(&telemetry, 0xa5, sizeof(telemetry));
  CHECK(Worr_LocalActionAuditGetTelemetryV1(&audit, &telemetry));
  CHECK(bytes_are_zero(&telemetry, sizeof(telemetry)));

  slots[2].reserved0 = 1;
  CHECK(!Worr_LocalActionAuditValidateV1(&audit));
  memset(&slots[2], 0, sizeof(slots[2]));
  audit.struct_size++;
  CHECK(!Worr_LocalActionAuditValidateV1(&audit));
  audit.struct_size = sizeof(audit);
  audit.reserved0 = 1;
  CHECK(!Worr_LocalActionAuditValidateV1(&audit));
  audit.reserved0 = 0;
  audit.pruned_through.epoch = 1;
  audit.pruned_through.sequence = 0;
  CHECK(!Worr_LocalActionAuditValidateV1(&audit));
  memset(&audit.pruned_through, 0, sizeof(audit.pruned_through));
  CHECK(Worr_LocalActionAuditValidateV1(&audit));
  return 0;
}

static int test_pairing_order_classes_and_immutability(void) {
  worr_local_action_audit_v1 audit;
  worr_local_action_audit_slot_v1 slots[8];
  worr_local_action_audit_slot_v1 copied;
  worr_local_action_audit_slot_v1 retained;
  worr_local_action_transaction_v2 predicted;
  worr_local_action_transaction_v2 authority;
  worr_local_action_transaction_v2 conflict;

  CHECK(Worr_LocalActionAuditInitV1(&audit, slots, 8, 31));

  CHECK(make_transaction(4, 1, WORR_LOCAL_ACTION_PRODUCER_PREDICTED, 8, 11, 0,
                         &predicted));
  CHECK(make_transaction(4, 1, WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE, 8, 11,
                         0, &authority));
  CHECK(Worr_LocalActionAuditSubmitV1(&audit, 31, &predicted) ==
        WORR_LOCAL_ACTION_AUDIT_INSERTED);
  CHECK(Worr_LocalActionAuditSubmitV1(&audit, 31, &authority) ==
        WORR_LOCAL_ACTION_AUDIT_PAIRED);
  memset(&copied, 0, sizeof(copied));
  CHECK(Worr_LocalActionAuditCopyV1(&audit, predicted.command.command_id,
                                    &copied) == WORR_LOCAL_ACTION_AUDIT_COPIED);
  CHECK(copied.correction.classification == WORR_LOCAL_ACTION_CORRECTION_NONE);
  CHECK(copied.first_arrival_serial == 1);

  CHECK(make_transaction(4, 2, WORR_LOCAL_ACTION_PRODUCER_PREDICTED, 8, 11, 0,
                         &predicted));
  CHECK(make_transaction(4, 2, WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE, 8, 12,
                         0, &authority));
  CHECK(Worr_LocalActionAuditSubmitV1(&audit, 31, &authority) ==
        WORR_LOCAL_ACTION_AUDIT_INSERTED);
  CHECK(Worr_LocalActionAuditSubmitV1(&audit, 31, &predicted) ==
        WORR_LOCAL_ACTION_AUDIT_PAIRED);
  CHECK(Worr_LocalActionAuditCopyV1(&audit, predicted.command.command_id,
                                    &copied) == WORR_LOCAL_ACTION_AUDIT_COPIED);
  CHECK(copied.correction.classification ==
        WORR_LOCAL_ACTION_CORRECTION_PRESENTATION_ONLY);
  CHECK(copied.correction.difference_bits ==
        WORR_LOCAL_ACTION_DIFF_PRESENTATION_FRAME);
  CHECK(copied.first_arrival_serial == 2);

  CHECK(make_transaction(4, 3, WORR_LOCAL_ACTION_PRODUCER_PREDICTED, 8, 11, 0,
                         &predicted));
  CHECK(make_transaction(4, 3, WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE, 7, 11,
                         0, &authority));
  CHECK(Worr_LocalActionAuditSubmitV1(&audit, 31, &predicted) ==
        WORR_LOCAL_ACTION_AUDIT_INSERTED);
  CHECK(Worr_LocalActionAuditSubmitV1(&audit, 31, &authority) ==
        WORR_LOCAL_ACTION_AUDIT_PAIRED);
  CHECK(Worr_LocalActionAuditCopyV1(&audit, predicted.command.command_id,
                                    &copied) == WORR_LOCAL_ACTION_AUDIT_COPIED);
  CHECK(copied.correction.classification ==
        WORR_LOCAL_ACTION_CORRECTION_GAMEPLAY_IMMEDIATE);
  CHECK((copied.correction.difference_bits & WORR_LOCAL_ACTION_DIFF_AMMO) != 0);

  CHECK(make_transaction(4, 4, WORR_LOCAL_ACTION_PRODUCER_PREDICTED, 8, 11, 0,
                         &predicted));
  CHECK(make_transaction(4, 4, WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE, 8, 11,
                         100, &authority));
  CHECK(Worr_LocalActionAuditSubmitV1(&audit, 31, &authority) ==
        WORR_LOCAL_ACTION_AUDIT_INSERTED);
  CHECK(Worr_LocalActionAuditSubmitV1(&audit, 31, &predicted) ==
        WORR_LOCAL_ACTION_AUDIT_PAIRED);
  CHECK(Worr_LocalActionAuditCopyV1(&audit, predicted.command.command_id,
                                    &copied) == WORR_LOCAL_ACTION_AUDIT_COPIED);
  CHECK(copied.correction.classification ==
        WORR_LOCAL_ACTION_CORRECTION_HARD_RESYNC);
  CHECK((copied.correction.difference_bits &
         WORR_LOCAL_ACTION_DIFF_SAMPLE_TIME) != 0);

  retained = slots[0];
  CHECK(make_transaction(4, 1, WORR_LOCAL_ACTION_PRODUCER_PREDICTED, 8, 11, 0,
                         &predicted));
  CHECK(Worr_LocalActionAuditSubmitV1(&audit, 31, &predicted) ==
        WORR_LOCAL_ACTION_AUDIT_DUPLICATE);
  CHECK(memcmp(&slots[0], &retained, sizeof(retained)) == 0);
  CHECK(make_transaction(4, 1, WORR_LOCAL_ACTION_PRODUCER_PREDICTED, 8, 13, 0,
                         &conflict));
  CHECK(Worr_LocalActionAuditSubmitV1(&audit, 31, &conflict) ==
        WORR_LOCAL_ACTION_AUDIT_CONFLICT);
  CHECK(memcmp(&slots[0], &retained, sizeof(retained)) == 0);

  CHECK(audit.count == 4);
  CHECK(audit.telemetry.pairs_classified == 4);
  CHECK(audit.telemetry.exact_pairs == 1);
  CHECK(audit.telemetry.presentation_corrections == 1);
  CHECK(audit.telemetry.gameplay_corrections == 1);
  CHECK(audit.telemetry.hard_resync_corrections == 1);
  CHECK(audit.telemetry.duplicates == 1);
  CHECK(audit.telemetry.conflicts == 1);
  CHECK(audit.telemetry.unmatched_predicted == 0);
  CHECK(audit.telemetry.unmatched_authoritative == 0);
  CHECK(Worr_LocalActionAuditValidateV1(&audit));
  return 0;
}

static int test_prune_block_capacity_and_out_of_order(void) {
  worr_local_action_audit_v1 audit;
  worr_local_action_audit_slot_v1 slots[3];
  worr_local_action_audit_slot_v1 retained[3];
  worr_local_action_transaction_v2 predicted;
  worr_local_action_transaction_v2 authority;
  worr_local_action_audit_slot_v1 untouched_output;
  worr_local_action_audit_slot_v1 output_before;
  worr_command_id_v1 through;
  uint32_t count_before;
  uint32_t head_before;
  uint32_t generation_before;
  worr_command_id_v1 watermark_before;

  CHECK(Worr_LocalActionAuditInitV1(&audit, slots, 3, 1));
  CHECK(make_transaction(7, 5, WORR_LOCAL_ACTION_PRODUCER_PREDICTED, 8, 11, 0,
                         &predicted));
  CHECK(Worr_LocalActionAuditSubmitV1(&audit, 1, &predicted) ==
        WORR_LOCAL_ACTION_AUDIT_INSERTED);
  CHECK(make_transaction(7, 3, WORR_LOCAL_ACTION_PRODUCER_PREDICTED, 8, 11, 0,
                         &predicted));
  CHECK(make_transaction(7, 3, WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE, 8, 11,
                         0, &authority));
  CHECK(Worr_LocalActionAuditSubmitV1(&audit, 1, &predicted) ==
        WORR_LOCAL_ACTION_AUDIT_INSERTED);
  CHECK(Worr_LocalActionAuditSubmitV1(&audit, 1, &authority) ==
        WORR_LOCAL_ACTION_AUDIT_PAIRED);
  CHECK(make_transaction(7, 7, WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE, 8, 11,
                         0, &authority));
  CHECK(Worr_LocalActionAuditSubmitV1(&audit, 1, &authority) ==
        WORR_LOCAL_ACTION_AUDIT_INSERTED);

  memcpy(retained, slots, sizeof(slots));
  CHECK(make_transaction(7, 9, WORR_LOCAL_ACTION_PRODUCER_PREDICTED, 8, 11, 0,
                         &predicted));
  CHECK(Worr_LocalActionAuditSubmitV1(&audit, 1, &predicted) ==
        WORR_LOCAL_ACTION_AUDIT_CAPACITY);
  CHECK(memcmp(retained, slots, sizeof(slots)) == 0);
  CHECK(audit.count == 3);
  CHECK(audit.telemetry.capacity_stalls == 1);

  through.epoch = 7;
  through.sequence = 5;
  memcpy(retained, slots, sizeof(slots));
  count_before = audit.count;
  head_before = audit.head;
  generation_before = audit.generation;
  watermark_before = audit.pruned_through;
  CHECK(Worr_LocalActionAuditPruneThroughV1(&audit, through) ==
        WORR_LOCAL_ACTION_AUDIT_PRUNE_BLOCKED);
  CHECK(memcmp(retained, slots, sizeof(slots)) == 0);
  CHECK(audit.count == count_before && audit.head == head_before);
  CHECK(audit.generation == generation_before);
  CHECK(memcmp(&audit.pruned_through, &watermark_before,
               sizeof(watermark_before)) == 0);
  CHECK(audit.telemetry.prune_attempts == 1);
  CHECK(audit.telemetry.prune_blocks == 1);

  CHECK(make_transaction(7, 5, WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE, 8, 11,
                         0, &authority));
  CHECK(Worr_LocalActionAuditSubmitV1(&audit, 1, &authority) ==
        WORR_LOCAL_ACTION_AUDIT_PAIRED);
  CHECK(Worr_LocalActionAuditPruneThroughV1(&audit, through) ==
        WORR_LOCAL_ACTION_AUDIT_PRUNED);
  CHECK(audit.count == 1);
  CHECK(audit.pruned_through.epoch == 7 && audit.pruned_through.sequence == 5);
  CHECK(slots[0].command_id.sequence == 7);
  CHECK((slots[0].flags & WORR_LOCAL_ACTION_AUDIT_SLOT_CLASSIFIED) == 0);
  CHECK(bytes_are_zero(&slots[1], sizeof(slots[1])));
  CHECK(bytes_are_zero(&slots[2], sizeof(slots[2])));

  memset(&untouched_output, 0xa5, sizeof(untouched_output));
  output_before = untouched_output;
  CHECK(Worr_LocalActionAuditCopyV1(&audit, through, &untouched_output) ==
        WORR_LOCAL_ACTION_AUDIT_STALE);
  CHECK(memcmp(&untouched_output, &output_before, sizeof(untouched_output)) ==
        0);
  CHECK(make_transaction(7, 4, WORR_LOCAL_ACTION_PRODUCER_PREDICTED, 8, 11, 0,
                         &predicted));
  CHECK(Worr_LocalActionAuditSubmitV1(&audit, 1, &predicted) ==
        WORR_LOCAL_ACTION_AUDIT_STALE);
  CHECK(Worr_LocalActionAuditPruneThroughV1(&audit, through) ==
        WORR_LOCAL_ACTION_AUDIT_PRUNE_REGRESSION);

  through.sequence = 6;
  CHECK(Worr_LocalActionAuditPruneThroughV1(&audit, through) ==
        WORR_LOCAL_ACTION_AUDIT_PRUNED);
  CHECK(audit.count == 1);
  through.sequence = 7;
  CHECK(Worr_LocalActionAuditPruneThroughV1(&audit, through) ==
        WORR_LOCAL_ACTION_AUDIT_PRUNE_BLOCKED);
  CHECK(audit.count == 1);
  CHECK(Worr_LocalActionAuditValidateV1(&audit));
  return 0;
}

static int test_command_epoch_wrap_and_connection_reset(void) {
  worr_local_action_audit_v1 audit;
  worr_local_action_audit_slot_v1 slots[4];
  worr_local_action_state_v2 state;
  worr_local_action_transaction_v2 predicted_max;
  worr_local_action_transaction_v2 authority_max;
  worr_local_action_transaction_v2 predicted_wrap;
  worr_local_action_transaction_v2 authority_wrap;
  worr_local_action_transaction_v2 old_connection;
  worr_command_id_v1 through;
  uint32_t old_generation;

  CHECK(Worr_LocalActionAuditInitV1(&audit, slots, 4, 100));
  CHECK(Worr_LocalActionStateInitV2(&state, 20, 1, 8, 11));
  state.applied_cursor.contiguous_sequence = UINT32_MAX - 1;
  state.sample_time_us = 9000;
  CHECK(build_from_state(&state, WORR_LOCAL_ACTION_PRODUCER_PREDICTED,
                         &predicted_max));
  CHECK(build_from_state(&state, WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE,
                         &authority_max));
  CHECK(predicted_max.command.command_id.epoch == 20);
  CHECK(predicted_max.command.command_id.sequence == UINT32_MAX);
  CHECK(build_from_state(&predicted_max.state_after,
                         WORR_LOCAL_ACTION_PRODUCER_PREDICTED,
                         &predicted_wrap));
  CHECK(build_from_state(&authority_max.state_after,
                         WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE,
                         &authority_wrap));
  CHECK(predicted_wrap.command.command_id.epoch == 21);
  CHECK(predicted_wrap.command.command_id.sequence == 1);

  CHECK(Worr_LocalActionAuditSubmitV1(&audit, 100, &predicted_max) ==
        WORR_LOCAL_ACTION_AUDIT_INSERTED);
  CHECK(Worr_LocalActionAuditSubmitV1(&audit, 100, &authority_max) ==
        WORR_LOCAL_ACTION_AUDIT_PAIRED);
  CHECK(Worr_LocalActionAuditSubmitV1(&audit, 100, &authority_wrap) ==
        WORR_LOCAL_ACTION_AUDIT_INSERTED);
  CHECK(Worr_LocalActionAuditSubmitV1(&audit, 100, &predicted_wrap) ==
        WORR_LOCAL_ACTION_AUDIT_PAIRED);
  through = predicted_wrap.command.command_id;
  CHECK(Worr_LocalActionAuditPruneThroughV1(&audit, through) ==
        WORR_LOCAL_ACTION_AUDIT_PRUNED);
  CHECK(audit.count == 0);
  CHECK(audit.pruned_through.epoch == 21 && audit.pruned_through.sequence == 1);
  CHECK(Worr_LocalActionAuditSubmitV1(&audit, 100, &predicted_max) ==
        WORR_LOCAL_ACTION_AUDIT_STALE);

  CHECK(make_transaction(50, 1, WORR_LOCAL_ACTION_PRODUCER_PREDICTED, 8, 11, 0,
                         &old_connection));
  CHECK(Worr_LocalActionAuditSubmitV1(&audit, 99, &old_connection) ==
        WORR_LOCAL_ACTION_AUDIT_WRONG_CONNECTION_EPOCH);
  CHECK(Worr_LocalActionAuditSubmitV1(&audit, 0, &old_connection) ==
        WORR_LOCAL_ACTION_AUDIT_INVALID_ARGUMENT);
  CHECK(Worr_LocalActionAuditResetV1(&audit, 100) ==
        WORR_LOCAL_ACTION_AUDIT_RESET_REGRESSION);
  CHECK(Worr_LocalActionAuditResetV1(&audit, 99) ==
        WORR_LOCAL_ACTION_AUDIT_RESET_REGRESSION);
  CHECK(Worr_LocalActionAuditResetV1(&audit, 0) ==
        WORR_LOCAL_ACTION_AUDIT_INVALID_ARGUMENT);
  old_generation = audit.generation;
  CHECK(Worr_LocalActionAuditResetV1(&audit, 101) ==
        WORR_LOCAL_ACTION_AUDIT_RESET);
  CHECK(audit.connection_epoch == 101);
  CHECK(audit.generation == old_generation + 1);
  CHECK(audit.pruned_through.epoch == 0 && audit.pruned_through.sequence == 0);
  CHECK(audit.next_arrival_serial == 1 && audit.count == 0);
  CHECK(bytes_are_zero(slots, sizeof(slots)));
  CHECK(Worr_LocalActionAuditSubmitV1(&audit, 100, &old_connection) ==
        WORR_LOCAL_ACTION_AUDIT_WRONG_CONNECTION_EPOCH);
  CHECK(Worr_LocalActionAuditSubmitV1(&audit, 101, &old_connection) ==
        WORR_LOCAL_ACTION_AUDIT_INSERTED);

  audit.generation = UINT32_MAX;
  CHECK(Worr_LocalActionAuditValidateV1(&audit));
  CHECK(Worr_LocalActionAuditResetV1(&audit, 102) ==
        WORR_LOCAL_ACTION_AUDIT_GENERATION_EXHAUSTED);
  CHECK(audit.connection_epoch == 101);
  CHECK(audit.count == 1);
  CHECK(Worr_LocalActionAuditValidateV1(&audit));
  return 0;
}

static int test_alias_corruption_and_failure_atomicity(void) {
  worr_local_action_audit_v1 audit;
  worr_local_action_audit_v1 audit_before;
  worr_local_action_audit_slot_v1 slots[3];
  worr_local_action_audit_slot_v1 slots_before[3];
  worr_local_action_transaction_v2 predicted;
  worr_local_action_transaction_v2 invalid;
  worr_local_action_audit_telemetry_v1 telemetry;
  uint64_t original_hash;

  CHECK(Worr_LocalActionAuditInitV1(&audit, slots, 3, 5));
  CHECK(make_transaction(8, 1, WORR_LOCAL_ACTION_PRODUCER_PREDICTED, 8, 11, 0,
                         &predicted));
  CHECK(Worr_LocalActionAuditSubmitV1(&audit, 5, &predicted) ==
        WORR_LOCAL_ACTION_AUDIT_INSERTED);

  audit_before = audit;
  memcpy(slots_before, slots, sizeof(slots));
  CHECK(Worr_LocalActionAuditSubmitV1(&audit, 5, &slots[0].predicted) ==
        WORR_LOCAL_ACTION_AUDIT_INVALID_ARGUMENT);
  CHECK(memcmp(&audit, &audit_before, sizeof(audit)) == 0);
  CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);
  CHECK(Worr_LocalActionAuditCopyV1(&audit, predicted.command.command_id,
                                    &slots[0]) ==
        WORR_LOCAL_ACTION_AUDIT_INVALID_ARGUMENT);
  CHECK(memcmp(&audit, &audit_before, sizeof(audit)) == 0);
  CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);
  CHECK(!Worr_LocalActionAuditGetTelemetryV1(
      &audit, (worr_local_action_audit_telemetry_v1 *)(void *)&slots[0]));
  CHECK(memcmp(&audit, &audit_before, sizeof(audit)) == 0);
  CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);

  invalid = predicted;
  invalid.transaction_hash ^= 1;
  memcpy(slots_before, slots, sizeof(slots));
  CHECK(Worr_LocalActionAuditSubmitV1(&audit, 5, &invalid) ==
        WORR_LOCAL_ACTION_AUDIT_INVALID_TRANSACTION);
  CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);
  CHECK(audit.telemetry.invalid_transactions == 1);
  invalid = predicted;
  invalid.producer_role = 99;
  CHECK(Worr_LocalActionAuditSubmitV1(&audit, 5, &invalid) ==
        WORR_LOCAL_ACTION_AUDIT_INVALID_ROLE);
  CHECK(audit.telemetry.invalid_roles == 1);

  original_hash = slots[0].predicted.transaction_hash;
  slots[0].predicted.transaction_hash ^= 1;
  CHECK(!Worr_LocalActionAuditValidateV1(&audit));
  audit_before = audit;
  memcpy(slots_before, slots, sizeof(slots));
  CHECK(Worr_LocalActionAuditSubmitV1(&audit, 5, &predicted) ==
        WORR_LOCAL_ACTION_AUDIT_INVALID_STATE);
  CHECK(memcmp(&audit, &audit_before, sizeof(audit)) == 0);
  CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);
  slots[0].predicted.transaction_hash = original_hash;
  CHECK(Worr_LocalActionAuditValidateV1(&audit));

  slots[0].first_arrival_serial = audit.next_arrival_serial;
  CHECK(!Worr_LocalActionAuditValidateV1(&audit));
  slots[0].first_arrival_serial = 1;
  slots[0].flags |= WORR_LOCAL_ACTION_AUDIT_SLOT_CLASSIFIED;
  CHECK(!Worr_LocalActionAuditValidateV1(&audit));
  slots[0].flags = WORR_LOCAL_ACTION_AUDIT_SLOT_HAS_PREDICTED;
  audit.telemetry.unmatched_predicted = 0;
  CHECK(!Worr_LocalActionAuditValidateV1(&audit));
  audit.telemetry.unmatched_predicted = 1;
  slots[2].struct_size = 1;
  CHECK(!Worr_LocalActionAuditValidateV1(&audit));
  memset(&slots[2], 0, sizeof(slots[2]));
  CHECK(Worr_LocalActionAuditValidateV1(&audit));

  memset(&telemetry, 0xa5, sizeof(telemetry));
  CHECK(Worr_LocalActionAuditGetTelemetryV1(&audit, &telemetry));
  CHECK(telemetry.invalid_transactions == 1);
  CHECK(telemetry.invalid_roles == 1);
  return 0;
}

static int test_serial_exhaustion_and_saturated_telemetry(void) {
  worr_local_action_audit_v1 audit;
  worr_local_action_audit_slot_v1 slots[4];
  worr_local_action_transaction_v2 predicted;
  worr_local_action_transaction_v2 authority;
  worr_local_action_audit_slot_v1 copied;
  worr_command_id_v1 through = {3, 2};

  CHECK(Worr_LocalActionAuditInitV1(&audit, slots, 4, 2));
  audit.next_arrival_serial = UINT64_MAX;
  CHECK(Worr_LocalActionAuditValidateV1(&audit));
  CHECK(make_transaction(3, 1, WORR_LOCAL_ACTION_PRODUCER_PREDICTED, 8, 11, 0,
                         &predicted));
  CHECK(Worr_LocalActionAuditSubmitV1(&audit, 2, &predicted) ==
        WORR_LOCAL_ACTION_AUDIT_SERIAL_EXHAUSTED);
  CHECK(audit.telemetry.serial_exhaustions == 1);
  audit.next_arrival_serial = 1;
  CHECK(Worr_LocalActionAuditValidateV1(&audit));

  CHECK(Worr_LocalActionAuditSubmitV1(&audit, 2, &predicted) ==
        WORR_LOCAL_ACTION_AUDIT_INSERTED);
  CHECK(make_transaction(3, 1, WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE, 8, 11,
                         0, &authority));
  CHECK(Worr_LocalActionAuditSubmitV1(&audit, 2, &authority) ==
        WORR_LOCAL_ACTION_AUDIT_PAIRED);

  memset(&audit.telemetry, 0xff, sizeof(audit.telemetry));
  audit.telemetry.unmatched_predicted = 0;
  audit.telemetry.unmatched_authoritative = 0;
  CHECK(Worr_LocalActionAuditValidateV1(&audit));
  CHECK(Worr_LocalActionAuditSubmitV1(&audit, 2, &predicted) ==
        WORR_LOCAL_ACTION_AUDIT_DUPLICATE);
  CHECK(audit.telemetry.submit_attempts == UINT64_MAX);
  CHECK(audit.telemetry.duplicates == UINT64_MAX);
  memset(&copied, 0, sizeof(copied));
  CHECK(Worr_LocalActionAuditCopyV1(&audit, predicted.command.command_id,
                                    &copied) == WORR_LOCAL_ACTION_AUDIT_COPIED);
  CHECK(audit.telemetry.copy_attempts == UINT64_MAX);
  CHECK(audit.telemetry.copies == UINT64_MAX);

  CHECK(make_transaction(3, 2, WORR_LOCAL_ACTION_PRODUCER_PREDICTED, 8, 11, 0,
                         &predicted));
  CHECK(Worr_LocalActionAuditSubmitV1(&audit, 2, &predicted) ==
        WORR_LOCAL_ACTION_AUDIT_INSERTED);
  CHECK(audit.telemetry.unmatched_predicted == 1);
  CHECK(Worr_LocalActionAuditPruneThroughV1(&audit, through) ==
        WORR_LOCAL_ACTION_AUDIT_PRUNE_BLOCKED);
  CHECK(audit.telemetry.prune_attempts == UINT64_MAX);
  CHECK(audit.telemetry.prune_blocks == UINT64_MAX);
  CHECK(audit.telemetry.unmatched_predicted == 1);
  CHECK(make_transaction(3, 2, WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE, 8, 11,
                         0, &authority));
  CHECK(Worr_LocalActionAuditSubmitV1(&audit, 2, &authority) ==
        WORR_LOCAL_ACTION_AUDIT_PAIRED);
  CHECK(audit.telemetry.unmatched_predicted == 0);
  CHECK(Worr_LocalActionAuditPruneThroughV1(&audit, through) ==
        WORR_LOCAL_ACTION_AUDIT_PRUNED);
  CHECK(audit.count == 0);
  CHECK(audit.telemetry.pruned_slots == UINT64_MAX);
  CHECK(Worr_LocalActionAuditResetV1(&audit, 3) ==
        WORR_LOCAL_ACTION_AUDIT_RESET);
  CHECK(audit.telemetry.resets == UINT64_MAX);
  CHECK(Worr_LocalActionAuditValidateV1(&audit));
  return 0;
}

static int test_operational_v2_terminal_ids_and_corruption(void) {
  worr_local_action_audit_v1 audit;
  worr_local_action_audit_v1 audit_before;
  worr_local_action_audit_slot_v1 slots[4];
  worr_local_action_audit_slot_v1 slots_before[4];
  worr_local_action_audit_slot_v1 copied;
  worr_local_action_audit_slot_v1 copied_before;
  worr_local_action_transaction_v2 predicted;
  worr_local_action_transaction_v2 authority;
  const worr_command_id_v1 terminal = {UINT32_MAX, UINT32_MAX};
  uint32_t original_frame;
  int32_t original_ammo_delta;

  CHECK(WORR_LOCAL_ACTION_AUDIT_OPERATIONAL_API_VERSION == 2);
  CHECK(Worr_LocalActionAuditInitV2(&audit, slots, 4, 600));
  CHECK(Worr_LocalActionAuditValidateOperationalV2(&audit));
  CHECK(Worr_LocalActionAuditValidateDeepV2(&audit));

  CHECK(make_transaction(UINT32_MAX, UINT32_MAX,
                         WORR_LOCAL_ACTION_PRODUCER_PREDICTED, 8, 11, 0,
                         &predicted));
  CHECK(make_transaction(UINT32_MAX, UINT32_MAX,
                         WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE, 8, 11, 0,
                         &authority));
  CHECK(predicted.command.command_id.epoch == terminal.epoch &&
        predicted.command.command_id.sequence == terminal.sequence);
  CHECK(Worr_LocalActionAuditSubmitV2(&audit, 600, &predicted) ==
        WORR_LOCAL_ACTION_AUDIT_INSERTED);
  CHECK(Worr_LocalActionAuditSubmitV2(&audit, 600, &authority) ==
        WORR_LOCAL_ACTION_AUDIT_PAIRED);
  CHECK(Worr_LocalActionAuditValidateOperationalV2(&audit));
  CHECK(Worr_LocalActionAuditValidateDeepV2(&audit));
  memset(&copied, 0, sizeof(copied));
  CHECK(Worr_LocalActionAuditCopyV2(&audit, terminal, &copied) ==
        WORR_LOCAL_ACTION_AUDIT_COPIED);
  CHECK(copied.command_id.epoch == terminal.epoch &&
        copied.command_id.sequence == terminal.sequence);

  /* A semantic corruption with intact headers is caught before use. */
  original_frame = slots[0].predicted.state_after.presentation_frame;
  slots[0].predicted.state_after.presentation_frame ^= 1u;
  CHECK(Worr_LocalActionAuditValidateOperationalV2(&audit));
  CHECK(!Worr_LocalActionAuditValidateDeepV2(&audit));
  audit_before = audit;
  memcpy(slots_before, slots, sizeof(slots));
  memset(&copied, 0xa5, sizeof(copied));
  copied_before = copied;
  CHECK(Worr_LocalActionAuditCopyV2(&audit, terminal, &copied) ==
        WORR_LOCAL_ACTION_AUDIT_INVALID_STATE);
  CHECK(memcmp(&audit, &audit_before, sizeof(audit)) == 0);
  CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);
  CHECK(memcmp(&copied, &copied_before, sizeof(copied)) == 0);
  CHECK(Worr_LocalActionAuditSubmitV2(&audit, 600, &predicted) ==
        WORR_LOCAL_ACTION_AUDIT_INVALID_STATE);
  CHECK(memcmp(&audit, &audit_before, sizeof(audit)) == 0);
  CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);
  slots[0].predicted.state_after.presentation_frame = original_frame;
  CHECK(Worr_LocalActionAuditValidateDeepV2(&audit));

  /* Structural corruption anywhere poisons every operational mutation. */
  slots[0].first_arrival_serial = audit.next_arrival_serial;
  CHECK(!Worr_LocalActionAuditValidateOperationalV2(&audit));
  audit_before = audit;
  memcpy(slots_before, slots, sizeof(slots));
  CHECK(Worr_LocalActionAuditResetV2(&audit, 601) ==
        WORR_LOCAL_ACTION_AUDIT_INVALID_STATE);
  CHECK(memcmp(&audit, &audit_before, sizeof(audit)) == 0);
  CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);
  slots[0].first_arrival_serial = 1;
  CHECK(Worr_LocalActionAuditValidateDeepV2(&audit));

  CHECK(make_transaction(1, 1, WORR_LOCAL_ACTION_PRODUCER_PREDICTED, 8, 11, 0,
                         &predicted));
  CHECK(make_transaction(1, 1, WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE, 8, 12,
                         0, &authority));
  CHECK(Worr_LocalActionAuditSubmitV2(&audit, 600, &predicted) ==
        WORR_LOCAL_ACTION_AUDIT_INSERTED);
  CHECK(Worr_LocalActionAuditSubmitV2(&audit, 600, &authority) ==
        WORR_LOCAL_ACTION_AUDIT_PAIRED);
  CHECK(slots[1].correction.classification ==
        WORR_LOCAL_ACTION_CORRECTION_PRESENTATION_ONLY);

  /* A still-schema-valid but non-reproducible correction is deep poison. */
  original_ammo_delta = slots[1].correction.ammo_delta;
  slots[1].correction.ammo_delta = 1;
  CHECK(Worr_LocalActionAuditValidateOperationalV2(&audit));
  CHECK(!Worr_LocalActionAuditValidateDeepV2(&audit));
  audit_before = audit;
  memcpy(slots_before, slots, sizeof(slots));
  memset(&copied, 0xa5, sizeof(copied));
  copied_before = copied;
  CHECK(Worr_LocalActionAuditCopyV2(&audit, predicted.command.command_id,
                                    &copied) ==
        WORR_LOCAL_ACTION_AUDIT_INVALID_STATE);
  CHECK(memcmp(&audit, &audit_before, sizeof(audit)) == 0);
  CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);
  CHECK(memcmp(&copied, &copied_before, sizeof(copied)) == 0);
  CHECK(Worr_LocalActionAuditPruneThroughV2(&audit,
                                            predicted.command.command_id) ==
        WORR_LOCAL_ACTION_AUDIT_INVALID_STATE);
  CHECK(memcmp(&audit, &audit_before, sizeof(audit)) == 0);
  CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);
  slots[1].correction.ammo_delta = original_ammo_delta;
  CHECK(Worr_LocalActionAuditValidateDeepV2(&audit));

  /* The linear identity set rejects duplicate retained command IDs. */
  slots[1].command_id = slots[0].command_id;
  slots[1].predicted.command.command_id = slots[0].command_id;
  slots[1].authoritative.command.command_id = slots[0].command_id;
  CHECK(!Worr_LocalActionAuditValidateOperationalV2(&audit));
  slots[1].command_id = predicted.command.command_id;
  slots[1].predicted.command.command_id = predicted.command.command_id;
  slots[1].authoritative.command.command_id = predicted.command.command_id;
  CHECK(Worr_LocalActionAuditValidateDeepV2(&audit));

  /* Inactive payload is not consumed by V2, but the deep audit finds it. */
  slots[3].predicted.events[0].kind = 1;
  CHECK(Worr_LocalActionAuditValidateOperationalV2(&audit));
  CHECK(!Worr_LocalActionAuditValidateDeepV2(&audit));
  CHECK(Worr_LocalActionAuditResetV2(&audit, 601) ==
        WORR_LOCAL_ACTION_AUDIT_RESET);
  CHECK(bytes_are_zero(slots, sizeof(slots)));
  CHECK(Worr_LocalActionAuditValidateDeepV2(&audit));
  return 0;
}

static int test_deterministic_stress(void) {
  worr_local_action_audit_v1 audit;
  worr_local_action_audit_slot_v1 slots[32];
  worr_local_action_transaction_v2 predicted;
  worr_local_action_transaction_v2 authority;
  worr_command_id_v1 through;
  uint32_t sequence;

  CHECK(Worr_LocalActionAuditInitV1(&audit, slots, 32, 88));
  for (sequence = 1; sequence <= 256; ++sequence) {
    CHECK(make_transaction(90, sequence, WORR_LOCAL_ACTION_PRODUCER_PREDICTED,
                           8, 11, 0, &predicted));
    CHECK(make_transaction(90, sequence,
                           WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE, 8, 11, 0,
                           &authority));
    if ((sequence & 1u) != 0) {
      CHECK(Worr_LocalActionAuditSubmitV1(&audit, 88, &predicted) ==
            WORR_LOCAL_ACTION_AUDIT_INSERTED);
      CHECK(Worr_LocalActionAuditSubmitV1(&audit, 88, &authority) ==
            WORR_LOCAL_ACTION_AUDIT_PAIRED);
    } else {
      CHECK(Worr_LocalActionAuditSubmitV1(&audit, 88, &authority) ==
            WORR_LOCAL_ACTION_AUDIT_INSERTED);
      CHECK(Worr_LocalActionAuditSubmitV1(&audit, 88, &predicted) ==
            WORR_LOCAL_ACTION_AUDIT_PAIRED);
    }
    CHECK(Worr_LocalActionAuditValidateV1(&audit));
    if ((sequence % 16u) == 0) {
      through.epoch = 90;
      through.sequence = sequence - 8;
      CHECK(Worr_LocalActionAuditPruneThroughV1(&audit, through) ==
            WORR_LOCAL_ACTION_AUDIT_PRUNED);
      CHECK(audit.count == 8);
      CHECK(Worr_LocalActionAuditValidateV1(&audit));
    }
  }
  through.epoch = 90;
  through.sequence = 256;
  CHECK(Worr_LocalActionAuditPruneThroughV1(&audit, through) ==
        WORR_LOCAL_ACTION_AUDIT_PRUNED);
  CHECK(audit.count == 0);
  CHECK(audit.telemetry.pairs_classified == 256);
  CHECK(audit.telemetry.exact_pairs == 256);
  CHECK(audit.telemetry.presentation_corrections == 0);
  CHECK(audit.telemetry.gameplay_corrections == 0);
  CHECK(audit.telemetry.hard_resync_corrections == 0);
  CHECK(audit.telemetry.capacity_stalls == 0);
  CHECK(audit.telemetry.pruned_slots == 256);
  CHECK(Worr_LocalActionAuditValidateV1(&audit));
  return 0;
}

int main(void) {
  int result;
  result = test_init_layout_and_validation();
  if (result != 0)
    return result;
  result = test_pairing_order_classes_and_immutability();
  if (result != 0)
    return result;
  result = test_prune_block_capacity_and_out_of_order();
  if (result != 0)
    return result;
  result = test_command_epoch_wrap_and_connection_reset();
  if (result != 0)
    return result;
  result = test_alias_corruption_and_failure_atomicity();
  if (result != 0)
    return result;
  result = test_serial_exhaustion_and_saturated_telemetry();
  if (result != 0)
    return result;
  result = test_operational_v2_terminal_ids_and_corruption();
  if (result != 0)
    return result;
  result = test_deterministic_stress();
  if (result != 0)
    return result;
  puts("local action prediction/authority audit tests passed");
  return 0;
}
