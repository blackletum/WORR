# Q3A BotLib Nav Rocket-Jump Route Policy

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T12`, `FR-04-T13`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice adds an explicit runtime policy gate for inherited Q3A/BSPC rocket-jump reachability. The Q2 AAS generator can already emit `TRAVEL_ROCKETJUMP` candidates on `mm-rage`, but normal bot routing now keeps those candidates disabled unless the server opts in with `sg_bot_allow_rocketjump 1`.

No new upstream source files were imported. The changes are WORR-native adapter, navigation-policy, and smoke-harness work over the already imported Q3A AAS route query code.

## Implementation

- `Q3A_BotLibImport_SetRoutePolicy()` now stores the current route policy in the Q3A import bridge.
- Normal route queries use `TFL_DEFAULT` by default and add `TFL_ROCKETJUMP` only while `sg_bot_allow_rocketjump` is enabled.
- Travel-type helper paths, including direct reachability fallback, now reject requested travel types that are not allowed by the active route policy. This prevents internal smoke/debug routes from bypassing the same rocket-jump gate used by normal route selection.
- `BotNavBuildRouteWithFallback()` refreshes the route policy from `sg_bot_allow_rocketjump` before each route query.
- The frame-command smoke harness adds:
  - Mode `14`: opt-in `TRAVEL_ROCKETJUMP` route validation with `sg_bot_allow_rocketjump 1`.
  - Mode `15`: default-blocked `TRAVEL_ROCKETJUMP` route validation with `sg_bot_allow_rocketjump 0`.
- `TRAVEL_ROCKETJUMP` is treated as route-only command validation for now. Real rocket-jump action policy, weapon selection, aim point selection, attack timing, and self-damage risk remain future behavior work under `FR-04-T15`.

## Validation

- `meson compile -C builddir-win`
- `python .\tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas`
- `sv_bot_frame_command_smoke 14` on `mm-rage` with `sg_bot_allow_rocketjump 1`:
  - `travel_type_goal_requests=2`
  - `travel_type_goal_resolved=2`
  - `travel_type_goal_assignments=1`
  - `travel_type_goal_start_warps=1`
  - `last_travel_type_goal_type=12`
  - `last_travel_type_goal_start_area=282`
  - `last_travel_type_goal_start_goal_area=304`
  - `last_reachability=312`
  - `last_reachability_type=12`
  - `route_failures=0`
  - `pass=1`
- `sv_bot_frame_command_smoke 15` on `mm-rage` with the default rocket-jump policy:
  - `travel_type_goal_expect_blocked=1`
  - `commands=0`
  - `route_commands=0`
  - `travel_type_goal_requests=8`
  - `travel_type_goal_resolved=0`
  - `travel_type_goal_assignments=0`
  - `travel_type_goal_start_warps=0`
  - `route_failures=8`
  - `pass=1`
- Regression smokes after the policy gate:
  - Normal `sv_bot_frame_command_smoke 3` on `mm-rage`: `commands=17`, `route_failures=0`, `pass=1`.
  - Natural `TRAVEL_ELEVATOR` `sv_bot_frame_command_smoke 12`: `last_reachability_type=11`, `route_commands=8`, `route_failures=0`, `pass=1`.
  - Natural `TRAVEL_BARRIERJUMP` `sv_bot_frame_command_smoke 13`: `last_reachability_type=4`, `movement_state_jump_commands=8`, `route_failures=0`, `pass=1`.

## Follow-Ups

- Implement a real rocket-jump action dispatcher before bots use rocket-jump routes during normal play.
- Tie rocket-jump route use to weapon/ammo availability, splash-risk estimation, vertical target selection, and skill policy.
- Add user-facing cvar documentation when rocket-jump routing/action behavior becomes supported outside the current developer smoke path.
