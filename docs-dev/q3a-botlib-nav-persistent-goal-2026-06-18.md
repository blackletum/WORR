# Q3A BotLib Nav Persistent Goal Slice

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice gives native `bot_nav` ownership of a persistent route goal area. The first successful route query assigns the returned Q3A goal area to the bot's route slot, later cadence/drift/target refreshes request that same area, and cache reuses keep reporting the active persistent goal.

This is still a position/area goal, not item utility selection. It gives the next item-goal slice a stable native goal owner to plug into instead of relying on the imported route helper's fallback area scan every refresh.

## Implementation Notes

- `BotNavRouteSlot` now stores a `persistentGoalArea` separate from cached route geometry.
- `BotNav_GetRouteSteer()` reuses the persistent goal area for route refreshes until the bot reaches the cached goal origin or the route falls back.
- `BotNavRefreshRoute()` requests the persistent goal area through `BotLibAdapter_BuildRouteSteer()` and falls back to a fresh imported route goal only if the preferred area no longer routes.
- `BotNavRouteStatus` now tracks persistent-goal requests, assignments, cache reuses, clears, fallbacks, the last persistent goal area, and the last clear reason.
- `q3a_bot_frame_command_status` now reports `route_goal_requests`, `route_goal_assignments`, `route_goal_cache_reuses`, `route_goal_clears`, `route_goal_fallbacks`, `last_persistent_goal_area`, and `last_goal_clear_reason`.
- The route debug label now includes the active route goal area.

No new upstream source files were imported for this slice. The work uses the existing preferred-goal-area parameter in the WORR-owned adapter and imported Q3A route builder.

## Validation

Commands run:

```text
meson compile -C builddir-win
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas
.\.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set net_port 27912 +set logfile 1 +set logfile_name q3a_bot_nav_persistent_goal_smoke +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sg_bot_debug_route 1 +set sg_bot_debug_goal 1 +set sv_bot_frame_command_smoke 2 +map mm-rage
```

Results:

- Build passed. Ninja still reports the pre-existing `premature end of file; recovering` warning.
- Refresh install passed and packaged `maps/mm-rage.aas` into `.install/basew/pak0.pkz`.
- Dedicated smoke passed with `frames=8`, `commands=8`, `route_queries=2`, `route_refreshes=2`, `route_reuses=6`, `route_goal_requests=1`, `route_goal_assignments=1`, `route_goal_cache_reuses=6`, `route_goal_clears=0`, `route_goal_fallbacks=0`, `last_persistent_goal_area=227`, `last_goal_clear_reason=0`, `route_failures=0`, and `pass=1`.

## Follow-Up

- Feed item/entity-selected goals into `persistentGoalArea` instead of using only the first imported fallback area.
- Add stuck and failed-goal reason strings to the same smoke/debug status channel.
- Split goal-clear reason counters once runtime recovery starts using them for behavior decisions.
