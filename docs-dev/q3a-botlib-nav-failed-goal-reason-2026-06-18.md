# Q3A BotLib Nav Failed Goal Reason

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice adds explicit failed-goal reason telemetry to the WORR-native `bot_nav` route goal layer. The existing goal clear reason only reported the last clear event; this update records whether the clear represented a failed navigation goal, which bot produced it, and which goal identity was abandoned.

The first failed-goal reasons are deliberately small and tied to behavior already implemented: route fallback, item unavailable, and item blacklisted. Reached goals and resets do not count as failures.

## Implementation

- Added `BotNavFailedGoalReason` values for route fallback, item unavailable, and blacklisted item goals.
- Added per-route-slot failed-goal state so the live route debug label can show the last failure reason for that bot.
- Added global `BotNavRouteStatus` fields for failed-goal event count, last failed reason, client, area, item entity, and item id.
- Recorded failed-goal details before persistent goal/item identity is cleared, preserving the abandoned goal context.
- Extended `q3a_bot_frame_command_status` with `failed_goal_events`, `last_failed_goal_reason`, `last_failed_goal_client`, `last_failed_goal_area`, `last_failed_goal_entity`, and `last_failed_goal_item`.

No new upstream Q3A source files were imported. The work is WORR-native diagnostic state over the existing BotLib/AAS adapter and route-goal policy.

## Validation

- `meson compile -C builddir-win`
- `python .\tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas`
- Normal two-bot smoke:
  - `.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set net_port 27915 +set logfile 1 +set logfile_name q3a_bot_nav_failed_goal_reason_normal_smoke +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sg_bot_debug_route 1 +set sg_bot_debug_goal 1 +set sv_bot_frame_command_smoke 3 +map mm-rage`
  - Result: `frames=17`, `commands=17`, `route_failures=0`, `failed_goal_events=0`, `last_failed_goal_reason=0`, `last_failed_goal_client=-1`, `last_failed_goal_entity=-1`, `pass=1`.
- Forced stalled-command smoke:
  - `.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set net_port 27915 +set logfile 1 +set logfile_name q3a_bot_nav_failed_goal_reason_forced_smoke +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sg_bot_debug_route 1 +set sg_bot_debug_goal 1 +set sv_bot_frame_command_smoke 4 +map mm-rage`
  - Result: `frames=29`, `commands=29`, `route_failures=0`, `stuck_detections=2`, `item_goal_blacklist_activations=2`, `failed_goal_events=2`, `last_goal_clear_reason=5`, `last_failed_goal_reason=3`, `last_failed_goal_client=1`, `last_failed_goal_area=251`, `last_failed_goal_entity=74`, `last_failed_goal_item=2`, `pass=1`.

## Follow-Up

- Door/trigger retry can now record a distinct failure or retry reason instead of relying on generic stuck state.
- Route fallback can be expanded later to distinguish AAS generator gaps from dynamic obstruction, once the adapter exposes enough detail.
