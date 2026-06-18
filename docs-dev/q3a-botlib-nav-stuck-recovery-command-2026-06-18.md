# Q3A BotLib Nav Stuck Recovery Command Slice

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice adds the first short movement response after the native stuck-progress watchdog fires. When `bot_nav` detects sustained no-progress toward the active AAS route goal, it now activates a brief recovery window alongside the existing repath. During that window, `Bot_BuildFrameCommand()` overrides the normal forward route-following command with a small back/strafe move.

The behavior is deliberately narrow. It does not try to solve doors, movers, area blacklisting, or full movement-state ownership yet; it gives the command path a deterministic escape nudge after the watchdog has already proven that normal route following stopped making progress.

## Implementation Notes

- `BotNavRouteSlot` now stores a short recovery-until frame and an alternating strafe side for each bot.
- Stuck detections call the same route refresh path as before and also activate the recovery window.
- `BotNav_GetRecoveryMove()` exposes the current recovery move to command building and reports recovery activation/frame counters in `BotNavRouteStatus`.
- `Bot_CommandApplyRecoveryMove()` applies `forwardMove = -80` and `sideMove = +/-140` while recovery is active, after route yaw is selected.
- The frame-command status now reports `stuck_recovery_activations`, `stuck_recovery_frames`, `last_stuck_recovery_*`, `recovery_command_uses`, and last recovery movement values.
- No Q3A import files changed for this slice. The work is WORR-native movement policy above the existing BotLib/AAS adapter path.

## Validation

Commands run:

```text
meson compile -C builddir-win
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas
.\.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set net_port 27915 +set logfile 1 +set logfile_name q3a_bot_nav_recovery_normal_smoke +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sg_bot_debug_route 1 +set sg_bot_debug_goal 1 +set sv_bot_frame_command_smoke 3 +map mm-rage
.\.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set net_port 27915 +set logfile 1 +set logfile_name q3a_bot_nav_recovery_forced_smoke +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sg_bot_debug_route 1 +set sg_bot_debug_goal 1 +set sv_bot_frame_command_smoke 4 +map mm-rage
```

Results:

- Build passed. Ninja still reports the pre-existing `premature end of file; recovering` warning.
- Refresh install passed, rebuilt `.install`, packaged `maps/mm-rage.aas` into `.install/basew/pak0.pkz`, and passed the q2aas archive audit.
- Normal two-bot smoke passed with movement applied and no recovery activation: `frames=17`, `commands=17`, `route_queries=5`, `route_reuses=12`, `stuck_detections=0`, `stuck_repath_refreshes=0`, `stuck_recovery_activations=0`, `stuck_recovery_frames=0`, `recovery_command_uses=0`, `route_failures=0`, and `pass=1`.
- Forced stalled-command smoke passed with recovery active: `frames=29`, `commands=29`, `route_queries=10`, `route_reuses=19`, `stuck_detections=2`, `stuck_repath_refreshes=2`, `stuck_recovery_activations=2`, `stuck_recovery_frames=11`, `last_stuck_recovery_client=1`, `last_stuck_recovery_side=-1`, `last_stuck_recovery_frames_remaining=2`, `recovery_command_uses=11`, `last_recovery_forward_move=-80`, `last_recovery_side_move=-140`, `route_failures=0`, and `pass=1`.

## Follow-Up

- Add goal blacklist cooldowns so repeatedly blocked pickups can be abandoned temporarily.
- Add door/trigger retry policy once movement-state handling exists.
- Add more failed-goal reason codes once doors, movers, and water/ladder movement states are integrated.
