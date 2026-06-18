# Bot Scenario Smokes

Lightweight local harness for WORR Q3A BotLib scenario validation. It wraps existing dedicated-server smoke modes, parses `q3a_bot_frame_command_status`, and can emit text, JSON, Markdown, and comparison reports.

For implementation history and validation notes, see `docs-dev/q3a-botlib-scenario-smoke-harness-2026-06-18.md`.

## Requirements

- Python 3 standard library only.
- For catalog/report/test-only commands: no game launch is required.
- For implemented scenario runs: `.install/worr_ded_x86_64.exe` and packaged `basew` / `mm-rage` assets must exist, usually after a refreshed install.
- Reports and stdout/stderr artifacts are written under `.tmp/bot_scenarios/` by default.

## Quickstart

List known scenarios:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --list
```

Run the implemented smoke suite:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario implemented --timeout 120 --base-port 28000 --format text --json-out .tmp\bot_scenarios\latest_report.json
```

Run one scenario:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario spawn_route_to_item --timeout 60
```

Run only pending placeholders without launching the game:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario pending --json-out .tmp\bot_scenarios\pending_report.json
```

## Scenarios

Implemented:

- `spawn_route_to_item`: mode `2`, verifies item-backed route commands.
- `recover_from_stall`: mode `4`, verifies stuck detection and recovery commands.
- `multi_bot_reservation`: mode `17`, verifies eight-bot route pressure and item reservation peak.
- `map_change_repeat`: mode `19`, verifies two map-repeat cycles, one map change, and final bot cleanup.

Pending placeholders:

- `engage_enemy`
- `switch_weapons`
- `health_armor_pickup`
- `team_objective`

Pending rows are reported but do not fail the suite unless `--fail-on-pending` is passed.

## Catalog

Emit the declarative scenario catalog:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --catalog --format json --json-out .tmp\bot_scenarios\catalog_report.json
```

Catalog entries include scenario status, task IDs, smoke mode, runtime budget, required status metrics, marker metrics, extra cvars, and pending blockers.

Pending catalog rows also include planned source smoke modes and promotion-required metrics. Those fields describe the counters a future source-backed smoke must emit before the placeholder can become an implemented scenario.

## Pending Gap Reports

Analyze an existing JSON report, usually `.tmp\bot_scenarios\latest_report.json`, to see which pending scenario rows and source counters are still missing:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario pending --pending-gap-report .tmp\bot_scenarios\latest_report.json --format text --json-out .tmp\bot_scenarios\pending_gap_report.json
```

This command does not launch the game. It compares pending placeholders against the report fixture and prints whether each scenario is ready for harness promotion or blocked by missing scenario rows, wrong smoke modes, pending fixture rows, or absent status/marker metrics.

## Markdown And Comparison Reports

Write JSON and Markdown for a run:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario implemented --timeout 120 --json-out .tmp\bot_scenarios\latest_report.json --markdown-out .tmp\bot_scenarios\latest_report.md
```

Compare a current report with a previous JSON report:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario pending --json-out .tmp\bot_scenarios\pending_compare_report.json --markdown-out .tmp\bot_scenarios\pending_compare_report.md --compare .tmp\bot_scenarios\latest_report.json
```

The comparison is name-based and reports status changes plus selected key metric deltas. It is intended as a quick local regression aid, not a statistical trend analyzer.

## Tests

Run offline parser/reporting tests:

```powershell
python tools\bot_scenarios\test_run_bot_scenarios.py
```

The tests use only the Python standard library. If `.tmp/bot_scenarios/latest_report.json` exists, they also validate key real-report scenario outcomes, including `map_change_repeat` when present. If the fixture is missing, that fixture check is skipped.

Compile-check the harness:

```powershell
python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py
```
