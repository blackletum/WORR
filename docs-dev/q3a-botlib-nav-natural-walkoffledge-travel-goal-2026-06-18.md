# Q3A BotLib Nav Natural Walk-Off-Ledge Travel Goal

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T12`, `FR-04-T13`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice extends the smoke-backed natural travel-type route-goal path with a route-only `TRAVEL_WALKOFFLEDGE` proof. Jump and ladder travel goals already validate movement-state button intent; walk-off-ledge is different because the correct command path is ordinary forward route following with no special button bit.

## Implementation

- Added `sv_bot_frame_command_smoke 11` as a one-bot natural travel-type route-goal smoke for AAS travel type `7` (`TRAVEL_WALKOFFLEDGE`).
- Split natural travel-type smoke pass logic so travel types with explicit movement-state commands still require their button counters, while route-only travel types require selected reachability, route-goal assignment, and route-command execution.
- Added a direct reachability-goal fallback in the Q3A BotLib import bridge so travel-type route requests can try a source area's matching reachability endpoint when the broader area scan does not select the requested travel type.

No new upstream source files were imported. This is WORR-native server smoke harness, bot-brain status, and BotLib adapter bridge work over the already imported Q3A AAS route-query implementation.

## Validation

- `meson compile -C builddir-win`
- `python .\tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas`
- Natural walk-off-ledge smoke on `mm-rage` with `sv_bot_frame_command_smoke 11` reports `q3a_bot_frame_command_smoke_travel_type_goal=7`, `travel_type_goal_start_warps=1`, `last_travel_type_goal_start_type=7`, `last_travel_type_goal_start_area=29`, `last_travel_type_goal_start_goal_area=34`, `travel_type_goal_requests=2`, `travel_type_goal_resolved=2`, `travel_type_goal_assignments=1`, `travel_type_goal_cache_reuses=6`, `last_travel_type_goal_type=7`, `last_reachability_type=7`, `route_commands=8`, `movement_state_commands=0`, `last_movement_state_forced_travel_type=0`, `route_failures=0`, and `pass=1`.
- Normal regression smoke on `mm-rage` with `sv_bot_frame_command_smoke 3` reports `travel_type_goal_requests=0`, `travel_type_goal_start_warps=0`, `movement_state_commands=0`, `route_failures=0`, and `pass=1`.
- Natural ladder regression smoke on `mm-rage` with `sv_bot_frame_command_smoke 10` still reports `last_reachability_type=6`, `movement_state_ladder_commands=8`, `route_failures=0`, and `pass=1`.

## Follow-Up

- Natural crouch, swim, and waterjump validation still need deterministic map-backed start cases.
- Natural barrier-jump validation is now covered by `docs-dev/q3a-botlib-nav-natural-barrierjump-travel-goal-2026-06-18.md`.
- Full door/elevator cooperation remains pending above route-only AAS reachability validation.
