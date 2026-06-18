# Q3A BotLib Nav Map Restart Lifecycle Smoke

Date: 2026-06-18

Related tasks: `FR-04-T02`, `FR-04-T13`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice extends the existing `sv_bot_frame_command_smoke 19` map-change repeat smoke with an opt-in forced map-restart lifecycle path and an explicit post-removal cleanup checkpoint.

Default mode `19` behavior remains compatible with the existing `gamemap` repeat smoke. The new `sv_bot_frame_command_smoke_map_repeat_restart` cvar defaults to `0`; when set to `1`, mode `19` queues `map "<current map>" force` between cycles instead of `gamemap "<current map>"`. That drives the heavier server restart path while preserving the same cycle/status/reload/final markers.

## Implementation

- `src/server/main.c` registers `sv_bot_frame_command_smoke_map_repeat_restart`, default `0`.
- Mode `19` now reports the selected reload command as trailing fields on relevant markers:
  - `command=gamemap restart=0` for the default path.
  - `command=map_force restart=1` for the forced restart path.
- The forced restart path queues `map "<map>" force`; the default path still queues `gamemap "<map>"`.
- The reload observation marker now includes `realtime_reset=<0|1>`. Forced restart resets `svs.realtime`, so elapsed time is reported as `elapsed_ms=0 realtime_reset=1` instead of underflowing.
- After each cycle removes bots and resets smoke runtime cvars, mode `19` captures the existing frame-command status and emits:

```text
q3a_bot_frame_command_smoke_map_repeat_cleanup_status_requested cycle=<n> phase=<phase> reason=<before_reload|final_cycle_complete> count=0 status_line=next
q3a_bot_frame_command_smoke_map_repeat_cleanup_status cycle=<n> phase=<phase> reason=<reason> count=0 active_reservations=0 pass=1 status_line=previous
```

- The cleanup status is a real gate. It fails the smoke with `reason=cleanup_status_failed` if server bot count is nonzero or `item_goal_active_reservations` is nonzero after removal.

## Validation

- Build:

```text
meson compile -C builddir-win
```

Passed after the final restart elapsed-time fix.

- Install refresh:

```text
python .\tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas
```

Passed. The staged `basew/pak0.pkz` includes `maps/mm-rage.aas` with SHA-256 `6459585e3c15eaa4170e23ca7465fc8255bd95b9b59d42e8615c39a67b707f9c`.

- Forced restart lifecycle command:

```text
.\.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set net_port 27971 +set logfile 1 +set logfile_name q3a_bot_nav_map_restart_lifecycle_smoke_rawls_final +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sg_bot_debug_route 1 +set sg_bot_debug_goal 1 +set sv_bot_frame_command_smoke 19 +set sv_bot_frame_command_smoke_map_repeat_cycles 3 +set sv_bot_frame_command_smoke_map_repeat_reload_timeout_ms 10000 +set sv_bot_frame_command_smoke_map_repeat_restart 1 +map mm-rage
```

Passed with three proof cycles, two forced restart transitions, and no failure/timeout/`commandMsec underflow`.

Cycle metrics:

- Cycle 1: `frames=92`, `commands=92`, `route_commands=92`, `route_failures=0`, `item_goal_active_reservations=8`, `item_goal_peak_active_reservations=8`, `pass=1`.
- Cleanup after cycle 1: `count=0 active_reservations=0 pass=1`.
- Restart observation 1: `old_spawncount=1825466945 new_spawncount=85741700 elapsed_ms=0 realtime_reset=1 command=map_force restart=1`.
- Cycle 2: `frames=92`, `commands=91`, `route_commands=91`, `route_failures=0`, `item_goal_active_reservations=8`, `item_goal_peak_active_reservations=8`, `pass=1`.
- Cleanup after cycle 2: `count=0 active_reservations=0 pass=1`.
- Restart observation 2: `old_spawncount=85741700 new_spawncount=492459259 elapsed_ms=0 realtime_reset=1 command=map_force restart=1`.
- Cycle 3: `frames=92`, `commands=91`, `route_commands=91`, `route_failures=0`, `item_goal_active_reservations=8`, `item_goal_peak_active_reservations=8`, `pass=1`.
- Final cleanup: `count=0 active_reservations=0 pass=1`.
- Final line: `q3a_bot_frame_command_smoke_map_repeat=complete cycles=3 map_changes=2 final_map=mm-rage final_spawncount=492459259 final_count=0`.

- Default `gamemap` compatibility command:

```text
.\.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set net_port 27972 +set logfile 1 +set logfile_name q3a_bot_nav_map_change_repeat_smoke_rawls_default_regression +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sg_bot_debug_route 1 +set sg_bot_debug_goal 1 +set sv_bot_frame_command_smoke 19 +set sv_bot_frame_command_smoke_map_repeat_cycles 2 +set sv_bot_frame_command_smoke_map_repeat_reload_timeout_ms 10000 +map mm-rage
```

Passed with the default command path: `command=gamemap restart=0`, `elapsed_ms=62 realtime_reset=0`, both cycle status checks `pass=1`, both cleanup checks `count=0 active_reservations=0 pass=1`, and final line `q3a_bot_frame_command_smoke_map_repeat=complete cycles=2 map_changes=1 final_map=mm-rage final_spawncount=788926850 final_count=0`.

## Remaining Risks

- Validation still uses same-map `mm-rage` because it is the packaged AAS map in this workspace.
- Forced restart resets `svs.realtime`; the smoke now reports that explicitly, but it cannot provide a continuous elapsed duration across the restart boundary.
- The cleanup gate intentionally checks active reservation state, not historical `last_*` telemetry fields, because those fields are last-observed debug values and may remain nonzero after valid cleanup.
