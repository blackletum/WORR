# Command-scoped authoritative action observation

Date: 2026-07-15

Project tasks: `FR-10-T08`, `FR-10-T09`

Status: implemented observational prerequisite; not a gameplay, protocol, or
presentation cutover.

## Outcome

WORR now records the real sgame weapon/action state immediately before and
after a validated canonical command callback. The record is pointer-free,
command-keyed, immutable after capture, and retained in a bounded per-client
sgame ledger. It gives the future cgame/sgame action bridge a truthful
authority oracle for the complete Rerelease catalogue before any local action
is predicted, reconciled, or presentation-suppressed.

The capture is intentionally one-way and observational. It does not replace
`p_weapon`, alter `ClientThink`, emit an event, affect a snapshot, change a
packet, allocate an event ID, or enable a cvar. Legacy gameplay and
presentation remain authoritative.

## Why this comes before a live weapon predictor

The shared local-action v2 core has correct deterministic behavior for its
declared simplified rules, but the live server uses a substantially richer
model:

- 22 selectable Rerelease weapon entries execute through distinct fire paths,
  including projectiles, hitscan/piercing queries, charge/hold behavior,
  staged multi-shot fire, grapple lifecycle, BFG wind-up, and world-dependent
  outcomes.
- `Weapon_Generic` and `Weapon_Repeating` drive animation through stateful
  frame and timing logic; individual weapon functions also apply powerups,
  selection policy, inventory changes, damage, recoil, and sound/effect
  output.
- `Think_Weapon` runs both during `ClientThink` and in
  `ClientBeginServerFrame`. The latter can advance held-fire state with no
  canonical command scope currently active.

Consequently, assigning a command prediction key while server send code writes
a muzzleflash or sound would be unsound: an off-command action has no exact
input boundary to correlate, and a packet-local ordinal is not the semantic
emitter ordinal needed for prediction suppression. A partial migration of one
or a few weapons would create competing weapon simulations and violate the
fidelity gate established for `FR-10-T08`.

This follows the useful idTech3 principle without copying its protocol: Quake
3 advances player state from command time and exposes predictable events from
that command-owned state. WORR retains its Rerelease weapon behavior while it
first measures where its current frame-driven action timing agrees with, or
escapes from, an authenticated command boundary.

## ABI

`inc/shared/local_action_observation.h` introduces ABI version 1.

| Value record | Size | Purpose |
|---|---:|---|
| `worr_local_action_observation_state_v1` | 64 bytes | Complete pointer-free view of live action-relevant state at one boundary |
| `worr_local_action_observation_record_v1` | 256 bytes | Canonical command plus pre/post states, change mask, client index, and semantic hash |

Each captured state includes:

- active and pending item IDs; active ammo item/count;
- mapped legacy phase (`holstered`, `raising`, `ready`, `firing`,
  `lowering`);
- authoritative gun frame/rate and signed, bounded remaining think/fire
  times relative to `level.time`;
- attack-held, latched, buffered-fire, weapon-thunk, alive, and eligible
  flags; and
- an explicit FNV-1a inventory fingerprint over every `pers.inventory` slot.

The inventory fingerprint catches action-relevant changes outside the active
ammo field (such as a pending switch's ammunition or auto-selection input)
without embedding a mutable sgame pointer in the ABI.

The pure C11 builder verifies a fully canonical `worr_command_record_v1`, all
state fields/ranges/reserved bytes, an exact fieldwise change mask, and a
domain-separated semantic hash. Its output buffer must be all zero; every
invalid call preserves the caller's bytes exactly. Record validation recomputes
the difference mask and hash, so stale or tampered ledger records cannot be
accepted by a future comparator.

## Live ownership and lifecycle

`src/game/sgame/network/local_action_observation.cpp` owns the sgame-only
ledger:

1. `GetGameAPI` discovers the existing versioned
   `WORR_AUTHORITATIVE_COMMAND_CONTEXT_IMPORT_V1` and validates its two
   callbacks. It retains no engine-owned command pointer.
2. `ClientSessionServiceImpl::ClientThink` creates an allocation-free RAII
   observation scope after binding its client. The scope accepts only an
   active-valid context whose client index matches `ent - g_entities - 1`.
3. The scope snapshots the real state at entry and, on every normal or early
   return path, snapshots it again, builds the immutable record, and stores it
   in that client's 32-record overwrite ring.
4. `Think_Weapon` independently counts whether its advance happened under a
   valid command scope or outside one. This is intentionally a timing coverage
   measurement, not an inferred fire event.
5. Game initialization and map spawn reset the ledger and its telemetry.

No scope is synthesized for legacy, rejected, bot, gap-fast-forward, or
server-frame-only activity. Missing/invalid context, entity/client mismatch,
negative gun frame/rate, invalid inventory range, or a failed core build drops
only the observation and increments internal rejection telemetry. It never
falls back to packet acknowledgement or guesses a command identity.

## Files

- `inc/shared/local_action_observation.h`
- `src/common/net/local_action_observation.c`
- `src/game/sgame/network/local_action_observation.hpp`
- `src/game/sgame/network/local_action_observation.cpp`
- `src/game/sgame/client/client_session_service_impl.cpp`
- `src/game/sgame/player/p_weapon.cpp`
- `src/game/sgame/gameplay/g_main.cpp`
- `src/game/sgame/gameplay/g_spawn.cpp`
- `tools/networking/local_action_observation_test.c`
- `meson.build`

## Validation

Focused Windows x86-64 validation passed:

```text
meson compile -C builddir-win local_action_observation_test
meson test -C builddir-win --no-rebuild --print-errorlogs \
  network-local-action-observation

1/1 passed

ninja -C builddir-win sgame_x86_64.dll
```

The test proves valid state capture, exact change masks, record hash
validation, zero-buffer fail-closed behavior, invalid phase rejection, invalid
canonical command rejection, and post-build corruption rejection. The sgame
link verifies the live RAII scope, command-context import, inventory capture,
and `Think_Weapon` instrumentation compile against the actual Rerelease game
module. No interactive client was launched.

## Promotion gates still open

This evidence does not make `FR-10-T08` or `FR-10-T09` complete. The remaining
bridge must:

1. collect long-running per-weapon command-scoped and unscoped timing evidence
   from the ledger, then define a replacement command-time lease for all
   frame-driven action advances;
2. build a bgame-owned catalogue adapter for all 22 weapon entries and their
   inventory, powerup, charge, multi-shot, projectile, hitscan, animation, and
   selection behavior;
3. run that adapter in cgame from the exact consumed cursor and in sgame as a
   shadow comparator against these observations;
4. produce command-derived semantic gameplay/audio/effect records at the
   source action boundary, not while serializing legacy bytes;
5. feed paired predicted/authoritative transactions into the existing audit
   ring, prove zero catalogue divergence under impairment, and enforce
   correction/presentation budgets; and only then
6. promote state ownership and predicted-side-effect suppression as one
   reviewed cutover, preserving legacy demos and non-capable peers according to
   their dedicated task gates.
