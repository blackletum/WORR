# Q3A BotLib Strict Source-Counter Budget Lane

Date: 2026-06-29

Tasks: `FR-04-T16`, `DV-03-T05`, `DV-05-T02`, `DV-05-T05`

## Summary

This round promotes the fresh high-bot source-counter soak from a one-off
observation into a maintained validation lane. The existing
`default_soak_budget.json` remains the compatibility budget for older logs and
partial source-counter captures. The new
`source_counter_soak_budget.json` is intentionally strict and is scoped to
fresh `high_bot_soak_degradation` runs with current telemetry.

## Changes

- Added `tools/bot_perf/source_counter_soak_budget.json`.
  - Requires the smoke pass, ten-minute duration slack, exactly eight bots,
    sustained command and route-command throughput, clean final route status,
    progress reports, all current source-counter groups, no memory or trace
    failure counters, and current bot-frame, route-query, route-reuse, and
    Q3A-route CPU derived metrics.
  - Keeps route pressure thresholds aligned with the 2026-06-28 source-counter
    soak baseline: route query rate, route refresh ratio, route reuse ratio,
    debug work, and recovery command churn.
- Extended `high_bot_soak_degradation` with an additional budget profile while
  preserving `default_soak_budget.json` as the primary `perf_budget` lane.
- Added scenario-harness support for multiple perf budgets per degradation
  policy.
  - `perf_budget` remains the first/default budget for compatibility.
  - `perf_budgets` carries every evaluated budget lane.
  - Markdown/text output lists each budget by profile name.
  - Scenario comparisons now expose per-profile pass metrics and an aggregate
    `perf_budget_all_pass_int` metric.
- Extended compact perf summaries with source-counter pass/group counts plus
  memory, visibility, and entity-trace failure counters so strict budget
  failures are visible without opening the raw analyzer report.
- Updated bot perf and scenario README guidance for the default-versus-strict
  budget split.

## Validation

- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py tools\bot_perf\analyze_bot_perf.py`
- `python -m pytest tools\bot_scenarios\test_run_bot_scenarios.py tools\bot_perf\test_analyze_bot_perf.py -q`
  - Result: `72 passed`.

The focused tests now prove that a synthetic current-source high-bot soak passes
both budget lanes, while an otherwise valid legacy-style high-bot soak without
`q3a_bot_source_counter_status` still passes the default budget and fails the
strict source-counter lane with explicit missing-current-counter failures.

## Provenance

No new upstream Q3A, Gladiator, BSPC, idTech3, Quake3e, baseq3a, or `q2proto/`
source files were imported or modified. This is WORR-native tooling, budget,
and documentation work layered over the existing bot scenario and perf analyzer
surfaces.

## Follow-Up

Run one fresh high-bot source-counter soak after the next meaningful behavior
change and compare both `default_soak_budget.json` and
`source_counter_soak_budget.json`. If both lanes remain green across that
variance pass, keep the strict lane as the standard current-source gate for
manual high-bot acceptance.
