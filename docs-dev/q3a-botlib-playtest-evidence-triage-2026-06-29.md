# Q3A BotLib Playtest Evidence Triage

Date: 2026-06-29

Task IDs: `FR-04-T04`, `FR-04-T06`, `FR-04-T07`, `FR-04-T16`, `DV-07-T06`

## Summary

This round completes the roadmap's M3/M8 playtest evidence triage slice. The
multiplayer playtest bundle now includes a structured operator notes template,
and a new triage tool converts those notes into release-friendly pass/fail
summaries plus scenario-candidate recommendations.

The intent is to keep manual Duel, CTF, FFA, and TDM play-depth validation from
turning into loose prose. When an operator records repeated or release-blocking
failure signals, the tool maps them to concrete regression categories that can
be promoted into automated scenario rows.

## Implementation

- Extended `tools/bot_playtest/generate_bot_playtest.py`.
  - Adds `worr.bot_playtest.notes.v1`.
  - Writes `bot_multiplayer_playtest_notes_template.json`.
  - Adds the notes template to the generated artifact map.
- Added `tools/bot_playtest/triage_bot_playtest.py`.
  - Reads `bot_multiplayer_playtest.json` plus operator notes.
  - Supports case outcomes: `pass`, `fail`, `blocked`, `pending`, and `skip`.
  - Classifies failure signals into route commitment, route stuck, close-threat
    spacing, weak-state retreat, min-player autofill, Duel queue/active-count,
    CTF objective response, and team fire/spacing categories.
  - Emits `bot_multiplayer_playtest_triage.json` and
    `bot_multiplayer_playtest_triage.md`.
  - Promotes repeated failures, and single critical release signals, into
    scenario-candidate recommendations.
- Added `tools/bot_playtest/test_triage_bot_playtest.py`.
  - Proves every generated failure signal is classifiable.
  - Proves pending notes produce a clean pending report.
  - Proves repeated weak-retreat failures and critical min-player/CTF signals
    become promoted scenario candidates.
  - Proves unknown case ids and unsupported outcomes generate warnings.
- Extended `tools/bot_release/run_bot_acceptance.py`.
  - Adds the `playtest_triage` acceptance gate.
  - Release acceptance now checks that generated playtest failure signals map to
    scenario-candidate categories.
- Updated `docs-user/bot-playtest.md`, `tools/bot_playtest/README.md`, and
  `tools/bot_release/README.md`.

## Artifacts

- `.tmp\bot_playtest\bot_multiplayer_playtest_notes_template.json`
- `.tmp\bot_playtest\bot_multiplayer_playtest_triage.json`
- `.tmp\bot_playtest\bot_multiplayer_playtest_triage.md`
- `.tmp\bot_release\bot_release_acceptance.json`

The fresh triage artifact is pending by design because no human playtest notes
have been filled in yet:

- cases: 4
- pending: 4
- failure signals: 0
- scenario candidates: 0
- warnings: 0

The refreshed release acceptance artifact reports 10/10 checks passing,
including `playtest_plan` and `playtest_triage`.

## Validation

Passed:

```powershell
python -m py_compile tools\bot_playtest\generate_bot_playtest.py tools\bot_playtest\triage_bot_playtest.py tools\bot_playtest\test_generate_bot_playtest.py tools\bot_playtest\test_triage_bot_playtest.py tools\bot_release\run_bot_acceptance.py tools\bot_release\test_run_bot_acceptance.py
python tools\bot_playtest\generate_bot_playtest.py --output-dir .tmp\bot_playtest
python tools\bot_playtest\triage_bot_playtest.py --plan .tmp\bot_playtest\bot_multiplayer_playtest.json --notes .tmp\bot_playtest\bot_multiplayer_playtest_notes_template.json
python -m pytest tools\bot_playtest\test_generate_bot_playtest.py tools\bot_playtest\test_triage_bot_playtest.py tools\bot_release\test_run_bot_acceptance.py tools\bot_surface\test_audit_bot_surface.py -q
python tools\bot_release\run_bot_acceptance.py --scenario-report .tmp\bot_scenarios\implemented_hazard_context.json --format json --output .tmp\bot_release\bot_release_acceptance.json
```

No native executable build is part of this tooling/documentation slice.

## Follow-Up

- Run the generated Duel and CTF play-depth cases on a fresh `.install` build.
- Fill in the notes template with real outcomes and rerun the triage tool.
- Convert promoted triage categories into scenario rows when failures repeat.
