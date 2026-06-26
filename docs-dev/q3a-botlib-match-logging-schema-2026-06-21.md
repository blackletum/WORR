# Q3A BotLib Match Logging Schema Round

Date: 2026-06-21

Tasks: `FR-07-T03`, `DV-03-T05`, `FR-04-T16`, `DV-07-T06`

## Summary

This round adds explicit schema and artifact version metadata to WORR match logging JSON exports, then promotes a deterministic smoke scenario that proves the metadata through the real native exporters.

The match-stats artifact now emits:

- `schemaName`: `worr.match_stats`
- `schemaVersion`: `1`
- `artifactType`: `match_stats`
- `artifactVersion`: `1`

The tournament-series artifact now emits:

- `schemaName`: `worr.tournament_series`
- `schemaVersion`: `1`
- `artifactType`: `tournament_series`
- `artifactVersion`: `1`

## Implementation Notes

- `src/game/sgame/match/match_logging.cpp` owns the schema constants and stamps the top-level metadata before writing match-stats and tournament-series JSON.
- `MATCH_LOGGING_STATUS_API_V1` exposes a narrow `PrintSchemaStatus` bridge so the server smoke can validate the exported shape without needing to finish a whole live match.
- `sv_bot_matchlog_smoke 2` runs a zero-bot schema proof, builds a sample match and series through the normal JSON builders, and emits `q3a_match_logging_schema`.
- `match_logging_schema` is now part of the default implemented scenario suite and hard-gates the schema names, schema versions, artifact types, artifact versions, players/event-log arrays, series matches array, embedded match schema version, and final zero-bot cleanup.

## Validation

- `python -m pytest tools\bot_scenarios\test_run_bot_scenarios.py -k match_logging_schema`
- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`
- `meson compile -C builddir-win worr_ded_engine_x86_64 sgame_x86_64`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas`
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario match_logging_schema --timeout 60 --base-port 28200 --format text --json-out .tmp\bot_scenarios\match_logging_schema_report.json --markdown-out .tmp\bot_scenarios\match_logging_schema_report.md`
- Focused `match_logging_schema` passed from `.tmp\bot_scenarios\20260621T161415Z`.
- Full implemented suite passed with 50 passed, 0 failed, 0 timed out, 0 errored, and 0 pending from `.tmp\bot_scenarios\20260621T161434Z`.

## Provenance

No Q3A, BSPC, or q2proto source was imported or modified. This is WORR-owned schema metadata, status-extension, server-smoke, harness, and documentation work.
