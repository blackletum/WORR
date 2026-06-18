# Q3A BotLib Nav Natural Barrier-Jump Travel Goal

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T12`, `FR-04-T13`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice extends the smoke-backed natural travel-type route-goal path with a map-backed `TRAVEL_BARRIERJUMP` proof. The packaged `mm-rage.aas` contains one barrier-jump reachability whose owner area can be routed around from its center, so the adapter now has a deterministic direct-reachability endpoint path for matching outgoing travel types.

## Implementation

- Added `sv_bot_frame_command_smoke 13` as a one-bot natural travel-type route-goal smoke for AAS travel type `4` (`TRAVEL_BARRIERJUMP`).
- Extended the travel-type route helper with a direct one-step route result when the current AAS area has an outgoing reachability that already matches the requested travel type.
- Extended the travel-type smoke start finder to try the matching reachability start point clamped inside the owner area, validate that the candidate still maps back to that area, then fall back to the owner area center if needed.
- Reused the existing movement-state command mapping where barrier-jump reachability presses `BUTTON_JUMP`.

No new upstream source files were imported. This is WORR-native adapter and smoke harness work over the already imported Q3A AAS route-query implementation.

## Validation

- `meson compile -C builddir-win`
- `python .\tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas`
- Natural barrier-jump smoke on `mm-rage` with `sv_bot_frame_command_smoke 13` reports `q3a_bot_frame_command_smoke_travel_type_goal=4`, `travel_type_goal_start_warps=1`, `last_travel_type_goal_start_type=4`, `last_travel_type_goal_start_area=292`, `last_travel_type_goal_start_goal_area=318`, `travel_type_goal_requests=8`, `travel_type_goal_resolved=8`, `travel_type_goal_assignments=8`, `last_travel_type_goal_type=4`, `last_reachability=319`, `last_reachability_type=4`, `last_reachability_flags=8`, `last_reachability_end_area=318`, `route_commands=8`, `movement_state_jump_commands=8`, `last_movement_state_forced_travel_type=0`, `route_failures=0`, and `pass=1`.
- Normal regression smoke on `mm-rage` with `sv_bot_frame_command_smoke 3` reports `frames=17`, `commands=17`, `route_commands=17`, `travel_type_goal_requests=0`, `travel_type_goal_start_warps=0`, `route_failures=0`, and `pass=1`.
- Natural elevator regression smoke on `mm-rage` with `sv_bot_frame_command_smoke 12` still reports `last_reachability_type=11`, `route_commands=8`, `movement_state_commands=0`, `movement_state_unsupported=0`, `route_failures=0`, and `pass=1`.

## Follow-Up

- Natural crouch, swim, and waterjump validation still need deterministic map-backed start cases.
- Full door/elevator cooperation remains pending above the route-only `TRAVEL_ELEVATOR` AAS reachability proof.
- The smoke start warp remains guarded by `sg_bot_nav_travel_type_goal_warp`; normal travel-type route requests do not move entities.
