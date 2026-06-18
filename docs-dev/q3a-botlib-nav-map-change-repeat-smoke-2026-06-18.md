# Q3A BotLib Nav Map Change Repeat Smoke

Date: 2026-06-18

Related tasks: `FR-04-T02`, `FR-04-T13`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice adds and hardens `sv_bot_frame_command_smoke 19`, a dedicated-server lifecycle smoke for the eight-bot Q3A BotLib route-command path. The smoke targets the remaining "Map change and repeat" runtime checklist item from `docs-dev/plans/q3a-botlib-aas-port.md`.

Mode `19` runs the same short eight-bot route-command and item-reservation pressure proof as mode `17`, removes the bots, requests a `gamemap` reload of the active map, waits for the server `spawncount` to change, and repeats the eight-bot proof after the reload. This exercises server map reload, `Nav_Unload` / `Nav_Load`, `Bot_RuntimeEndLevel` / `Bot_RuntimeBeginLevel`, bot slot cleanup, AAS reload, and post-reload route-command generation in one deterministic smoke.

The hardened version adds explicit cycle/reload/pass/cleanup status lines plus a reload-wait timeout so harnesses can distinguish a pre-reload failure, a queued reload, an observed reload, a post-reload failure, and a clean final shutdown.

## Implementation

- `src/server/main.c` now treats mode `19` as a non-overlapping map-change repeat smoke.
- `sv_bot_frame_command_smoke_map_repeat_cycles` controls the number of proof cycles. The default is `2`, values below `2` clamp to `2`, and values above `8` clamp to `8`.
- `sv_bot_frame_command_smoke_map_repeat_reload_timeout_ms` controls the reload wait guard. The default is `10000`, values below `1000` clamp to `1000`, and values above `120000` clamp to `120000`.
- Mode `19` targets up to eight public bot slots, matching mode `17`.
- Each proof cycle prints `q3a_bot_frame_command_smoke_map_repeat_cycle=begin` with `phase=pre_reload` or `phase=post_reload`.
- Each cycle brackets status collection with `q3a_bot_frame_command_smoke_map_repeat_cycle_status_requested ... status_line=next` and `q3a_bot_frame_command_smoke_map_repeat_cycle_status_complete ... status_line=previous pass=<n> pass_source=<source> official_pass=<0|1>`.
- The completion marker is forced onto a fresh line, even when game-side policy/status output before it is long or unterminated.
- The cycle status pass uses the official `q3a_bot_frame_command_status pass=0|1` token when present. If the game-side status line does not provide a boolean pass token, mode `19` falls back to a server-side summary using `frames`, `commands`, `route_commands`, `route_failures`, `item_goal_reservation_skips`, and `item_goal_peak_active_reservations`.
- Between cycles, the smoke prints `q3a_bot_frame_command_smoke_map_repeat_map_change_request`, queues `gamemap "<current map>"`, prints `q3a_bot_frame_command_smoke_map_repeat_reload=queued`, and suppresses the normal auto-quit until the requested reload is observed or the timeout expires.
- After `sv.spawncount` changes, the smoke prints the existing `q3a_bot_frame_command_smoke_map_repeat_reloaded` line and the harness-friendly `q3a_bot_frame_command_smoke_map_repeat_reload=observed` line with elapsed/timeout milliseconds.
- If the reload wait expires, the smoke removes any remaining bots, resets the smoke runtime cvars, and emits `q3a_bot_frame_command_smoke_map_repeat_reload=timeout`, `q3a_bot_frame_command_smoke_map_repeat_cleanup ... reason=reload_timeout`, and `q3a_bot_frame_command_smoke_map_repeat=failed reason=reload_timeout ... pass=0` before clean shutdown.
- After the configured cycle count completes, the smoke removes all bots, resets the smoke runtime cvars, prints `q3a_bot_frame_command_smoke_map_repeat_cleanup ... reason=final_cycle_complete`, then prints `q3a_bot_frame_command_smoke_map_repeat=complete` and falls back to the existing `sv_bot_frame_command_smoke >= 2` auto-quit behavior.

No new upstream Q3A or BSPC files were imported for this slice. The implementation is WORR-owned server smoke harness work only.

## Validation

- Build: `meson compile -C builddir-win` passed after the final hardening patch.
- Install refresh: `python .\tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas` passed and validated the staged `windows-x86_64` payload. The staged `basew/pak0.pkz` includes `maps/mm-rage.aas` with SHA-256 `6459585e3c15eaa4170e23ca7465fc8255bd95b9b59d42e8615c39a67b707f9c`.
- Mode `19` runtime command:

```text
.\.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set net_port 27968 +set logfile 1 +set logfile_name q3a_bot_nav_map_change_repeat_smoke_hardened_final3 +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sg_bot_debug_route 1 +set sg_bot_debug_goal 1 +set sv_bot_frame_command_smoke 19 +set sv_bot_frame_command_smoke_map_repeat_cycles 2 +set sv_bot_frame_command_smoke_map_repeat_reload_timeout_ms 10000 +map mm-rage
```

- Cycle 1 on `mm-rage` used spawncount `980451193` and reported `frames=92`, `commands=92`, `route_requests=92`, `route_queries=29`, `route_refreshes=29`, `route_reuses=63`, `route_commands=92`, `route_failures=0`, `item_goal_reservation_skips=42`, `item_goal_active_reservations=8`, `item_goal_peak_active_reservations=8`, `route_debug_routes=92`, `route_debug_goals=92`, `skipped_inactive=0`, and `pass=1`.
- Cycle 1 harness pass line: `q3a_bot_frame_command_smoke_map_repeat_cycle_status_complete cycle=1 phase=pre_reload target_cycles=2 status_line=previous pass=1 pass_source=q3a_bot_frame_command_status official_pass=1`.
- The smoke requested `gamemap "mm-rage"` after cycle 1 and emitted `q3a_bot_frame_command_smoke_map_repeat_reload=queued cycle=1 next_cycle=2 completed_cycles=1 target_cycles=2 map_changes=1 from_spawncount=980451193 map=mm-rage timeout_ms=10000`.
- Reload observation line: `q3a_bot_frame_command_smoke_map_repeat_reload=observed cycle=2 phase=post_reload completed_cycles=1 map_changes=1 old_spawncount=980451193 new_spawncount=799895723 elapsed_ms=61 timeout_ms=10000 map=mm-rage`.
- Cycle 2 after reload reported `frames=184`, `commands=183`, `route_requests=91`, `route_queries=29`, `route_refreshes=29`, `route_reuses=62`, `route_commands=183`, `route_failures=0`, `item_goal_reservation_skips=42`, `item_goal_active_reservations=8`, `item_goal_peak_active_reservations=8`, `route_debug_routes=91`, `route_debug_goals=91`, `skipped_runtime=1`, `skipped_inactive=0`, and `pass=1`.
- Cycle 2 harness pass line: `q3a_bot_frame_command_smoke_map_repeat_cycle_status_complete cycle=2 phase=post_reload target_cycles=2 status_line=previous pass=1 pass_source=q3a_bot_frame_command_status official_pass=1`.
- Final lifecycle lines: `q3a_bot_frame_command_smoke_map_repeat_cleanup cycle=2 phase=post_reload reason=final_cycle_complete final_count=0` and `q3a_bot_frame_command_smoke_map_repeat=complete cycles=2 map_changes=1 final_map=mm-rage final_spawncount=799895723 final_count=0`.
- No `q3a_bot_frame_command_smoke_map_repeat_reload=timeout`, `q3a_bot_frame_command_smoke_map_repeat=failed`, or `commandMsec underflow` line appeared in the mode `19` validation output.
- Mode `17` regression command:

```text
.\.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set net_port 27969 +set logfile 1 +set logfile_name q3a_bot_nav_map_change_repeat_mode17_regression_hardened_final +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sg_bot_debug_route 1 +set sg_bot_debug_goal 1 +set sv_bot_frame_command_smoke 17 +map mm-rage
```

- Mode `17` regression reported `q3a_bot_frame_command_smoke_multi_bot_target=8`, `frames=92`, `commands=92`, `route_requests=92`, `route_queries=29`, `route_refreshes=29`, `route_reuses=63`, `route_commands=92`, `route_failures=0`, `item_goal_reservation_skips=42`, `item_goal_active_reservations=8`, `item_goal_peak_active_reservations=8`, `route_debug_routes=92`, `route_debug_goals=92`, `skipped_inactive=0`, and `pass=1`.
- No `commandMsec underflow` or failure line appeared in the mode `17` regression output.

## Follow-Ups

- Canonical plan, roadmap, and credits integration should mark the "Map change and repeat" checklist item complete in the parent thread.
- The timeout failure path is implemented and compiled, but validation used the normal successful same-map reload path; a future harness-only negative test can deliberately suppress or break reload observation if that path needs runtime proof.
- The game-side frame-command aggregate counters remain cumulative across a map reload; the second-cycle pass still depends on fresh per-level route status and item-reservation telemetry, but a future telemetry slice could add explicit frame-command status reset markers per level.
- Longer multi-map or mixed-map repeat smokes can build on `sv_bot_frame_command_smoke_map_repeat_cycles` once additional packaged AAS maps are available.
