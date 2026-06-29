# Q3A BotLib Source-Counter Variance Soak Runner - 2026-06-29

Tasks: `FR-04-T16`, `DV-03-T05`, `DV-05-T05`

## Summary

Added `tools/bot_perf/run_source_counter_variance_soak.py` as the repeatable
post-change evidence runner for the M7 source-counter soak lane. The existing
manual process had the correct pieces, but it required operators to run two
long `high_bot_soak_degradation` scenarios, find both stdout logs, preserve
scenario duration metadata, and then invoke `analyze_bot_perf.py` with both the
strict per-run budget and the comparison-level variance budget.

The new runner standardizes that workflow:

- Runs `high_bot_soak_degradation` two times by default through
  `tools/bot_scenarios/run_bot_scenarios.py`.
- Writes per-run scenario reports, Markdown reports, and captured
  runner stdout/stderr under `run-01`, `run-02`, and later numbered run
  directories.
- Extracts the scenario-owned stdout paths from each run report.
- Writes `combined-scenario-report.json` so `analyze_bot_perf.py` receives
  like-for-like scenario name and duration metadata for every stdout log.
- Invokes `analyze_bot_perf.py --budget source_counter_soak_budget.json
  --variance-budget source_counter_variance_budget.json`.
- Writes a top-level orchestrator JSON report and the analyzer Markdown
  comparison report.
- Supports `--dry-run` for reviewing planned commands without launching the
  ten-minute soaks.

## Operator Command

Default post-change run:

```powershell
python .\tools\bot_perf\run_source_counter_variance_soak.py --artifact-dir .tmp\bot_perf\post_change_source_counter_variance --json-out .tmp\bot_perf\post_change_source_counter_variance.json --markdown-out .tmp\bot_perf\post_change_source_counter_variance.md
```

Dry-run command shape check:

```powershell
python .\tools\bot_perf\run_source_counter_variance_soak.py --dry-run --artifact-dir .tmp\bot_perf\source_counter_variance_runner_dry --json-out .tmp\bot_perf\source_counter_variance_runner_dry.json
```

The default `--runs 2` setting matches
`tools/bot_perf/source_counter_variance_budget.json`. Higher repeat counts can
be used for local investigation, but release evidence should keep the
artifact directory and JSON report so the compared stdout logs are traceable.

## Evidence Contract

This runner does not replace either underlying gate. Each scenario run still
has to pass the scenario harness and its `perf_budget` / `perf_budgets` lanes.
The final analyzer step then hard-gates the strict current-source budget for
each stdout log and the repeated-run variance budget across all logs.

The top-level JSON report records:

- The exact scenario and analyzer commands.
- The per-run scenario reports and stdout paths.
- The combined scenario report path.
- The analyzer stdout/stderr captures.
- The nested analyzer JSON output, including `comparison.variance_budget`.

That makes it suitable for attaching to the bot release acceptance evidence
set after movement, combat, routing, or packaging behavior changes.

## Validation

Implemented tests in
`tools/bot_perf/test_run_source_counter_variance_soak.py` covering:

- Dry-run command/report generation without requiring a built install.
- Scenario stdout-path extraction from a scenario harness report.
- Combined scenario report metadata for two source runs.
- Analyzer command construction with strict and variance budget inputs.
- Rejection of `--runs 1`, because variance comparison needs at least two
  runs.

Validation commands:

```powershell
python -m pytest tools\bot_perf\test_run_source_counter_variance_soak.py -q
python -m py_compile tools\bot_perf\run_source_counter_variance_soak.py tools\bot_perf\test_run_source_counter_variance_soak.py
```

Both passed locally on 2026-06-29.
