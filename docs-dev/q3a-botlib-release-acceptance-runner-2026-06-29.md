# Q3A BotLib Release Acceptance Runner

Date: 2026-06-29

Tasks: `FR-04-T07`, `FR-04-T16`, `DV-07-T06`

## Summary

This round adds the first executable M8 release-readiness dry run for bots.
`tools/bot_release/run_bot_acceptance.py` turns the scattered release checklist
into a single report that validates public controls, profiles, packaging,
staged AAS, user docs, the multiplayer playtest plan, playtest triage coverage,
bot perf budget tooling, and scenario evidence.

The runner is intentionally artifact-oriented. It does not replace a final
build, full scenario rerun, source-counter soak, or manual execution of the
playtest cases, but it does prove the current staged install and manual
validation scaffold are internally consistent before those more expensive
checks are run.

## Implementation

- Added `tools/bot_release/run_bot_acceptance.py`.
  - Runs the public bot surface audit from `tools/bot_surface`.
  - Runs first-party profile validation from `tools/bot_profiles`.
  - Verifies `assets/botfiles/bots.txt` exposes the current first-party
    min-player rotation roster: `bulwark`, `relay`, `smoke`, `vanguard`, and
    `vector`.
  - Reuses `tools/package_assets.py` release-payload helpers to validate
    authored botfiles and the staged `.install/basew/pak0.pkz` plus loose
    `.install/basew/botfiles` mirror.
  - Verifies the staged reference AAS files are present for `mm-rage`, `q2dm1`,
    `q2dm2`, `q2dm8`, `q2ctf1`, `base1`, `base2`, and `train`.
  - Verifies core user-facing bot docs exist and include the expected setup and
    command/default surface, including `docs-user/bot-cvars.md`.
  - Verifies the multiplayer playtest generator covers FFA, Duel, TDM, and CTF,
    exercises `bot_min_players`, uses release AAS-gated maps, and keeps
    generated configs off legacy-prefixed or smoke-only bot controls.
  - Verifies the multiplayer playtest triage catalog can classify every
    generated failure signal into scenario-candidate categories.
  - Verifies the bot perf default budget, strict source-counter budget,
    repeated-run variance budget, and perf README variance docs are present
    and parse successfully.
  - Consumes a scenario JSON report, or discovers the best local report under
    `.tmp/bot_scenarios`, then gates on at least 114 rows, a clean summary,
    and required representative rows across spawn, arbitration, combat, FFA,
    Duel, CTF, coop, and movement/hazard coverage.
  - Emits text or JSON output and writes reports under `.tmp/` when requested.
- Added `tools/bot_release/test_run_bot_acceptance.py`.
  - Covers scenario report success/failure gates.
  - Covers missing scenario artifact warning mode.
  - Covers `bots.txt` roster failures.
  - Covers the generated multiplayer playtest plan gate.
  - Covers the generated multiplayer playtest triage gate.
  - Covers the bot perf budget/variance tooling gate.
  - Runs the current repository acceptance gate against
    `.tmp\bot_scenarios\implemented_hazard_context.json`.
- Added `tools/bot_release/README.md`.

## Acceptance Result

The current acceptance artifact is:

`.tmp\bot_release\bot_release_acceptance.json`

It reports 11/11 checks passing:

- public bot surface: 94 bot cvars, 5 commands, 13 public defaults, 0 violations;
- profile pack: 5 files, 5 profiles, 0 errors, 0 warnings;
- `bots.txt`: 5 first-party entries;
- authored botfiles: 31 release members;
- staged botfiles: 31 mirrored/archive members, staged archive present;
- staged AAS: 8 required maps, 11,043,980 total AAS bytes;
- user docs: setup/cvar/profile/map-readiness/playtest docs present;
- playtest plan: 4 cases covering FFA, Duel, TDM, and CTF;
- playtest triage: generated failure signals map to scenario candidates;
- perf tooling: 2 per-run budgets and 1 variance budget validate;
- scenario evidence: 114 total rows, 114 passed, 0 failed/timeouts/errors,
  20 movement-tagged rows, and all 8 required representative scenarios present.

## Validation

Passed:

```powershell
python -m py_compile tools\bot_playtest\generate_bot_playtest.py tools\bot_playtest\triage_bot_playtest.py tools\bot_playtest\test_generate_bot_playtest.py tools\bot_playtest\test_triage_bot_playtest.py tools\bot_release\run_bot_acceptance.py tools\bot_release\test_run_bot_acceptance.py
python tools\bot_release\run_bot_acceptance.py --scenario-report .tmp\bot_scenarios\implemented_hazard_context.json
python tools\bot_release\run_bot_acceptance.py --scenario-report .tmp\bot_scenarios\implemented_hazard_context.json --format json --output .tmp\bot_release\bot_release_acceptance.json
python -m pytest tools\bot_playtest\test_generate_bot_playtest.py tools\bot_playtest\test_triage_bot_playtest.py tools\bot_release\test_run_bot_acceptance.py tools\bot_surface\test_audit_bot_surface.py -q
```

Follow-up acceptance still needs a fresh local build/`.install` refresh, a
post-build full scenario rerun, two fresh source-counter variance soaks after
the next behavior change, and representative manual playtests.
