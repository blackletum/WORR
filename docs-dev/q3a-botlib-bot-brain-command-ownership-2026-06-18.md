# Q3A BotLib Bot Brain Command Ownership

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice moves the current high-level bot frame command and status implementation out of the public `bot_think.*` surface and into a new WORR-native `bot_brain.*` module. `bot_think.*` remains as the stable lifecycle and server-extension-facing wrapper, while `bot_brain.*` becomes the home for command ownership and later behavior/goal policy growth.

## Implementation

- Added `src/game/sgame/bots/bot_brain.hpp` and moved the existing frame-command/status implementation into `src/game/sgame/bots/bot_brain.cpp`.
- Kept `Bot_BeginFrame`, `Bot_EndFrame`, `Bot_BuildFrameCommand`, and `Bot_FrameCommandPrintStatus` available through `bot_think.cpp` as thin wrappers around `BotBrain_*` functions.
- Added `bot_brain.hpp` to the bot umbrella include and added `bot_brain.cpp` to the `sgame` Meson source list.
- Preserved the server/game extension ABI and the existing `sv_bot_frame_command_smoke` command paths; no caller outside the bot module needs to know that command ownership moved.
- No new upstream source files were imported. This is WORR-native organization around the existing BotLib/AAS adapter and nav command path.

## Validation

- `meson compile -C builddir-win`
- `python .\tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas`
- Normal refreshed-install smoke on `mm-rage` with `sv_bot_frame_command_smoke 3` reports `frames=17`, `commands=17`, `route_failures=0`, `movement_state_attempts=17`, `movement_state_commands=0`, and `pass=1`.
- Forced jump smoke with `sv_bot_frame_command_smoke 5` reports `movement_state_commands=17`, `movement_state_jump_commands=17`, `last_movement_state_forced_travel_type=5`, and `pass=1`.
- Stalled-command regression smoke with `sv_bot_frame_command_smoke 4` reports `stuck_detections=2`, `failed_goal_events=2`, `recovery_command_uses=11`, `route_failures=0`, and `pass=1`.
