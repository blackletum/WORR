/* Deterministic hostile checks for FR-10-T08/T09 local-action v2. */
#include "shared/local_action_abi.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                    \
    do {                                                                    \
        if (!(condition)) {                                                 \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,       \
                    __LINE__, #condition);                                  \
            return 1;                                                       \
        }                                                                   \
    } while (0)

static bool bytes_are_zero(const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    size_t i;
    for (i = 0; i < size; ++i) {
        if (bytes[i] != 0)
            return false;
    }
    return true;
}

static worr_local_action_weapon_rule_v2 absent_rule(void)
{
    worr_local_action_weapon_rule_v2 rule;
    memset(&rule, 0, sizeof(rule));
    rule.struct_size = sizeof(rule);
    rule.schema_version = WORR_LOCAL_ACTION_ABI_VERSION;
    rule.model_revision = WORR_LOCAL_ACTION_MODEL_REVISION;
    return rule;
}

static worr_local_action_weapon_rule_v2 make_rule(uint32_t weapon_id,
                                                   bool automatic,
                                                   uint32_t raise_ms,
                                                   uint32_t lower_ms,
                                                   uint32_t refire_ms)
{
    worr_local_action_weapon_rule_v2 rule = absent_rule();
    rule.flags = WORR_LOCAL_ACTION_WEAPON_USES_AMMO |
                 WORR_LOCAL_ACTION_WEAPON_PREDICT_AUDIO |
                 WORR_LOCAL_ACTION_WEAPON_PREDICT_EFFECT;
    if (automatic)
        rule.flags |= WORR_LOCAL_ACTION_WEAPON_AUTOMATIC;
    rule.weapon_id = weapon_id;
    rule.ammo_per_shot = 1;
    rule.raise_duration_ms = raise_ms;
    rule.lower_duration_ms = lower_ms;
    rule.refire_duration_ms = refire_ms;
    rule.ready_frame = weapon_id * 10 + 1;
    rule.fire_frame = weapon_id * 10 + 2;
    rule.fire_audio_asset_id = weapon_id * 100 + 1;
    rule.dry_audio_asset_id = weapon_id * 100 + 2;
    rule.fire_effect_id = weapon_id * 100 + 3;
    rule.switch_effect_id = weapon_id * 100 + 4;
    return rule;
}

static worr_local_action_intent_v2 make_intent(uint32_t flags,
                                                uint32_t weapon_id,
                                                uint32_t ammo_units)
{
    worr_local_action_intent_v2 intent;
    memset(&intent, 0, sizeof(intent));
    intent.struct_size = sizeof(intent);
    intent.schema_version = WORR_LOCAL_ACTION_ABI_VERSION;
    intent.flags = flags;
    intent.requested_weapon_id = weapon_id;
    intent.requested_ammo_units = ammo_units;
    return intent;
}

static worr_command_record_v1 make_command(
    const worr_local_action_state_v2 *state, uint8_t duration_ms)
{
    worr_command_record_v1 command;
    worr_command_id_v1 next_id = {0, 0};
    memset(&command, 0, sizeof(command));
    command.struct_size = sizeof(command);
    command.schema_version = WORR_COMMAND_ABI_VERSION;
    (void)Worr_CommandCursorNextIdV1(state->applied_cursor, &next_id);
    command.command_id = next_id;
    command.sample_time_us = state->sample_time_us +
                             (uint64_t)duration_ms * UINT64_C(1000);
    command.movement_model_revision = WORR_PREDICTION_MODEL_REVISION;
    command.command.struct_size = sizeof(command.command);
    command.command.schema_version = WORR_PREDICTION_ABI_VERSION;
    command.command.duration_ms = duration_ms;
    command.render_watermark.struct_size =
        sizeof(command.render_watermark);
    command.render_watermark.schema_version = WORR_COMMAND_ABI_VERSION;
    return command;
}

static bool build(const worr_local_action_state_v2 *state,
                  uint8_t duration_ms,
                  worr_local_action_intent_v2 intent,
                  worr_local_action_weapon_rule_v2 active,
                  worr_local_action_weapon_rule_v2 pending,
                  uint32_t role,
                  worr_local_action_transaction_v2 *transaction)
{
    const worr_command_record_v1 command = make_command(state, duration_ms);
    memset(transaction, 0, sizeof(*transaction));
    return Worr_LocalActionBuildTransactionV2(
        state, &command, &intent, &active, &pending, role, transaction);
}

static int test_schema_and_fail_closed_validation(void)
{
    worr_local_action_state_v2 state;
    worr_local_action_state_v2 before;
    worr_local_action_weapon_rule_v2 rule = make_rule(1, false, 20, 30, 40);
    worr_local_action_weapon_rule_v2 absent = absent_rule();
    worr_local_action_intent_v2 intent = make_intent(0, 0, 0);
    worr_local_action_transaction_v2 transaction;
    worr_local_action_transaction_v2 untouched;
    worr_command_record_v1 command;

    CHECK(sizeof(state) == 72);
    CHECK(sizeof(rule) == 64);
    CHECK(sizeof(intent) == 24);
    CHECK(sizeof(worr_local_action_event_v2) == 48);
    CHECK(sizeof(transaction) == 1216);
    CHECK(sizeof(worr_local_action_correction_v2) == 64);
    CHECK(Worr_LocalActionStateInitV2(&state, 9, 1, 8, rule.ready_frame));
    CHECK(Worr_LocalActionStateValidateV2(&state));
    CHECK(Worr_LocalActionWeaponRuleValidateV2(&rule));
    CHECK(Worr_LocalActionWeaponRuleValidateV2(&absent));
    CHECK(Worr_LocalActionIntentValidateV2(&intent));

    before = state;
    CHECK(!Worr_LocalActionStateInitV2(&state, 0, 1, 8, 11));
    CHECK(memcmp(&state, &before, sizeof(state)) == 0);
    CHECK(!Worr_LocalActionStateInitV2(&state, 1, 0, 1, 0));
    CHECK(memcmp(&state, &before, sizeof(state)) == 0);

    state.flags = WORR_LOCAL_ACTION_STATE_DRY_FIRE_LATCHED;
    CHECK(!Worr_LocalActionStateValidateV2(&state));
    state = before;
    state.phase = WORR_LOCAL_ACTION_PHASE_RAISING;
    CHECK(!Worr_LocalActionStateValidateV2(&state));
    state = before;
    state.pending_ammo_units = 1;
    CHECK(!Worr_LocalActionStateValidateV2(&state));
    state = before;
    state.reserved0 = 1;
    CHECK(!Worr_LocalActionStateValidateV2(&state));
    state = before;

    rule.reserved0 = 1;
    CHECK(!Worr_LocalActionWeaponRuleValidateV2(&rule));
    rule = make_rule(1, false, 20, 30, 40);
    rule.refire_duration_ms = 0;
    CHECK(!Worr_LocalActionWeaponRuleValidateV2(&rule));
    rule = make_rule(1, false, 20, 30, 40);
    rule.flags &= ~WORR_LOCAL_ACTION_WEAPON_PREDICT_AUDIO;
    CHECK(!Worr_LocalActionWeaponRuleValidateV2(&rule));
    rule = make_rule(1, false, 20, 30, 40);
    rule.flags |= UINT32_C(0x80000000);
    CHECK(!Worr_LocalActionWeaponRuleValidateV2(&rule));
    rule = make_rule(1, false, 20, 30, 40);

    intent.requested_weapon_id = 1;
    CHECK(!Worr_LocalActionIntentValidateV2(&intent));
    intent = make_intent(WORR_LOCAL_ACTION_INTENT_SWITCH_REQUEST, 0, 1);
    CHECK(!Worr_LocalActionIntentValidateV2(&intent));
    intent = make_intent(0, 0, 0);

    command = make_command(&state, 0);
    memset(&transaction, 0xa5, sizeof(transaction));
    untouched = transaction;
    CHECK(!Worr_LocalActionBuildTransactionV2(
        &state, &command, &intent, &rule, &absent,
        WORR_LOCAL_ACTION_PRODUCER_PREDICTED, &transaction));
    CHECK(memcmp(&transaction, &untouched, sizeof(transaction)) == 0);

    memset(&transaction, 0, sizeof(transaction));
    command.command_id.sequence++;
    CHECK(!Worr_LocalActionBuildTransactionV2(
        &state, &command, &intent, &rule, &absent,
        WORR_LOCAL_ACTION_PRODUCER_PREDICTED, &transaction));
    CHECK(bytes_are_zero(&transaction, sizeof(transaction)));
    command = make_command(&state, 1);
    command.sample_time_us--;
    CHECK(!Worr_LocalActionBuildTransactionV2(
        &state, &command, &intent, &rule, &absent,
        WORR_LOCAL_ACTION_PRODUCER_PREDICTED, &transaction));
    CHECK(bytes_are_zero(&transaction, sizeof(transaction)));
    command = make_command(&state, 0);
    command.command.duration_ms = 251;
    command.sample_time_us = state.sample_time_us + UINT64_C(251000);
    CHECK(!Worr_LocalActionBuildTransactionV2(
        &state, &command, &intent, &rule, &absent,
        WORR_LOCAL_ACTION_PRODUCER_PREDICTED, &transaction));
    CHECK(bytes_are_zero(&transaction, sizeof(transaction)));
    command = make_command(&state, 0);
    command.reserved0 = 1;
    CHECK(!Worr_LocalActionBuildTransactionV2(
        &state, &command, &intent, &rule, &absent,
        WORR_LOCAL_ACTION_PRODUCER_PREDICTED, &transaction));
    CHECK(bytes_are_zero(&transaction, sizeof(transaction)));
    CHECK(!Worr_LocalActionBuildTransactionV2(
        &state, &command, &intent, &absent, &absent,
        WORR_LOCAL_ACTION_PRODUCER_PREDICTED, &transaction));
    CHECK(bytes_are_zero(&transaction, sizeof(transaction)));
    return 0;
}

static int test_prediction_authority_fire_parity(void)
{
    worr_local_action_state_v2 state;
    worr_local_action_weapon_rule_v2 semi = make_rule(1, false, 0, 0, 20);
    worr_local_action_weapon_rule_v2 automatic = make_rule(1, true, 0, 0, 20);
    worr_local_action_weapon_rule_v2 absent = absent_rule();
    worr_local_action_intent_v2 held =
        make_intent(WORR_LOCAL_ACTION_INTENT_ATTACK_HELD, 0, 0);
    worr_local_action_intent_v2 released = make_intent(0, 0, 0);
    worr_local_action_transaction_v2 predicted;
    worr_local_action_transaction_v2 authority;
    worr_local_action_transaction_v2 next;

    CHECK(Worr_LocalActionStateInitV2(&state, 1, 1, 3, semi.ready_frame));
    CHECK(build(&state, 0, held, semi, absent,
                WORR_LOCAL_ACTION_PRODUCER_PREDICTED, &predicted));
    CHECK(build(&state, 0, held, semi, absent,
                WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE, &authority));
    CHECK(Worr_LocalActionTransactionValidateV2(&predicted));
    CHECK(Worr_LocalActionTransactionValidateV2(&authority));
    CHECK(predicted.transaction_hash == authority.transaction_hash);
    CHECK(predicted.state_hash == authority.state_hash);
    CHECK(predicted.event_hash == authority.event_hash);
    CHECK(memcmp(&predicted.state_after, &authority.state_after,
                 sizeof(predicted.state_after)) == 0);
    CHECK(memcmp(predicted.events, authority.events,
                 sizeof(predicted.events)) == 0);
    CHECK(predicted.event_count == 3);
    CHECK(predicted.events[0].kind == WORR_LOCAL_ACTION_EVENT_WEAPON_FIRE);
    CHECK(predicted.events[0].prediction_key.lane ==
          WORR_EVENT_PREDICTION_LANE_GAMEPLAY);
    CHECK(predicted.events[1].prediction_key.lane ==
          WORR_EVENT_PREDICTION_LANE_AUDIO);
    CHECK(predicted.events[2].prediction_key.lane ==
          WORR_EVENT_PREDICTION_LANE_EFFECT);
    CHECK(predicted.events[0].prediction_key.emitter_ordinal == 1);
    CHECK(predicted.events[1].prediction_key.emitter_ordinal == 1);
    CHECK(predicted.events[2].prediction_key.emitter_ordinal == 1);
    CHECK(predicted.state_after.active_ammo_units == 2);
    CHECK(predicted.state_after.shot_sequence == 1);
    CHECK(predicted.state_after.action_sequence == 1);
    CHECK(predicted.state_after.phase == WORR_LOCAL_ACTION_PHASE_FIRING);

    CHECK(build(&predicted.state_after, 20, held, semi, absent,
                WORR_LOCAL_ACTION_PRODUCER_PREDICTED, &next));
    CHECK(next.state_after.phase == WORR_LOCAL_ACTION_PHASE_READY);
    CHECK(next.state_after.shot_sequence == 1);
    CHECK(next.event_count == 0);
    CHECK(build(&next.state_after, 0, released, semi, absent,
                WORR_LOCAL_ACTION_PRODUCER_PREDICTED, &predicted));
    CHECK(build(&predicted.state_after, 0, held, semi, absent,
                WORR_LOCAL_ACTION_PRODUCER_PREDICTED, &next));
    CHECK(next.state_after.shot_sequence == 2);

    state = predicted.state_after;
    CHECK(build(&state, 0, held, automatic, absent,
                WORR_LOCAL_ACTION_PRODUCER_PREDICTED, &next));
    CHECK(next.state_after.shot_sequence == 2);
    CHECK(build(&next.state_after, 20, held, automatic, absent,
                WORR_LOCAL_ACTION_PRODUCER_PREDICTED, &predicted));
    CHECK(predicted.state_after.shot_sequence == 3);
    return 0;
}

static int test_dry_fire_latch_and_exhaustion(void)
{
    worr_local_action_state_v2 state;
    worr_local_action_weapon_rule_v2 rule = make_rule(1, false, 0, 0, 10);
    worr_local_action_weapon_rule_v2 absent = absent_rule();
    worr_local_action_intent_v2 held =
        make_intent(WORR_LOCAL_ACTION_INTENT_ATTACK_HELD, 0, 0);
    worr_local_action_intent_v2 released = make_intent(0, 0, 0);
    worr_local_action_transaction_v2 transaction;

    CHECK(Worr_LocalActionStateInitV2(&state, 2, 1, 0, rule.ready_frame));
    CHECK(build(&state, 0, held, rule, absent,
                WORR_LOCAL_ACTION_PRODUCER_PREDICTED, &transaction));
    CHECK(transaction.event_count == 2);
    CHECK(transaction.events[0].kind == WORR_LOCAL_ACTION_EVENT_DRY_FIRE);
    CHECK((transaction.state_after.flags &
           WORR_LOCAL_ACTION_STATE_DRY_FIRE_LATCHED) != 0);
    state = transaction.state_after;
    CHECK(build(&state, 0, held, rule, absent,
                WORR_LOCAL_ACTION_PRODUCER_PREDICTED, &transaction));
    CHECK(transaction.event_count == 0);
    state = transaction.state_after;
    CHECK(build(&state, 0, released, rule, absent,
                WORR_LOCAL_ACTION_PRODUCER_PREDICTED, &transaction));
    CHECK(transaction.state_after.flags == 0);
    state = transaction.state_after;
    CHECK(build(&state, 0, held, rule, absent,
                WORR_LOCAL_ACTION_PRODUCER_PREDICTED, &transaction));
    CHECK(transaction.event_count == 2);

    state = transaction.state_before;
    state.action_sequence = UINT32_MAX;
    memset(&transaction, 0, sizeof(transaction));
    {
        const worr_command_record_v1 command = make_command(&state, 0);
        CHECK(!Worr_LocalActionBuildTransactionV2(
            &state, &command, &held, &rule, &absent,
            WORR_LOCAL_ACTION_PRODUCER_PREDICTED, &transaction));
        CHECK(bytes_are_zero(&transaction, sizeof(transaction)));
    }
    CHECK(Worr_LocalActionStateInitV2(&state, 2, 1, 1, rule.ready_frame));
    state.shot_sequence = UINT32_MAX;
    memset(&transaction, 0, sizeof(transaction));
    {
        const worr_command_record_v1 command = make_command(&state, 0);
        CHECK(!Worr_LocalActionBuildTransactionV2(
            &state, &command, &held, &rule, &absent,
            WORR_LOCAL_ACTION_PRODUCER_PREDICTED, &transaction));
        CHECK(bytes_are_zero(&transaction, sizeof(transaction)));
    }
    return 0;
}

static int test_switch_phases_and_zero_duration_normalization(void)
{
    worr_local_action_state_v2 state;
    worr_local_action_weapon_rule_v2 first = make_rule(1, false, 10, 20, 30);
    worr_local_action_weapon_rule_v2 second = make_rule(2, false, 30, 40, 50);
    worr_local_action_weapon_rule_v2 zero = make_rule(3, false, 0, 0, 10);
    worr_local_action_weapon_rule_v2 absent = absent_rule();
    worr_local_action_intent_v2 idle = make_intent(0, 0, 0);
    worr_local_action_intent_v2 switch_second =
        make_intent(WORR_LOCAL_ACTION_INTENT_SWITCH_REQUEST, 2, 7);
    worr_local_action_transaction_v2 transaction;

    CHECK(Worr_LocalActionStateInitV2(&state, 3, 1, 5, first.ready_frame));
    CHECK(build(&state, 0, switch_second, first, second,
                WORR_LOCAL_ACTION_PRODUCER_PREDICTED, &transaction));
    CHECK(transaction.state_after.phase == WORR_LOCAL_ACTION_PHASE_LOWERING);
    CHECK(transaction.state_after.phase_remaining_ms == 20);
    CHECK(transaction.state_after.pending_weapon_id == 2);
    CHECK(transaction.event_count == 2);
    CHECK(transaction.events[0].kind ==
          WORR_LOCAL_ACTION_EVENT_SWITCH_BEGIN);
    state = transaction.state_after;

    {
        const worr_local_action_intent_v2 retarget =
            make_intent(WORR_LOCAL_ACTION_INTENT_SWITCH_REQUEST, 3, 9);
        CHECK(!build(&state, 0, retarget, first, zero,
                     WORR_LOCAL_ACTION_PRODUCER_PREDICTED, &transaction));
        CHECK(bytes_are_zero(&transaction, sizeof(transaction)));
    }

    CHECK(build(&state, 20, idle, first, second,
                WORR_LOCAL_ACTION_PRODUCER_PREDICTED, &transaction));
    CHECK(transaction.state_after.active_weapon_id == 2);
    CHECK(transaction.state_after.active_ammo_units == 7);
    CHECK(transaction.state_after.phase == WORR_LOCAL_ACTION_PHASE_RAISING);
    CHECK(transaction.state_after.phase_remaining_ms == 30);
    CHECK(transaction.events[0].kind ==
          WORR_LOCAL_ACTION_EVENT_SWITCH_COMMIT);
    state = transaction.state_after;
    CHECK(build(&state, 30, idle, second, absent,
                WORR_LOCAL_ACTION_PRODUCER_PREDICTED, &transaction));
    CHECK(transaction.state_after.phase == WORR_LOCAL_ACTION_PHASE_READY);
    CHECK(transaction.state_after.presentation_frame == second.ready_frame);
    CHECK(transaction.events[0].kind ==
          WORR_LOCAL_ACTION_EVENT_WEAPON_READY);

    state = transaction.state_after;
    {
        const worr_local_action_intent_v2 holster =
            make_intent(WORR_LOCAL_ACTION_INTENT_SWITCH_REQUEST, 0, 0);
        CHECK(build(&state, 0, holster, second, absent,
                    WORR_LOCAL_ACTION_PRODUCER_PREDICTED, &transaction));
        CHECK(transaction.state_after.phase ==
              WORR_LOCAL_ACTION_PHASE_LOWERING);
        state = transaction.state_after;
        CHECK(build(&state, 40, idle, second, absent,
                    WORR_LOCAL_ACTION_PRODUCER_PREDICTED, &transaction));
        CHECK(transaction.state_after.phase ==
              WORR_LOCAL_ACTION_PHASE_HOLSTERED);
        CHECK(transaction.state_after.active_weapon_id == 0);
        CHECK(transaction.state_after.presentation_frame == 0);
    }

    state = transaction.state_after;
    {
        const worr_local_action_intent_v2 equip =
            make_intent(WORR_LOCAL_ACTION_INTENT_SWITCH_REQUEST, 3, 9);
        CHECK(build(&state, 0, equip, absent, zero,
                    WORR_LOCAL_ACTION_PRODUCER_PREDICTED, &transaction));
        CHECK(transaction.state_after.phase == WORR_LOCAL_ACTION_PHASE_READY);
        CHECK(transaction.state_after.active_weapon_id == 3);
        CHECK(transaction.event_count == 4);
        CHECK(transaction.events[0].kind ==
              WORR_LOCAL_ACTION_EVENT_SWITCH_BEGIN);
        CHECK(transaction.events[2].kind ==
              WORR_LOCAL_ACTION_EVENT_SWITCH_COMMIT);
        CHECK(transaction.events[3].kind ==
              WORR_LOCAL_ACTION_EVENT_WEAPON_READY);
    }
    return 0;
}

static int test_identity_wrap_corruption_and_atomicity(void)
{
    worr_local_action_state_v2 state;
    worr_local_action_weapon_rule_v2 rule = make_rule(1, false, 0, 0, 10);
    worr_local_action_weapon_rule_v2 absent = absent_rule();
    worr_local_action_intent_v2 idle = make_intent(0, 0, 0);
    worr_local_action_transaction_v2 transaction;
    worr_local_action_transaction_v2 corrupt;

    CHECK(Worr_LocalActionStateInitV2(&state, 5, 1, 1, rule.ready_frame));
    state.applied_cursor.contiguous_sequence = UINT32_MAX;
    CHECK(build(&state, 0, idle, rule, absent,
                WORR_LOCAL_ACTION_PRODUCER_PREDICTED, &transaction));
    CHECK(transaction.command.command_id.epoch == 6);
    CHECK(transaction.command.command_id.sequence == 1);
    CHECK(transaction.state_after.applied_cursor.epoch == 6);

    state.applied_cursor.epoch = UINT32_MAX;
    state.applied_cursor.contiguous_sequence = UINT32_MAX;
    memset(&transaction, 0, sizeof(transaction));
    {
        worr_command_record_v1 command = make_command(&state, 0);
        command.command_id.epoch = UINT32_MAX;
        command.command_id.sequence = UINT32_MAX;
        CHECK(!Worr_LocalActionBuildTransactionV2(
            &state, &command, &idle, &rule, &absent,
            WORR_LOCAL_ACTION_PRODUCER_PREDICTED, &transaction));
    }
    CHECK(bytes_are_zero(&transaction, sizeof(transaction)));

    CHECK(Worr_LocalActionStateInitV2(&state, 5, 1, 1, rule.ready_frame));
    CHECK(build(&state, 0, idle, rule, absent,
                WORR_LOCAL_ACTION_PRODUCER_PREDICTED, &transaction));
    corrupt = transaction;
    corrupt.transaction_hash ^= 1;
    CHECK(!Worr_LocalActionTransactionValidateV2(&corrupt));
    corrupt = transaction;
    corrupt.events[WORR_LOCAL_ACTION_MAX_EVENTS - 1].asset_id = 1;
    CHECK(!Worr_LocalActionTransactionValidateV2(&corrupt));
    corrupt = transaction;
    corrupt.state_after.reserved0 = 1;
    CHECK(!Worr_LocalActionTransactionValidateV2(&corrupt));
    corrupt = transaction;
    corrupt.event_count = WORR_LOCAL_ACTION_MAX_EVENTS + 1;
    CHECK(!Worr_LocalActionTransactionValidateV2(&corrupt));

    state.sample_time_us = UINT64_MAX;
    memset(&transaction, 0, sizeof(transaction));
    {
        worr_command_record_v1 command = make_command(&state, 0);
        command.command.duration_ms = 1;
        CHECK(!Worr_LocalActionBuildTransactionV2(
            &state, &command, &idle, &rule, &absent,
            WORR_LOCAL_ACTION_PRODUCER_PREDICTED, &transaction));
    }
    CHECK(bytes_are_zero(&transaction, sizeof(transaction)));
    return 0;
}

static int test_event_record_adapter(void)
{
    worr_local_action_state_v2 state;
    worr_local_action_weapon_rule_v2 rule = make_rule(1, false, 0, 0, 10);
    worr_local_action_weapon_rule_v2 absent = absent_rule();
    worr_local_action_intent_v2 held =
        make_intent(WORR_LOCAL_ACTION_INTENT_ATTACK_HELD, 0, 0);
    worr_local_action_transaction_v2 transaction;
    worr_local_action_event_record_context_v2 predicted_context;
    worr_local_action_event_record_context_v2 authority_context;
    worr_event_record_v1 predicted_record;
    worr_event_record_v1 authority_record;
    worr_event_record_v1 untouched;
    uint64_t predicted_hash;
    uint64_t authority_hash;
    uint32_t i;

    CHECK(Worr_LocalActionStateInitV2(&state, 7, 1, 2, rule.ready_frame));
    CHECK(build(&state, 0, held, rule, absent,
                WORR_LOCAL_ACTION_PRODUCER_PREDICTED, &transaction));
    memset(&predicted_context, 0, sizeof(predicted_context));
    predicted_context.struct_size = sizeof(predicted_context);
    predicted_context.schema_version = WORR_LOCAL_ACTION_ABI_VERSION;
    predicted_context.model_revision = WORR_LOCAL_ACTION_MODEL_REVISION;
    predicted_context.producer_role =
        WORR_LOCAL_ACTION_PRODUCER_PREDICTED;
    predicted_context.source_tick = UINT32_MAX - 2;
    predicted_context.lifetime_ticks = 5;
    predicted_context.source_entity.index = 1;
    predicted_context.source_entity.generation = 3;
    predicted_context.subject_entity.index = WORR_EVENT_NO_ENTITY;
    authority_context = predicted_context;
    authority_context.producer_role =
        WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE;
    authority_context.event_id.stream_epoch = 4;

    for (i = 0; i < transaction.event_count; ++i) {
        authority_context.event_id.sequence = i + 1;
        memset(&predicted_record, 0, sizeof(predicted_record));
        memset(&authority_record, 0, sizeof(authority_record));
        CHECK(Worr_LocalActionBuildEventRecordV2(
            &transaction.events[i], &predicted_context, 64,
            &predicted_record));
        CHECK(Worr_LocalActionBuildEventRecordV2(
            &transaction.events[i], &authority_context, 64,
            &authority_record));
        CHECK(Worr_EventRecordCandidateValidateV1(&predicted_record, 64));
        CHECK(Worr_EventRecordValidateV1(&authority_record, 64));
        CHECK(Worr_EventRecordSemanticHashV1(&predicted_record, 64,
                                             &predicted_hash));
        CHECK(Worr_EventRecordSemanticHashV1(&authority_record, 64,
                                             &authority_hash));
        CHECK(predicted_hash == authority_hash);
        CHECK(Worr_EventRecordSemanticallyEqualV1(
            &predicted_record, &authority_record, 64));
        CHECK(predicted_record.expiry_tick == 2);
    }

    memset(&predicted_record, 0xa5, sizeof(predicted_record));
    untouched = predicted_record;
    CHECK(!Worr_LocalActionBuildEventRecordV2(
        &transaction.events[0], &predicted_context, 64,
        &predicted_record));
    CHECK(memcmp(&predicted_record, &untouched, sizeof(predicted_record)) ==
          0);
    memset(&predicted_record, 0, sizeof(predicted_record));
    predicted_context.event_id.sequence = 1;
    CHECK(!Worr_LocalActionBuildEventRecordV2(
        &transaction.events[0], &predicted_context, 64,
        &predicted_record));
    CHECK(bytes_are_zero(&predicted_record, sizeof(predicted_record)));
    predicted_context.event_id.sequence = 0;
    predicted_context.lifetime_ticks = 0;
    CHECK(!Worr_LocalActionBuildEventRecordV2(
        &transaction.events[0], &predicted_context, 64,
        &predicted_record));
    CHECK(bytes_are_zero(&predicted_record, sizeof(predicted_record)));
    return 0;
}

static int test_correction_classes(void)
{
    worr_local_action_weapon_rule_v2 rule = make_rule(1, false, 0, 0, 10);
    worr_local_action_weapon_rule_v2 effect_only = rule;
    worr_local_action_weapon_rule_v2 absent = absent_rule();
    worr_local_action_intent_v2 idle = make_intent(0, 0, 0);
    worr_local_action_intent_v2 held =
        make_intent(WORR_LOCAL_ACTION_INTENT_ATTACK_HELD, 0, 0);
    worr_local_action_state_v2 pstate;
    worr_local_action_state_v2 astate;
    worr_local_action_transaction_v2 predicted;
    worr_local_action_transaction_v2 authority;
    worr_local_action_correction_v2 correction;
    worr_local_action_correction_v2 untouched;

    CHECK(Worr_LocalActionStateInitV2(&pstate, 10, 1, 2,
                                      rule.ready_frame));
    astate = pstate;
    CHECK(build(&pstate, 0, idle, rule, absent,
                WORR_LOCAL_ACTION_PRODUCER_PREDICTED, &predicted));
    CHECK(build(&astate, 0, idle, rule, absent,
                WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE, &authority));
    memset(&correction, 0, sizeof(correction));
    CHECK(Worr_LocalActionCorrectionClassifyV2(
        &predicted, &authority, &correction));
    CHECK(correction.classification == WORR_LOCAL_ACTION_CORRECTION_NONE);
    CHECK(Worr_LocalActionCorrectionValidateV2(&correction));

    astate = pstate;
    astate.presentation_frame++;
    CHECK(build(&astate, 0, idle, rule, absent,
                WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE, &authority));
    memset(&correction, 0, sizeof(correction));
    CHECK(Worr_LocalActionCorrectionClassifyV2(
        &predicted, &authority, &correction));
    CHECK(correction.classification ==
          WORR_LOCAL_ACTION_CORRECTION_PRESENTATION_ONLY);
    CHECK(correction.difference_bits ==
          WORR_LOCAL_ACTION_DIFF_PRESENTATION_FRAME);

    astate = pstate;
    astate.active_ammo_units = 1;
    CHECK(build(&pstate, 0, held, rule, absent,
                WORR_LOCAL_ACTION_PRODUCER_PREDICTED, &predicted));
    CHECK(build(&astate, 0, held, rule, absent,
                WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE, &authority));
    memset(&correction, 0, sizeof(correction));
    CHECK(Worr_LocalActionCorrectionClassifyV2(
        &predicted, &authority, &correction));
    CHECK(correction.classification ==
          WORR_LOCAL_ACTION_CORRECTION_GAMEPLAY_IMMEDIATE);
    CHECK((correction.difference_bits & WORR_LOCAL_ACTION_DIFF_AMMO) != 0);
    CHECK(correction.ammo_delta == -1);

    astate = pstate;
    astate.applied_cursor.contiguous_sequence = 1;
    CHECK(build(&pstate, 0, idle, rule, absent,
                WORR_LOCAL_ACTION_PRODUCER_PREDICTED, &predicted));
    CHECK(build(&astate, 0, idle, rule, absent,
                WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE, &authority));
    memset(&correction, 0, sizeof(correction));
    CHECK(Worr_LocalActionCorrectionClassifyV2(
        &predicted, &authority, &correction));
    CHECK(correction.classification ==
          WORR_LOCAL_ACTION_CORRECTION_HARD_RESYNC);
    CHECK((correction.difference_bits & WORR_LOCAL_ACTION_DIFF_COMMAND_ID) !=
          0);

    astate = pstate;
    astate.sample_time_us = 1000;
    CHECK(build(&pstate, 0, idle, rule, absent,
                WORR_LOCAL_ACTION_PRODUCER_PREDICTED, &predicted));
    CHECK(build(&astate, 0, idle, rule, absent,
                WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE, &authority));
    memset(&correction, 0, sizeof(correction));
    CHECK(Worr_LocalActionCorrectionClassifyV2(
        &predicted, &authority, &correction));
    CHECK(correction.classification ==
          WORR_LOCAL_ACTION_CORRECTION_HARD_RESYNC);
    CHECK((correction.difference_bits & WORR_LOCAL_ACTION_DIFF_SAMPLE_TIME) !=
          0);

    effect_only.flags &=
        ~(uint32_t)WORR_LOCAL_ACTION_WEAPON_PREDICT_AUDIO;
    effect_only.fire_audio_asset_id = 0;
    effect_only.dry_audio_asset_id = 0;
    astate = pstate;
    CHECK(build(&pstate, 0, held, rule, absent,
                WORR_LOCAL_ACTION_PRODUCER_PREDICTED, &predicted));
    CHECK(build(&astate, 0, held, effect_only, absent,
                WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE, &authority));
    memset(&correction, 0, sizeof(correction));
    CHECK(Worr_LocalActionCorrectionClassifyV2(
        &predicted, &authority, &correction));
    CHECK(correction.classification ==
          WORR_LOCAL_ACTION_CORRECTION_HARD_RESYNC);
    CHECK((correction.difference_bits & WORR_LOCAL_ACTION_DIFF_EVENT_KEY) !=
          0);

    memset(&correction, 0xa5, sizeof(correction));
    untouched = correction;
    CHECK(!Worr_LocalActionCorrectionClassifyV2(
        &authority, &predicted, &correction));
    CHECK(memcmp(&correction, &untouched, sizeof(correction)) == 0);
    return 0;
}

static uint32_t lcg_next(uint32_t *seed)
{
    *seed = *seed * UINT32_C(1664525) + UINT32_C(1013904223);
    return *seed;
}

static int replay_digest(uint32_t initial_seed, uint64_t *digest_out)
{
    worr_local_action_state_v2 state;
    worr_local_action_weapon_rule_v2 rule = make_rule(1, true, 0, 0, 7);
    worr_local_action_weapon_rule_v2 absent = absent_rule();
    uint64_t digest = UINT64_C(0xcbf29ce484222325);
    uint32_t seed = initial_seed;
    uint32_t i;

    CHECK(Worr_LocalActionStateInitV2(&state, 15, 1, 1000,
                                      rule.ready_frame));
    state.applied_cursor.contiguous_sequence = UINT32_MAX - 20;
    for (i = 0; i < 4096; ++i) {
        const uint32_t bits = lcg_next(&seed);
        const uint8_t duration = (uint8_t)(bits % 17);
        const worr_local_action_intent_v2 intent = make_intent(
            (bits & 0x20u) != 0 ? WORR_LOCAL_ACTION_INTENT_ATTACK_HELD : 0,
            0, 0);
        worr_local_action_transaction_v2 predicted;
        worr_local_action_transaction_v2 authority;
        CHECK(build(&state, duration, intent, rule, absent,
                    WORR_LOCAL_ACTION_PRODUCER_PREDICTED, &predicted));
        CHECK(build(&state, duration, intent, rule, absent,
                    WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE, &authority));
        CHECK(predicted.transaction_hash == authority.transaction_hash);
        CHECK(memcmp(&predicted.state_after, &authority.state_after,
                     sizeof(predicted.state_after)) == 0);
        CHECK(memcmp(predicted.events, authority.events,
                     sizeof(predicted.events)) == 0);
        digest ^= predicted.transaction_hash;
        digest *= UINT64_C(1099511628211);
        state = predicted.state_after;
    }
    *digest_out = digest;
    return 0;
}

static int test_deterministic_randomized_replay(void)
{
    uint64_t first;
    uint64_t second;
    CHECK(replay_digest(UINT32_C(0x12345678), &first) == 0);
    CHECK(replay_digest(UINT32_C(0x12345678), &second) == 0);
    CHECK(first == second);
    CHECK(first != 0);
    return 0;
}

int main(void)
{
    if (test_schema_and_fail_closed_validation() != 0 ||
        test_prediction_authority_fire_parity() != 0 ||
        test_dry_fire_latch_and_exhaustion() != 0 ||
        test_switch_phases_and_zero_duration_normalization() != 0 ||
        test_identity_wrap_corruption_and_atomicity() != 0 ||
        test_event_record_adapter() != 0 ||
        test_correction_classes() != 0 ||
        test_deterministic_randomized_replay() != 0) {
        return 1;
    }
    puts("local action v2 tests passed");
    return 0;
}
