# Q3A BotLib Match Logging Catalog Round

Date: 2026-06-21

Tasks: `FR-07-T03`, `DV-03-T05`, `FR-04-T16`, `DV-07-T04`, `DV-07-T06`

## Summary

This round extends the match logging schema work with a downstream discovery
artifact. Successful match-stat and tournament-series exports now update
`basew/matches/catalog.json` beside the emitted artifacts. The catalog is a
small JSON index with stable schema metadata, relative artifact paths, and
latest-artifact pointers so external tools do not need to crawl directory
contents or infer filenames.

## Runtime Changes

- Added `worr.match_catalog` schema metadata with schema/artifact version `1`.
- Added a thread-safe catalog update path guarded by a match logging catalog
  mutex, because normal match exports run on the detached match-stats worker
  while tournament series exports can run on the game thread.
- Catalog entries now record:
  - source artifact type and schema metadata,
  - artifact ID,
  - relative `jsonPath` and optional `htmlPath`,
  - match map/gametype/ruleset/timing/player/team summary fields,
  - tournament series best-of, win target, gametype, and match count.
- Existing catalog files are validated before append. Invalid or mismatched
  catalog metadata is rebuilt rather than extended with ambiguous data.

## Validation Surface

`MatchLogging_PrintSchemaStatus()` now builds sample match-stats and
tournament-series artifacts, builds a sample catalog from the same helper used
by the live writer path, and writes/re-reads a scratch catalog under
`.tmp/match_logging_catalog_smoke`. The dedicated server smoke emits a new
`q3a_match_logging_catalog` marker and folds that result into the existing
`sv_bot_matchlog_smoke 2` pass/fail result.

The `match_logging_schema` scenario now requires:

- `catalog_schema_name=worr.match_catalog`,
- catalog schema/artifact version `1`,
- `catalog_artifact_count=2`,
- `latest_match_stats=schema-smoke-match`,
- `latest_tournament_series=schema-smoke-series`,
- relative paths `schema-smoke-match.json` and
  `series_schema-smoke-series.json`,
- `catalog_write_pass=1` with at least two retained scratch catalog entries.

## Files

- `src/game/sgame/match/match_logging.cpp`
- `tools/bot_scenarios/run_bot_scenarios.py`
- `tools/bot_scenarios/test_run_bot_scenarios.py`
- `tools/bot_scenarios/README.md`
- `docs-user/competitive-server-tools.md`
- `docs-dev/plans/q3a-botlib-aas-port.md`
- `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`
- `docs-dev/q3a-botlib-aas-credits.md`

## Validation

- `python -m pytest tools\bot_scenarios\test_run_bot_scenarios.py -k match_logging_schema`
- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`
- `meson compile -C builddir-win worr_ded_engine_x86_64 sgame_x86_64`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas`
- `python tools\bot_scenarios\run_bot_scenarios.py --install-dir .install --scenario match_logging_schema`
- `python -m pytest tools\bot_scenarios\test_run_bot_scenarios.py`

Focused scenario result:

- `.tmp\bot_scenarios\20260621T163834Z`: 1 passed, 0 failed, 0 timed out,
  0 errored, 0 pending.

## Provenance

No upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were imported
or modified. This is WORR-native match logging, scenario-harness, and
documentation work.
