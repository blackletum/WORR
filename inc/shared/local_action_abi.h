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
#include "shared/event_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WORR_LOCAL_ACTION_ABI_VERSION 2u
#define WORR_LOCAL_ACTION_MODEL_REVISION 1u
#define WORR_LOCAL_ACTION_MAX_EVENTS 16u
#define WORR_LOCAL_ACTION_MAX_WEAPON_ID 65535u
#define WORR_LOCAL_ACTION_MAX_AMMO_UNITS 1000000u
#define WORR_LOCAL_ACTION_MAX_PHASE_DURATION_MS 60000u

typedef enum worr_local_action_phase_v2_e {
    WORR_LOCAL_ACTION_PHASE_HOLSTERED = 0,
    WORR_LOCAL_ACTION_PHASE_RAISING = 1,
    WORR_LOCAL_ACTION_PHASE_READY = 2,
    WORR_LOCAL_ACTION_PHASE_FIRING = 3,
    WORR_LOCAL_ACTION_PHASE_LOWERING = 4,
} worr_local_action_phase_v2;

enum {
    WORR_LOCAL_ACTION_STATE_TRIGGER_HELD = 1u << 0,
    WORR_LOCAL_ACTION_STATE_DRY_FIRE_LATCHED = 1u << 1,
};

/*
 * Pointer-free state owned identically by the predictor and authority.
 * The cursor/time pair names the exact command boundary represented here.
 */
typedef struct worr_local_action_state_v2_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t model_revision;
    uint32_t flags;
    worr_command_cursor_v1 applied_cursor;
    uint64_t sample_time_us;
    uint32_t active_weapon_id;
    uint32_t pending_weapon_id;
    uint32_t active_ammo_units;
    uint32_t pending_ammo_units;
    uint32_t phase;
    uint32_t phase_remaining_ms;
    uint32_t shot_sequence;
    uint32_t action_sequence;
    uint32_t presentation_frame;
    uint32_t reserved0;
} worr_local_action_state_v2;

enum {
    WORR_LOCAL_ACTION_WEAPON_AUTOMATIC = 1u << 0,
    WORR_LOCAL_ACTION_WEAPON_USES_AMMO = 1u << 1,
    WORR_LOCAL_ACTION_WEAPON_PREDICT_AUDIO = 1u << 2,
    WORR_LOCAL_ACTION_WEAPON_PREDICT_EFFECT = 1u << 3,
};

/* A valid rule with weapon_id zero is the canonical absent rule. */
typedef struct worr_local_action_weapon_rule_v2_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t model_revision;
    uint32_t flags;
    uint32_t weapon_id;
    uint32_t ammo_per_shot;
    uint32_t raise_duration_ms;
    uint32_t lower_duration_ms;
    uint32_t refire_duration_ms;
    uint32_t ready_frame;
    uint32_t fire_frame;
    uint32_t fire_audio_asset_id;
    uint32_t dry_audio_asset_id;
    uint32_t fire_effect_id;
    uint32_t switch_effect_id;
    uint32_t reserved0;
} worr_local_action_weapon_rule_v2;

enum {
    WORR_LOCAL_ACTION_INTENT_ATTACK_HELD = 1u << 0,
    WORR_LOCAL_ACTION_INTENT_SWITCH_REQUEST = 1u << 1,
};

/* Semantic input adapter output; this ABI deliberately has no legacy button. */
typedef struct worr_local_action_intent_v2_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t flags;
    uint32_t requested_weapon_id;
    uint32_t requested_ammo_units;
    uint32_t reserved0;
} worr_local_action_intent_v2;

typedef enum worr_local_action_event_kind_v2_e {
    WORR_LOCAL_ACTION_EVENT_SWITCH_BEGIN = 1,
    WORR_LOCAL_ACTION_EVENT_SWITCH_COMMIT = 2,
    WORR_LOCAL_ACTION_EVENT_WEAPON_READY = 3,
    WORR_LOCAL_ACTION_EVENT_WEAPON_FIRE = 4,
    WORR_LOCAL_ACTION_EVENT_DRY_FIRE = 5,
} worr_local_action_event_kind_v2;

/*
 * Each semantic emitter always has a gameplay lane and may have matching
 * audio/effect lanes.  The key is derived directly from worr_command_id_v1.
 */
typedef struct worr_local_action_event_v2_s {
    worr_event_prediction_key_v1 prediction_key;
    uint64_t source_time_us;
    uint32_t kind;
    uint32_t weapon_id;
    uint32_t action_sequence;
    uint32_t ammo_after;
    uint32_t asset_id;
    uint32_t reserved0;
} worr_local_action_event_v2;

typedef enum worr_local_action_producer_role_v2_e {
    WORR_LOCAL_ACTION_PRODUCER_PREDICTED = 1,
    WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE = 2,
} worr_local_action_producer_role_v2;

/*
 * Immutable command transaction.  Producer role is provenance only and is
 * intentionally excluded from all semantic hashes.
 */
typedef struct worr_local_action_transaction_v2_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t model_revision;
    uint32_t producer_role;
    worr_local_action_state_v2 state_before;
    worr_command_record_v1 command;
    worr_local_action_intent_v2 intent;
    worr_local_action_weapon_rule_v2 active_rule;
    worr_local_action_weapon_rule_v2 pending_rule;
    worr_local_action_state_v2 state_after;
    uint32_t event_count;
    uint32_t reserved0;
    worr_local_action_event_v2 events[WORR_LOCAL_ACTION_MAX_EVENTS];
    uint64_t state_hash;
    uint64_t event_hash;
    uint64_t transaction_hash;
} worr_local_action_transaction_v2;

/* Adapter context for entering the existing canonical event journal. */
typedef struct worr_local_action_event_record_context_v2_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t model_revision;
    uint32_t producer_role;
    worr_event_id_v1 event_id;
    uint32_t source_tick;
    uint32_t lifetime_ticks;
    worr_event_entity_ref_v1 source_entity;
    worr_event_entity_ref_v1 subject_entity;
} worr_local_action_event_record_context_v2;

typedef enum worr_local_action_correction_class_v2_e {
    WORR_LOCAL_ACTION_CORRECTION_NONE = 0,
    WORR_LOCAL_ACTION_CORRECTION_PRESENTATION_ONLY = 1,
    WORR_LOCAL_ACTION_CORRECTION_GAMEPLAY_IMMEDIATE = 2,
    WORR_LOCAL_ACTION_CORRECTION_HARD_RESYNC = 3,
} worr_local_action_correction_class_v2;

enum {
    WORR_LOCAL_ACTION_DIFF_COMMAND_ID = 1u << 0,
    WORR_LOCAL_ACTION_DIFF_SAMPLE_TIME = 1u << 1,
    WORR_LOCAL_ACTION_DIFF_ACTIVE_WEAPON = 1u << 2,
    WORR_LOCAL_ACTION_DIFF_PENDING_WEAPON = 1u << 3,
    WORR_LOCAL_ACTION_DIFF_PHASE = 1u << 4,
    WORR_LOCAL_ACTION_DIFF_PHASE_TIMER = 1u << 5,
    WORR_LOCAL_ACTION_DIFF_AMMO = 1u << 6,
    WORR_LOCAL_ACTION_DIFF_SHOT_SEQUENCE = 1u << 7,
    WORR_LOCAL_ACTION_DIFF_ACTION_SEQUENCE = 1u << 8,
    WORR_LOCAL_ACTION_DIFF_LATCH_FLAGS = 1u << 9,
    WORR_LOCAL_ACTION_DIFF_PRESENTATION_FRAME = 1u << 10,
    WORR_LOCAL_ACTION_DIFF_EVENT_COUNT = 1u << 11,
    WORR_LOCAL_ACTION_DIFF_EVENT_KEY = 1u << 12,
    WORR_LOCAL_ACTION_DIFF_EVENT_PAYLOAD = 1u << 13,
};

enum {
    WORR_LOCAL_ACTION_CORRECTION_AUTHORITY_IMMEDIATE = 1u << 0,
    WORR_LOCAL_ACTION_CORRECTION_HARD_RESYNC_REQUIRED = 1u << 1,
    WORR_LOCAL_ACTION_CORRECTION_PRESENTATION_SMOOTHING_ALLOWED = 1u << 2,
};

typedef struct worr_local_action_correction_v2_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t model_revision;
    uint32_t classification;
    uint32_t difference_bits;
    uint32_t flags;
    worr_command_cursor_v1 predicted_cursor;
    worr_command_cursor_v1 authoritative_cursor;
    int32_t ammo_delta;
    int32_t phase_timer_delta_ms;
    uint64_t predicted_transaction_hash;
    uint64_t authoritative_transaction_hash;
} worr_local_action_correction_v2;

bool Worr_LocalActionStateInitV2(worr_local_action_state_v2 *state,
                                 uint32_t command_epoch,
                                 uint32_t active_weapon_id,
                                 uint32_t active_ammo_units,
                                 uint32_t presentation_frame);
bool Worr_LocalActionStateValidateV2(
    const worr_local_action_state_v2 *state);
bool Worr_LocalActionWeaponRuleValidateV2(
    const worr_local_action_weapon_rule_v2 *rule);
bool Worr_LocalActionIntentValidateV2(
    const worr_local_action_intent_v2 *intent);
bool Worr_LocalActionEventValidateV2(
    const worr_local_action_event_v2 *event);

/* output must be entirely zero; every failure leaves it byte-identical. */
bool Worr_LocalActionBuildTransactionV2(
    const worr_local_action_state_v2 *state_before,
    const worr_command_record_v1 *command,
    const worr_local_action_intent_v2 *intent,
    const worr_local_action_weapon_rule_v2 *active_rule,
    const worr_local_action_weapon_rule_v2 *pending_rule,
    uint32_t producer_role,
    worr_local_action_transaction_v2 *transaction_out);
bool Worr_LocalActionTransactionValidateV2(
    const worr_local_action_transaction_v2 *transaction);

/* output must be entirely zero; every failure leaves it byte-identical. */
bool Worr_LocalActionBuildEventRecordV2(
    const worr_local_action_event_v2 *event,
    const worr_local_action_event_record_context_v2 *context,
    uint32_t max_entities,
    worr_event_record_v1 *record_out);

/* output must be entirely zero; every failure leaves it byte-identical. */
bool Worr_LocalActionCorrectionClassifyV2(
    const worr_local_action_transaction_v2 *predicted,
    const worr_local_action_transaction_v2 *authoritative,
    worr_local_action_correction_v2 *correction_out);
bool Worr_LocalActionCorrectionValidateV2(
    const worr_local_action_correction_v2 *correction);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_LOCAL_ACTION_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define WORR_LOCAL_ACTION_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

WORR_LOCAL_ACTION_STATIC_ASSERT(sizeof(worr_local_action_state_v2) == 72,
                                "local action state v2 layout changed");
WORR_LOCAL_ACTION_STATIC_ASSERT(
    offsetof(worr_local_action_state_v2, sample_time_us) == 24,
    "local action state sample-time offset changed");
WORR_LOCAL_ACTION_STATIC_ASSERT(
    sizeof(worr_local_action_weapon_rule_v2) == 64,
    "local action weapon rule v2 layout changed");
WORR_LOCAL_ACTION_STATIC_ASSERT(sizeof(worr_local_action_intent_v2) == 24,
                                "local action intent v2 layout changed");
WORR_LOCAL_ACTION_STATIC_ASSERT(sizeof(worr_local_action_event_v2) == 48,
                                "local action event v2 layout changed");
WORR_LOCAL_ACTION_STATIC_ASSERT(
    offsetof(worr_local_action_event_v2, source_time_us) == 16,
    "local action event source-time offset changed");
WORR_LOCAL_ACTION_STATIC_ASSERT(
    sizeof(worr_local_action_event_record_context_v2) == 48,
    "local action event context v2 layout changed");
WORR_LOCAL_ACTION_STATIC_ASSERT(
    sizeof(worr_local_action_transaction_v2) == 1216,
    "local action transaction v2 layout changed");
WORR_LOCAL_ACTION_STATIC_ASSERT(
    offsetof(worr_local_action_transaction_v2, events) == 424,
    "local action transaction event offset changed");
WORR_LOCAL_ACTION_STATIC_ASSERT(
    offsetof(worr_local_action_transaction_v2, transaction_hash) == 1208,
    "local action transaction hash offset changed");
WORR_LOCAL_ACTION_STATIC_ASSERT(
    sizeof(worr_local_action_correction_v2) == 64,
    "local action correction v2 layout changed");

#undef WORR_LOCAL_ACTION_STATIC_ASSERT
