# Q3A BotLib Nav Eight-Bot Frame Command Smoke

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T13`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice adds an eight-bot frame-command validation mode for the native WORR/Q3A BotLib route path. The goal is to push the staged dedicated-server smoke beyond the four-bot reservation proof and confirm that eight fake-client bots can enter through the normal bot slot lifecycle, hold active item reservations, build route-steered commands, draw route/goal debug output, and cleanly remove themselves at the end of the run.

No new upstream source files were imported. This is WORR-native dedicated-server smoke-harness work over the existing bot lifecycle, item-goal, item-reservation, route-cache, stuck-recovery, debug overlay, and frame-command paths.

## Implementation

- `sv_bot_frame_command_smoke 17` now targets up to eight public bot slots.
- The smoke target helper checks mode `17` before mode `16`, keeping the prior four-bot validation mode stable.
- The smoke bot-name helper now covers `B|Mover` through `B|MoverEight`, letting the existing one-bot-per-frame add loop grow the target from one to eight without adding another special-case loop.
- The existing status pass contract is reused with `expected_min_frames=8` and `expected_min_commands=8`, so the smoke verifies every active bot can produce routed commands.
- Position-goal, travel-type-goal, stalled-command, two-bot, and four-bot smoke modes keep their existing target semantics.

## Validation

- `meson compile -C builddir-win`
- `python .\tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas`
- `sv_bot_frame_command_smoke 17` on `mm-rage`:
  - `q3a_bot_frame_command_smoke_multi_bot_target=8`
  - Added `B|Mover`, `B|MoverTwo`, `B|MoverThree`, `B|MoverFour`, `B|MoverFive`, `B|MoverSix`, `B|MoverSeven`, and `B|MoverEight`.
  - `frames=92`
  - `commands=92`
  - `route_requests=92`
  - `route_queries=29`
  - `route_refreshes=29`
  - `route_reuses=63`
  - `route_commands=92`
  - `route_failures=0`
  - `route_goal_assignments=11`
  - `item_goal_assignments=11`
  - `item_goal_reservation_skips=49`
  - `item_goal_active_reservations=8`
  - `item_goal_blacklist_activations=3`
  - `failed_goal_events=3`
  - `stuck_detections=3`
  - `stuck_repath_refreshes=3`
  - `stuck_recovery_activations=3`
  - `route_debug_routes=92`
  - `route_debug_goals=92`
  - `route_debug_lines=497`
  - `route_debug_polyline_segments=589`
  - `expected_min_frames=8`
  - `expected_min_commands=8`
  - `pass=1`
- Four-bot `sv_bot_frame_command_smoke 16` regression on `mm-rage`:
  - `q3a_bot_frame_command_smoke_multi_bot_target=4`
  - `frames=38`
  - `commands=38`
  - `route_commands=38`
  - `route_failures=0`
  - `item_goal_reservation_skips=6`
  - `item_goal_active_reservations=4`
  - `expected_min_frames=4`
  - `expected_min_commands=4`
  - `pass=1`

## Follow-Ups

- Add a map-change repeat smoke so the eight-bot path proves level shutdown/reload cleanup under load.
- Add CPU budget telemetry for eight active bot command frames before raising default public-bot expectations.
