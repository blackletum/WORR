/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "shared/command_context.h"
#include "shared/local_action_catalog.h"
#include "shared/local_action_command_lease.h"
#include "shared/local_action_observation.h"
#include "shared/local_action_shadow.h"
#include "shared/local_interaction_abi.h"

#include <array>
#include <cstdint>

struct gentity_t;

/*
 * Captures live legacy weapon state around an authenticated canonical command.
 * This is intentionally observational: neither constructor nor destructor may
 * modify game state, enqueue a network message, or drive presentation.
 */
class SG_LocalActionObservationScope {
public:
  explicit SG_LocalActionObservationScope(gentity_t *entity);
  ~SG_LocalActionObservationScope();

  SG_LocalActionObservationScope(const SG_LocalActionObservationScope &) =
      delete;
  SG_LocalActionObservationScope &
  operator=(const SG_LocalActionObservationScope &) = delete;

private:
  gentity_t *entity_ = nullptr;
  worr_authoritative_command_context_v1 context_{};
  worr_local_action_observation_state_v1 state_before_{};
  bool active_ = false;
};

/*
 * Makes the latest authenticated observation record available to at most one
 * weapon advance in this ClientBeginServerFrame call. It never changes the
 * engine command context or any gameplay state.
 */
class SG_LocalActionObservationFrameLeaseScope {
public:
  explicit SG_LocalActionObservationFrameLeaseScope(gentity_t *entity);
  ~SG_LocalActionObservationFrameLeaseScope();

  SG_LocalActionObservationFrameLeaseScope(
      const SG_LocalActionObservationFrameLeaseScope &) = delete;
  SG_LocalActionObservationFrameLeaseScope &
  operator=(const SG_LocalActionObservationFrameLeaseScope &) = delete;

private:
  uint32_t client_index_ = WORR_LOCAL_ACTION_COMMAND_LEASE_MAX_CLIENTS;
  bool active_ = false;
};

/* Captures only the weapon advance executed inside a live frame lease. */
class SG_LocalActionObservationLeasedAdvanceScope {
public:
  explicit SG_LocalActionObservationLeasedAdvanceScope(gentity_t *entity);
  ~SG_LocalActionObservationLeasedAdvanceScope();

  SG_LocalActionObservationLeasedAdvanceScope(
      const SG_LocalActionObservationLeasedAdvanceScope &) = delete;
  SG_LocalActionObservationLeasedAdvanceScope &
  operator=(const SG_LocalActionObservationLeasedAdvanceScope &) = delete;

private:
  gentity_t *entity_ = nullptr;
  uint32_t client_index_ = WORR_LOCAL_ACTION_COMMAND_LEASE_MAX_CLIENTS;
  worr_command_record_v1 command_{};
  worr_local_action_observation_state_v1 state_before_{};
  bool active_ = false;
};

void SG_LocalActionObservationInitialize();
void SG_LocalActionObservationResetMap();
void SG_LocalActionObservationResetClient(gentity_t *entity);
void SG_LocalActionObservationNoteWeaponThink(gentity_t *entity);

struct SG_LocalActionObservationCatalogTelemetry {
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

/* Read-only diagnostics; no gameplay or presentation authority is implied. */
bool SG_LocalActionObservationCopyCatalogTelemetry(
    SG_LocalActionObservationCatalogTelemetry *telemetry_out);

/* Exact value-copy evidence captured around the authenticated callback. */
bool SG_LocalActionObservationCopyScopedRecordForCommand(
    uint32_t client_index, worr_command_id_v1 command_id,
    worr_local_action_observation_record_v1 *record_out);

/* Exact value-copy evidence for one post-command leased weapon advance. */
bool SG_LocalActionObservationCopyLeasedRecordForCommand(
    uint32_t client_index, worr_command_id_v1 command_id,
    worr_local_action_observation_record_v1 *record_out);

/*
 * Joins scoped and leased observations only when every non-timer field is
 * exact and both relative weapon timers show the same bounded clock decay. It
 * never invents a missing advance or repairs a gameplay-state gap.
 */
bool SG_LocalActionObservationCopyJoinedRecordForCommand(
    uint32_t client_index, worr_command_id_v1 command_id,
    worr_local_action_observation_record_v1 *record_out);

/* Descriptor-complete, still-shadow-only model derived from the joined row. */
bool SG_LocalActionObservationCopyShadowForCommand(
    uint32_t client_index, worr_command_id_v1 command_id,
    worr_local_action_shadow_v1 *shadow_out);

/*
 * Copies the newest exact attack-bearing shadow inside one inclusive command
 * interval.  This is a read-only diagnostic/receipt correlation helper for
 * callbacks that legally run under the preceding command's one-frame lease;
 * it never changes the lease, weapon state, or gameplay authority.
 */
bool SG_LocalActionObservationCopyLatestAttackShadowInRange(
    uint32_t client_index, worr_command_id_v1 first_command_id,
    worr_command_id_v1 last_command_id,
    worr_command_id_v1 *command_id_out,
    worr_local_action_shadow_v1 *shadow_out);

/*
 * Read-only bounded lookup for a future authoritative event bridge. It never
 * creates an event, changes game state, or exposes mutable ledger storage.
 */
bool SG_LocalInteractionObservationCopyForCommand(
    uint32_t client_index, worr_command_id_v1 command_id,
    worr_local_interaction_transaction_v1 *transaction_out);

/*
 * Read-only sparse projection for a future per-peer reliable authority
 * carrier. It succeeds only for an authoritative Hook request edge and never
 * allocates an event, chooses a recipient, writes a packet, or changes game
 * state. `receipt_out` is untouched on failure.
 */
bool SG_LocalInteractionObservationCopyAuthorityReceiptForCommand(
    uint32_t client_index, worr_command_id_v1 command_id,
    worr_local_interaction_authority_receipt_v1 *receipt_out);
