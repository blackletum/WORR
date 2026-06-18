# Q3A BotLib Nav Natural Elevator Travel Goal

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T12`, `FR-04-T13`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice extends the smoke-backed natural travel-type route-goal path with a route-only `TRAVEL_ELEVATOR` proof. This validates that the packaged `mm-rage.aas` elevator reachability can be selected as the next route step without forcing `sg_bot_frame_command_smoke_travel_type`; it does not yet implement full mover wait/use cooperation.

## Implementation

- Added `sv_bot_frame_command_smoke 12` as a one-bot natural travel-type route-goal smoke for AAS travel type `11` (`TRAVEL_ELEVATOR`).
- Classified `TRAVEL_ELEVATOR` as an explicit route-only command path in `bot_brain.cpp`, avoiding unsupported movement-state accounting while still requiring route-goal assignment and route-command execution.
- Reused the existing travel-type start warp and direct reachability-goal fallback to select the packaged `mm-rage.aas` elevator candidate deterministically.

No new upstream source files were imported. This is WORR-native server smoke harness and bot-brain command-status work over the already imported Q3A AAS route-query implementation.

## Validation

- `meson compile -C builddir-win`
- `python .\tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas`
- Natural elevator smoke on `mm-rage` with `sv_bot_frame_command_smoke 12` reports `q3a_bot_frame_command_smoke_travel_type_goal=11`, `travel_type_goal_start_warps=1`, `last_travel_type_goal_start_type=11`, `last_travel_type_goal_start_area=241`, `last_travel_type_goal_start_goal_area=261`, `travel_type_goal_requests=2`, `travel_type_goal_resolved=2`, `travel_type_goal_assignments=1`, `travel_type_goal_cache_reuses=6`, `last_travel_type_goal_type=11`, `last_reachability_type=11`, `route_commands=8`, `movement_state_commands=0`, `movement_state_unsupported=0`, `last_movement_state_forced_travel_type=0`, `route_failures=0`, and `pass=1`.
- Normal regression smoke on `mm-rage` with `sv_bot_frame_command_smoke 3` reports `travel_type_goal_requests=0`, `travel_type_goal_start_warps=0`, `movement_state_commands=0`, `movement_state_unsupported=0`, `route_failures=0`, and `pass=1`.
- Natural walk-off-ledge regression smoke on `mm-rage` with `sv_bot_frame_command_smoke 11` still reports `last_reachability_type=7`, `route_commands=8`, `movement_state_commands=0`, `movement_state_unsupported=0`, `route_failures=0`, and `pass=1`.

## Follow-Up

- Natural crouch, swim, and waterjump validation still need deterministic map-backed start cases.
- Natural barrier-jump validation is now covered by `docs-dev/q3a-botlib-nav-natural-barrierjump-travel-goal-2026-06-18.md`.
- Full door/elevator cooperation remains pending above this route-only AAS reachability proof.
