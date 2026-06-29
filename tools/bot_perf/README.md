# WORR Bot Perf Tools

Small local tools for analyzing Q3A BotLib frame-command smoke output without launching the game.

For implementation details, baseline numbers, and instrumentation gaps, see:

- `docs-dev/q3a-botlib-bot-perf-telemetry-2026-06-18.md`
- `docs-dev/q3a-botlib-high-bot-soak-budget-2026-06-18.md`

## Quickstart

Analyze the current ten-minute soak log:

```powershell
python .\tools\bot_perf\analyze_bot_perf.py .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
```

The analyzer reads `q3a_bot_frame_command_smoke_soak` progress/completion lines plus `q3a_bot_frame_command_status` and `q3a_bot_source_counter_status` lines. Long smokes may split detailed route counters and compact summary counters across multiple status lines, so the analyzer merges repeated status/source-status lines with later duplicate fields winning. It reports derived metrics such as commands per bot per second, route refresh/reuse ratios, debug-output pressure, recovery-command pressure, and missing CPU/visibility instrumentation.

Run the manual ten-minute high-bot soak through the scenario harness with enough timeout headroom:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario high_bot_soak_degradation --timeout 720 --base-port 28000 --format text --json-out .tmp\bot_scenarios\high_bot_soak_report.json --markdown-out .tmp\bot_scenarios\high_bot_soak_report.md
```

The soak itself is configured for `bot_frame_command_smoke 18` with `bot_frame_command_smoke_soak_ms=600000`. The `720` second timeout leaves room for startup, staged bot joins, shutdown, and slower local machines.

The scenario harness automatically evaluates the perf budgets named by the long-soak degradation policy. It records the primary/default result as `perf_budget` and the complete evaluated set as `perf_budgets` in JSON and Markdown reports. Use this standalone analyzer when you want extra text/CSV output, Markdown comparison tables, or ad-hoc analysis of older stdout files.

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

Use the generous default soak budget for the manual `high_bot_soak_degradation` run:

```powershell
python .\tools\bot_perf\analyze_bot_perf.py --budget .\tools\bot_perf\default_soak_budget.json .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
```

Analyze the stdout captured by the scenario harness:

```powershell
$soakReport = Get-Content .tmp\bot_scenarios\high_bot_soak_report.json | ConvertFrom-Json
python .\tools\bot_perf\analyze_bot_perf.py --budget .\tools\bot_perf\default_soak_budget.json "$($soakReport.scenarios[0].stdout_path)"
```

For current `high_bot_soak_degradation` reports, the same default-budget result is already available under `$soakReport.scenarios[0].perf_budget`.

Fresh current-source high-bot soaks also have a stricter source-counter lane:

```powershell
python .\tools\bot_perf\analyze_bot_perf.py --budget .\tools\bot_perf\source_counter_soak_budget.json .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
```

`default_soak_budget.json` stays compatible with older pre-counter or partial-counter soak logs by treating CPU/source-derived checks as optional warnings. `source_counter_soak_budget.json` is intentionally stricter: it requires `source_counter_pass_int=1`, all current source-counter groups, and current bot-frame, route-query, route-reuse, Q3A-route, memory, visibility, and entity-trace metrics. The scenario harness evaluates both budgets for `high_bot_soak_degradation`; `perf_budget` remains the default lane and `perf_budgets` contains both lane results.

Budget files have checks under:

- `checks.metrics`: derived analyzer fields such as `commands_per_bot_sec`, `route_refresh_ratio`, and `debug_work_units_per_bot_sec`.
- `checks.status`: raw final status fields such as `route_failures`, `route_invalid_slots`, and `skipped_inactive`.

Each check supports numeric `min`, numeric `max`, optional boolean `required`, and optional `description`.

The default soak budget makes these high-bot invariants required:

- Smoke pass, ten-minute duration slack, and exactly eight detected bots.
- Sustained command and route-command throughput through both derived per-bot/sec rates and raw final counters.
- Zero route failures, zero invalid route slots, zero route-debug missing frames, and zero inactive target-bot skips.
- Regular soak progress reports.

It also keeps pressure budgets for route-query rate, route refresh/reuse ratios, debug work, and recovery command churn. It intentionally does not constrain `item_goal_active_reservations` or `item_goal_peak_active_reservations`; the long-soak policy allows item-reservation occupancy to decay as pickups are consumed, hidden, cleared, blacklisted, and reassigned.

CPU/source-counter checks such as `bot_frame_cpu_ms_per_bot_sec`, `route_query_cpu_ms_per_bot_sec`, `route_reuse_cpu_ms_per_bot_sec`, and `q3a_route_cpu_ms_per_bot_sec` are present but optional. Missing optional fields appear as budget warnings, not failures, until long-soak source-counter baselines are stable.

Budgeted source-counter fields now carry explicit diagnostics:

- `source_counter_status`, `source_counter_pass`, and `source_counter_pass_int` summarize whether every known source-counter group was present in the log.
- `source_counter_groups_present_count` and `source_counter_groups_missing_count` make the readiness state easy to compare across runs.
- `missing_current_counters` lists missing source-counter groups with their primary counter and accepted current counter names.
- When `--budget` is used, flat fields such as `budget_status`, `budget_pass_int`, `budget_required_failed`, `budget_optional_missing`, and `budget_missing_current_counters` are added to each JSON/CSV run row.
- The nested `budget.missing_current_counters` diagnostics map a missing budget metric back to the raw source-counter inputs that would satisfy it, for example `route_query_cpu_ms_per_bot_sec` to `route_query_cpu_ns` or `bot_route_cpu_ms`.

The strict source-counter soak budget checks `source_counter_pass_int` with `min=1` and `max=1`, so legacy pre-counter logs should be evaluated with the default budget unless the goal is to prove current telemetry completeness.

Repeated current-source soaks also have a comparison-level variance lane:

```powershell
python .\tools\bot_perf\analyze_bot_perf.py --budget .\tools\bot_perf\source_counter_soak_budget.json --variance-budget .\tools\bot_perf\source_counter_variance_budget.json --markdown-out .tmp\bot_perf\source_counter_variance.md .tmp\bot_perf\soak-a.stdout.txt .tmp\bot_perf\soak-b.stdout.txt
```

The variance budget is evaluated across all supplied logs after the per-run budget. It requires at least two like-for-like runs by default and fails when comparison guards detect mixed scenario names, bot counts, duration sources, or missing duration data. Each `checks.metrics` entry can constrain `max_delta`, `max_relative_range`, or `max_relative_range_pct`; missing required metrics fail, while optional metrics can be marked with `"required": false`.

For post-change verification, prefer the repeatable orchestrator so every run uses the same scenario harness settings and the analyzer receives a merged scenario report with duration metadata:

```powershell
python .\tools\bot_perf\run_source_counter_variance_soak.py --artifact-dir .tmp\bot_perf\post_change_source_counter_variance --json-out .tmp\bot_perf\post_change_source_counter_variance.json --markdown-out .tmp\bot_perf\post_change_source_counter_variance.md
```

The orchestrator runs `high_bot_soak_degradation` twice by default, captures each scenario report under `run-01` and `run-02`, writes `combined-scenario-report.json`, and then invokes `analyze_bot_perf.py` with `source_counter_soak_budget.json` plus `source_counter_variance_budget.json`. Use `--dry-run` to inspect the planned commands without launching the ten-minute soaks.

Exit behavior:

- `0`: all required checks passed.
- `1`: at least one required per-run or variance check failed or was missing.

## Comparison Reports

When more than one log is supplied, text output includes a comparison section with latest, best, and worst values for key metrics.

Comparison output also emits guard warnings when the input set is not like-for-like, such as mixed scenario names, mixed bot counts, mixed duration sources, or missing duration data. The report still prints the table, but those guards mean the best/worst values should be read as an overview rather than a strict regression ranking.

Write a Markdown report:

```powershell
python .\tools\bot_perf\analyze_bot_perf.py --budget .\tools\bot_perf\default_soak_budget.json --markdown-out .tmp\bot_perf\soak_compare.md .tmp\q3a_bot_nav_soak_10min_final.stdout.txt .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
```

The Markdown report contains run, comparison, and budget tables. Run rows include source-counter pass/fail state, missing source-counter group counts, budget warning counts, and missing-current-counter counts. `.tmp/bot_perf/` is a good local scratch location for generated reports.

When `--variance-budget` is supplied, the Markdown report also includes a variance-budget table with each checked metric, its observed value count, range, relative range percentage, limits, and pass/fail message.

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
python -m py_compile .\tools\bot_perf\analyze_bot_perf.py .\tools\bot_perf\run_source_counter_variance_soak.py .\tools\bot_perf\test_analyze_bot_perf.py .\tools\bot_perf\test_run_source_counter_variance_soak.py
```
