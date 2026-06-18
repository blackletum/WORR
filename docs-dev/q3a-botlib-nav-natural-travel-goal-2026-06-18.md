# Q3A BotLib Nav Natural Travel Goal

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T12`, `FR-04-T13`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice adds a smoke-backed natural travel-type route goal for the WORR/Q3A BotLib AAS path. The previous movement-state smokes proved jump, crouch, and swim button translation through forced test travel types; this slice proves real `TRAVEL_JUMP`, `TRAVEL_LADDER`, `TRAVEL_WALKOFFLEDGE`, `TRAVEL_ELEVATOR`, and `TRAVEL_BARRIERJUMP` reachabilities selected from the packaged `mm-rage.aas` can drive either movement-state button intent or route-only travel validation without the forced override cvar.

## Implementation

- Added `Q3A_BotLibImport_BuildRouteSteerForTravelType()` and `BotLibAdapter_BuildRouteSteerForTravelType()` to find a route whose selected next reachability matches a requested AAS travel type.
- Added `Q3A_BotLibImport_FindRouteStartForTravelType()` and the adapter wrapper so smoke code can locate an AAS start area whose normal route result begins with the requested travel type.
- Added a direct reachability-goal fallback for travel-type routing so a source area's matching reachability endpoint can be tried when broad area scanning does not select the requested travel type.
- Extended the direct fallback to build a one-step route result from a matching outgoing reachability and to clamp smoke start candidates inside the owner area before validating they still map back to that route area.
- Extended `BotNavRouteRequest` and route status with travel-type goal counters, assignment/cache tracking, and last travel-type goal fields.
- Added `sg_bot_nav_travel_type_goal` as an internal route-request cvar and `sg_bot_nav_travel_type_goal_warp` as an explicit smoke-only start warp guard.
- Added `sv_bot_frame_command_smoke 9`, which requests `TRAVEL_JUMP`, warps the smoke bot once to a validated jump-capable AAS area, and verifies natural movement-state jump commands with `sg_bot_frame_command_smoke_travel_type` left at `0`.
- Added `sv_bot_frame_command_smoke 10`, which requests `TRAVEL_LADDER`, warps the smoke bot once to a validated ladder-capable AAS area, and verifies natural ladder movement-state command accounting with the forced travel-type cvar still left at `0`.
- Added `sv_bot_frame_command_smoke 11`, which requests `TRAVEL_WALKOFFLEDGE`, warps the smoke bot once to a validated walk-off-ledge-capable AAS area, and verifies route-only natural travel-type goal execution without requiring movement-state button counters.
- Added `sv_bot_frame_command_smoke 12`, which requests `TRAVEL_ELEVATOR`, warps the smoke bot once to a validated elevator-capable AAS area, and verifies route-only natural travel-type goal execution without requiring mover-specific command policy.
- Added `sv_bot_frame_command_smoke 13`, which requests `TRAVEL_BARRIERJUMP`, warps the smoke bot once to the validated barrier-jump-capable AAS area, and verifies natural jump-button command intent from the real reachability.

No new upstream source files were imported. This is WORR-native adapter, nav, bot-brain, and smoke harness work over the already imported Q3A AAS route-query code.

## Validation

- `meson compile -C builddir-win`
- `python .\tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas`
- Natural jump smoke on `mm-rage` with `sv_bot_frame_command_smoke 9` reports `travel_type_goal_start_warps=1`, `last_travel_type_goal_start_area=107`, `last_travel_type_goal_start_goal_area=111`, `travel_type_goal_requests=2`, `travel_type_goal_resolved=2`, `travel_type_goal_assignments=1`, `travel_type_goal_cache_reuses=6`, `last_travel_type_goal_type=5`, `last_reachability_type=5`, `movement_state_jump_commands=8`, `last_movement_state_forced_travel_type=0`, `route_failures=0`, and `pass=1`.
- Natural ladder smoke on `mm-rage` with `sv_bot_frame_command_smoke 10` reports `travel_type_goal_start_warps=1`, `last_travel_type_goal_start_type=6`, `last_travel_type_goal_start_area=142`, `last_travel_type_goal_start_goal_area=143`, `travel_type_goal_requests=2`, `travel_type_goal_resolved=2`, `travel_type_goal_assignments=1`, `travel_type_goal_cache_reuses=6`, `last_travel_type_goal_type=6`, `last_reachability_type=6`, `movement_state_ladder_commands=8`, `last_movement_state_forced_travel_type=0`, `route_failures=0`, and `pass=1`.
- Natural walk-off-ledge smoke on `mm-rage` with `sv_bot_frame_command_smoke 11` reports `travel_type_goal_start_warps=1`, `last_travel_type_goal_start_type=7`, `last_travel_type_goal_start_area=29`, `last_travel_type_goal_start_goal_area=34`, `travel_type_goal_requests=2`, `travel_type_goal_resolved=2`, `travel_type_goal_assignments=1`, `travel_type_goal_cache_reuses=6`, `last_travel_type_goal_type=7`, `last_reachability_type=7`, `route_commands=8`, `movement_state_commands=0`, `last_movement_state_forced_travel_type=0`, `route_failures=0`, and `pass=1`.
- Natural elevator smoke on `mm-rage` with `sv_bot_frame_command_smoke 12` reports `travel_type_goal_start_warps=1`, `last_travel_type_goal_start_type=11`, `last_travel_type_goal_start_area=241`, `last_travel_type_goal_start_goal_area=261`, `travel_type_goal_requests=2`, `travel_type_goal_resolved=2`, `travel_type_goal_assignments=1`, `travel_type_goal_cache_reuses=6`, `last_travel_type_goal_type=11`, `last_reachability_type=11`, `route_commands=8`, `movement_state_commands=0`, `movement_state_unsupported=0`, `last_movement_state_forced_travel_type=0`, `route_failures=0`, and `pass=1`.
- Natural barrier-jump smoke on `mm-rage` with `sv_bot_frame_command_smoke 13` reports `travel_type_goal_start_warps=1`, `last_travel_type_goal_start_type=4`, `last_travel_type_goal_start_area=292`, `last_travel_type_goal_start_goal_area=318`, `travel_type_goal_requests=8`, `travel_type_goal_resolved=8`, `travel_type_goal_assignments=8`, `last_travel_type_goal_type=4`, `last_reachability=319`, `last_reachability_type=4`, `movement_state_jump_commands=8`, `last_movement_state_forced_travel_type=0`, `route_failures=0`, and `pass=1`.
- Normal regression smoke with `sv_bot_frame_command_smoke 3` reports `travel_type_goal_requests=0`, `travel_type_goal_start_warps=0`, `movement_state_commands=0`, `route_failures=0`, and `pass=1`.
- Forced jump regression smoke with `sv_bot_frame_command_smoke 5` reports `movement_state_jump_commands=17`, `last_movement_state_forced_travel_type=5`, `travel_type_goal_requests=0`, `route_failures=0`, and `pass=1`.
- Position-goal regression smoke with `sv_bot_frame_command_smoke 8` reports `position_goal_requests=8`, `travel_type_goal_requests=0`, `travel_type_goal_start_warps=0`, `route_failures=0`, and `pass=1`.
- Stalled-command regression smoke with `sv_bot_frame_command_smoke 4` reports `stuck_detections=2`, `failed_goal_events=2`, `recovery_command_uses=11`, `travel_type_goal_requests=0`, `route_failures=0`, and `pass=1`.

## Follow-Up

- Natural crouch, swim, and waterjump validation still need deterministic map-backed start cases.
- Full door/elevator cooperation remains pending above the route-only `TRAVEL_ELEVATOR` AAS reachability proof.
- The smoke start warp is intentionally guarded by `sg_bot_nav_travel_type_goal_warp`; normal travel-type route requests do not move entities.
