# Q3A BotLib Bot Performance Telemetry Analyzer

Date: 2026-06-18

Related tasks: `DV-05-T02`, `DV-05-T03`, `DV-05-T05`, `DV-07-T02`, `FR-04-T16`

## Summary

This slice adds a self-contained analyzer for the existing Q3A BotLib frame-command smoke logs. The tool consumes one or more dedicated-server stdout/log captures, extracts `q3a_bot_frame_command_smoke_soak` progress/completion lines plus the final `q3a_bot_frame_command_status`, and reports derived per-second and per-bot metrics for the current route-command soak path.

The analyzer is intentionally read-only and tooling-only. It does not change game code, server code, BotLib imports, the Meson build, or canonical project tracking docs. It establishes the first reusable budget-facing baseline from the ten-minute eight-bot `mm-rage` soak while documenting the counters still needed for direct CPU and visibility-trace budgets.

## Tooling

New tool:

- `tools/bot_perf/analyze_bot_perf.py`
- `tools/bot_perf/test_analyze_bot_perf.py`
- `tools/bot_perf/README.md`

Usage:

```powershell
python .\tools\bot_perf\analyze_bot_perf.py .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
python .\tools\bot_perf\analyze_bot_perf.py --format json .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
python .\tools\bot_perf\analyze_bot_perf.py --format csv .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
python .\tools\bot_perf\analyze_bot_perf.py --budget .\tools\bot_perf\default_soak_budget.json .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
python .\tools\bot_perf\analyze_bot_perf.py --budget .\tools\bot_perf\default_soak_budget.json --markdown-out .tmp\bot_perf\soak_compare.md .tmp\q3a_bot_nav_soak_10min_final.stdout.txt .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
python .\tools\bot_perf\analyze_bot_perf.py --scenario-report .tmp\bot_scenarios\latest_report.json --format csv ".tmp\bot_scenarios\20260618T085132Z\*.stdout.txt"
python .\tools\bot_perf\analyze_bot_perf.py --scenario-report .tmp\bot_scenarios\latest_report.json --markdown-out .tmp\bot_perf\scenario_compare.md ".tmp\bot_scenarios\20260618T085132Z\*.stdout.txt"
```

The default text report is meant for quick local review. JSON and CSV output are available for future trend capture or CI budget comparisons. When `--budget` is provided, the analyzer exits with `0` when all required thresholds pass and `1` when any required threshold fails or is missing.

The parser is tolerant of noisy server output before the marker text. This matters because the current long-soak stdout can glue bot/server chatter to the front of a progress line before `q3a_bot_frame_command_smoke_soak_progress`.

Glob inputs are accepted by the analyzer. Quote globs in PowerShell, for example `".tmp\bot_scenarios\20260618T085132Z\*.stdout.txt"`, so the tool owns expansion and ordering.

Scenario-runner stdout logs can be paired with `--scenario-report .tmp\bot_scenarios\latest_report.json`. The sidecar report supplies scenario names and `duration_seconds`, letting the analyzer compute per-second and per-bot rates for short scenario runs whose stdout does not include soak elapsed markers.

## Regression Tests

`tools/bot_perf/test_analyze_bot_perf.py` provides lightweight standard-library regression coverage without launching the game.

Run:

```powershell
python .\tools\bot_perf\test_analyze_bot_perf.py
python -m unittest .\tools\bot_perf\test_analyze_bot_perf.py
```

Coverage:

- Glued/noisy marker prefix parsing for soak begin, progress, complete, and final status lines.
- Soak duration selection from complete `elapsed_ms`.
- Fallback duration selection from the last progress line when no complete line exists.
- Budget pass and budget fail behavior through `evaluate_budget`.
- Budget failure exit code through `main()`.
- Multi-run comparison shape, including latest file, best/worst run, and budget pass/fail aggregation.
- Scenario report sidecar metadata, including scenario name and duration-derived per-bot rates.
- Optional real fixture validation for `.tmp/q3a_bot_nav_soak_10min_final.stdout.txt`; this test skips when the fixture is absent and checks the known baseline when it exists.

## Multi-Run Comparison Reports

The analyzer accepts multiple input logs. Text output still prints each run first, then adds a `comparison:` section when more than one input is present. The comparison section reports the latest value, best value, and worst value for key budget-facing metrics:

- `commands_per_bot_sec` where higher is better.
- `route_queries_per_bot_sec` where lower is better.
- `route_refresh_ratio` where lower is better.
- `route_reuse_ratio` where higher is better.
- `route_failures` where lower is better.
- `debug_work_units_per_bot_sec` where lower is better.
- `recovery_command_uses_per_bot_sec` where lower is better.
- `stuck_detections_per_sec` where lower is better.

When a budget is supplied, the comparison also reports budget pass/fail counts and per-input budget state.

JSON output remains a list for single-run analysis. For multiple inputs it becomes an object:

```json
{
  "runs": [],
  "comparison": {}
}
```

CSV output remains per-run rows so existing spreadsheet-style consumers keep working. Use text, JSON, or Markdown output for the comparison summary.

Markdown report generation:

```powershell
python .\tools\bot_perf\analyze_bot_perf.py --budget .\tools\bot_perf\default_soak_budget.json --markdown-out .tmp\bot_perf\soak_compare.md .tmp\q3a_bot_nav_soak_10min_final.stdout.txt .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
```

The generated Markdown report contains:

- A run table with smoke pass, budget pass, budget failure count, duration, bot count, throughput, route pressure, cache ratios, debug pressure, and recovery pressure.
- A comparison table with metric goal, latest value, best run, and worst run.
- A budget table when `--budget` is supplied.

## Scenario Log Baselines

Existing scenario artifacts under `.tmp/bot_scenarios/` were checked with:

```powershell
python .\tools\bot_perf\analyze_bot_perf.py --format csv ".tmp\bot_scenarios\*\*.stdout.txt"
python .\tools\bot_perf\analyze_bot_perf.py --scenario-report .tmp\bot_scenarios\latest_report.json --format csv ".tmp\bot_scenarios\20260618T085132Z\*.stdout.txt"
python .\tools\bot_perf\analyze_bot_perf.py --scenario-report .tmp\bot_scenarios\latest_report.json --markdown-out .tmp\bot_perf\scenario_compare.md ".tmp\bot_scenarios\20260618T085132Z\*.stdout.txt"
```

The full scenario stdout glob found ten logs across `20260618T084142Z`, `20260618T084310Z`, and `20260618T085132Z`. All ten contain a usable `q3a_bot_frame_command_status` record and all ten reported `pass=1` with `route_failures=0`. Those stdout logs do not contain soak elapsed markers, so without a scenario report they are usable for raw status counters and ratios only, not per-second baselines.

`.tmp/bot_scenarios/latest_report.json` supplies durations for the latest four scenario stdout logs under `20260618T085132Z`. Those four produce full derived rate records:

| Scenario | Pass | Duration Sec | Bots | Cmd/Bot/Sec | Route Query/Bot/Sec | Refresh Ratio | Reuse Ratio | Debug Work/Bot/Sec | Recovery Cmd/Bot/Sec | Route Failures |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `map_change_repeat` | 1 | 1.141 | 8 | 20.048 | 3.067 | 0.3077 | 0.6923 | 155.237 | 1.424 | 0 |
| `multi_bot_reservation` | 1 | 0.890 | 8 | 12.921 | 3.933 | 0.3043 | 0.6957 | 198.876 | 0.983 | 0 |
| `recover_from_stall` | 1 | 0.594 | 2 | 24.411 | 8.418 | 0.3448 | 0.6552 | 335.859 | 9.259 | 0 |
| `spawn_route_to_item` | 1 | 1.360 | 1 | 5.882 | 1.471 | 0.2500 | 0.7500 | 100.000 | 0.000 | 0 |

Generated comparison artifact:

- `.tmp/bot_perf/scenario_compare.md`

The scenario comparison intentionally does not use `tools/bot_perf/default_soak_budget.json`; that budget is calibrated for the ten-minute eight-bot soak, not sub-two-second scenario smoke runs.

## Budget Thresholds

New default example budget:

- `tools/bot_perf/default_soak_budget.json`

Budget files use schema `worr-bot-perf-budget-v1` and keep thresholds under `checks.metrics` and `checks.status`.

Example:

```json
{
  "checks": {
    "metrics": {
      "commands_per_bot_sec": {
        "min": 20,
        "max": 80
      },
      "route_refresh_ratio": {
        "max": 0.55
      }
    },
    "status": {
      "route_failures": {
        "max": 0
      },
      "skipped_inactive": {
        "max": 0
      }
    }
  }
}
```

Threshold objects support numeric `min`, numeric `max`, optional boolean `required`, and optional text `description`. `required` defaults to `true`. Missing required metrics fail the budget; missing optional metrics are reported as warnings. `metrics` checks read the analyzer's derived output, while `status` checks read raw key-value counters from the final `q3a_bot_frame_command_status` line.

The first default soak budget is intentionally generous around the current ten-minute eight-bot `mm-rage` baseline:

- Requires the smoke `pass=1`.
- Requires duration between `540` and `720` seconds.
- Requires exactly eight detected bots.
- Allows `20` to `80` commands/bot/sec around the current `40.007`.
- Caps route queries at `25`/bot/sec around the current `11.119`.
- Caps route refresh ratio at `0.55` and requires route reuse ratio at least `0.40`.
- Caps debug work units at `1500`/bot/sec around the current `566.767`.
- Caps recovery command uses at `45`/bot/sec around the current `15.014`.
- Requires raw `route_failures=0`, `route_invalid_slots=0`, `route_debug_missing_frames=0`, and `skipped_inactive=0`.

## Derived Metrics

The tool derives rates from the final soak `elapsed_ms` when present. For the current smoke format, the bot count is taken from the soak completion `count`, then falls back to soak begin `count`/`target`, then to `expected_min_commands` or `expected_min_frames`.

Current derived metrics include:

- Throughput: `commands/sec`, `commands/bot/sec`, and `frames/sec`.
- Route pressure: `route_requests/sec`, `route_queries/sec`, `route_refreshes/sec`, `route_reuses/sec`, per-bot route request/query/refresh/command rates, route query/refresh/reuse ratios, and route failures.
- Debug-output pressure: route debug routes/goals per second, direct debug primitives per second, polyline segments per second, combined debug work units per second, and combined debug work units per bot per second.
- Recovery pressure: stuck detections per second, stuck recovery activations per second, recovery command uses per second, and recovery command uses per bot per second.
- Goal churn: route goal assignments per second, item goal assignments per second, item reservation skips per second, and peak active item reservations.
- Soak cadence: progress report count plus average/min/max progress interval seconds.

## Validation Baseline

Validation commands:

```powershell
python -m py_compile .\tools\bot_perf\analyze_bot_perf.py
python -m py_compile .\tools\bot_perf\analyze_bot_perf.py .\tools\bot_perf\test_analyze_bot_perf.py
python .\tools\bot_perf\test_analyze_bot_perf.py
python -m unittest .\tools\bot_perf\test_analyze_bot_perf.py
python .\tools\bot_perf\analyze_bot_perf.py .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
python .\tools\bot_perf\analyze_bot_perf.py --format json .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
python .\tools\bot_perf\analyze_bot_perf.py --format csv .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
python .\tools\bot_perf\analyze_bot_perf.py --budget .\tools\bot_perf\default_soak_budget.json .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
python .\tools\bot_perf\analyze_bot_perf.py --format json --budget .\tools\bot_perf\default_soak_budget.json .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
python .\tools\bot_perf\analyze_bot_perf.py --budget .\tools\bot_perf\default_soak_budget.json .tmp\q3a_bot_nav_soak_10min_final.stdout.txt .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
python .\tools\bot_perf\analyze_bot_perf.py --format json --budget .\tools\bot_perf\default_soak_budget.json .tmp\q3a_bot_nav_soak_10min_final.stdout.txt .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
python .\tools\bot_perf\analyze_bot_perf.py --budget .\tools\bot_perf\default_soak_budget.json --markdown-out .tmp\bot_perf\soak_compare.md .tmp\q3a_bot_nav_soak_10min_final.stdout.txt .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
python .\tools\bot_perf\analyze_bot_perf.py .tmp\q3a_bot_nav_soak_redirect_short.stdout.txt .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
python .\tools\bot_perf\analyze_bot_perf.py --format csv ".tmp\bot_scenarios\*\*.stdout.txt"
python .\tools\bot_perf\analyze_bot_perf.py --scenario-report .tmp\bot_scenarios\latest_report.json --format json ".tmp\bot_scenarios\20260618T085132Z\*.stdout.txt"
python .\tools\bot_perf\analyze_bot_perf.py --scenario-report .tmp\bot_scenarios\latest_report.json --markdown-out .tmp\bot_perf\scenario_compare.md ".tmp\bot_scenarios\20260618T085132Z\*.stdout.txt"
```

All validation commands passed.

Parser/analyzer regression result:

```text
Ran 7 tests
OK
```

The sixth test is fixture-aware. In the current workspace the real ten-minute soak fixture exists, so it validated:

- `pass=1`
- `route_failures=0`
- `commands_per_bot_sec=40.007`, accepted near `40`
- `route_refresh_ratio=0.2851`, accepted near `0.285`
- default budget result `pass=true`

When `.tmp/q3a_bot_nav_soak_10min_final.stdout.txt` is not present, the test uses `skipTest` and the synthetic parser/analyzer tests still run.

Budget validation result:

```text
budget: pass checks=16 path=tools\bot_perf\default_soak_budget.json
```

Failure-path validation used a transient budget requiring `commands_per_bot_sec >= 999999`; the analyzer returned exit code `1` and printed:

```text
failure: metrics.commands_per_bot_sec=40.007 is below min 999999
```

Multi-run validation with the ten-minute soak file passed twice reported:

```text
comparison: runs=2 latest=.tmp\q3a_bot_nav_soak_10min_final.stdout.txt
  budget: passed=2 failed=0 latest=pass
```

The matching JSON validation reported `runs=2`, `comparison_metrics=8`, and `budget_failed=0`. Single-run JSON compatibility was also checked and still returns a list with one run.

Markdown validation wrote `.tmp/bot_perf/soak_compare.md` with `Runs`, `Comparison`, and `Budget` tables. A no-budget mixed comparison using the short soak plus the ten-minute soak reported different best/worst selections, including:

```text
commands/bot/sec: latest=40.007 best=40.457 (run 1) worst=40.007 (run 2)
route queries/bot/sec: latest=11.119 best=11.119 (run 2) worst=11.349 (run 1)
debug work units/bot/sec: latest=566.767 best=566.767 (run 2) worst=620.248 (run 1)
recovery commands/bot/sec: latest=15.014 best=10.875 (run 1) worst=15.014 (run 2)
```

Baseline file: `.tmp/q3a_bot_nav_soak_10min_final.stdout.txt`

Source smoke status:

- `pass=1`
- `elapsed_ms=600001`
- `frames=192036`
- `commands=192036`
- `route_requests=187232`
- `route_queries=53372`
- `route_refreshes=53372`
- `route_reuses=133860`
- `route_commands=192036`
- `route_failures=0`
- `route_debug_routes=187232`
- `route_debug_goals=187232`
- `route_debug_lines=892164`
- `route_debug_crosses=374464`
- `route_debug_arrows=187232`
- `route_debug_labels=187232`
- `route_debug_polyline_segments=1079396`
- `stuck_detections=11789`
- `stuck_recovery_activations=11789`
- `recovery_command_uses=72066`
- `route_goal_assignments=4889`
- `item_goal_assignments=1451`
- `item_goal_reservation_skips=3455`
- `item_goal_peak_active_reservations=2`
- `expected_min_commands=8`

Derived baseline:

- Duration: `600.001` seconds.
- Bot count: `8`.
- Command throughput: `320.059` commands/sec, `40.007` commands/bot/sec.
- Frame-command throughput: `320.059` frames/sec.
- Route requests: `312.053`/sec, `39.007`/bot/sec.
- Route queries: `88.953`/sec, `11.119`/bot/sec.
- Route refreshes: `88.953`/sec, `11.119`/bot/sec.
- Route reuses: `223.100`/sec, `27.887`/bot/sec.
- Route query ratio: `0.2851`.
- Route refresh ratio: `0.2851`.
- Route reuse ratio: `0.7149`.
- Route failures: `0`.
- Route debug routes/goals: `312.053` each per second.
- Direct debug primitives: `2735.149`/sec.
- Debug polyline segments: `1798.990`/sec.
- Combined debug work units: `4534.139`/sec, `566.767`/bot/sec.
- Stuck detections: `19.648`/sec.
- Stuck recovery activations: `19.648`/sec.
- Recovery command uses: `120.110`/sec, `15.014`/bot/sec.
- Route goal assignments: `8.148`/sec.
- Item goal assignments: `2.418`/sec.
- Item reservation skips: `5.758`/sec.
- Progress reports: `9`, average interval `60.011` seconds, min `60.000`, max `60.027`.

## Instrumentation Gaps

The current smoke status does not expose direct CPU milliseconds or visibility trace counts. The analyzer reports these missing fields explicitly so future source-side instrumentation can be added without changing the reporting workflow.

Source-counter planning lives in `docs-dev/q3a-botlib-bot-perf-source-counters-2026-06-18.md`.

Recommended source counters for a follow-up source-owning slice:

- In `src/game/sgame/bots/bot_brain.cpp`: per-frame and cumulative bot brain CPU time, preferably `bot_frame_cpu_ns` and `bot_frame_cpu_samples`, gated behind a perf/debug cvar.
- In `src/game/sgame/bots/bot_nav.cpp`: route query CPU time, route cache hit/miss timing, and optional maximum single-query time, preferably `bot_route_cpu_ns`, `bot_route_cpu_samples`, and `bot_route_cpu_max_ns`.
- In the Q3A bridge visibility callbacks under `src/game/sgame/bots/q3a/q3a_botlib_import.c` or their current WORR-facing owner: cumulative `aas_inpvs_checks`, `aas_inphs_checks`, and visibility row/decompression counts if the callback path can distinguish them cheaply.
- In entity trace and collision callback paths: cumulative visibility/collision trace attempts and hit/miss counters if the team wants to budget trace pressure separately from route-query pressure.
- In the final `q3a_bot_frame_command_status` emitter: include the above counters in the existing key-value line so `tools/bot_perf/analyze_bot_perf.py` can pick them up without a second log format.

Until those counters exist, the actionable budget proxy is route/debug/recovery pressure:

- Route recomputation currently refreshes on about `28.51%` of route requests.
- Debug overlay pressure is high with both route and goal debug enabled: about `4534.139` combined debug work units/sec across eight bots.
- Recovery pressure is also high: about `120.110` recovery command uses/sec and `19.648` stuck detections/sec across the run.

## Residual Risks

- The tool reports derived rates from cumulative smoke counters; it does not sample live frame-time or CPU time.
- Per-bot values are estimates from aggregate counters divided by the detected bot count, not individual bot-slot telemetry.
- Debug work units are a practical pressure proxy. They combine direct line/cross/arrow/label counts and polyline segment counts, but they are not renderer draw-call timings.
- Visibility pressure cannot be measured directly until the source counters above are emitted.
- Logs with multiple complete smoke runs in one file currently report the last final status and aggregate progress lines in that file. Use one log per run for clean baselines.
- Multiple-run JSON intentionally changes shape from a run list to an object with `runs` and `comparison`; single-run JSON remains list-shaped for compatibility.
- Best/worst ties currently report the first matching run.
