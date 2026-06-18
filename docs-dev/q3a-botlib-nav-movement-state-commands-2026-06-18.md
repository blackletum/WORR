# Q3A BotLib Nav Movement State Commands

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice teaches the WORR-native bot command builder to translate selected Q3A AAS reachability travel types into Quake II Rerelease `usercmd_t` button intent. Route following already handled yaw and forward movement; this update adds the first movement-state buttons for jump, crouch, swim, and ladder-style vertical intent.

The implementation stays above the BotLib adapter. Imported Q3A route data still arrives as a normalized reachability travel type, and `bot_think.cpp` maps that type onto WORR/Q2 command buttons consumed by the existing pmove code.

## Implementation

- Added movement-state command telemetry to `BotFrameCommandStatus`.
- Added `Bot_CommandApplyMovementState()` in `bot_think.cpp`.
- Mapped AAS crouch reachability to `BUTTON_CROUCH`.
- Mapped AAS barrier jump, jump, and waterjump reachability to `BUTTON_JUMP`.
- Mapped swim reachability to `BUTTON_JUMP` or `BUTTON_CROUCH` based on whether the current route target is above or below the bot.
- Added the same vertical intent mapping for ladder reachability, while leaving map-backed ladder validation as a later movement-state checklist item.
- Extended `q3a_bot_frame_command_status` with movement-state attempts, command counts, jump/crouch/swim/ladder counters, unsupported travel counts, last travel type, forced smoke travel type, and last command buttons.
- Added dedicated server smoke modes:
  - `sv_bot_frame_command_smoke 5`: force jump reachability command translation.
  - `sv_bot_frame_command_smoke 6`: force crouch reachability command translation.
  - `sv_bot_frame_command_smoke 7`: force swim reachability command translation.

No new upstream Q3A source files were imported. This is WORR-native command translation over already imported Q3A AAS route-query output.

## Validation

- `meson compile -C builddir-win`
- `python .\tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas`
- Normal two-bot smoke:
  - `.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set net_port 27915 +set logfile 1 +set logfile_name q3a_bot_nav_movement_state_normal_smoke +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sg_bot_debug_route 1 +set sg_bot_debug_goal 1 +set sv_bot_frame_command_smoke 3 +map mm-rage`
  - Result: `frames=17`, `commands=17`, `route_failures=0`, `movement_state_attempts=17`, `movement_state_commands=0`, `last_movement_state_travel_type=2`, `last_movement_state_forced_travel_type=0`, `pass=1`.
- Forced jump smoke:
  - `.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set net_port 27916 +set logfile 1 +set logfile_name q3a_bot_nav_movement_state_jump_smoke +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sg_bot_debug_route 1 +set sg_bot_debug_goal 1 +set sv_bot_frame_command_smoke 5 +map mm-rage`
  - Result: `movement_state_commands=17`, `movement_state_jump_commands=17`, `last_movement_state_travel_type=5`, `last_movement_state_forced_travel_type=5`, `last_movement_state_buttons=8`, `pass=1`.
- Forced crouch smoke:
  - `.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set net_port 27917 +set logfile 1 +set logfile_name q3a_bot_nav_movement_state_crouch_smoke +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sg_bot_debug_route 1 +set sg_bot_debug_goal 1 +set sv_bot_frame_command_smoke 6 +map mm-rage`
  - Result: `movement_state_commands=17`, `movement_state_crouch_commands=17`, `last_movement_state_travel_type=3`, `last_movement_state_forced_travel_type=3`, `last_movement_state_buttons=16`, `pass=1`.
- Forced swim smoke:
  - `.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set net_port 27918 +set logfile 1 +set logfile_name q3a_bot_nav_movement_state_swim_smoke +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sg_bot_debug_route 1 +set sg_bot_debug_goal 1 +set sv_bot_frame_command_smoke 7 +map mm-rage`
  - Result: `movement_state_commands=17`, `movement_state_swim_commands=17`, `movement_state_jump_commands=3`, `movement_state_crouch_commands=14`, `last_movement_state_travel_type=8`, `last_movement_state_forced_travel_type=8`, `pass=1`.
- Stall regression smoke:
  - `.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set net_port 27919 +set logfile 1 +set logfile_name q3a_bot_nav_movement_state_stall_regression_smoke +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sg_bot_debug_route 1 +set sg_bot_debug_goal 1 +set sv_bot_frame_command_smoke 4 +map mm-rage`
  - Result: `stuck_detections=2`, `failed_goal_events=2`, `recovery_command_uses=11`, `movement_state_commands=0`, `pass=1`.

## Follow-Up

- Validate natural jump/crouch/swim reachability on maps that contain those route types without smoke forcing.
- Door/plat wait/use, teleporter traversal, and trigger retry still need explicit action-state handling above route steering.
- Ladder command intent exists, but map-backed ladder traversal needs separate validation before that checklist item should be considered complete.
