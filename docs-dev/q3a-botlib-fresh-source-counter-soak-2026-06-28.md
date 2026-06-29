# Q3A BotLib Fresh Source-Counter Soak Round - 2026-06-28

Task IDs: `FR-04-T16`, `DV-03-T05`, `DV-05-T02`, `DV-05-T05`

## Summary

This round closes the first M7 fresh source-counter soak slice. The manual
ten-minute, eight-bot `high_bot_soak_degradation` scenario now evaluates the
default bot perf budget directly through the scenario harness, and the fresh
soak artifact includes complete CPU, route, visibility, trace, entity-trace,
and memory source-counter groups.

## Implementation

- Added a compact `perf_budget` result block to implemented scenario rows whose
  degradation policy names a JSON budget under `tools/bot_perf/`.
- The scenario runner now loads `tools/bot_perf/analyze_bot_perf.py`, evaluates
  `tools/bot_perf/default_soak_budget.json`, and fails the scenario if required
  derived budget checks fail.
- Scenario Markdown/text reports now show the perf-budget state beside the
  degradation policy, including source-counter pass/fail, warning, failure, and
  missing-current-counter counts.
- Scenario comparison reports now include prefixed `perf_*` derived metrics so
  future high-bot reports can show budget-rate drift.
- Fixed the perf analyzer to merge repeated `q3a_bot_frame_command_status` and
  `q3a_bot_source_counter_status` lines with later duplicate fields winning.
  The long soak emits detailed route-cache counters and later compact summary
  status lines; replacing the status dictionary hid `route_queries`,
  `route_refreshes`, and `route_reuses`.
- Updated `tools/bot_perf/default_soak_budget.json` to the fresh source-counter
  route-cache baseline: `route_queries_per_bot_sec <= 30`,
  `route_refresh_ratio <= 0.65`, and `route_reuse_ratio >= 0.35`.

## Evidence

- `python -m pytest tools\bot_perf\test_analyze_bot_perf.py -q` passed:
  `14 passed`.
- `python -m pytest tools\bot_scenarios\test_run_bot_scenarios.py -q` passed:
  `57 passed`.
- First source-counter soak artifact:
  `.tmp\bot_scenarios\fresh_source_counter_soak\20260628T085637Z`.
  It proved source-counter completeness and exposed the analyzer merge gap.
- Final green source-counter soak report:
  `.tmp\bot_scenarios\fresh_source_counter_soak_pass_report.json`.
- Final green scenario artifact:
  `.tmp\bot_scenarios\fresh_source_counter_soak_pass\20260628T090904Z`.
- Standalone perf Markdown report:
  `.tmp\bot_perf\fresh_source_counter_soak_pass_20260628T090904Z.md`.

Final pass metrics from the scenario `perf_budget` block:

- Overall scenario summary: `1` passed, `0` failed, `0` timeouts, `0` errors.
- Degradation policy: `passed`.
- Perf budget: `pass`, `0` failures, `0` warnings, `0` optional missing
  fields, `0` missing current counters.
- Source counters: `pass`; present groups are `bot_frame_cpu`,
  `route_query_cpu`, `q3a_route_cpu`, `q3a_memory`, `visibility`,
  `static_bsp_trace`, and `entity_trace`; missing groups: none.
- Duration: `600.004` seconds of soak time, `601.438` seconds scenario runtime.
- Throughput: `40.007` commands/bot/sec and `40.007`
  route-commands/bot/sec.
- Route pressure: `22.373` route queries/bot/sec, `0.5736` refresh ratio,
  `0.4264` reuse ratio.
- CPU: `2.671` bot-frame ms/bot/sec, `0.181`
  route-query ms/bot/sec, `0.002` route-reuse ms/bot/sec, and `0.132`
  Q3A-route ms/bot/sec.
- Recovery/debug pressure: `4.091` recovery commands/bot/sec and `449.55`
  debug work units/bot/sec.
- Progress cadence: `9` progress reports, roughly one per minute.

## Notes

The default budget still keeps source-counter-derived CPU checks optional so
legacy pre-counter logs can be analyzed.

2026-06-29 follow-up: the strict current-source lane now exists as
`tools/bot_perf/source_counter_soak_budget.json` and is evaluated by the
scenario harness as an additional `perf_budgets` profile for
`high_bot_soak_degradation`.
