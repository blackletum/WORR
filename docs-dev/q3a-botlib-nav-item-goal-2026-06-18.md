# Q3A BotLib Nav Item Goal Slice

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice gives `bot_nav` its first native item-selected route goal. Instead of always accepting the imported fallback goal scan, a bot now scans active WORR pickup entities, scores basic pickup utility, resolves the selected pickup origin to a Q3A AAS area, and requests that area as the persistent route goal.

This is intentionally still a navigation slice, not the full item/weapon brain. It proves the plumbing for live entity-selected goals so later behavior can replace the simple utility score with a richer bot brain and reservation layer.

## Implementation Notes

- `Q3A_BotLibImport_FindRouteAreaForPoint()` exposes the existing imported AAS point-to-route-area helper through the WORR-owned import bridge.
- `BotLibAdapter_FindRouteAreaForPoint()` keeps that lookup behind `botlib_adapter.*` so `bot_nav` does not reach into Q3A globals.
- `bot_nav.*` now scans active pickup entities after the client slots, filters hidden/no-touch/non-trigger pickups, and gives first-pass utility to health, armor, ammo, weapons, powerups, and high-value items.
- Route slots now remember the selected item entity number, entity spawn count, item id, and AAS goal area. The goal clears and invalidates the cached route if the pickup disappears, respawn-hides, changes slot identity, or stops being an active pickup.
- `q3a_bot_frame_command_status` now reports item-goal scan/candidate/assignment/reuse/clear counters plus the last selected item entity, item id, area, and score.
- The route debug label includes the selected item entity number when a cached route is drawn.

No new upstream source files were imported for this slice. The Q3A import-wrapper change is WORR-native adapter code around already imported AAS sample/route functionality.

## Validation

Commands run:

```text
meson compile -C builddir-win
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas
.\.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set net_port 27915 +set logfile 1 +set logfile_name q3a_bot_nav_item_goal_smoke +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sg_bot_debug_route 1 +set sg_bot_debug_goal 1 +set sv_bot_frame_command_smoke 2 +map mm-rage
```

Results:

- Build passed. Ninja still reports the pre-existing `premature end of file; recovering` warning.
- Refresh install passed and packaged `maps/mm-rage.aas` into `.install/basew/pak0.pkz`.
- Dedicated smoke passed with `frames=8`, `commands=8`, `route_queries=2`, `route_reuses=6`, `route_goal_requests=2`, `route_goal_assignments=1`, `item_goal_scans=1`, `item_goal_candidates=45`, `item_goal_assignments=1`, `item_goal_reuses=7`, `item_goal_clears=0`, `last_persistent_goal_area=415`, `last_item_goal_entity=32`, `last_item_goal_area=415`, `last_item_goal_item=53`, `last_item_goal_score=828`, `route_failures=0`, and `pass=1`.

## Follow-Up

- Move goal scoring and ownership into `bot_brain.*` once the first blackboard/perception slice exists.
- Add item reservation so multiple bots do not converge on the same pickup by default.
- Add failed-item-goal reason counters and blacklist cooldowns for unreachable or repeatedly contested pickups.
