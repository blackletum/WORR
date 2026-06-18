# Q3A BotLib Nav Look-Ahead Steering Slice

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice starts smoothing bot route steering by using the bounded route polyline already returned by the BotLib adapter. Instead of always aiming at only the immediate route step, the frame-command builder now selects the farthest sampled route point within a short local look-ahead window.

This is still intentionally lightweight. It does not add full corner cutting, obstacle avoidance, or velocity-aware movement, but it proves the command path can consume richer route data than the first step.

## Implementation Notes

- `Bot_CommandSelectRouteTarget()` now clamps `routePointCount`, starts from the first sampled point, and advances to later sampled points while they remain within the local look-ahead distance.
- The existing fallback to the route goal remains in place when the selected route target is degenerate.
- `q3a_bot_frame_command_status` now reports `lookahead_attempts`, `lookahead_uses`, `last_lookahead_index`, and `last_lookahead_point_count`.
- No Q3A import files changed for this slice. The work is WORR-native command steering over the existing adapter route-point payload.

## Validation

Commands run:

```text
meson compile -C builddir-win
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas
.\.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set net_port 27915 +set logfile 1 +set logfile_name q3a_bot_nav_lookahead_smoke +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sg_bot_debug_route 1 +set sg_bot_debug_goal 1 +set sv_bot_frame_command_smoke 3 +map mm-rage
```

Results:

- Build passed. Ninja still reports the pre-existing `premature end of file; recovering` warning.
- Refresh install passed, rebuilt `.install`, and packaged `maps/mm-rage.aas` into `.install/basew/pak0.pkz`.
- Dedicated two-bot smoke passed with `frames=17`, `commands=17`, `route_queries=5`, `route_reuses=12`, `route_goal_assignments=2`, `item_goal_reservation_skips=1`, `item_goal_active_reservations=2`, `last_route_point_count=2`, `lookahead_attempts=17`, `lookahead_uses=9`, `last_lookahead_index=0`, `last_lookahead_point_count=2`, `route_failures=0`, and `pass=1`.

## Follow-Up

- Tune velocity-aware aim direction after longer-running movement smokes expose higher-speed command frames.
- Add corner-cut safety checks before steering deeper into the route polyline.
- Expand stuck/repath counters with richer recovery modes and goal blacklist policy once recovery grows beyond short back/strafe commands.
