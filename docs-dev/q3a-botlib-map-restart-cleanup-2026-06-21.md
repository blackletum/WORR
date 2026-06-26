# Q3A BotLib Map-Restart Cleanup Proof

Date: 2026-06-21

Tasks: `FR-04-T06`, `FR-04-T16`, `DV-03-T05`, `DV-07-T06`

## Summary

This round promotes the existing restart-capable frame-command map-repeat smoke
into a first-class scenario harness proof named `map_restart_cleanup`.

The server-side smoke mode already supported two reload paths:

- Default `gamemap` reloads for map-change repeat validation.
- Opt-in forced restarts through `sv_bot_frame_command_smoke_map_repeat_restart 1`,
  which queues the `map "<current map>" force` path and reports it as
  `command=map_force`.

The new scenario keeps using smoke mode `19`, but adds explicit marker gates so
the forced restart path cannot silently fall back to the default map-change
flow.

## Scenario Contract

`tools/bot_scenarios/run_bot_scenarios.py` now declares:

- `map_restart_cleanup`
- `smoke_mode=19`
- `extra_cvars`:
  - `sv_bot_frame_command_smoke_map_repeat_cycles=2`
  - `sv_bot_frame_command_smoke_map_repeat_restart=1`
- `selection_tags=("match", "restart")`

The scenario requires:

- The final source `q3a_bot_frame_command_status` to pass.
- Eight post-restart bot command/route-command evidence.
- Zero route failures.
- Peak eight-bot item reservation pressure after restart.
- `q3a_bot_frame_command_smoke_map_repeat_cycle=begin` with
  `command=map_force` and `restart=1`.
- `q3a_bot_frame_command_smoke_map_repeat_reload=queued` with
  `command=map_force` and `restart=1`.
- `q3a_bot_frame_command_smoke_map_repeat_reload=observed` with
  `command=map_force`, `restart=1`, and `completed_cycles=1`.
- Cleanup status with `pass=1` and `count=0`.
- Final completion with `cycles=2`, `map_changes=1`, and `final_count=0`.

This closes the current Phase 7 map-restart cleanup proof gap without changing
the gameplay-facing server path. It also gives future match-flow work a
dedicated regression scenario instead of relying on the broader map-change
repeat row.

## Validation

Commands run:

```powershell
python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py
python -m unittest tools.bot_scenarios.test_run_bot_scenarios
python tools\bot_scenarios\run_bot_scenarios.py --catalog --scenario implemented --format json --json-out .tmp\bot_scenarios\catalog_after_restart.json
python tools\bot_scenarios\run_bot_scenarios.py --scenario map_restart_cleanup --timeout 120 --base-port 28100 --format text --json-out .tmp\bot_scenarios\map_restart_cleanup_report.json --markdown-out .tmp\bot_scenarios\map_restart_cleanup_report.md
python tools\bot_scenarios\run_bot_scenarios.py --scenario implemented --timeout 120 --base-port 28200 --format text --json-out .tmp\bot_scenarios\latest_report.json --markdown-out .tmp\bot_scenarios\latest_report.md
```

Results:

- Python compile check passed.
- Scenario harness unit suite passed: 34 tests.
- Implemented catalog now reports 39 implemented scenarios, 0 pending, and 1
  manual degradation row.
- Focused `map_restart_cleanup` passed from the refreshed staged install:
  - `expected_min_commands=8`
  - `commands=91`
  - `route_commands=91`
  - `route_failures=0`
  - `item_goal_peak_active_reservations=8`
  - `cycles=2`
  - `map_changes=1`
  - `final_count=0`
- Full implemented scenario suite passed: 39 passed, 0 failed, 0 timed out, 0
  errored, and 0 pending from `.tmp\bot_scenarios\20260621T115153Z`.

No C/C++ source changed in this round, so no build or `.install` refresh was
required. The focused runtime proof used the existing staged
`.install/worr_ded_x86_64.exe`, `.install/basew/pak0.pkz`, and packaged
`mm-rage` AAS payload.

## Provenance

This is WORR-native scenario harness, documentation, and planning work. No new
Quake III Arena, BSPC, Gladiator, idTech3, or `q2proto` source files were
imported or modified.
