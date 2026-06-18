# Q3A BotLib Nav Natural Ladder Travel Goal

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T12`, `FR-04-T13`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice extends the smoke-backed natural travel-type route goal path with map-backed `TRAVEL_LADDER` validation. The earlier natural travel-goal proof covered `TRAVEL_JUMP`; this round proves the same packaged `mm-rage.aas` route-goal mechanism can select a real ladder reachability and drive the normal movement-state command path without setting `sg_bot_frame_command_smoke_travel_type`.

## Implementation

- Extended the server frame-command smoke travel-type goal switch so mode `10` participates in the one-bot natural travel-goal path.
- Mapped `sv_bot_frame_command_smoke 10` to AAS travel type `6` (`TRAVEL_LADDER`), reusing `sg_bot_nav_travel_type_goal` and the smoke-only `sg_bot_nav_travel_type_goal_warp` guard.
- Kept the validation on the existing WORR-native BotLib adapter and route-request path; no new upstream Q3A files were imported.

## Validation

- `meson compile -C builddir-win`
- `python .\tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas`
- Natural ladder smoke on `mm-rage` with `sv_bot_frame_command_smoke 10` reports `q3a_bot_frame_command_smoke_travel_type_goal=6`, `travel_type_goal_start_warps=1`, `last_travel_type_goal_start_type=6`, `last_travel_type_goal_start_area=142`, `last_travel_type_goal_start_goal_area=143`, `travel_type_goal_requests=2`, `travel_type_goal_resolved=2`, `travel_type_goal_assignments=1`, `travel_type_goal_cache_reuses=6`, `last_reachability_type=6`, `movement_state_ladder_commands=8`, `last_movement_state_forced_travel_type=0`, `route_failures=0`, and `pass=1`.
- Normal regression smoke on `mm-rage` with `sv_bot_frame_command_smoke 3` reports `travel_type_goal_requests=0`, `travel_type_goal_start_warps=0`, `movement_state_commands=0`, `route_failures=0`, and `pass=1`.

## Follow-Up

- Natural crouch, swim, and waterjump validation still need deterministic map-backed start cases.
- Door/trigger retry and higher-level behavior policy remain outside this route-goal smoke slice.
