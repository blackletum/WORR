# WORR Bot Perf Tools

Small local tools for analyzing Q3A BotLib frame-command smoke output without launching the game.

For implementation details, baseline numbers, and instrumentation gaps, see:

- `docs-dev/q3a-botlib-bot-perf-telemetry-2026-06-18.md`

## Quickstart

Analyze the current ten-minute soak log:

```powershell
python .\tools\bot_perf\analyze_bot_perf.py .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
```

The analyzer reads `q3a_bot_frame_command_smoke_soak` progress/completion lines plus the final `q3a_bot_frame_command_status` line. It reports derived metrics such as commands per bot per second, route refresh/reuse ratios, debug-output pressure, recovery-command pressure, and missing CPU/visibility instrumentation.

## Common Commands

Text output:

```powershell
python .\tools\bot_perf\analyze_bot_perf.py .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
```

JSON output:

```powershell
python .\tools\bot_perf\analyze_bot_perf.py --format json .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
```

CSV output:

```powershell
python .\tools\bot_perf\analyze_bot_perf.py --format csv .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
```

Analyze multiple logs:

```powershell
python .\tools\bot_perf\analyze_bot_perf.py .tmp\q3a_bot_nav_soak_redirect_short.stdout.txt .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
```

Analyze scenario stdout logs with durations from the scenario runner report:

```powershell
$scenarioReport = Get-Content .tmp\bot_scenarios\latest_report.json | ConvertFrom-Json
python .\tools\bot_perf\analyze_bot_perf.py --scenario-report .tmp\bot_scenarios\latest_report.json "$($scenarioReport.artifact_dir)\*.stdout.txt"
```

PowerShell users should quote glob inputs so the analyzer expands them consistently. Scenario stdout logs without `--scenario-report` still produce raw status counters and route ratios, but `duration_sec` and per-second rates stay `n/a` unless the stdout itself contains soak elapsed markers.

## Budgets

Use the generous default soak budget:

```powershell
python .\tools\bot_perf\analyze_bot_perf.py --budget .\tools\bot_perf\default_soak_budget.json .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
```

Budget files have checks under:

- `checks.metrics`: derived analyzer fields such as `commands_per_bot_sec`, `route_refresh_ratio`, and `debug_work_units_per_bot_sec`.
- `checks.status`: raw final status fields such as `route_failures`, `route_invalid_slots`, and `skipped_inactive`.

Each check supports numeric `min`, numeric `max`, optional boolean `required`, and optional `description`.

Exit behavior:

- `0`: all required checks passed.
- `1`: at least one required check failed or was missing.

## Comparison Reports

When more than one log is supplied, text output includes a comparison section with latest, best, and worst values for key metrics.

Comparison output also emits guard warnings when the input set is not like-for-like, such as mixed scenario names, mixed bot counts, mixed duration sources, or missing duration data. The report still prints the table, but those guards mean the best/worst values should be read as an overview rather than a strict regression ranking.

Write a Markdown report:

```powershell
python .\tools\bot_perf\analyze_bot_perf.py --budget .\tools\bot_perf\default_soak_budget.json --markdown-out .tmp\bot_perf\soak_compare.md .tmp\q3a_bot_nav_soak_10min_final.stdout.txt .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
```

The Markdown report contains run, comparison, and budget tables. `.tmp/bot_perf/` is a good local scratch location for generated reports.

Scenario comparison report:

```powershell
$scenarioReport = Get-Content .tmp\bot_scenarios\latest_report.json | ConvertFrom-Json
python .\tools\bot_perf\analyze_bot_perf.py --scenario-report .tmp\bot_scenarios\latest_report.json --markdown-out .tmp\bot_perf\scenario_compare.md "$($scenarioReport.artifact_dir)\*.stdout.txt"
```

## Tests

Run parser/analyzer regression tests:

```powershell
python .\tools\bot_perf\test_analyze_bot_perf.py
```

Or through unittest:

```powershell
python -m unittest .\tools\bot_perf\test_analyze_bot_perf.py
```

The tests use synthetic logs for core parser and analyzer behavior. If `.tmp/q3a_bot_nav_soak_10min_final.stdout.txt` exists, an optional fixture test validates the known soak baseline; otherwise that test skips.

Compile-check the scripts:

```powershell
python -m py_compile .\tools\bot_perf\analyze_bot_perf.py .\tools\bot_perf\test_analyze_bot_perf.py
```
