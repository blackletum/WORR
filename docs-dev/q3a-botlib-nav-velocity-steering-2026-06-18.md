# Q3A BotLib Nav Velocity-Aware Steering Slice

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice adds the first velocity-aware adjustment to bot route command steering. After the route-point look-ahead target is selected, `Bot_BuildFrameCommand()` now computes yaw from a short projected bot origin when the bot has measurable horizontal velocity.

The behavior is intentionally small: it predicts only `0.10` seconds ahead, starts at a low `12 u/s` movement threshold so the path can be exercised in dedicated smoke, and falls back to the original direction if the projected origin would overshoot the selected route target.

## Implementation Notes

- `Bot_CommandApplyVelocityLead()` now flattens the current bot velocity, predicts a near-future horizontal origin, and returns a target direction from that projected origin when the lead is useful.
- The helper rejects the lead path when speed is below threshold, when the lead offset would be farther than the current target distance, or when the resulting direction is degenerate.
- `q3a_bot_frame_command_status` now reports `velocity_lead_attempts`, `velocity_lead_uses`, `last_velocity_lead_speed_sq`, and `last_velocity_lead_offset_sq`.
- No Q3A import files changed for this slice. The work is WORR-native command steering over the existing adapter route-point payload.

## Validation

Commands run:

```text
meson compile -C builddir-win
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas
.\.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set net_port 27915 +set logfile 1 +set logfile_name q3a_bot_nav_velocity_smoke +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sg_bot_debug_route 1 +set sg_bot_debug_goal 1 +set sv_bot_frame_command_smoke 3 +map mm-rage
```

Results:

- Build passed. Ninja still reports the pre-existing `premature end of file; recovering` warning.
- Refresh install passed, rebuilt `.install`, and packaged `maps/mm-rage.aas` into `.install/basew/pak0.pkz`.
- Dedicated two-bot smoke passed with `frames=17`, `commands=17`, `route_queries=5`, `route_reuses=12`, `route_goal_assignments=2`, `item_goal_reservation_skips=1`, `item_goal_active_reservations=2`, `lookahead_attempts=9`, `lookahead_uses=9`, `velocity_lead_attempts=17`, `velocity_lead_uses=3`, `last_velocity_lead_speed_sq=182`, `last_velocity_lead_offset_sq=1`, `route_failures=0`, and `pass=1`.

## Follow-Up

- Tune the lead horizon once bots move for longer than the short dedicated smoke window.
- Add corner-cut safety checks before steering deeper into the route polyline.
- Add stuck/repath counters so failed steering choices can be diagnosed rather than silently falling back.
