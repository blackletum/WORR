/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "local_action_observation.hpp"

#include "local_action_shadow.hpp"
#include "local_action_weapon_catalog.hpp"

#include "../g_local.hpp"
#include "shared/command_context.h"
#include "shared/local_action_observation.h"
#include "shared/local_interaction_abi.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace {

constexpr uint32_t kHistoryCapacity = 32;
static_assert(WORR_LOCAL_ACTION_COMMAND_LEASE_MAX_CLIENTS ==
              WORR_LOCAL_ACTION_OBSERVATION_MAX_CLIENTS);

const worr_command_context_import_v1 *command_context_import;
const worr_local_interaction_authority_import_v1 *authority_import;
const worr_local_action_shadow_authority_import_v1 *shadow_authority_import;
cvar_t *shadow_authority_enabled;
std::array<
    std::array<worr_local_action_observation_record_v1, kHistoryCapacity>,
    WORR_LOCAL_ACTION_OBSERVATION_MAX_CLIENTS>
    records;
std::array<uint32_t, WORR_LOCAL_ACTION_OBSERVATION_MAX_CLIENTS> write_heads;
std::array<
    std::array<worr_local_action_observation_record_v1, kHistoryCapacity>,
    WORR_LOCAL_ACTION_OBSERVATION_MAX_CLIENTS>
    leased_records;
std::array<uint32_t, WORR_LOCAL_ACTION_OBSERVATION_MAX_CLIENTS>
    leased_write_heads;
std::array<worr_local_action_command_lease_v1,
           WORR_LOCAL_ACTION_OBSERVATION_MAX_CLIENTS>
    command_leases;
std::array<worr_local_interaction_state_v1,
           WORR_LOCAL_ACTION_OBSERVATION_MAX_CLIENTS>
    interaction_states;
std::array<std::array<worr_local_interaction_transaction_v1, kHistoryCapacity>,
           WORR_LOCAL_ACTION_OBSERVATION_MAX_CLIENTS>
    interaction_records;
std::array<uint32_t, WORR_LOCAL_ACTION_OBSERVATION_MAX_CLIENTS>
    interaction_write_heads;

struct telemetry_t {
  uint64_t scoped_attempts = 0;
  uint64_t scoped_records = 0;
  uint64_t leased_records = 0;
  uint64_t rejected_scope = 0;
  uint64_t rejected_state = 0;
  uint64_t scoped_weapon_thinks = 0;
  uint64_t leased_weapon_thinks = 0;
  uint64_t unscoped_weapon_thinks = 0;
  uint64_t interaction_records = 0;
  uint64_t interaction_rebases = 0;
  uint64_t interaction_discontinuities = 0;
  uint64_t interaction_rejected = 0;
  bool catalog_ready = false;
  bool lease_ready = false;
  uint64_t unmapped_scoped_records = 0;
  uint64_t grapple_scoped_records = 0;
  uint64_t unmapped_weapon_thinks = 0;
  uint64_t grapple_weapon_thinks = 0;
  uint64_t lease_offers = 0;
  uint64_t lease_supersedes = 0;
  uint64_t lease_duplicates = 0;
  uint64_t lease_rebases = 0;
  uint64_t lease_claims = 0;
  uint64_t lease_expired = 0;
  uint64_t lease_rejected = 0;
  std::array<uint64_t, WORR_LOCAL_ACTION_CATALOG_COUNT + 1>
      scoped_records_by_catalog{};
  std::array<uint64_t, WORR_LOCAL_ACTION_CATALOG_COUNT + 1>
      leased_records_by_catalog{};
  std::array<uint64_t, WORR_LOCAL_ACTION_CATALOG_COUNT + 1>
      scoped_weapon_thinks_by_catalog{};
  std::array<uint64_t, WORR_LOCAL_ACTION_CATALOG_COUNT + 1>
      leased_weapon_thinks_by_catalog{};
  std::array<uint64_t, WORR_LOCAL_ACTION_CATALOG_COUNT + 1>
      unscoped_weapon_thinks_by_catalog{};
};

telemetry_t telemetry;

void saturating_increment(uint64_t &value) {
  if (value != std::numeric_limits<uint64_t>::max())
    ++value;
}

void observe_catalog_item(
    uint32_t raw_item_id,
    std::array<uint64_t, WORR_LOCAL_ACTION_CATALOG_COUNT + 1> &counts,
    uint64_t &unmapped_count, uint64_t &grapple_count) {
  if (raw_item_id == 0)
    return;
  if (raw_item_id == static_cast<uint32_t>(IT_WEAPON_GRAPPLE)) {
    saturating_increment(grapple_count);
    return;
  }
  if (!telemetry.catalog_ready ||
      raw_item_id >= static_cast<uint32_t>(IT_TOTAL)) {
    saturating_increment(unmapped_count);
    return;
  }
  const auto catalog_id =
      SG_LocalActionCatalogFromItem(static_cast<item_id_t>(raw_item_id));
  const uint32_t index = static_cast<uint32_t>(catalog_id);
  if (index == 0 || index > WORR_LOCAL_ACTION_CATALOG_COUNT) {
    saturating_increment(unmapped_count);
    return;
  }
  saturating_increment(counts[index]);
}

uint64_t inventory_hash(const gclient_t *client) {
  uint64_t hash = UINT64_C(1469598103934665603);

  for (const int32_t value : client->pers.inventory) {
    uint32_t bits = static_cast<uint32_t>(value);
    for (unsigned int index = 0; index != 4; ++index) {
      hash ^= static_cast<uint8_t>(bits & UINT32_C(0xff));
      hash *= UINT64_C(1099511628211);
      bits >>= 8;
    }
  }
  return hash;
}

int32_t remaining_ms(GameTime deadline) {
  const int64_t value = (deadline - level.time).milliseconds();
  constexpr int64_t maximum = WORR_LOCAL_ACTION_OBSERVATION_MAX_TIMER_MS;
  if (value < -maximum)
    return static_cast<int32_t>(-maximum);
  if (value > maximum)
    return static_cast<int32_t>(maximum);
  return static_cast<int32_t>(value);
}

uint32_t observation_phase(const gclient_t *client) {
  if (!client->pers.weapon)
    return WORR_LOCAL_ACTION_OBSERVATION_HOLSTERED;

  switch (client->weaponState) {
  case WeaponState::Activating:
    return WORR_LOCAL_ACTION_OBSERVATION_RAISING;
  case WeaponState::Ready:
    return WORR_LOCAL_ACTION_OBSERVATION_READY;
  case WeaponState::Firing:
    return WORR_LOCAL_ACTION_OBSERVATION_FIRING;
  case WeaponState::Dropping:
    return WORR_LOCAL_ACTION_OBSERVATION_LOWERING;
  }

  return WORR_LOCAL_ACTION_OBSERVATION_HOLSTERED;
}

bool capture_state(gentity_t *entity,
                   worr_local_action_observation_state_v1 *state_out) {
  gclient_t *client;
  const Item *active;
  const Item *pending;
  worr_local_action_observation_state_v1 state{};

  if (!entity || !entity->client || !state_out)
    return false;
  client = entity->client;
  active = client->pers.weapon;
  pending = client->weapon.pending;
  if (client->ps.gunFrame < 0 || client->ps.gunRate < 0 ||
      client->ps.gunRate > 1000) {
    return false;
  }

  state.struct_size = sizeof(state);
  state.schema_version = WORR_LOCAL_ACTION_OBSERVATION_ABI_VERSION;
  state.phase = observation_phase(client);
  state.inventory_hash = inventory_hash(client);
  state.active_weapon_id = active ? static_cast<uint32_t>(active->id) : 0;
  state.pending_weapon_id = pending ? static_cast<uint32_t>(pending->id) : 0;
  if (active && active->ammo != IT_NULL && active->ammo < IT_TOTAL) {
    state.active_ammo_item_id = static_cast<uint32_t>(active->ammo);
    state.active_ammo_units = client->pers.inventory[active->ammo];
  }
  state.presentation_frame = static_cast<uint32_t>(client->ps.gunFrame);
  state.presentation_rate = static_cast<uint32_t>(client->ps.gunRate);
  state.think_remaining_ms = remaining_ms(client->weapon.thinkTime);
  state.fire_remaining_ms = remaining_ms(client->weapon.fireFinished);
  if (client->buttons & BUTTON_ATTACK)
    state.flags |= WORR_LOCAL_ACTION_OBSERVATION_ATTACK_HELD;
  if (client->latchedButtons & BUTTON_ATTACK)
    state.flags |= WORR_LOCAL_ACTION_OBSERVATION_ATTACK_LATCHED;
  if (client->weapon.fireBuffered)
    state.flags |= WORR_LOCAL_ACTION_OBSERVATION_FIRE_BUFFERED;
  if (client->weapon.thunk)
    state.flags |= WORR_LOCAL_ACTION_OBSERVATION_WEAPON_THUNK;
  if (client->buttons & BUTTON_HOOK)
    state.flags |= WORR_LOCAL_ACTION_OBSERVATION_OFFHAND_HOOK_HELD;
  if (client->grapple.state != GrappleState::None)
    state.flags |= WORR_LOCAL_ACTION_OBSERVATION_OFFHAND_HOOK_ACTIVE;
  if (entity->health > 0)
    state.flags |= WORR_LOCAL_ACTION_OBSERVATION_PLAYER_ALIVE;
  if (ClientIsPlaying(client) && !client->eliminated)
    state.flags |= WORR_LOCAL_ACTION_OBSERVATION_PLAYER_ELIGIBLE;

  if (!Worr_LocalActionObservationStateValidateV1(&state))
    return false;
  *state_out = state;
  return true;
}

bool valid_import(const worr_command_context_import_v1 *candidate) {
  return candidate && candidate->struct_size == sizeof(*candidate) &&
         candidate->api_version == WORR_COMMAND_CONTEXT_API_VERSION &&
         candidate->GetCurrent && candidate->GetScopeState;
}

bool valid_authority_import(
    const worr_local_interaction_authority_import_v1 *candidate) {
  return candidate && candidate->struct_size == sizeof(*candidate) &&
         candidate->api_version ==
             WORR_LOCAL_INTERACTION_AUTHORITY_API_VERSION &&
         candidate->PublishReceipt;
}

bool valid_shadow_authority_import(
    const worr_local_action_shadow_authority_import_v1 *candidate) {
  return candidate && candidate->struct_size == sizeof(*candidate) &&
         candidate->api_version ==
             WORR_LOCAL_ACTION_SHADOW_AUTHORITY_API_VERSION &&
         candidate->PublishReceipt;
}

bool client_index_for_entity(gentity_t *entity, uint32_t *client_index_out) {
  ptrdiff_t entity_index;

  if (!entity || !client_index_out || !g_entities)
    return false;
  entity_index = entity - g_entities;
  if (entity_index <= 0 || static_cast<uint64_t>(entity_index - 1) >=
                               WORR_LOCAL_ACTION_OBSERVATION_MAX_CLIENTS) {
    return false;
  }
  *client_index_out = static_cast<uint32_t>(entity_index - 1);
  return true;
}

bool current_context_for_entity(gentity_t *entity,
                                worr_authoritative_command_context_v1 *out) {
  uint32_t client_index;

  if (!entity || !out || !valid_import(command_context_import) ||
      command_context_import->GetScopeState() !=
          WORR_COMMAND_CONTEXT_SCOPE_ACTIVE_VALID ||
      !command_context_import->GetCurrent(out) ||
      !client_index_for_entity(entity, &client_index)) {
    return false;
  }
  return client_index == out->client_index &&
         out->client_index < WORR_LOCAL_ACTION_OBSERVATION_MAX_CLIENTS;
}

bool offer_command_lease(const worr_authoritative_command_context_v1 &context) {
  uint32_t result = 0;

  if (!telemetry.lease_ready ||
      context.client_index >= WORR_LOCAL_ACTION_OBSERVATION_MAX_CLIENTS ||
      !Worr_LocalActionCommandLeaseOfferV1(
          &command_leases[context.client_index], &context.command, &result)) {
    saturating_increment(telemetry.lease_rejected);
    return false;
  }
  switch (result) {
  case WORR_LOCAL_ACTION_COMMAND_LEASE_OFFER_ACCEPTED:
    saturating_increment(telemetry.lease_offers);
    break;
  case WORR_LOCAL_ACTION_COMMAND_LEASE_OFFER_SUPERSEDED:
    saturating_increment(telemetry.lease_supersedes);
    break;
  case WORR_LOCAL_ACTION_COMMAND_LEASE_OFFER_DUPLICATE:
    saturating_increment(telemetry.lease_duplicates);
    break;
  case WORR_LOCAL_ACTION_COMMAND_LEASE_OFFER_REBASED:
    saturating_increment(telemetry.lease_rebases);
    break;
  default:
    saturating_increment(telemetry.lease_rejected);
    return false;
  }
  return true;
}

void append_record(const worr_local_action_observation_record_v1 &record) {
  uint32_t &head = write_heads[record.client_index];
  records[record.client_index][head % kHistoryCapacity] = record;
  if (head != std::numeric_limits<uint32_t>::max())
    ++head;
}

void append_leased_record(
    const worr_local_action_observation_record_v1 &record) {
  uint32_t &head = leased_write_heads[record.client_index];
  leased_records[record.client_index][head % kHistoryCapacity] = record;
  if (head != std::numeric_limits<uint32_t>::max())
    ++head;
}

bool hook_held(const worr_local_action_observation_state_v1 &state) {
  return (state.flags & WORR_LOCAL_ACTION_OBSERVATION_OFFHAND_HOOK_HELD) != 0;
}

bool hook_active(const worr_local_action_observation_state_v1 &state) {
  return (state.flags & WORR_LOCAL_ACTION_OBSERVATION_OFFHAND_HOOK_ACTIVE) != 0;
}

bool interaction_state_matches_observation(
    const worr_local_interaction_state_v1 &interaction,
    const worr_local_action_observation_state_v1 &observation) {
  const bool interaction_held =
      (interaction.flags & WORR_LOCAL_INTERACTION_STATE_HOOK_HELD) != 0;
  const bool interaction_active =
      (interaction.flags & WORR_LOCAL_INTERACTION_STATE_HOOK_ACTIVE) != 0;
  return interaction_held == hook_held(observation) &&
         interaction_active == hook_active(observation);
}

bool interaction_cursor_accepts(
    const worr_local_interaction_state_v1 &interaction,
    const worr_command_record_v1 &command) {
  worr_command_id_v1 next{};

  return Worr_LocalInteractionStateValidateV1(&interaction) &&
         Worr_CommandCursorNextIdV1(interaction.applied_cursor, &next) &&
         next.epoch == command.command_id.epoch &&
         next.sequence == command.command_id.sequence;
}

bool rebase_interaction_before_command(
    uint32_t client_index, const worr_command_record_v1 &command,
    const worr_local_action_observation_state_v1 &state_before) {
  worr_local_interaction_state_v1 rebased{};

  if (!Worr_LocalInteractionRebaseBeforeCommandV1(
          &command, hook_held(state_before), hook_active(state_before),
          &rebased)) {
    return false;
  }
  interaction_states[client_index] = rebased;
  saturating_increment(telemetry.interaction_rebases);
  return true;
}

void append_interaction_record(
    uint32_t client_index,
    const worr_local_interaction_transaction_v1 &transaction) {
  uint32_t &head = interaction_write_heads[client_index];

  interaction_records[client_index][head % kHistoryCapacity] = transaction;
  if (head != std::numeric_limits<uint32_t>::max())
    ++head;
}

bool observe_interaction(
    const worr_authoritative_command_context_v1 &context,
    const worr_local_action_observation_state_v1 &state_before,
    const worr_local_action_observation_state_v1 &state_after) {
  const uint32_t client_index = context.client_index;
  worr_local_interaction_state_v1 &interaction =
      interaction_states[client_index];
  worr_local_interaction_intent_v1 intent{};
  worr_local_interaction_transaction_v1 transaction{};
  const bool discontinuity =
      !interaction_cursor_accepts(interaction, context.command) ||
      !interaction_state_matches_observation(interaction, state_before);

  if (discontinuity) {
    if (!rebase_interaction_before_command(client_index, context.command,
                                           state_before)) {
      return false;
    }
    saturating_increment(telemetry.interaction_discontinuities);
  }

  intent.struct_size = sizeof(intent);
  intent.schema_version = WORR_LOCAL_INTERACTION_ABI_VERSION;
  if ((context.command.command.buttons & WORR_LOCAL_INTERACTION_HOOK_BUTTON) !=
      0) {
    intent.flags |= WORR_LOCAL_INTERACTION_INTENT_HOOK_HELD;
  }
  if (!Worr_LocalInteractionBuildAuthoritativeHookV1(
          &interaction, &context.command, &intent, hook_active(state_after),
          &transaction)) {
    return false;
  }

  interaction = transaction.state_after;
  append_interaction_record(client_index, transaction);
  /* The cgame retains prediction evidence only for a Hook request edge.
   * Publishing receipts for ordinary no-op or persisted commands creates
   * permanently unmatched private records and eventually forces a false
   * resync.  Active/rejected authority for the request remains encoded in
   * that request-bearing transaction's outcome flags. */
  if (authority_import &&
      (transaction.outcome_flags &
       WORR_LOCAL_INTERACTION_OUTCOME_HOOK_REQUESTED) != 0) {
    worr_local_interaction_authority_receipt_v1 receipt{};
    if (Worr_LocalInteractionAuthorityReceiptBuildV1(&transaction, &receipt))
      (void)authority_import->PublishReceipt(client_index, &receipt);
  }
  saturating_increment(telemetry.interaction_records);
  return true;
}

} // namespace

void SG_LocalActionObservationInitialize() {
  command_context_import = nullptr;
  authority_import = nullptr;
  shadow_authority_import = nullptr;
  shadow_authority_enabled =
      gi.cvar("sg_local_action_shadow_receipts", "0", CVAR_NOFLAGS);
  if (gi.GetExtension) {
    const auto *candidate = static_cast<const worr_command_context_import_v1 *>(
        gi.GetExtension(WORR_COMMAND_CONTEXT_IMPORT_V1));
    if (valid_import(candidate))
      command_context_import = candidate;
    const auto *authority_candidate =
        static_cast<const worr_local_interaction_authority_import_v1 *>(
            gi.GetExtension(WORR_LOCAL_INTERACTION_AUTHORITY_IMPORT_V1));
    if (valid_authority_import(authority_candidate))
      authority_import = authority_candidate;
    const auto *shadow_authority_candidate =
        static_cast<const worr_local_action_shadow_authority_import_v1 *>(
            gi.GetExtension(WORR_LOCAL_ACTION_SHADOW_AUTHORITY_IMPORT_V1));
    if (valid_shadow_authority_import(shadow_authority_candidate))
      shadow_authority_import = shadow_authority_candidate;
  }
  SG_LocalActionObservationResetMap();
}

void SG_LocalActionObservationResetMap() {
  bool leases_ready = true;

  std::memset(records.data(), 0, sizeof(records));
  std::memset(write_heads.data(), 0, sizeof(write_heads));
  std::memset(leased_records.data(), 0, sizeof(leased_records));
  std::memset(leased_write_heads.data(), 0, sizeof(leased_write_heads));
  std::memset(command_leases.data(), 0, sizeof(command_leases));
  std::memset(interaction_states.data(), 0, sizeof(interaction_states));
  std::memset(interaction_records.data(), 0, sizeof(interaction_records));
  std::memset(interaction_write_heads.data(), 0,
              sizeof(interaction_write_heads));
  telemetry = {};
  for (uint32_t client_index = 0;
       client_index < WORR_LOCAL_ACTION_OBSERVATION_MAX_CLIENTS;
       ++client_index) {
    if (!Worr_LocalActionCommandLeaseInitV1(client_index,
                                            &command_leases[client_index])) {
      leases_ready = false;
    }
  }
  telemetry.catalog_ready = SG_LocalActionWeaponCatalogValidate();
  telemetry.lease_ready = leases_ready;
}

void SG_LocalActionObservationResetClient(gentity_t *entity) {
  uint32_t client_index;

  if (!client_index_for_entity(entity, &client_index))
    return;
  records[client_index] = {};
  write_heads[client_index] = 0;
  leased_records[client_index] = {};
  leased_write_heads[client_index] = 0;
  command_leases[client_index] = {};
  interaction_states[client_index] = {};
  interaction_records[client_index] = {};
  interaction_write_heads[client_index] = 0;
  if (!Worr_LocalActionCommandLeaseInitV1(
          client_index, &command_leases[client_index])) {
    telemetry.lease_ready = false;
  }
}

SG_LocalActionObservationScope::SG_LocalActionObservationScope(
    gentity_t *entity) {
  saturating_increment(telemetry.scoped_attempts);
  if (!current_context_for_entity(entity, &context_)) {
    saturating_increment(telemetry.rejected_scope);
    return;
  }
  if (!capture_state(entity, &state_before_)) {
    saturating_increment(telemetry.rejected_state);
    return;
  }
  entity_ = entity;
  active_ = true;
}

SG_LocalActionObservationScope::~SG_LocalActionObservationScope() {
  worr_local_action_observation_state_v1 state_after{};
  worr_local_action_observation_record_v1 record{};

  if (!active_)
    return;
  if (!capture_state(entity_, &state_after) ||
      !Worr_LocalActionObservationBuildV1(context_.client_index,
                                          &context_.command, &state_before_,
                                          &state_after, &record)) {
    saturating_increment(telemetry.rejected_state);
  } else {
    append_record(record);
    saturating_increment(telemetry.scoped_records);
    observe_catalog_item(
        state_before_.active_weapon_id, telemetry.scoped_records_by_catalog,
        telemetry.unmapped_scoped_records, telemetry.grapple_scoped_records);
    if (!observe_interaction(context_, state_before_, state_after))
      saturating_increment(telemetry.interaction_rejected);
    (void)offer_command_lease(context_);
  }
}

SG_LocalActionObservationFrameLeaseScope::
    SG_LocalActionObservationFrameLeaseScope(gentity_t *entity) {
  if (!telemetry.lease_ready ||
      !client_index_for_entity(entity, &client_index_)) {
    return;
  }
  auto &lease = command_leases[client_index_];
  if (lease.phase == WORR_LOCAL_ACTION_COMMAND_LEASE_PENDING &&
      Worr_LocalActionCommandLeaseBeginFrameV1(&lease)) {
    active_ = true;
  } else if (lease.phase != WORR_LOCAL_ACTION_COMMAND_LEASE_EMPTY) {
    saturating_increment(telemetry.lease_rejected);
  }
}

SG_LocalActionObservationFrameLeaseScope::
    ~SG_LocalActionObservationFrameLeaseScope() {
  uint32_t result = 0;

  if (!active_)
    return;
  if (!Worr_LocalActionCommandLeaseEndFrameV1(&command_leases[client_index_],
                                              &result)) {
    saturating_increment(telemetry.lease_rejected);
    return;
  }
  if (result == WORR_LOCAL_ACTION_COMMAND_LEASE_END_CLAIMED)
    return;
  if (result == WORR_LOCAL_ACTION_COMMAND_LEASE_END_EXPIRED)
    saturating_increment(telemetry.lease_expired);
  else
    saturating_increment(telemetry.lease_rejected);
}

SG_LocalActionObservationLeasedAdvanceScope::
    SG_LocalActionObservationLeasedAdvanceScope(gentity_t *entity) {
  if (!telemetry.lease_ready)
    return;
  if (!client_index_for_entity(entity, &client_index_)) {
    saturating_increment(telemetry.lease_rejected);
    return;
  }
  auto &lease = command_leases[client_index_];
  if (lease.phase == WORR_LOCAL_ACTION_COMMAND_LEASE_EMPTY)
    return;
  if (lease.phase != WORR_LOCAL_ACTION_COMMAND_LEASE_FRAME_ACTIVE ||
      !Worr_LocalActionCommandLeaseClaimV1(&lease, &command_)) {
    saturating_increment(telemetry.lease_rejected);
    return;
  }

  entity_ = entity;
  saturating_increment(telemetry.leased_weapon_thinks);
  saturating_increment(telemetry.lease_claims);
  const uint32_t active_item_id =
      entity && entity->client && entity->client->pers.weapon
          ? static_cast<uint32_t>(entity->client->pers.weapon->id)
          : 0;
  observe_catalog_item(
      active_item_id, telemetry.leased_weapon_thinks_by_catalog,
      telemetry.unmapped_weapon_thinks, telemetry.grapple_weapon_thinks);
  if (!capture_state(entity_, &state_before_)) {
    saturating_increment(telemetry.rejected_state);
    return;
  }
  active_ = true;
}

SG_LocalActionObservationLeasedAdvanceScope::
    ~SG_LocalActionObservationLeasedAdvanceScope() {
  worr_local_action_observation_state_v1 state_after{};
  worr_local_action_observation_record_v1 record{};

  if (!active_)
    return;
  if (!capture_state(entity_, &state_after) ||
      !Worr_LocalActionObservationBuildV1(
          client_index_, &command_, &state_before_, &state_after, &record)) {
    saturating_increment(telemetry.rejected_state);
    return;
  }
  append_leased_record(record);
  saturating_increment(telemetry.leased_records);
  observe_catalog_item(
      state_before_.active_weapon_id, telemetry.leased_records_by_catalog,
      telemetry.unmapped_scoped_records, telemetry.grapple_scoped_records);
  if (shadow_authority_import && shadow_authority_enabled &&
      shadow_authority_enabled->integer != 0) {
    worr_local_action_shadow_v1 shadow{};
    worr_local_action_shadow_authority_receipt_v1 receipt{};
    /* This first receipt slice reconciles attack-bearing weapon actions.
     * Activation/idle weapon thinks have no client action intent, and
     * emitting them would fill the private reconciliation ring before a
     * player acts. Release-edge/deployable semantics remain a later catalog
     * milestone while all entries are still V2-blocked. */
    if ((command_.command.buttons & BUTTON_ATTACK) != 0 &&
        SG_LocalActionObservationCopyShadowForCommand(
            client_index_, command_.command_id, &shadow) &&
        Worr_LocalActionShadowAuthorityReceiptBuildV1(&shadow, &receipt)) {
      (void)shadow_authority_import->PublishReceipt(client_index_, &receipt);
    }
  }
}

void SG_LocalActionObservationNoteWeaponThink(gentity_t *entity) {
  worr_authoritative_command_context_v1 context{};
  uint32_t client_index;
  const uint32_t active_item_id =
      entity && entity->client && entity->client->pers.weapon
          ? static_cast<uint32_t>(entity->client->pers.weapon->id)
          : 0;

  if (current_context_for_entity(entity, &context)) {
    saturating_increment(telemetry.scoped_weapon_thinks);
    observe_catalog_item(
        active_item_id, telemetry.scoped_weapon_thinks_by_catalog,
        telemetry.unmapped_weapon_thinks, telemetry.grapple_weapon_thinks);
    return;
  }
  if (telemetry.lease_ready && client_index_for_entity(entity, &client_index) &&
      command_leases[client_index].phase ==
          WORR_LOCAL_ACTION_COMMAND_LEASE_CLAIMED)
    return;
  saturating_increment(telemetry.unscoped_weapon_thinks);
  observe_catalog_item(
      active_item_id, telemetry.unscoped_weapon_thinks_by_catalog,
      telemetry.unmapped_weapon_thinks, telemetry.grapple_weapon_thinks);
}

bool SG_LocalActionObservationCopyCatalogTelemetry(
    SG_LocalActionObservationCatalogTelemetry *telemetry_out) {
  if (!telemetry_out)
    return false;
  SG_LocalActionObservationCatalogTelemetry candidate{};
  candidate.catalog_ready = telemetry.catalog_ready;
  candidate.lease_ready = telemetry.lease_ready;
  candidate.unmapped_scoped_records = telemetry.unmapped_scoped_records;
  candidate.grapple_scoped_records = telemetry.grapple_scoped_records;
  candidate.unmapped_weapon_thinks = telemetry.unmapped_weapon_thinks;
  candidate.grapple_weapon_thinks = telemetry.grapple_weapon_thinks;
  candidate.lease_offers = telemetry.lease_offers;
  candidate.lease_supersedes = telemetry.lease_supersedes;
  candidate.lease_duplicates = telemetry.lease_duplicates;
  candidate.lease_rebases = telemetry.lease_rebases;
  candidate.lease_claims = telemetry.lease_claims;
  candidate.lease_expired = telemetry.lease_expired;
  candidate.lease_rejected = telemetry.lease_rejected;
  candidate.scoped_records_by_catalog = telemetry.scoped_records_by_catalog;
  candidate.leased_records_by_catalog = telemetry.leased_records_by_catalog;
  candidate.scoped_weapon_thinks_by_catalog =
      telemetry.scoped_weapon_thinks_by_catalog;
  candidate.leased_weapon_thinks_by_catalog =
      telemetry.leased_weapon_thinks_by_catalog;
  candidate.unscoped_weapon_thinks_by_catalog =
      telemetry.unscoped_weapon_thinks_by_catalog;
  *telemetry_out = candidate;
  return true;
}

bool SG_LocalActionObservationCopyScopedRecordForCommand(
    uint32_t client_index, worr_command_id_v1 command_id,
    worr_local_action_observation_record_v1 *record_out) {
  const uint32_t head = client_index < WORR_LOCAL_ACTION_OBSERVATION_MAX_CLIENTS
                            ? write_heads[client_index]
                            : 0;
  const uint32_t count = std::min(head, kHistoryCapacity);

  if (!record_out ||
      client_index >= WORR_LOCAL_ACTION_OBSERVATION_MAX_CLIENTS ||
      !Worr_CommandIdValidV1(command_id, false)) {
    return false;
  }
  for (uint32_t offset = 1; offset <= count; ++offset) {
    const auto &candidate =
        records[client_index][(head - offset) % kHistoryCapacity];
    if (candidate.command.command_id.epoch != command_id.epoch ||
        candidate.command.command_id.sequence != command_id.sequence ||
        !Worr_LocalActionObservationRecordValidateV1(&candidate)) {
      continue;
    }
    *record_out = candidate;
    return true;
  }
  return false;
}

bool SG_LocalActionObservationCopyLeasedRecordForCommand(
    uint32_t client_index, worr_command_id_v1 command_id,
    worr_local_action_observation_record_v1 *record_out) {
  const uint32_t head = client_index < WORR_LOCAL_ACTION_OBSERVATION_MAX_CLIENTS
                            ? leased_write_heads[client_index]
                            : 0;
  const uint32_t count = std::min(head, kHistoryCapacity);

  if (!record_out ||
      client_index >= WORR_LOCAL_ACTION_OBSERVATION_MAX_CLIENTS ||
      !Worr_CommandIdValidV1(command_id, false)) {
    return false;
  }
  for (uint32_t offset = 1; offset <= count; ++offset) {
    const auto &candidate =
        leased_records[client_index][(head - offset) % kHistoryCapacity];
    if (candidate.command.command_id.epoch != command_id.epoch ||
        candidate.command.command_id.sequence != command_id.sequence ||
        !Worr_LocalActionObservationRecordValidateV1(&candidate)) {
      continue;
    }
    *record_out = candidate;
    return true;
  }
  return false;
}

bool SG_LocalActionObservationCopyJoinedRecordForCommand(
    uint32_t client_index, worr_command_id_v1 command_id,
    worr_local_action_observation_record_v1 *record_out) {
  worr_local_action_observation_record_v1 scoped{};
  worr_local_action_observation_record_v1 leased{};
  worr_local_action_observation_record_v1 joined{};

  if (!record_out ||
      !SG_LocalActionObservationCopyScopedRecordForCommand(
          client_index, command_id, &scoped) ||
      !SG_LocalActionObservationCopyLeasedRecordForCommand(
          client_index, command_id, &leased) ||
      !Worr_CommandRecordSemanticallyEqualV1(
          &scoped.command, &leased.command,
          WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS) ||
      !Worr_LocalActionObservationStatesContiguousV1(
          &scoped.state_after, &leased.state_before,
          WORR_LOCAL_ACTION_OBSERVATION_MAX_CONTINUITY_ELAPSED_MS) ||
      !Worr_LocalActionObservationBuildV1(client_index, &scoped.command,
                                          &scoped.state_before,
                                          &leased.state_after, &joined)) {
    return false;
  }
  *record_out = joined;
  return true;
}

bool SG_LocalActionObservationCopyShadowForCommand(
    uint32_t client_index, worr_command_id_v1 command_id,
    worr_local_action_shadow_v1 *shadow_out) {
  worr_local_action_observation_record_v1 joined{};
  worr_local_action_shadow_v1 shadow{};

  if (!shadow_out ||
      !SG_LocalActionObservationCopyJoinedRecordForCommand(
          client_index, command_id, &joined) ||
      !SG_LocalActionBuildShadowFromObservation(&joined, &shadow)) {
    return false;
  }
  *shadow_out = shadow;
  return true;
}

bool SG_LocalActionObservationCopyLatestAttackShadowInRange(
    uint32_t client_index, worr_command_id_v1 first_command_id,
    worr_command_id_v1 last_command_id,
    worr_command_id_v1 *command_id_out,
    worr_local_action_shadow_v1 *shadow_out) {
  const uint32_t head = client_index < WORR_LOCAL_ACTION_OBSERVATION_MAX_CLIENTS
                            ? leased_write_heads[client_index]
                            : 0;
  const uint32_t count = std::min(head, kHistoryCapacity);

  if (!command_id_out || !shadow_out ||
      client_index >= WORR_LOCAL_ACTION_OBSERVATION_MAX_CLIENTS ||
      !Worr_CommandIdValidV1(first_command_id, false) ||
      !Worr_CommandIdValidV1(last_command_id, false) ||
      first_command_id.epoch != last_command_id.epoch ||
      first_command_id.sequence > last_command_id.sequence) {
    return false;
  }

  for (uint32_t offset = 1; offset <= count; ++offset) {
    const auto &candidate =
        leased_records[client_index][(head - offset) % kHistoryCapacity];
    const auto command_id = candidate.command.command_id;
    if (command_id.epoch != first_command_id.epoch ||
        command_id.sequence < first_command_id.sequence ||
        command_id.sequence > last_command_id.sequence ||
        (candidate.command.command.buttons & BUTTON_ATTACK) == 0 ||
        !Worr_LocalActionObservationRecordValidateV1(&candidate)) {
      continue;
    }
    worr_local_action_shadow_v1 shadow{};
    if (!SG_LocalActionObservationCopyShadowForCommand(
            client_index, command_id, &shadow)) {
      continue;
    }
    *command_id_out = command_id;
    *shadow_out = shadow;
    return true;
  }
  return false;
}

bool SG_LocalInteractionObservationCopyForCommand(
    uint32_t client_index, worr_command_id_v1 command_id,
    worr_local_interaction_transaction_v1 *transaction_out) {
  const uint32_t head = client_index < WORR_LOCAL_ACTION_OBSERVATION_MAX_CLIENTS
                            ? interaction_write_heads[client_index]
                            : 0;
  const uint32_t count = std::min(head, kHistoryCapacity);

  if (!transaction_out ||
      client_index >= WORR_LOCAL_ACTION_OBSERVATION_MAX_CLIENTS ||
      !Worr_CommandIdValidV1(command_id, false)) {
    return false;
  }

  for (uint32_t offset = 1; offset <= count; ++offset) {
    const auto &candidate =
        interaction_records[client_index][(head - offset) % kHistoryCapacity];
    if (candidate.producer != WORR_LOCAL_INTERACTION_PRODUCER_AUTHORITATIVE ||
        candidate.command.command_id.epoch != command_id.epoch ||
        candidate.command.command_id.sequence != command_id.sequence ||
        !Worr_LocalInteractionTransactionValidateV1(&candidate)) {
      continue;
    }
    *transaction_out = candidate;
    return true;
  }
  return false;
}

bool SG_LocalInteractionObservationCopyAuthorityReceiptForCommand(
    uint32_t client_index, worr_command_id_v1 command_id,
    worr_local_interaction_authority_receipt_v1 *receipt_out) {
  worr_local_interaction_transaction_v1 transaction{};
  worr_local_interaction_authority_receipt_v1 receipt{};

  if (!receipt_out ||
      !SG_LocalInteractionObservationCopyForCommand(client_index, command_id,
                                                    &transaction) ||
      !Worr_LocalInteractionAuthorityReceiptBuildV1(&transaction, &receipt)) {
    return false;
  }
  *receipt_out = receipt;
  return true;
}
