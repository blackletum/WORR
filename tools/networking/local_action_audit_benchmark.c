/* Max-capacity CPU budget evidence for the FR-10-T08/T14 audit hot path. */
#include "common/net/local_action_audit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define HOT_ITERATIONS 2000u
#define DEEP_ITERATIONS 8u
#define HOT_BUDGET_NS UINT64_C(500000)
#define LIFECYCLE_BUDGET_NS UINT64_C(10000000)

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,         \
              #condition);                                                     \
      return 1;                                                                \
    }                                                                          \
  } while (0)

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

static bool make_transaction(uint32_t command_epoch, uint32_t command_sequence,
                             uint32_t role,
                             worr_local_action_transaction_v2 *transaction) {
  const worr_local_action_weapon_rule_v2 active = make_rule();
  const worr_local_action_weapon_rule_v2 pending = absent_rule();
  const worr_local_action_intent_v2 intent = idle_intent();
  worr_local_action_state_v2 state;
  worr_command_record_v1 command;

  if (!Worr_LocalActionStateInitV2(&state, command_epoch, 1, 8, 11))
    return false;
  state.applied_cursor.contiguous_sequence = command_sequence - 1u;
  state.sample_time_us = (uint64_t)(command_sequence - 1u) * UINT64_C(1000);

  memset(&command, 0, sizeof(command));
  command.struct_size = sizeof(command);
  command.schema_version = WORR_COMMAND_ABI_VERSION;
  if (!Worr_CommandCursorNextIdV1(state.applied_cursor, &command.command_id)) {
    return false;
  }
  command.sample_time_us = state.sample_time_us + UINT64_C(1000);
  command.movement_model_revision = WORR_PREDICTION_MODEL_REVISION;
  command.command.struct_size = sizeof(command.command);
  command.command.schema_version = WORR_PREDICTION_ABI_VERSION;
  command.command.duration_ms = 1;
  command.render_watermark.struct_size = sizeof(command.render_watermark);
  command.render_watermark.schema_version = WORR_COMMAND_ABI_VERSION;

  memset(transaction, 0, sizeof(*transaction));
  return Worr_LocalActionBuildTransactionV2(&state, &command, &intent, &active,
                                            &pending, role, transaction);
}

static uint64_t elapsed_ns(clock_t begin, clock_t end) {
  const double seconds = (double)(end - begin) / (double)CLOCKS_PER_SEC;
  return (uint64_t)(seconds * 1000000000.0);
}

static uint64_t per_operation_ns(clock_t begin, clock_t end,
                                 uint32_t iterations) {
  const uint64_t total = elapsed_ns(begin, end);
  return (total + iterations - 1u) / iterations;
}

static uint64_t wall_time_ns(void) {
  struct timespec now;
  if (timespec_get(&now, TIME_UTC) != TIME_UTC)
    return 0;
  return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

int main(void) {
  worr_local_action_audit_v1 audit;
  worr_local_action_audit_slot_v1 *slots;
  worr_local_action_audit_slot_v1 copied;
  worr_local_action_audit_telemetry_v1 telemetry;
  worr_local_action_transaction_v2 predicted;
  worr_local_action_transaction_v2 authority;
  worr_local_action_transaction_v2 terminal_predicted;
  worr_command_id_v1 terminal = {UINT32_MAX, UINT32_MAX};
  volatile uint64_t witness = 0;
  uint64_t operational_ns;
  uint64_t duplicate_ns;
  uint64_t copy_ns;
  uint64_t telemetry_ns;
  uint64_t deep_ns;
  uint64_t prune_ns;
  uint64_t reset_ns;
  uint64_t wall_begin;
  uint64_t wall_end;
  uint32_t index;
  clock_t begin;
  clock_t end;

  slots = (worr_local_action_audit_slot_v1 *)calloc(
      WORR_LOCAL_ACTION_AUDIT_MAX_CAPACITY, sizeof(*slots));
  CHECK(slots != NULL);
  CHECK(Worr_LocalActionAuditInitV2(&audit, slots,
                                    WORR_LOCAL_ACTION_AUDIT_MAX_CAPACITY, 700));

  for (index = 1; index < WORR_LOCAL_ACTION_AUDIT_MAX_CAPACITY; ++index) {
    CHECK(make_transaction(10, index, WORR_LOCAL_ACTION_PRODUCER_PREDICTED,
                           &predicted));
    CHECK(make_transaction(10, index, WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE,
                           &authority));
    CHECK(Worr_LocalActionAuditSubmitV2(&audit, 700, &predicted) ==
          WORR_LOCAL_ACTION_AUDIT_INSERTED);
    CHECK(Worr_LocalActionAuditSubmitV2(&audit, 700, &authority) ==
          WORR_LOCAL_ACTION_AUDIT_PAIRED);
  }
  CHECK(make_transaction(UINT32_MAX, UINT32_MAX,
                         WORR_LOCAL_ACTION_PRODUCER_PREDICTED,
                         &terminal_predicted));
  CHECK(make_transaction(UINT32_MAX, UINT32_MAX,
                         WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE, &authority));
  CHECK(Worr_LocalActionAuditSubmitV2(&audit, 700, &terminal_predicted) ==
        WORR_LOCAL_ACTION_AUDIT_INSERTED);
  CHECK(Worr_LocalActionAuditSubmitV2(&audit, 700, &authority) ==
        WORR_LOCAL_ACTION_AUDIT_PAIRED);
  CHECK(audit.count == WORR_LOCAL_ACTION_AUDIT_MAX_CAPACITY);
  CHECK(Worr_LocalActionAuditValidateDeepV2(&audit));

  begin = clock();
  for (index = 0; index < HOT_ITERATIONS; ++index)
    witness += Worr_LocalActionAuditValidateOperationalV2(&audit) ? 1u : 0u;
  end = clock();
  operational_ns = per_operation_ns(begin, end, HOT_ITERATIONS);

  begin = clock();
  for (index = 0; index < HOT_ITERATIONS; ++index) {
    witness += (uint64_t)Worr_LocalActionAuditSubmitV2(&audit, 700,
                                                       &terminal_predicted);
  }
  end = clock();
  duplicate_ns = per_operation_ns(begin, end, HOT_ITERATIONS);

  begin = clock();
  for (index = 0; index < HOT_ITERATIONS; ++index) {
    witness += (uint64_t)Worr_LocalActionAuditCopyV2(&audit, terminal, &copied);
  }
  end = clock();
  copy_ns = per_operation_ns(begin, end, HOT_ITERATIONS);

  begin = clock();
  for (index = 0; index < HOT_ITERATIONS; ++index) {
    witness += Worr_LocalActionAuditGetTelemetryV2(&audit, &telemetry)
                   ? telemetry.unmatched_predicted + 1u
                   : 0u;
  }
  end = clock();
  telemetry_ns = per_operation_ns(begin, end, HOT_ITERATIONS);

  begin = clock();
  for (index = 0; index < DEEP_ITERATIONS; ++index)
    witness += Worr_LocalActionAuditValidateDeepV2(&audit) ? 1u : 0u;
  end = clock();
  deep_ns = per_operation_ns(begin, end, DEEP_ITERATIONS);

  wall_begin = wall_time_ns();
  CHECK(wall_begin != 0);
  CHECK(Worr_LocalActionAuditPruneThroughV2(&audit, terminal) ==
        WORR_LOCAL_ACTION_AUDIT_PRUNED);
  wall_end = wall_time_ns();
  CHECK(wall_end >= wall_begin);
  prune_ns = wall_end - wall_begin;
  CHECK(audit.count == 0);

  wall_begin = wall_time_ns();
  CHECK(wall_begin != 0);
  CHECK(Worr_LocalActionAuditResetV2(&audit, 701) ==
        WORR_LOCAL_ACTION_AUDIT_RESET);
  wall_end = wall_time_ns();
  CHECK(wall_end >= wall_begin);
  reset_ns = wall_end - wall_begin;
  CHECK(Worr_LocalActionAuditValidateDeepV2(&audit));

  printf("local_action_audit_perf capacity=%u iterations=%u "
         "operational_ns=%llu duplicate_terminal_ns=%llu "
         "copy_terminal_ns=%llu telemetry_ns=%llu deep_ns=%llu "
         "prune_ns=%llu reset_ns=%llu hot_budget_ns=%llu "
         "lifecycle_budget_ns=%llu witness=%llu\n",
         WORR_LOCAL_ACTION_AUDIT_MAX_CAPACITY, HOT_ITERATIONS,
         (unsigned long long)operational_ns, (unsigned long long)duplicate_ns,
         (unsigned long long)copy_ns, (unsigned long long)telemetry_ns,
         (unsigned long long)deep_ns, (unsigned long long)prune_ns,
         (unsigned long long)reset_ns, (unsigned long long)HOT_BUDGET_NS,
         (unsigned long long)LIFECYCLE_BUDGET_NS, (unsigned long long)witness);

  CHECK(operational_ns <= HOT_BUDGET_NS);
  CHECK(duplicate_ns <= HOT_BUDGET_NS);
  CHECK(copy_ns <= HOT_BUDGET_NS);
  CHECK(telemetry_ns <= HOT_BUDGET_NS);
  CHECK(prune_ns <= LIFECYCLE_BUDGET_NS);
  CHECK(reset_ns <= LIFECYCLE_BUDGET_NS);
  free(slots);
  return 0;
}
