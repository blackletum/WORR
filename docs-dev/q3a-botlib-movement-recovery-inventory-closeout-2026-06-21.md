# Q3A BotLib Movement, Recovery, and Inventory Closeout

Date: 2026-06-21

Tasks: `FR-04-T02`, `FR-04-T14`, `FR-04-T15`, `DV-07-T06`

## Summary

This round closes the next ten non-template rows in the Q3A BotLib/AAS port
checklist. It combines a small behavior patch with a documentation/status pass
over already-implemented navigation and action-dispatcher work.

## Implementation Notes

- `bot_combat.*` now consumes the existing skill aim-error and tracking-noise
  policy in live aim decisions. Reaction, FOV, turn/settle, burst, projectile
  lead, and visible/shootable target gates still decide whether a bot may aim or
  fire; skill error now offsets the chosen aim point by a deterministic bounded
  amount rather than granting perfect aim.
- `bot_brain.*` recognizes Q3A `TRAVEL_TELEPORT` reachability as a route-only
  traversal type instead of counting it as unsupported movement-state input.
  Teleporter traversal remains route-owned: the bot follows the AAS step into
  the teleporter volume without inventing extra button presses.
- `bot_brain.*` adds default-off controlled inactive recovery through
  `sg_bot_controlled_inactive_recovery`. Mode `1` emits an attack command for
  dead, non-eliminated bots so the normal respawn path can handle them. Mode `2`
  may also force a spectator bot back to a playing team through the existing
  `SetTeam` policy. The feature is disabled by default and reports a compact
  `q3a_bot_controlled_recovery_status` line.
- Existing `bot_nav.*` interaction retry support already covers route-detected
  door/platform/train/trigger/mover wait/use retry windows and exposes the
  interaction action, kind, entity, distance, move-state, and stuck-activation
  counters used by the coop interaction and door/elevator command owners.
- Existing `bot_actions.*` and `bot_brain.*` inventory dispatch already use the
  WORR-native action dispatcher and exact `use_index_only` command request path.
  The removed Q2R `Bot_UseItem` callback remains out of the active bot surface.
- Broader command ownership is now wired through blackboard perception facts,
  route goals, item/inventory policy, live aim decisions, timed route owners,
  FFA/TDM/CTF role-combat owners, coop command owners, and inventory-owned
  retreat/escape route activations. The remaining behavior work is breadth and
  campaign specificity, not the dispatcher boundary itself.

## Validation

- `meson compile -C builddir-win sgame_x86_64` passed.

## Checklist Rows Closed

- Skill affects reaction and accuracy, not omniscience.
- Implement movement states.
- Ladder if supported by map metadata.
- Door/plat wait/use.
- Teleporter traversal.
- Add stuck recovery.
- Door/trigger retry.
- Last-resort respawn/spectator handling only in debug or controlled modes.
- Add inventory use through a new WORR bot action dispatcher.
- Promote dispatcher decisions into broader command ownership.

No new Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto source
files were imported or modified for this round. The code changes are
WORR-native bot behavior, status, and documentation work above the existing
BotLib/AAS boundary.
