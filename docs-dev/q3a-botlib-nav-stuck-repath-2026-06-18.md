# Q3A BotLib Nav Stuck Repath Slice

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice adds the first native stuck-progress watchdog to AAS-backed bot navigation. `bot_nav` now tracks whether a bot is making measurable progress toward its active route goal across cached route uses and cadence refreshes. If the bot remains stagnant for a sustained window, the route refresh reason becomes `Stuck` and the bot forces a repath.

The detection is intentionally conservative. It watches goal-distance improvement over several command frames, keeps progress memory across ordinary cadence refreshes for the same goal, and resets when the selected goal changes or clears.

## Implementation Notes

- `BotNavRouteSlot` now records the active progress goal, last goal-distance squared, stagnant frame count, and a short stuck-repath cooldown.
- `BotNavCheckStuckProgress()` records progress checks, stagnant frames, stuck detections, last stuck reason, last stuck client, last stuck frame count, last distance, and last progress delta.
- `BotNavRefreshReason::Stuck` forces `BotNavRefreshRoute()` through the existing route rebuild path and reports `stuck_repath_refreshes`.
- Route debug labels now include the current stuck reason and stagnant-frame count for the selected cached route.
- `sv_bot_frame_command_smoke 4` is an internal stalled-command smoke mode. It still builds bot commands, but skips applying them through `SV_BotClientThink()`, making a deterministic no-progress case for the stuck watchdog.
- No Q3A import files changed for this slice. The work is WORR-native route-cache policy above the existing BotLib/AAS adapter path.

## Validation

Commands run:

```text
meson compile -C builddir-win
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas
.\.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set net_port 27915 +set logfile 1 +set logfile_name q3a_bot_nav_stuck_normal_smoke +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sg_bot_debug_route 1 +set sg_bot_debug_goal 1 +set sv_bot_frame_command_smoke 3 +map mm-rage
.\.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set net_port 27915 +set logfile 1 +set logfile_name q3a_bot_nav_stuck_forced_smoke +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sg_bot_debug_route 1 +set sg_bot_debug_goal 1 +set sv_bot_frame_command_smoke 4 +map mm-rage
```

Results:

- Build passed. Ninja still reports the pre-existing `premature end of file; recovering` warning.
- Refresh install passed, rebuilt `.install`, and packaged `maps/mm-rage.aas` into `.install/basew/pak0.pkz`.
- Normal two-bot smoke passed with movement applied and no stuck repath: `frames=17`, `commands=17`, `route_queries=5`, `route_reuses=12`, `stuck_checks=15`, `stuck_stalls=9`, `stuck_detections=0`, `stuck_repath_refreshes=0`, `route_failures=0`, and `pass=1`.
- Forced stalled-command smoke passed with the watchdog active: `frames=29`, `commands=29`, `route_queries=10`, `route_reuses=19`, `stuck_checks=27`, `stuck_stalls=25`, `stuck_detections=2`, `stuck_repath_refreshes=2`, `last_stuck_reason=1`, `last_stuck_frames=4`, `last_stuck_progress_delta=0`, `route_failures=0`, and `pass=1`.

## Follow-Up

- Add short dodge/back-off commands for repeated stuck detections on the same goal.
- Add goal blacklist cooldowns so repeatedly blocked pickups can be abandoned temporarily.
- Add more failed-goal reason codes once doors, movers, and water/ladder movement states are integrated.
