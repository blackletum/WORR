# Q3A BotLib Nav Four-Bot Frame Command Smoke

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T13`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice adds a four-bot frame-command validation mode for the native WORR/Q3A BotLib route path. The goal is to prove that the staged dedicated server can add four fake-client bots, assign separate active-pickup route goals through the existing item reservation policy, build route-steered commands for all of them, and cleanly remove them at the end of the smoke.

No new upstream source files were imported. This is WORR-native server smoke-harness work over the existing bot slot lifecycle, route cache, item-goal, and frame-command paths.

## Implementation

- `sv_bot_frame_command_smoke 16` now targets up to four public bot slots.
- The smoke harness now uses a small bot-name helper and can add extra smoke bots one per server frame until the target count is reached.
- Multi-bot smoke setup prints `q3a_bot_frame_command_smoke_multi_bot_target=<count>` when the target is above two bots.
- The existing status pass criteria are reused with `expected_min_frames` and `expected_min_commands` set to the target bot count, so the smoke validates that all four bots can produce routed commands.
- Position-goal, travel-type-goal, stalled-command, and two-bot smoke modes keep their existing target semantics.

## Validation

- `meson compile -C builddir-win`
- `python .\tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas`
- `sv_bot_frame_command_smoke 16` on `mm-rage`:
  - `q3a_bot_frame_command_smoke_multi_bot_target=4`
  - Added `B|Mover`, `B|MoverTwo`, `B|MoverThree`, and `B|MoverFour`.
  - `frames=38`
  - `commands=38`
  - `route_requests=38`
  - `route_queries=11`
  - `route_refreshes=11`
  - `route_reuses=27`
  - `route_commands=38`
  - `route_failures=0`
  - `route_goal_assignments=4`
  - `item_goal_assignments=4`
  - `item_goal_reservation_skips=6`
  - `item_goal_active_reservations=4`
  - `route_debug_routes=38`
  - `route_debug_goals=38`
  - `expected_min_frames=4`
  - `expected_min_commands=4`
  - `pass=1`
- Normal two-bot `sv_bot_frame_command_smoke 3` regression on `mm-rage`:
  - `frames=17`
  - `commands=17`
  - `route_commands=17`
  - `route_failures=0`
  - `item_goal_reservation_skips=1`
  - `item_goal_active_reservations=2`
  - `expected_min_frames=2`
  - `expected_min_commands=2`
  - `pass=1`

## Follow-Ups

- Add a longer soak smoke once route steering and behavior policy are less synthetic.
- Keep the four-bot smoke as the lower-count regression beside the eight-bot validation mode.
