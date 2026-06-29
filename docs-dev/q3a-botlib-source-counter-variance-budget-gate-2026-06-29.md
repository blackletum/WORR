# Q3A BotLib Source-Counter Variance Budget Gate

Date: 2026-06-29

Tasks: `FR-04-T16`, `DV-03-T05`, `DV-05-T05`, `DV-07-T06`

## Purpose

This slice adds a repeated-run stability gate for the M7 bot performance lane.
The existing bot perf analyzer could validate a single soak against per-run
budgets and compare best/worst values across logs, but it could not fail a
set of like-for-like soaks when the run-to-run spread became unstable.

## Implementation

- Added `--variance-budget` support to
  `tools/bot_perf/analyze_bot_perf.py`.
- Added a new `worr-bot-perf-variance-budget-v1` JSON schema with:
  - top-level `min_runs`;
  - top-level `require_like_for_like`;
  - `checks.metrics.<metric>.max_delta`;
  - `checks.metrics.<metric>.max_relative_range`;
  - `checks.metrics.<metric>.max_relative_range_pct`;
  - optional metric checks via `"required": false`.
- Added comparison-level pass/fail output to text, JSON, and Markdown reports.
- Added `tools/bot_perf/source_counter_variance_budget.json` for repeated
  current-source `high_bot_soak_degradation` runs.
- Extended `tools/bot_release/run_bot_acceptance.py` with a `perf_tooling`
  check that validates the default budget, strict source-counter budget,
  variance budget, and README coverage.
- Updated `tools/bot_perf/README.md` and `tools/bot_release/README.md` so the
  variance gate is discoverable from the perf and release workflows.

## Validation

Focused tests:

```text
python -m pytest tools\bot_perf\test_analyze_bot_perf.py -q
18 passed

python -m pytest tools\bot_release\test_run_bot_acceptance.py -q
8 passed
```

CLI proof:

```text
python tools\bot_perf\analyze_bot_perf.py --budget tools\bot_perf\source_counter_soak_budget.json --variance-budget tools\bot_perf\source_counter_variance_budget.json --format json --markdown-out .tmp\bot_perf\source_counter_variance_gate.md --scenario-report .tmp\bot_scenarios\fresh_source_counter_soak_pass_report.json <fresh-source-stdout> <fresh-source-stdout>
```

The same-log control passed the strict per-run source-counter budget twice and
the comparison-level variance budget:

```text
per-run budget: passed=2 failed=0 latest_pass=true
variance budget: status=pass pass_int=1 checks=14 failed=0 warnings=0
source counters: pass, 7 groups present
```

Artifacts:

- `.tmp\bot_perf\source_counter_variance_gate.json`
- `.tmp\bot_perf\source_counter_variance_gate.md`

## Follow-Ups

- Run two fresh high-bot source-counter soaks after the next behavior-changing
  bot slice and compare them with `source_counter_variance_budget.json`.
- Tighten variance thresholds after a few real repeated-run samples from
  different developer machines.
