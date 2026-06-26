# Q3A BotLib Entity Scheduling and Fairness Closeout

Date: 2026-06-21

Tasks: `FR-04-T02`, `FR-04-T12`, `FR-04-T14`, `FR-04-T15`, `DV-07-T06`

## Summary

This round closes the next Phase 4 checklist rows for the remaining BotLib
entity snapshot categories, staggered expensive checks, item desirability
staggering, route recomputation rate-limit evidence, and the first aim fairness
constraint.

The work is WORR-native. No new Q3A, BSPC, Quake3e, baseq3a, Gladiator, or
`q2proto/` files were imported or modified.

## Runtime Entity Snapshot Coverage

`BotRuntimeBuildEntitySnapshot()` now maps the remaining high-level WORR entity
families into explicit BotLib adapter entity types:

- Items, powerups, and ammo keep the pickup `item` path.
- Dropped items are split from regular map pickups using dropped-item spawn
  flags.
- Hazards include trap-danger entities, projectiles, missile movement, and
  bouncing hazards.
- Doors, platforms, and other movers keep the mover path.
- CTF and neutral flag/objective entities are split into an objective snapshot
  type, including dropped objective entities.

The loaded AAS debug status now reports current per-frame counts for items,
dropped items, hazards, movers, and objectives in addition to players, bots,
spectators, and monsters/NPCs. That keeps `AAS_UpdateEntity` coverage visible
without requiring raw entity dumps during behavior work.

## Expensive Check Scheduling

`bot_nav.*` now staggers item desirability refreshes per bot. A route slot keeps
a cached item-goal candidate with spawn-count validation, reservation checks,
blacklist checks, and active-pickup validation. Bots reuse the cached candidate
until their next staggered desirability frame, then rescan and refresh the
cache.

Route recomputation already had cadence and refresh windows; this round exposes
dedicated status counters for rate-limit checks, reuses, and refreshes. The new
`q3a_bot_route_schedule_status` line keeps these counters out of the oversized
frame-command diagnostic while preserving machine-readable evidence for future
scenario gates.

## Aim Knowledge Fairness

The first fairness constraint is closed by the existing live aim path rather
than a duplicate gate:

- `Bot_PerceptionEnrichActionContext()` builds combat context only from the
  bot blackboard's current enemy snapshot.
- `Bot_CommandKnownVisibleEnemy()` only returns a target when the current
  blackboard enemy is valid, alive, and currently visible.
- `BotCombat_EvaluateAimPolicy()` refuses aim or fire without an enemy, without
  visibility, outside FOV, or without shootability.
- `BotCombat_BuildLiveAimDecision()` applies the same visible/shootable checks
  when the aim policy helper is disabled.

Together, bots can aim or fire only at entities represented by their current
perception facts. Last-seen memory remains useful for route and blackboard
state, but it does not grant an omniscient live aim target.

The remaining skill/omniscience checklist row stays open because it needs a
broader audit that every future skill knob changes reaction, accuracy, burst,
FOV, turn, and noise behavior without adding hidden knowledge.

## Validation

- `meson compile -C builddir-win sgame_x86_64`
- `git diff --check`
