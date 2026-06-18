# Q3A BotLib Nav Item Reservation Slice

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T14`, `FR-04-T15`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice adds the first native item reservation policy above the active-pickup route-goal path. When a bot scans pickups for a new item goal, it now skips active pickups already selected by another bot's live route slot, then continues scoring the remaining candidates.

The goal is intentionally modest: avoid obvious two-bot pileups without introducing the full bot brain, timed item model, or contested-goal negotiation yet.

## Implementation Notes

- `bot_nav.*` now treats a route slot with an available selected pickup as an active item reservation.
- `BotNavFindPickupGoal()` skips pickup entities reserved by another bot and records reservation skip counters plus the skipped entity and owning client.
- `BotNav_GetRouteStatus()` refreshes the active reservation count before smoke/status output.
- Bot disconnect now resets that client's nav route slot through `BotNav_ResetClient()` so reservations do not survive removed bot clients.
- `sv_bot_frame_command_smoke 3` adds two bots before requesting frame-command status; mode `2` remains the one-bot quick smoke.
- `q3a_bot_frame_command_status` now reports `item_goal_reservation_skips`, `item_goal_active_reservations`, `last_item_goal_reserved_entity`, and `last_item_goal_reserved_by_client`.

No new upstream source files were imported for this slice. This is WORR-native reservation policy layered over the existing BotLib/AAS route and item-goal adapter path.

## Validation

Commands run:

```text
meson compile -C builddir-win
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas
.\.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set net_port 27915 +set logfile 1 +set logfile_name q3a_bot_nav_item_reservation_smoke +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sg_bot_debug_route 1 +set sg_bot_debug_goal 1 +set sv_bot_frame_command_smoke 3 +map mm-rage
```

Results:

- Build passed. Ninja still reports the pre-existing `premature end of file; recovering` warning.
- Refresh install passed, rebuilt `.install`, and packaged `maps/mm-rage.aas` into `.install/basew/pak0.pkz`.
- Dedicated two-bot smoke passed with `frames=17`, `commands=17`, `route_queries=5`, `route_reuses=12`, `route_goal_assignments=2`, `item_goal_scans=2`, `item_goal_candidates=89`, `item_goal_assignments=2`, `item_goal_reuses=15`, `item_goal_reservation_skips=1`, `item_goal_active_reservations=2`, `last_item_goal_reserved_entity=32`, `last_item_goal_reserved_by_client=0`, `last_item_goal_entity=74`, `last_item_goal_area=251`, `last_item_goal_item=2`, `last_persistent_goal_area=251`, `route_failures=0`, and `pass=1`.

## Follow-Up

- Add reservation expiry and contested-goal cooldowns once failed-goal/stuck reasons exist.
- Move item ownership into `bot_brain.*` when the blackboard/perception layer is introduced.
- Replace first-pass utility scoring with item timing, role, weapon, and tactical context.
