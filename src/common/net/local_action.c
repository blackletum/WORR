/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "shared/local_action_abi.h"

#include <limits.h>
#include <string.h>

#define LOCAL_ACTION_STATE_KNOWN_FLAGS                                    \
    ((uint32_t)(WORR_LOCAL_ACTION_STATE_TRIGGER_HELD |                    \
                WORR_LOCAL_ACTION_STATE_DRY_FIRE_LATCHED))
#define LOCAL_ACTION_WEAPON_KNOWN_FLAGS                                   \
    ((uint32_t)(WORR_LOCAL_ACTION_WEAPON_AUTOMATIC |                      \
                WORR_LOCAL_ACTION_WEAPON_USES_AMMO |                      \
                WORR_LOCAL_ACTION_WEAPON_PREDICT_AUDIO |                  \
                WORR_LOCAL_ACTION_WEAPON_PREDICT_EFFECT))
#define LOCAL_ACTION_INTENT_KNOWN_FLAGS                                   \
    ((uint32_t)(WORR_LOCAL_ACTION_INTENT_ATTACK_HELD |                    \
                WORR_LOCAL_ACTION_INTENT_SWITCH_REQUEST))
#define LOCAL_ACTION_DIFFERENCE_KNOWN_BITS                                \
    ((uint32_t)(WORR_LOCAL_ACTION_DIFF_COMMAND_ID |                       \
                WORR_LOCAL_ACTION_DIFF_SAMPLE_TIME |                     \
                WORR_LOCAL_ACTION_DIFF_ACTIVE_WEAPON |                   \
                WORR_LOCAL_ACTION_DIFF_PENDING_WEAPON |                  \
                WORR_LOCAL_ACTION_DIFF_PHASE |                           \
                WORR_LOCAL_ACTION_DIFF_PHASE_TIMER |                     \
                WORR_LOCAL_ACTION_DIFF_AMMO |                            \
                WORR_LOCAL_ACTION_DIFF_SHOT_SEQUENCE |                   \
                WORR_LOCAL_ACTION_DIFF_ACTION_SEQUENCE |                 \
                WORR_LOCAL_ACTION_DIFF_LATCH_FLAGS |                     \
                WORR_LOCAL_ACTION_DIFF_PRESENTATION_FRAME |              \
                WORR_LOCAL_ACTION_DIFF_EVENT_COUNT |                     \
                WORR_LOCAL_ACTION_DIFF_EVENT_KEY |                       \
                WORR_LOCAL_ACTION_DIFF_EVENT_PAYLOAD))

static const uint64_t fnv_offset_basis = UINT64_C(14695981039346656037);
static const uint64_t fnv_prime = UINT64_C(1099511628211);

static bool bytes_are_zero(const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    size_t i;
    if (!data)
        return false;
    for (i = 0; i < size; ++i) {
        if (bytes[i] != 0)
            return false;
    }
    return true;
}

static uint64_t hash_u8(uint64_t hash, uint8_t value)
{
    return (hash ^ value) * fnv_prime;
}

static uint64_t hash_u32(uint64_t hash, uint32_t value)
{
    unsigned int i;
    for (i = 0; i < 4; ++i)
        hash = hash_u8(hash, (uint8_t)(value >> (i * 8)));
    return hash;
}

static uint64_t hash_u64(uint64_t hash, uint64_t value)
{
    unsigned int i;
    for (i = 0; i < 8; ++i)
        hash = hash_u8(hash, (uint8_t)(value >> (i * 8)));
    return hash;
}

static uint64_t begin_hash(uint32_t domain)
{
    uint64_t hash = fnv_offset_basis;
    hash = hash_u32(hash, UINT32_C(0x574f5252)); /* WORR */
    return hash_u32(hash, domain);
}

static bool producer_role_valid(uint32_t role)
{
    return role == WORR_LOCAL_ACTION_PRODUCER_PREDICTED ||
           role == WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE;
}

static bool phase_valid(uint32_t phase)
{
    return phase <= WORR_LOCAL_ACTION_PHASE_LOWERING;
}

bool Worr_LocalActionStateInitV2(worr_local_action_state_v2 *state,
                                 uint32_t command_epoch,
                                 uint32_t active_weapon_id,
                                 uint32_t active_ammo_units,
                                 uint32_t presentation_frame)
{
    worr_local_action_state_v2 candidate;

    if (!state || command_epoch == 0 ||
        active_weapon_id > WORR_LOCAL_ACTION_MAX_WEAPON_ID ||
        active_ammo_units > WORR_LOCAL_ACTION_MAX_AMMO_UNITS ||
        (active_weapon_id == 0 && active_ammo_units != 0)) {
        return false;
    }

    memset(&candidate, 0, sizeof(candidate));
    candidate.struct_size = sizeof(candidate);
    candidate.schema_version = WORR_LOCAL_ACTION_ABI_VERSION;
    candidate.model_revision = WORR_LOCAL_ACTION_MODEL_REVISION;
    candidate.applied_cursor.epoch = command_epoch;
    candidate.active_weapon_id = active_weapon_id;
    candidate.active_ammo_units = active_ammo_units;
    candidate.phase = active_weapon_id != 0 ? WORR_LOCAL_ACTION_PHASE_READY
                                            : WORR_LOCAL_ACTION_PHASE_HOLSTERED;
    candidate.presentation_frame = active_weapon_id != 0
                                       ? presentation_frame
                                       : 0;
    if (!Worr_LocalActionStateValidateV2(&candidate))
        return false;
    *state = candidate;
    return true;
}

bool Worr_LocalActionStateValidateV2(
    const worr_local_action_state_v2 *state)
{
    if (!state || state->struct_size != sizeof(*state) ||
        state->schema_version != WORR_LOCAL_ACTION_ABI_VERSION ||
        state->model_revision != WORR_LOCAL_ACTION_MODEL_REVISION ||
        (state->flags & ~LOCAL_ACTION_STATE_KNOWN_FLAGS) != 0 ||
        ((state->flags & WORR_LOCAL_ACTION_STATE_DRY_FIRE_LATCHED) != 0 &&
         (state->flags & WORR_LOCAL_ACTION_STATE_TRIGGER_HELD) == 0) ||
        !Worr_CommandCursorValidV1(state->applied_cursor) ||
        state->active_weapon_id > WORR_LOCAL_ACTION_MAX_WEAPON_ID ||
        state->pending_weapon_id > WORR_LOCAL_ACTION_MAX_WEAPON_ID ||
        state->active_ammo_units > WORR_LOCAL_ACTION_MAX_AMMO_UNITS ||
        state->pending_ammo_units > WORR_LOCAL_ACTION_MAX_AMMO_UNITS ||
        !phase_valid(state->phase) || state->reserved0 != 0) {
        return false;
    }

    switch (state->phase) {
    case WORR_LOCAL_ACTION_PHASE_HOLSTERED:
        return state->active_weapon_id == 0 &&
               state->pending_weapon_id == 0 &&
               state->active_ammo_units == 0 &&
               state->pending_ammo_units == 0 &&
               state->phase_remaining_ms == 0 &&
               state->presentation_frame == 0;
    case WORR_LOCAL_ACTION_PHASE_READY:
        return state->active_weapon_id != 0 &&
               state->pending_weapon_id == 0 &&
               state->pending_ammo_units == 0 &&
               state->phase_remaining_ms == 0;
    case WORR_LOCAL_ACTION_PHASE_RAISING:
    case WORR_LOCAL_ACTION_PHASE_FIRING:
        return state->active_weapon_id != 0 &&
               state->pending_weapon_id == 0 &&
               state->pending_ammo_units == 0 &&
               state->phase_remaining_ms != 0 &&
               state->phase_remaining_ms <=
                   WORR_LOCAL_ACTION_MAX_PHASE_DURATION_MS;
    case WORR_LOCAL_ACTION_PHASE_LOWERING:
        return state->active_weapon_id != 0 &&
               (state->pending_weapon_id != 0 ||
                state->pending_ammo_units == 0) &&
               state->phase_remaining_ms != 0 &&
               state->phase_remaining_ms <=
                   WORR_LOCAL_ACTION_MAX_PHASE_DURATION_MS;
    default:
        return false;
    }
}

bool Worr_LocalActionWeaponRuleValidateV2(
    const worr_local_action_weapon_rule_v2 *rule)
{
    if (!rule || rule->struct_size != sizeof(*rule) ||
        rule->schema_version != WORR_LOCAL_ACTION_ABI_VERSION ||
        rule->model_revision != WORR_LOCAL_ACTION_MODEL_REVISION ||
        (rule->flags & ~LOCAL_ACTION_WEAPON_KNOWN_FLAGS) != 0 ||
        rule->weapon_id > WORR_LOCAL_ACTION_MAX_WEAPON_ID ||
        rule->ammo_per_shot > WORR_LOCAL_ACTION_MAX_AMMO_UNITS ||
        rule->raise_duration_ms > WORR_LOCAL_ACTION_MAX_PHASE_DURATION_MS ||
        rule->lower_duration_ms > WORR_LOCAL_ACTION_MAX_PHASE_DURATION_MS ||
        rule->refire_duration_ms > WORR_LOCAL_ACTION_MAX_PHASE_DURATION_MS ||
        rule->reserved0 != 0) {
        return false;
    }

    if (rule->weapon_id == 0) {
        return rule->flags == 0 && rule->ammo_per_shot == 0 &&
               rule->raise_duration_ms == 0 &&
               rule->lower_duration_ms == 0 &&
               rule->refire_duration_ms == 0 && rule->ready_frame == 0 &&
               rule->fire_frame == 0 && rule->fire_audio_asset_id == 0 &&
               rule->dry_audio_asset_id == 0 && rule->fire_effect_id == 0 &&
               rule->switch_effect_id == 0;
    }

    if (rule->refire_duration_ms == 0)
        return false;
    if ((rule->flags & WORR_LOCAL_ACTION_WEAPON_USES_AMMO) != 0) {
        if (rule->ammo_per_shot == 0)
            return false;
    } else if (rule->ammo_per_shot != 0) {
        return false;
    }
    if ((rule->flags & WORR_LOCAL_ACTION_WEAPON_PREDICT_AUDIO) != 0) {
        if (rule->fire_audio_asset_id == 0 ||
            rule->dry_audio_asset_id == 0)
            return false;
    } else if (rule->fire_audio_asset_id != 0 ||
               rule->dry_audio_asset_id != 0) {
        return false;
    }
    if ((rule->flags & WORR_LOCAL_ACTION_WEAPON_PREDICT_EFFECT) != 0) {
        if (rule->fire_effect_id == 0 || rule->switch_effect_id == 0)
            return false;
    } else if (rule->fire_effect_id != 0 || rule->switch_effect_id != 0) {
        return false;
    }
    return true;
}

bool Worr_LocalActionIntentValidateV2(
    const worr_local_action_intent_v2 *intent)
{
    if (!intent || intent->struct_size != sizeof(*intent) ||
        intent->schema_version != WORR_LOCAL_ACTION_ABI_VERSION ||
        (intent->flags & ~LOCAL_ACTION_INTENT_KNOWN_FLAGS) != 0 ||
        intent->requested_weapon_id > WORR_LOCAL_ACTION_MAX_WEAPON_ID ||
        intent->requested_ammo_units > WORR_LOCAL_ACTION_MAX_AMMO_UNITS ||
        intent->reserved0 != 0) {
        return false;
    }
    if ((intent->flags & WORR_LOCAL_ACTION_INTENT_SWITCH_REQUEST) == 0)
        return intent->requested_weapon_id == 0 &&
               intent->requested_ammo_units == 0;
    return intent->requested_weapon_id != 0 ||
           intent->requested_ammo_units == 0;
}

bool Worr_LocalActionEventValidateV2(
    const worr_local_action_event_v2 *event)
{
    const bool gameplay = event &&
        event->prediction_key.lane == WORR_EVENT_PREDICTION_LANE_GAMEPLAY;
    if (!event || event->prediction_key.command_epoch == 0 ||
        event->prediction_key.command_sequence == 0 ||
        event->prediction_key.emitter_ordinal == 0 ||
        (event->prediction_key.lane != WORR_EVENT_PREDICTION_LANE_GAMEPLAY &&
         event->prediction_key.lane != WORR_EVENT_PREDICTION_LANE_AUDIO &&
         event->prediction_key.lane != WORR_EVENT_PREDICTION_LANE_EFFECT) ||
        event->kind < WORR_LOCAL_ACTION_EVENT_SWITCH_BEGIN ||
        event->kind > WORR_LOCAL_ACTION_EVENT_DRY_FIRE ||
        event->weapon_id > WORR_LOCAL_ACTION_MAX_WEAPON_ID ||
        event->ammo_after > WORR_LOCAL_ACTION_MAX_AMMO_UNITS ||
        event->reserved0 != 0 || (gameplay && event->asset_id != 0) ||
        (!gameplay && event->asset_id == 0)) {
        return false;
    }
    return event->kind == WORR_LOCAL_ACTION_EVENT_SWITCH_BEGIN ||
           event->kind == WORR_LOCAL_ACTION_EVENT_SWITCH_COMMIT ||
           event->weapon_id != 0;
}

static uint64_t hash_state(const worr_local_action_state_v2 *state)
{
    uint64_t hash = begin_hash(UINT32_C(0x4c415332)); /* LAS2 */
    hash = hash_u32(hash, state->model_revision);
    hash = hash_u32(hash, state->flags);
    hash = hash_u32(hash, state->applied_cursor.epoch);
    hash = hash_u32(hash, state->applied_cursor.contiguous_sequence);
    hash = hash_u64(hash, state->sample_time_us);
    hash = hash_u32(hash, state->active_weapon_id);
    hash = hash_u32(hash, state->pending_weapon_id);
    hash = hash_u32(hash, state->active_ammo_units);
    hash = hash_u32(hash, state->pending_ammo_units);
    hash = hash_u32(hash, state->phase);
    hash = hash_u32(hash, state->phase_remaining_ms);
    hash = hash_u32(hash, state->shot_sequence);
    hash = hash_u32(hash, state->action_sequence);
    return hash_u32(hash, state->presentation_frame);
}

static uint64_t hash_rule(uint64_t hash,
                          const worr_local_action_weapon_rule_v2 *rule)
{
    hash = hash_u32(hash, rule->model_revision);
    hash = hash_u32(hash, rule->flags);
    hash = hash_u32(hash, rule->weapon_id);
    hash = hash_u32(hash, rule->ammo_per_shot);
    hash = hash_u32(hash, rule->raise_duration_ms);
    hash = hash_u32(hash, rule->lower_duration_ms);
    hash = hash_u32(hash, rule->refire_duration_ms);
    hash = hash_u32(hash, rule->ready_frame);
    hash = hash_u32(hash, rule->fire_frame);
    hash = hash_u32(hash, rule->fire_audio_asset_id);
    hash = hash_u32(hash, rule->dry_audio_asset_id);
    hash = hash_u32(hash, rule->fire_effect_id);
    return hash_u32(hash, rule->switch_effect_id);
}

static uint64_t hash_intent(uint64_t hash,
                            const worr_local_action_intent_v2 *intent)
{
    hash = hash_u32(hash, intent->flags);
    hash = hash_u32(hash, intent->requested_weapon_id);
    return hash_u32(hash, intent->requested_ammo_units);
}

static uint64_t hash_event(uint64_t hash,
                           const worr_local_action_event_v2 *event)
{
    hash = hash_u32(hash, event->prediction_key.command_epoch);
    hash = hash_u32(hash, event->prediction_key.command_sequence);
    hash = hash_u32(hash, event->prediction_key.emitter_ordinal);
    hash = hash_u32(hash, event->prediction_key.lane);
    hash = hash_u64(hash, event->source_time_us);
    hash = hash_u32(hash, event->kind);
    hash = hash_u32(hash, event->weapon_id);
    hash = hash_u32(hash, event->action_sequence);
    hash = hash_u32(hash, event->ammo_after);
    return hash_u32(hash, event->asset_id);
}

typedef struct local_action_emitter_s {
    worr_local_action_transaction_v2 *transaction;
    uint32_t ordinal;
} local_action_emitter;

static bool emit_lane(local_action_emitter *emitter,
                      uint32_t lane,
                      uint32_t kind,
                      uint32_t weapon_id,
                      uint32_t action_sequence,
                      uint32_t ammo_after,
                      uint32_t asset_id)
{
    worr_local_action_event_v2 *event;
    if (emitter->transaction->event_count >= WORR_LOCAL_ACTION_MAX_EVENTS)
        return false;
    event = &emitter->transaction->events[emitter->transaction->event_count++];
    event->prediction_key.command_epoch =
        emitter->transaction->command.command_id.epoch;
    event->prediction_key.command_sequence =
        emitter->transaction->command.command_id.sequence;
    event->prediction_key.emitter_ordinal = emitter->ordinal;
    event->prediction_key.lane = lane;
    event->source_time_us = emitter->transaction->command.sample_time_us;
    event->kind = kind;
    event->weapon_id = weapon_id;
    event->action_sequence = action_sequence;
    event->ammo_after = ammo_after;
    event->asset_id = asset_id;
    return Worr_LocalActionEventValidateV2(event);
}

static bool emit_semantic(local_action_emitter *emitter,
                          uint32_t kind,
                          uint32_t weapon_id,
                          uint32_t action_sequence,
                          uint32_t ammo_after,
                          uint32_t audio_asset_id,
                          uint32_t effect_id)
{
    if (emitter->ordinal == UINT32_MAX)
        return false;
    ++emitter->ordinal;
    if (!emit_lane(emitter, WORR_EVENT_PREDICTION_LANE_GAMEPLAY, kind,
                   weapon_id, action_sequence, ammo_after, 0)) {
        return false;
    }
    if (audio_asset_id != 0 &&
        !emit_lane(emitter, WORR_EVENT_PREDICTION_LANE_AUDIO, kind,
                   weapon_id, action_sequence, ammo_after,
                   audio_asset_id)) {
        return false;
    }
    return effect_id == 0 ||
           emit_lane(emitter, WORR_EVENT_PREDICTION_LANE_EFFECT, kind,
                     weapon_id, action_sequence, ammo_after, effect_id);
}

static const worr_local_action_weapon_rule_v2 *rule_for_weapon(
    uint32_t weapon_id,
    const worr_local_action_weapon_rule_v2 *active_rule,
    const worr_local_action_weapon_rule_v2 *pending_rule)
{
    if (active_rule->weapon_id == weapon_id)
        return active_rule;
    if (pending_rule->weapon_id == weapon_id)
        return pending_rule;
    return NULL;
}

static bool complete_phase(
    worr_local_action_state_v2 *state,
    const worr_local_action_weapon_rule_v2 *active_rule,
    const worr_local_action_weapon_rule_v2 *pending_rule,
    local_action_emitter *emitter)
{
    const worr_local_action_weapon_rule_v2 *rule;
    switch (state->phase) {
    case WORR_LOCAL_ACTION_PHASE_RAISING:
        rule = rule_for_weapon(state->active_weapon_id, active_rule,
                               pending_rule);
        if (!rule)
            return false;
        state->phase = WORR_LOCAL_ACTION_PHASE_READY;
        state->phase_remaining_ms = 0;
        state->presentation_frame = rule->ready_frame;
        return emit_semantic(emitter, WORR_LOCAL_ACTION_EVENT_WEAPON_READY,
                             state->active_weapon_id,
                             state->action_sequence,
                             state->active_ammo_units, 0, 0);
    case WORR_LOCAL_ACTION_PHASE_FIRING:
        rule = rule_for_weapon(state->active_weapon_id, active_rule,
                               pending_rule);
        if (!rule)
            return false;
        state->phase = WORR_LOCAL_ACTION_PHASE_READY;
        state->phase_remaining_ms = 0;
        state->presentation_frame = rule->ready_frame;
        return true;
    case WORR_LOCAL_ACTION_PHASE_LOWERING: {
        const uint32_t weapon_id = state->pending_weapon_id;
        const uint32_t ammo_units = state->pending_ammo_units;
        if (!emit_semantic(emitter, WORR_LOCAL_ACTION_EVENT_SWITCH_COMMIT,
                           weapon_id, state->action_sequence, ammo_units,
                           0, 0)) {
            return false;
        }
        state->active_weapon_id = weapon_id;
        state->active_ammo_units = ammo_units;
        state->pending_weapon_id = 0;
        state->pending_ammo_units = 0;
        if (weapon_id == 0) {
            state->phase = WORR_LOCAL_ACTION_PHASE_HOLSTERED;
            state->phase_remaining_ms = 0;
            state->presentation_frame = 0;
            return true;
        }
        rule = rule_for_weapon(weapon_id, active_rule, pending_rule);
        if (!rule)
            return false;
        state->phase = WORR_LOCAL_ACTION_PHASE_RAISING;
        state->phase_remaining_ms = rule->raise_duration_ms;
        state->presentation_frame = rule->ready_frame;
        return true;
    }
    default:
        return false;
    }
}

static bool advance_phase(
    worr_local_action_state_v2 *state,
    uint32_t elapsed_ms,
    const worr_local_action_weapon_rule_v2 *active_rule,
    const worr_local_action_weapon_rule_v2 *pending_rule,
    local_action_emitter *emitter)
{
    unsigned int guard;
    for (guard = 0; guard < 8; ++guard) {
        if (state->phase != WORR_LOCAL_ACTION_PHASE_RAISING &&
            state->phase != WORR_LOCAL_ACTION_PHASE_FIRING &&
            state->phase != WORR_LOCAL_ACTION_PHASE_LOWERING) {
            return true;
        }
        if (state->phase_remaining_ms != 0 &&
            elapsed_ms < state->phase_remaining_ms) {
            state->phase_remaining_ms -= elapsed_ms;
            return true;
        }
        if (state->phase_remaining_ms != 0) {
            elapsed_ms -= state->phase_remaining_ms;
            state->phase_remaining_ms = 0;
        }
        if (!complete_phase(state, active_rule, pending_rule, emitter))
            return false;
    }
    return false;
}

static bool increment_action(worr_local_action_state_v2 *state)
{
    if (state->action_sequence == UINT32_MAX)
        return false;
    ++state->action_sequence;
    return true;
}

static bool start_switch(
    worr_local_action_state_v2 *state,
    const worr_local_action_intent_v2 *intent,
    const worr_local_action_weapon_rule_v2 *active_rule,
    const worr_local_action_weapon_rule_v2 *pending_rule,
    local_action_emitter *emitter)
{
    const uint32_t requested = intent->requested_weapon_id;
    const worr_local_action_weapon_rule_v2 *effect_rule;

    if (state->phase == WORR_LOCAL_ACTION_PHASE_LOWERING) {
        return requested == state->pending_weapon_id;
    }
    if (requested == state->active_weapon_id)
        return true;
    if (!increment_action(state))
        return false;

    effect_rule = state->active_weapon_id != 0 ? active_rule : pending_rule;
    if (!emit_semantic(
            emitter, WORR_LOCAL_ACTION_EVENT_SWITCH_BEGIN, requested,
            state->action_sequence, intent->requested_ammo_units, 0,
            effect_rule->flags & WORR_LOCAL_ACTION_WEAPON_PREDICT_EFFECT
                ? effect_rule->switch_effect_id
                : 0)) {
        return false;
    }

    if (state->active_weapon_id == 0) {
        state->active_weapon_id = requested;
        state->active_ammo_units = intent->requested_ammo_units;
        if (!emit_semantic(emitter,
                           WORR_LOCAL_ACTION_EVENT_SWITCH_COMMIT, requested,
                           state->action_sequence,
                           state->active_ammo_units, 0, 0)) {
            return false;
        }
        if (requested == 0)
            return true;
        state->phase = WORR_LOCAL_ACTION_PHASE_RAISING;
        state->phase_remaining_ms = pending_rule->raise_duration_ms;
        state->presentation_frame = pending_rule->ready_frame;
    } else {
        state->pending_weapon_id = requested;
        state->pending_ammo_units = intent->requested_ammo_units;
        state->phase = WORR_LOCAL_ACTION_PHASE_LOWERING;
        state->phase_remaining_ms = active_rule->lower_duration_ms;
    }
    return advance_phase(state, 0, active_rule, pending_rule, emitter);
}

static bool process_attack(
    worr_local_action_state_v2 *state,
    const worr_local_action_intent_v2 *intent,
    const worr_local_action_weapon_rule_v2 *active_rule,
    const worr_local_action_weapon_rule_v2 *pending_rule,
    local_action_emitter *emitter)
{
    const bool held =
        (intent->flags & WORR_LOCAL_ACTION_INTENT_ATTACK_HELD) != 0;
    const bool was_held =
        (state->flags & WORR_LOCAL_ACTION_STATE_TRIGGER_HELD) != 0;
    const bool dry_latched =
        (state->flags & WORR_LOCAL_ACTION_STATE_DRY_FIRE_LATCHED) != 0;
    const worr_local_action_weapon_rule_v2 *rule;
    bool fire;

    state->flags &= ~(uint32_t)(WORR_LOCAL_ACTION_STATE_TRIGGER_HELD |
                                WORR_LOCAL_ACTION_STATE_DRY_FIRE_LATCHED);
    if (held)
        state->flags |= WORR_LOCAL_ACTION_STATE_TRIGGER_HELD;
    if (!held || state->phase != WORR_LOCAL_ACTION_PHASE_READY)
        return true;

    rule = rule_for_weapon(state->active_weapon_id, active_rule,
                           pending_rule);
    if (!rule)
        return false;
    fire = (rule->flags & WORR_LOCAL_ACTION_WEAPON_AUTOMATIC) != 0 ||
           !was_held;
    if (!fire)
        return true;

    if ((rule->flags & WORR_LOCAL_ACTION_WEAPON_USES_AMMO) != 0 &&
        state->active_ammo_units < rule->ammo_per_shot) {
        if (dry_latched) {
            state->flags |= WORR_LOCAL_ACTION_STATE_DRY_FIRE_LATCHED;
            return true;
        }
        if (!increment_action(state))
            return false;
        state->flags |= WORR_LOCAL_ACTION_STATE_DRY_FIRE_LATCHED;
        return emit_semantic(
            emitter, WORR_LOCAL_ACTION_EVENT_DRY_FIRE,
            state->active_weapon_id, state->action_sequence,
            state->active_ammo_units,
            rule->flags & WORR_LOCAL_ACTION_WEAPON_PREDICT_AUDIO
                ? rule->dry_audio_asset_id
                : 0,
            0);
    }

    if (state->shot_sequence == UINT32_MAX || !increment_action(state))
        return false;
    ++state->shot_sequence;
    if ((rule->flags & WORR_LOCAL_ACTION_WEAPON_USES_AMMO) != 0)
        state->active_ammo_units -= rule->ammo_per_shot;
    state->phase = WORR_LOCAL_ACTION_PHASE_FIRING;
    state->phase_remaining_ms = rule->refire_duration_ms;
    state->presentation_frame = rule->fire_frame;
    return emit_semantic(
        emitter, WORR_LOCAL_ACTION_EVENT_WEAPON_FIRE,
        state->active_weapon_id, state->action_sequence,
        state->active_ammo_units,
        rule->flags & WORR_LOCAL_ACTION_WEAPON_PREDICT_AUDIO
            ? rule->fire_audio_asset_id
            : 0,
        rule->flags & WORR_LOCAL_ACTION_WEAPON_PREDICT_EFFECT
            ? rule->fire_effect_id
            : 0);
}

static bool rules_match_state_and_intent(
    const worr_local_action_state_v2 *state,
    const worr_local_action_intent_v2 *intent,
    const worr_local_action_weapon_rule_v2 *active_rule,
    const worr_local_action_weapon_rule_v2 *pending_rule)
{
    uint32_t expected_pending = state->pending_weapon_id;
    if (active_rule->weapon_id != state->active_weapon_id)
        return false;
    if (state->phase == WORR_LOCAL_ACTION_PHASE_LOWERING) {
        if (pending_rule->weapon_id != state->pending_weapon_id)
            return false;
        if ((intent->flags & WORR_LOCAL_ACTION_INTENT_SWITCH_REQUEST) != 0 &&
            (intent->requested_weapon_id != state->pending_weapon_id ||
             intent->requested_ammo_units != state->pending_ammo_units)) {
            return false;
        }
        return true;
    }
    if ((intent->flags & WORR_LOCAL_ACTION_INTENT_SWITCH_REQUEST) != 0 &&
        intent->requested_weapon_id != state->active_weapon_id) {
        expected_pending = intent->requested_weapon_id;
    }
    return pending_rule->weapon_id == expected_pending;
}

static bool build_transaction_impl(
    const worr_local_action_state_v2 *state_before,
    const worr_command_record_v1 *command,
    const worr_local_action_intent_v2 *intent,
    const worr_local_action_weapon_rule_v2 *active_rule,
    const worr_local_action_weapon_rule_v2 *pending_rule,
    uint32_t producer_role,
    worr_local_action_transaction_v2 *transaction_out)
{
    worr_command_id_v1 next_id;
    uint64_t elapsed_us;
    uint64_t command_hash;
    uint64_t hash;
    uint32_t i;
    local_action_emitter emitter;

    if (!state_before || !command || !intent || !active_rule ||
        !pending_rule || !transaction_out ||
        !producer_role_valid(producer_role) ||
        !Worr_LocalActionStateValidateV2(state_before) ||
        !Worr_CommandRecordValidateV1(
            command, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS) ||
        !Worr_LocalActionIntentValidateV2(intent) ||
        !Worr_LocalActionWeaponRuleValidateV2(active_rule) ||
        !Worr_LocalActionWeaponRuleValidateV2(pending_rule) ||
        !rules_match_state_and_intent(state_before, intent, active_rule,
                                      pending_rule) ||
        !Worr_CommandCursorNextIdV1(state_before->applied_cursor, &next_id) ||
        next_id.epoch != command->command_id.epoch ||
        next_id.sequence != command->command_id.sequence) {
        return false;
    }

    elapsed_us = (uint64_t)command->command.duration_ms * UINT64_C(1000);
    if (UINT64_MAX - state_before->sample_time_us < elapsed_us ||
        state_before->sample_time_us + elapsed_us != command->sample_time_us)
        return false;

    memset(transaction_out, 0, sizeof(*transaction_out));
    transaction_out->struct_size = sizeof(*transaction_out);
    transaction_out->schema_version = WORR_LOCAL_ACTION_ABI_VERSION;
    transaction_out->model_revision = WORR_LOCAL_ACTION_MODEL_REVISION;
    transaction_out->producer_role = producer_role;
    transaction_out->state_before = *state_before;
    transaction_out->command = *command;
    transaction_out->intent = *intent;
    transaction_out->active_rule = *active_rule;
    transaction_out->pending_rule = *pending_rule;
    transaction_out->state_after = *state_before;
    emitter.transaction = transaction_out;
    emitter.ordinal = 0;

    if (!advance_phase(&transaction_out->state_after,
                       command->command.duration_ms, active_rule,
                       pending_rule, &emitter)) {
        return false;
    }
    if ((intent->flags & WORR_LOCAL_ACTION_INTENT_SWITCH_REQUEST) != 0 &&
        !start_switch(&transaction_out->state_after, intent, active_rule,
                      pending_rule, &emitter)) {
        return false;
    }
    if (!process_attack(&transaction_out->state_after, intent, active_rule,
                        pending_rule, &emitter)) {
        return false;
    }

    transaction_out->state_after.applied_cursor.epoch =
        command->command_id.epoch;
    transaction_out->state_after.applied_cursor.contiguous_sequence =
        command->command_id.sequence;
    transaction_out->state_after.sample_time_us = command->sample_time_us;
    if (!Worr_LocalActionStateValidateV2(&transaction_out->state_after))
        return false;

    transaction_out->state_hash = hash_state(&transaction_out->state_after);
    hash = begin_hash(UINT32_C(0x4c414532)); /* LAE2 */
    hash = hash_u32(hash, transaction_out->event_count);
    for (i = 0; i < transaction_out->event_count; ++i) {
        if (!Worr_LocalActionEventValidateV2(&transaction_out->events[i]))
            return false;
        hash = hash_event(hash, &transaction_out->events[i]);
    }
    transaction_out->event_hash = hash;
    if (!Worr_CommandRecordSemanticHashV1(
            command, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS,
            &command_hash)) {
        return false;
    }
    hash = begin_hash(UINT32_C(0x4c415432)); /* LAT2 */
    hash = hash_u32(hash, WORR_LOCAL_ACTION_MODEL_REVISION);
    hash = hash_u64(hash, hash_state(state_before));
    hash = hash_u64(hash, command_hash);
    hash = hash_intent(hash, intent);
    hash = hash_rule(hash, active_rule);
    hash = hash_rule(hash, pending_rule);
    hash = hash_u64(hash, transaction_out->state_hash);
    hash = hash_u64(hash, transaction_out->event_hash);
    transaction_out->transaction_hash = hash;
    return true;
}

bool Worr_LocalActionBuildTransactionV2(
    const worr_local_action_state_v2 *state_before,
    const worr_command_record_v1 *command,
    const worr_local_action_intent_v2 *intent,
    const worr_local_action_weapon_rule_v2 *active_rule,
    const worr_local_action_weapon_rule_v2 *pending_rule,
    uint32_t producer_role,
    worr_local_action_transaction_v2 *transaction_out)
{
    worr_local_action_transaction_v2 candidate;
    if (!transaction_out ||
        !bytes_are_zero(transaction_out, sizeof(*transaction_out))) {
        return false;
    }
    if (!build_transaction_impl(state_before, command, intent, active_rule,
                                pending_rule, producer_role, &candidate)) {
        return false;
    }
    *transaction_out = candidate;
    return true;
}

bool Worr_LocalActionTransactionValidateV2(
    const worr_local_action_transaction_v2 *transaction)
{
    worr_local_action_transaction_v2 expected;
    if (!transaction || transaction->struct_size != sizeof(*transaction) ||
        transaction->schema_version != WORR_LOCAL_ACTION_ABI_VERSION ||
        transaction->model_revision != WORR_LOCAL_ACTION_MODEL_REVISION ||
        !producer_role_valid(transaction->producer_role) ||
        transaction->reserved0 != 0 ||
        transaction->event_count > WORR_LOCAL_ACTION_MAX_EVENTS) {
        return false;
    }
    if (!build_transaction_impl(
            &transaction->state_before, &transaction->command,
            &transaction->intent, &transaction->active_rule,
            &transaction->pending_rule, transaction->producer_role,
            &expected)) {
        return false;
    }
    return memcmp(transaction, &expected, sizeof(expected)) == 0;
}

bool Worr_LocalActionBuildEventRecordV2(
    const worr_local_action_event_v2 *event,
    const worr_local_action_event_record_context_v2 *context,
    uint32_t max_entities,
    worr_event_record_v1 *record_out)
{
    worr_event_record_v1 candidate;
    if (!record_out || !bytes_are_zero(record_out, sizeof(*record_out)) ||
        !Worr_LocalActionEventValidateV2(event) || !context ||
        context->struct_size != sizeof(*context) ||
        context->schema_version != WORR_LOCAL_ACTION_ABI_VERSION ||
        context->model_revision != WORR_LOCAL_ACTION_MODEL_REVISION ||
        !producer_role_valid(context->producer_role) ||
        context->lifetime_ticks == 0 ||
        context->lifetime_ticks > INT32_MAX || max_entities == 0 ||
        !Worr_EventEntityRefValidV1(context->source_entity, max_entities,
                                    false) ||
        !Worr_EventEntityRefValidV1(context->subject_entity, max_entities,
                                    true)) {
        return false;
    }
    if (context->producer_role == WORR_LOCAL_ACTION_PRODUCER_PREDICTED) {
        if (context->event_id.stream_epoch != 0 ||
            context->event_id.sequence != 0)
            return false;
    } else if (context->event_id.stream_epoch == 0 ||
               context->event_id.sequence == 0) {
        return false;
    }

    memset(&candidate, 0, sizeof(candidate));
    candidate.struct_size = sizeof(candidate);
    candidate.schema_version = WORR_EVENT_ABI_VERSION;
    candidate.model_revision = WORR_EVENT_MODEL_REVISION;
    candidate.flags = WORR_EVENT_FLAG_REPLAY_SAFE |
                      WORR_EVENT_FLAG_PRESENT_ONCE;
    if (context->producer_role ==
        WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE) {
        candidate.flags |= WORR_EVENT_FLAG_HAS_AUTHORITY_ID;
        candidate.event_id = context->event_id;
    }
    candidate.source_tick = context->source_tick;
    candidate.source_ordinal = event->prediction_key.emitter_ordinal;
    candidate.source_time_us = event->source_time_us;
    candidate.source_entity = context->source_entity;
    candidate.subject_entity = context->subject_entity;
    candidate.delivery_class = WORR_EVENT_DELIVERY_TRANSIENT;
    candidate.prediction_class = WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE;
    candidate.prediction_key = event->prediction_key;
    candidate.expiry_tick = context->source_tick + context->lifetime_ticks;

    switch (event->prediction_key.lane) {
    case WORR_EVENT_PREDICTION_LANE_GAMEPLAY: {
        worr_event_payload_u32x4_v1 payload;
        candidate.event_type = WORR_EVENT_TYPE_GAMEPLAY_CUE;
        candidate.payload_kind = WORR_EVENT_PAYLOAD_U32X4;
        candidate.payload_size = sizeof(payload);
        payload.value[0] = event->kind;
        payload.value[1] = event->weapon_id;
        payload.value[2] = event->action_sequence;
        payload.value[3] = event->ammo_after;
        memcpy(candidate.payload, &payload, sizeof(payload));
        break;
    }
    case WORR_EVENT_PREDICTION_LANE_AUDIO: {
        worr_event_payload_audio_v1 payload;
        memset(&payload, 0, sizeof(payload));
        candidate.event_type = WORR_EVENT_TYPE_AUDIO_CUE;
        candidate.payload_kind = WORR_EVENT_PAYLOAD_AUDIO;
        candidate.payload_size = sizeof(payload);
        payload.asset_id = event->asset_id;
        payload.volume = 1.0f;
        payload.attenuation = 1.0f;
        payload.pitch = 1.0f;
        memcpy(candidate.payload, &payload, sizeof(payload));
        break;
    }
    case WORR_EVENT_PREDICTION_LANE_EFFECT: {
        worr_event_payload_effect_v1 payload;
        memset(&payload, 0, sizeof(payload));
        candidate.event_type = WORR_EVENT_TYPE_VISUAL_EFFECT;
        candidate.payload_kind = WORR_EVENT_PAYLOAD_EFFECT;
        candidate.payload_size = sizeof(payload);
        payload.effect_id = event->asset_id;
        payload.variant = event->kind;
        memcpy(candidate.payload, &payload, sizeof(payload));
        break;
    }
    default:
        return false;
    }

    if (context->producer_role == WORR_LOCAL_ACTION_PRODUCER_PREDICTED) {
        if (!Worr_EventRecordCandidateValidateV1(&candidate, max_entities))
            return false;
    } else if (!Worr_EventRecordValidateV1(&candidate, max_entities)) {
        return false;
    }
    *record_out = candidate;
    return true;
}

static bool key_equal(const worr_local_action_event_v2 *left,
                      const worr_local_action_event_v2 *right)
{
    return left->prediction_key.command_epoch ==
               right->prediction_key.command_epoch &&
           left->prediction_key.command_sequence ==
               right->prediction_key.command_sequence &&
           left->prediction_key.emitter_ordinal ==
               right->prediction_key.emitter_ordinal &&
           left->prediction_key.lane == right->prediction_key.lane;
}

static bool event_payload_equal(const worr_local_action_event_v2 *left,
                                const worr_local_action_event_v2 *right)
{
    return left->source_time_us == right->source_time_us &&
           left->kind == right->kind &&
           left->weapon_id == right->weapon_id &&
           left->action_sequence == right->action_sequence &&
           left->ammo_after == right->ammo_after &&
           left->asset_id == right->asset_id;
}

static int32_t delta_u32(uint32_t authoritative, uint32_t predicted)
{
    const int64_t difference =
        (int64_t)authoritative - (int64_t)predicted;
    if (difference > INT32_MAX)
        return INT32_MAX;
    if (difference < INT32_MIN)
        return INT32_MIN;
    return (int32_t)difference;
}

bool Worr_LocalActionCorrectionClassifyV2(
    const worr_local_action_transaction_v2 *predicted,
    const worr_local_action_transaction_v2 *authoritative,
    worr_local_action_correction_v2 *correction_out)
{
    worr_local_action_correction_v2 candidate;
    const worr_local_action_state_v2 *p;
    const worr_local_action_state_v2 *a;
    uint32_t compared;
    uint32_t i;
    uint32_t gameplay_bits;
    uint32_t hard_bits;

    if (!correction_out ||
        !bytes_are_zero(correction_out, sizeof(*correction_out)) ||
        !Worr_LocalActionTransactionValidateV2(predicted) ||
        !Worr_LocalActionTransactionValidateV2(authoritative) ||
        predicted->producer_role != WORR_LOCAL_ACTION_PRODUCER_PREDICTED ||
        authoritative->producer_role !=
            WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE) {
        return false;
    }

    memset(&candidate, 0, sizeof(candidate));
    candidate.struct_size = sizeof(candidate);
    candidate.schema_version = WORR_LOCAL_ACTION_ABI_VERSION;
    candidate.model_revision = WORR_LOCAL_ACTION_MODEL_REVISION;
    candidate.predicted_cursor = predicted->state_after.applied_cursor;
    candidate.authoritative_cursor =
        authoritative->state_after.applied_cursor;
    candidate.predicted_transaction_hash = predicted->transaction_hash;
    candidate.authoritative_transaction_hash =
        authoritative->transaction_hash;
    p = &predicted->state_after;
    a = &authoritative->state_after;

    if (predicted->command.command_id.epoch !=
            authoritative->command.command_id.epoch ||
        predicted->command.command_id.sequence !=
            authoritative->command.command_id.sequence) {
        candidate.difference_bits |= WORR_LOCAL_ACTION_DIFF_COMMAND_ID;
    }
    if (predicted->command.sample_time_us !=
        authoritative->command.sample_time_us) {
        candidate.difference_bits |= WORR_LOCAL_ACTION_DIFF_SAMPLE_TIME;
    }
    if (p->active_weapon_id != a->active_weapon_id)
        candidate.difference_bits |= WORR_LOCAL_ACTION_DIFF_ACTIVE_WEAPON;
    if (p->pending_weapon_id != a->pending_weapon_id)
        candidate.difference_bits |= WORR_LOCAL_ACTION_DIFF_PENDING_WEAPON;
    if (p->phase != a->phase)
        candidate.difference_bits |= WORR_LOCAL_ACTION_DIFF_PHASE;
    if (p->phase_remaining_ms != a->phase_remaining_ms)
        candidate.difference_bits |= WORR_LOCAL_ACTION_DIFF_PHASE_TIMER;
    if (p->active_ammo_units != a->active_ammo_units ||
        p->pending_ammo_units != a->pending_ammo_units)
        candidate.difference_bits |= WORR_LOCAL_ACTION_DIFF_AMMO;
    if (p->shot_sequence != a->shot_sequence)
        candidate.difference_bits |= WORR_LOCAL_ACTION_DIFF_SHOT_SEQUENCE;
    if (p->action_sequence != a->action_sequence)
        candidate.difference_bits |= WORR_LOCAL_ACTION_DIFF_ACTION_SEQUENCE;
    if (p->flags != a->flags)
        candidate.difference_bits |= WORR_LOCAL_ACTION_DIFF_LATCH_FLAGS;
    if (p->presentation_frame != a->presentation_frame)
        candidate.difference_bits |=
            WORR_LOCAL_ACTION_DIFF_PRESENTATION_FRAME;
    if (predicted->event_count != authoritative->event_count)
        candidate.difference_bits |= WORR_LOCAL_ACTION_DIFF_EVENT_COUNT;
    compared = predicted->event_count < authoritative->event_count
                   ? predicted->event_count
                   : authoritative->event_count;
    for (i = 0; i < compared; ++i) {
        if (!key_equal(&predicted->events[i], &authoritative->events[i]))
            candidate.difference_bits |= WORR_LOCAL_ACTION_DIFF_EVENT_KEY;
        if (!event_payload_equal(&predicted->events[i],
                                 &authoritative->events[i])) {
            candidate.difference_bits |= WORR_LOCAL_ACTION_DIFF_EVENT_PAYLOAD;
        }
    }

    candidate.ammo_delta =
        delta_u32(a->active_ammo_units, p->active_ammo_units);
    candidate.phase_timer_delta_ms =
        delta_u32(a->phase_remaining_ms, p->phase_remaining_ms);
    hard_bits = WORR_LOCAL_ACTION_DIFF_COMMAND_ID |
                WORR_LOCAL_ACTION_DIFF_SAMPLE_TIME |
                WORR_LOCAL_ACTION_DIFF_EVENT_KEY;
    gameplay_bits = LOCAL_ACTION_DIFFERENCE_KNOWN_BITS &
                    ~(hard_bits |
                      WORR_LOCAL_ACTION_DIFF_PRESENTATION_FRAME);
    if ((candidate.difference_bits & hard_bits) != 0) {
        candidate.classification = WORR_LOCAL_ACTION_CORRECTION_HARD_RESYNC;
        candidate.flags = WORR_LOCAL_ACTION_CORRECTION_AUTHORITY_IMMEDIATE |
                          WORR_LOCAL_ACTION_CORRECTION_HARD_RESYNC_REQUIRED;
    } else if ((candidate.difference_bits & gameplay_bits) != 0) {
        candidate.classification =
            WORR_LOCAL_ACTION_CORRECTION_GAMEPLAY_IMMEDIATE;
        candidate.flags = WORR_LOCAL_ACTION_CORRECTION_AUTHORITY_IMMEDIATE;
    } else if (candidate.difference_bits != 0) {
        candidate.classification =
            WORR_LOCAL_ACTION_CORRECTION_PRESENTATION_ONLY;
        candidate.flags =
            WORR_LOCAL_ACTION_CORRECTION_AUTHORITY_IMMEDIATE |
            WORR_LOCAL_ACTION_CORRECTION_PRESENTATION_SMOOTHING_ALLOWED;
    }
    if (!Worr_LocalActionCorrectionValidateV2(&candidate))
        return false;
    *correction_out = candidate;
    return true;
}

bool Worr_LocalActionCorrectionValidateV2(
    const worr_local_action_correction_v2 *correction)
{
    if (!correction || correction->struct_size != sizeof(*correction) ||
        correction->schema_version != WORR_LOCAL_ACTION_ABI_VERSION ||
        correction->model_revision != WORR_LOCAL_ACTION_MODEL_REVISION ||
        correction->classification >
            WORR_LOCAL_ACTION_CORRECTION_HARD_RESYNC ||
        (correction->difference_bits &
         ~LOCAL_ACTION_DIFFERENCE_KNOWN_BITS) != 0 ||
        !Worr_CommandCursorValidV1(correction->predicted_cursor) ||
        !Worr_CommandCursorValidV1(correction->authoritative_cursor) ||
        correction->predicted_transaction_hash == 0 ||
        correction->authoritative_transaction_hash == 0) {
        return false;
    }
    switch (correction->classification) {
    case WORR_LOCAL_ACTION_CORRECTION_NONE:
        return correction->difference_bits == 0 && correction->flags == 0 &&
               correction->ammo_delta == 0 &&
               correction->phase_timer_delta_ms == 0;
    case WORR_LOCAL_ACTION_CORRECTION_PRESENTATION_ONLY:
        return correction->difference_bits ==
                   WORR_LOCAL_ACTION_DIFF_PRESENTATION_FRAME &&
               correction->flags ==
                   (WORR_LOCAL_ACTION_CORRECTION_AUTHORITY_IMMEDIATE |
                    WORR_LOCAL_ACTION_CORRECTION_PRESENTATION_SMOOTHING_ALLOWED);
    case WORR_LOCAL_ACTION_CORRECTION_GAMEPLAY_IMMEDIATE:
        return correction->difference_bits != 0 &&
               (correction->difference_bits &
                (WORR_LOCAL_ACTION_DIFF_COMMAND_ID |
                 WORR_LOCAL_ACTION_DIFF_SAMPLE_TIME |
                 WORR_LOCAL_ACTION_DIFF_EVENT_KEY)) == 0 &&
               correction->difference_bits !=
                   WORR_LOCAL_ACTION_DIFF_PRESENTATION_FRAME &&
               correction->flags ==
                   WORR_LOCAL_ACTION_CORRECTION_AUTHORITY_IMMEDIATE;
    case WORR_LOCAL_ACTION_CORRECTION_HARD_RESYNC:
        return (correction->difference_bits &
                (WORR_LOCAL_ACTION_DIFF_COMMAND_ID |
                 WORR_LOCAL_ACTION_DIFF_SAMPLE_TIME |
                 WORR_LOCAL_ACTION_DIFF_EVENT_KEY)) != 0 &&
               correction->flags ==
                   (WORR_LOCAL_ACTION_CORRECTION_AUTHORITY_IMMEDIATE |
                    WORR_LOCAL_ACTION_CORRECTION_HARD_RESYNC_REQUIRED);
    default:
        return false;
    }
}
