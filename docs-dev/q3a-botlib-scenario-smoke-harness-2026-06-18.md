# Q3A BotLib Scenario Smoke Harness

Date: 2026-06-18

Tasks: `DV-03-T05`

## Summary

This slice adds a self-contained scenario-smoke runner for the landed Q3A BotLib dedicated-server smoke modes. The harness lives under `tools/bot_scenarios/` so it can be used by follow-up validation work without touching game code or canonical project planning documents.

The new `run_bot_scenarios.py` script launches `.install/worr_ded_x86_64.exe`, selects existing `sv_bot_frame_command_smoke` modes, captures stdout/stderr artifacts, parses the final `q3a_bot_frame_command_status` line, and checks scenario-specific metrics. It emits a human-readable summary and can also write or print machine-readable JSON.

Follow-up update: the harness now also has a machine-readable catalog mode. Scenario definitions are declarative and include task IDs, smoke modes, runtime budgets, status-line metric requirements, marker-line metric requirements, extra cvars, and pending blockers. Normal JSON run reports embed the selected catalog entries alongside per-run results.

Second follow-up update: the harness can now write Markdown reports with `--markdown-out` and attach a lightweight historical comparison with `--compare <previous.json>`. The comparison reads one previous harness JSON report, compares scenario status by name, and reports changes for key status/route/recovery/reservation/map-repeat metrics.

Third follow-up update: the harness now has an offline standard-library-only regression test module. The tests import `run_bot_scenarios.py` directly and exercise parsing, evaluation, catalog, Markdown, and comparison helpers without launching the game.

Fourth follow-up update: the test module now performs optional fixture validation against `.tmp/bot_scenarios/latest_report.json`. If that report is absent, the fixture test skips cleanly; if present, it validates the key implemented scenario outcomes from the real report and includes `map_change_repeat` marker checks when that scenario row exists.

Final local-use polish: `tools/bot_scenarios/README.md` now provides a concise quickstart, scenario list, catalog/report examples, fixture-test notes, and requirements for developers running the harness locally.

Pending-scenario planning follow-up: `docs-dev/q3a-botlib-pending-scenario-counters-2026-06-18.md` now defines proposed smoke setups, status counters, pass criteria, likely owner modules, and harness catalog representation for `engage_enemy`, `switch_weapons`, `health_armor_pickup`, and `team_objective`.

## Implemented Scenarios

- `spawn_route_to_item`: runs `sv_bot_frame_command_smoke 2` on `mm-rage` and verifies command output, route commands, zero route failures, item-goal assignment, and a resolved item AAS area.
- `recover_from_stall`: runs `sv_bot_frame_command_smoke 4` and verifies stuck detection, repath refreshes, recovery activation, recovery command use, zero route failures, and final pass.
- `multi_bot_reservation`: runs `sv_bot_frame_command_smoke 17` and verifies eight-bot command pressure, zero route failures, and `item_goal_peak_active_reservations >= 8`.
- `map_change_repeat`: runs `sv_bot_frame_command_smoke 19`, forces `sv_bot_frame_command_smoke_map_repeat_cycles 2`, verifies the final repeated frame-command status, and checks the `q3a_bot_frame_command_smoke_map_repeat=complete` marker for `cycles=2`, `map_changes=1`, and `final_count=0`.

## Pending Scenario Rows

The harness reports the following scenario placeholders as `pending` without failing the suite unless `--fail-on-pending` is passed:

- `engage_enemy`: waiting on a dedicated smoke mode that reports enemy acquisition or attack-button counters.
- `switch_weapons`: waiting on weapon inventory/selection/change-command status.
- `health_armor_pickup`: waiting on a smoke setup that forces damaged health/armor state and verifies pickup completion.
- `team_objective`: waiting on team-objective route/goal completion status.

## Usage

List scenarios:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --list
```

Emit the declarative machine-readable scenario catalog:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --catalog --format json --json-out .tmp\bot_scenarios\catalog_report.json
```

Emit catalog JSON and Markdown while comparing against a previous run report:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --catalog --format text --json-out .tmp\bot_scenarios\catalog_compare_report.json --markdown-out .tmp\bot_scenarios\catalog_compare_report.md --compare .tmp\bot_scenarios\latest_report.json
```

Emit a human-readable catalog row for one scenario:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --catalog --scenario map_change_repeat --format text
```

Run the currently implemented scenario suite:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario implemented --timeout 90 --base-port 27970 --format both --json-out .tmp\bot_scenarios\latest_report.json
```

Run one scenario:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario spawn_route_to_item --timeout 60
```

Run only placeholders:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario pending --json-out .tmp\bot_scenarios\pending_report.json
```

Write a Markdown run report and compare it with a previous report:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario pending --format text --json-out .tmp\bot_scenarios\pending_compare_report.json --markdown-out .tmp\bot_scenarios\pending_compare_report.md --compare .tmp\bot_scenarios\latest_report.json
```

Run the offline parser/reporting regression tests:

```powershell
python tools\bot_scenarios\test_run_bot_scenarios.py
```

## Report Fields

Markdown reports include:

- Top-level repo, timestamp, artifact directory when available, and summary counts.
- Scenario table fields: scenario name, status, smoke mode, task IDs, key metrics, pending blockers, and stdout/stderr artifact paths when present.
- Optional comparison section with the previous report path, comparison summary, previous/current scenario status, status-change flag, and key metric deltas.

JSON comparison data is attached under `comparison` and includes:

- `previous_path`
- `summary`: `total`, `matched`, `added`, `removed`, `status_changed`, and `metric_changed`
- `scenarios`: per-scenario `previous_status`, `current_status`, `added`, `removed`, `status_changed`, and `metric_changes`

Key metric comparison currently covers `frames`, `commands`, `route_commands`, `route_failures`, `stuck_detections`, `recovery_command_uses`, `item_goal_assignments`, `item_goal_peak_active_reservations`, `cycles`, `map_changes`, `final_count`, `duration_seconds`, and `pass`.

## Test Coverage

`tools/bot_scenarios/test_run_bot_scenarios.py` covers:

- `q3a_bot_frame_command_status` parsing when noisy output prefixes the marker and multiple status lines are present.
- Mode `19` map-repeat marker metric parsing for `cycles`, `map_changes`, and `final_count`.
- Metric check evaluation for pass, fail, and missing-metric cases.
- Pending scenario catalog shape and Markdown catalog rendering.
- Historical comparison status changes and key metric deltas, including removed and added scenario rows.
- Optional `.tmp/bot_scenarios/latest_report.json` validation when the fixture exists, checking real implemented scenario pass status, route cleanliness, scenario-specific counters, and map-repeat lifecycle metrics.

## Validation

Command:

```powershell
python tools\bot_scenarios\test_run_bot_scenarios.py
```

Result:

- `Ran 6 tests`
- `OK`

The optional fixture existed in this workspace, so `test_latest_report_fixture_when_available` validated:

- `spawn_route_to_item`: `status=passed`, `pass=1`, `route_failures=0`, command counts, route-command counts, and item-goal assignment.
- `recover_from_stall`: `status=passed`, `pass=1`, `route_failures=0`, stuck detection, and recovery command use.
- `multi_bot_reservation`: `status=passed`, `pass=1`, `route_failures=0`, and `item_goal_peak_active_reservations >= 8`.
- `map_change_repeat`: when present, `status=passed`, `pass=1`, `route_failures=0`, `item_goal_peak_active_reservations >= 8`, `cycles=2`, `map_changes=1`, and `final_count=0`.

Command:

```powershell
python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py
```

Result:

- Passed.

Command:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --catalog --format json --json-out .tmp\bot_scenarios\catalog_report.json
```

Result:

- Overall catalog: `8` scenarios, `4` implemented, `4` pending.
- Catalog fields per scenario: `name`, `title`, `status`, `task_ids`, `smoke_mode`, `description`, `runtime_budget_seconds`, `required_metrics`, `required_marker_metrics`, `extra_cvars`, and `pending_blockers`.
- `map_change_repeat` catalog row reports smoke mode `19`, tasks `DV-03-T05` and `FR-04-T16`, status-line checks for `pass`, `expected_min_commands`, `commands`, `route_commands`, `route_failures`, and `item_goal_peak_active_reservations`, plus marker checks for `cycles`, `map_changes`, and `final_count`.

Command:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --catalog --format text --json-out .tmp\bot_scenarios\catalog_compare_report.json --markdown-out .tmp\bot_scenarios\catalog_compare_report.md --compare .tmp\bot_scenarios\latest_report.json
```

Result:

- Catalog generation passed without launching the dedicated server.
- Markdown report was written to `.tmp\bot_scenarios\catalog_compare_report.md`.
- JSON comparison summary against `.tmp\bot_scenarios\latest_report.json`: `total=8`, `matched=4`, `added=4`, `removed=0`, `status_changed=8`, `metric_changed=4`.
- The comparison intentionally shows catalog `implemented` rows versus previous run `passed` rows as status changes; this validates schema compatibility across catalog and run reports.

Command:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario pending --format text --json-out .tmp\bot_scenarios\pending_compare_report.json --markdown-out .tmp\bot_scenarios\pending_compare_report.md --compare .tmp\bot_scenarios\latest_report.json
```

Result:

- Pending-only report passed without launching implemented scenario smokes.
- Markdown report was written to `.tmp\bot_scenarios\pending_compare_report.md`.
- JSON comparison summary against `.tmp\bot_scenarios\latest_report.json`: `total=8`, `matched=0`, `added=4`, `removed=4`, `status_changed=8`, `metric_changed=4`.

Command:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario implemented --timeout 120 --base-port 28000 --format text --json-out .tmp\bot_scenarios\latest_report.json
```

Result:

- Overall: `pass`
- `spawn_route_to_item`: `frames=8`, `commands=8`, `route_commands=8`, `route_failures=0`, `item_goal_assignments=1`, `item_goal_peak_active_reservations=1`, `pass=1`, runtime `1.36s` within `20s` budget.
- `recover_from_stall`: `frames=29`, `commands=29`, `route_commands=29`, `route_failures=0`, `stuck_detections=2`, `recovery_command_uses=11`, `item_goal_assignments=4`, `item_goal_peak_active_reservations=2`, `pass=1`, runtime `0.594s` within `20s` budget.
- `multi_bot_reservation`: `frames=92`, `commands=92`, `route_commands=92`, `route_failures=0`, `stuck_detections=3`, `recovery_command_uses=7`, `item_goal_assignments=10`, `item_goal_peak_active_reservations=8`, `pass=1`, runtime `0.89s` within `30s` budget.
- `map_change_repeat`: `frames=184`, `commands=183`, `route_commands=183`, `route_failures=0`, `stuck_detections=3`, `recovery_command_uses=13`, `item_goal_assignments=10`, `item_goal_peak_active_reservations=8`, `pass=1`, marker metrics `cycles=2`, `map_changes=1`, `final_count=0`, runtime `1.141s` within `45s` budget.

Command:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario pending --format text --json-out .tmp\bot_scenarios\pending_report.json
```

Result:

- Overall: `pass`
- Pending rows reported for `engage_enemy`, `switch_weapons`, `health_armor_pickup`, and `team_objective`.

## Notes

- The harness treats `q3a_bot_frame_command_status pass=1` plus per-scenario metric checks as the scenario authority.
- Status parsing requires `q3a_bot_frame_command_status` as a real marker token, so lifecycle fields such as `pass_source=q3a_bot_frame_command_status` do not overwrite the last actual status line.
- When a smoke prints additional cleanup status rows with `expected_min_commands=0`, status parsing prefers the latest real status row with a positive command proof. This keeps map-repeat cleanup proofs from overwriting the scenario's main command metrics.
- Marker checks let scenario definitions consume non-status lifecycle lines such as `q3a_bot_frame_command_smoke_map_repeat=complete` without changing the game-side status line.
- Runtime budgets are reported in JSON and text output as guardrail telemetry; they are not hard failure gates because local process startup cost can vary across developer machines.
- Historical comparison is intentionally lightweight: it compares scenarios by name and reports selected key metric deltas only. It does not attempt statistical trend analysis or enforce regression thresholds.
- Offline tests are intentionally focused on harness parsing/reporting behavior; they do not prove dedicated-server smoke behavior or asset availability.
- Fixture-aware tests only validate `.tmp/bot_scenarios/latest_report.json` when it exists. Missing fixtures are treated as skipped optional coverage rather than test failures.
- Dedicated process stdout/stderr are saved below `.tmp/bot_scenarios/<timestamp>/`.
- `commandMsec underflow` is treated as a forbidden output pattern because it previously indicated invalid long-run bot command accounting.
- Pending scenario promotion criteria are specified in `docs-dev/q3a-botlib-pending-scenario-counters-2026-06-18.md` so future source-side smoke work can land without guessing which counters the harness expects.
- Canonical roadmap, plan, and credits integration were intentionally left to the parent thread per subagent ownership boundaries.
