# Q3A BotLib Nav Soak Frame Command Smoke

Date: 2026-06-18

Related tasks: `FR-04-T02`, `FR-04-T13`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice adds `sv_bot_frame_command_smoke 18`, a long-running eight-bot dedicated-server soak for the native WORR/Q3A BotLib frame-command path. The smoke targets the "run 10 minutes without crash" validation item from the Q3A BotLib AAS port plan while continuing to exercise the normal bot slot lifecycle, packaged `mm-rage.aas` loading, cached route commands, route/goal debug counters, stuck recovery, item-goal churn, and cleanup at the end of the run.

Mode `17` remains the focused eight-bot item-reservation pressure proof. Mode `18` is the duration proof: it keeps eight bots alive and issuing route-steered commands for the configured soak window, requires zero route failures, and reports progress/final status without treating the final instantaneous item-reservation count as a pass gate.

## Implementation

- `src/server/main.c` now registers `sv_bot_frame_command_smoke_soak_ms`, defaulting to `600000`, and clamps requested soak durations to at least one second.
- Mode `18` shares the existing staged add loop with mode `17`, targeting up to eight public bot slots and waiting until all target bots are present before the timed soak begins.
- The smoke reports `q3a_bot_frame_command_smoke_soak=begin`, periodic `q3a_bot_frame_command_smoke_soak_progress`, and `q3a_bot_frame_command_smoke_soak=complete` lines before emitting the normal `q3a_bot_frame_command_status`.
- The harness sets internal `sg_bot_frame_command_smoke_soak=1` while mode `18` is active so `bot_brain` can keep command-count and route-failure pass semantics while skipping the final active-reservation gate. Long soaks naturally consume, hide, clear, and reassign item goals, so the final active-reservation count is not equivalent to the short reservation-pressure check.
- `bot_nav` now tracks `itemGoalPeakActiveReservations` and exposes `item_goal_peak_active_reservations` in frame-command status. This preserves visibility into reservation pressure across both short and long runs.
- `SV_BotClientThink()` now tops up a bot client's local `command_msec` budget before applying an engine-authored bot command when the budget would otherwise underflow. This prevents long server-authored bot command runs from tripping the human-client anti-speed accounting path.

No new upstream Q3A or BSPC files were imported for this slice. The changes are WORR-owned smoke harness, fake-client accounting, and bot navigation/status work over the existing imported BotLib/AAS runtime.

## Validation

- Build: `meson compile -C builddir-win` passed.
- Install refresh: `python .\tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas` passed and re-injected packaged `maps/mm-rage.aas`.
- Short mode `18` sanity run with a 10-second duration reported `elapsed_ms=10008`, `route_failures=0`, `item_goal_peak_active_reservations=8`, and `pass=1`.
- Full mode `18` soak on `mm-rage` ran for `elapsed_ms=600001` with `reports=9`, `frames=192036`, `commands=192036`, `route_requests=187232`, `route_queries=53372`, `route_refreshes=53372`, `route_reuses=133860`, `route_commands=192036`, `route_failures=0`, `route_goal_assignments=4889`, `item_goal_assignments=1451`, `item_goal_reservation_skips=3455`, `item_goal_active_reservations=1`, `item_goal_peak_active_reservations=2`, `stuck_detections=11789`, `stuck_recovery_activations=11789`, `recovery_command_uses=72066`, `route_debug_routes=187232`, `route_debug_goals=187232`, `skipped_inactive=0`, and `pass=1`.
- Eight-bot mode `17` regression after the soak changes reported `frames=92`, `commands=92`, `route_failures=0`, `item_goal_active_reservations=8`, `item_goal_peak_active_reservations=8`, and `pass=1`.
- The post-fix sanity and regression smokes did not reproduce the earlier `commandMsec underflow` spam.

## Follow-Ups

- Add a map-change repeat smoke so the long-running path also proves BotLib level shutdown/reload cleanup under bot load.
- Add scenario tests above the smoke harness for spawn-to-item, blocked-route recovery, combat engagement, weapon switching, health/armor pickup, and team-objective behavior.
- Measure CPU cost per bot, route recomputation rate, visibility trace count, and debug overlay cost during long-duration bot runs.
- Tune item-goal and stuck-recovery policy now that the long soak shows sustained route cleanliness but high stuck/recovery and goal-churn activity.
