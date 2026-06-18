# Q3A BotLib Nav Position Goal

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T12`, `FR-04-T13`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice adds a debug/smoke-backed position navigation goal path through the WORR-native bot brain and navigation layers. The path lets `bot_brain.*` request a specific world point, lets `bot_nav.*` resolve that point to an AAS route area, and keeps the exact resolved goal origin through the Q3A BotLib adapter instead of routing only to the area's center.

## Implementation

- Added `BotNavRouteRequest` position-goal input and position-goal counters to `BotNavRouteStatus`.
- Added `sg_bot_nav_position_goal_enable`, `sg_bot_nav_position_goal_x`, `sg_bot_nav_position_goal_y`, and `sg_bot_nav_position_goal_z` as internal command-smoke inputs owned by `bot_brain.cpp`.
- Updated `bot_nav.*` so position goals take priority over item goals, resolve through `BotLibAdapter_FindRouteAreaForPoint()`, remain persistent across cache reuse, and clear when normal non-position routing resumes.
- Added `Q3A_BotLibImport_BuildRouteSteerToGoal()` and `BotLibAdapter_BuildRouteSteerToGoal()` so preferred position routes preserve the resolved goal origin while area-only item and fallback routes keep the previous behavior.
- Added `sv_bot_frame_command_smoke 8` as a one-bot position-goal smoke on `mm-rage`; mode 8 forces the rocket-launcher point `64 -304 82`, which resolves to the usable AAS route point `64 -304 98`.
- No new upstream source files were imported. This is WORR-native adapter and command-policy work over already imported Q3A AAS route queries.

## Validation

- `meson compile -C builddir-win`
- `python .\tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas`
- Position-goal smoke with `sv_bot_frame_command_smoke 8` reports `frames=8`, `commands=8`, `position_goal_requests=8`, `position_goal_resolved=8`, `position_goal_assignments=1`, `position_goal_cache_reuses=6`, `item_goal_scans=0`, `route_goal_fallbacks=0`, `last_position_goal_area=227`, `last_position_goal_x=64`, `last_position_goal_y=-304`, `last_position_goal_z=98`, `route_failures=0`, and `pass=1`.
- Normal regression smoke with `sv_bot_frame_command_smoke 3` reports `frames=17`, `commands=17`, `item_goal_reservation_skips=1`, `item_goal_active_reservations=2`, `position_goal_requests=0`, `route_failures=0`, and `pass=1`.
- Stalled-command regression smoke with `sv_bot_frame_command_smoke 4` reports `stuck_detections=2`, `failed_goal_events=2`, `recovery_command_uses=11`, `position_goal_requests=0`, `route_failures=0`, and `pass=1`.
- Forced jump regression smoke with `sv_bot_frame_command_smoke 5` reports `movement_state_jump_commands=17`, `position_goal_requests=0`, `route_failures=0`, and `pass=1`.
