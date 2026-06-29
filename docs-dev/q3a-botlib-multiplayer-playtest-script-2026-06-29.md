# Q3A BotLib Multiplayer Playtest Script

Date: 2026-06-29

Task IDs: `FR-04-T04`, `FR-04-T06`, `FR-04-T07`, `FR-04-T16`, `DV-07-T06`

## Summary

This round implements the M3/M8 "Fresh multiplayer playtest script" slice from
the bot implementation completion roadmap. The new playtest generator turns the
current FFA, Duel, TDM, and CTF bot behavior expectations into concrete configs,
a human checklist, and a JSON artifact that can travel with release validation
notes.

The goal is to make live bot scrutiny repeatable for the exact behavior classes
that have driven recent bug reports: roaming, item pickup, close-range spacing,
weak-state retreat, Duel pacing, TDM role/friendly-fire behavior, and CTF
objective transitions.

## Implementation

- Added `tools/bot_playtest/generate_bot_playtest.py`.
  - Generates four playtest cases:
    - `ffa_practice` on `q2dm1`
    - `duel_rotation` on `q2dm1`
    - `tdm_roles` on `q2dm8`
    - `ctf_objectives` on `q2ctf1`
  - Writes per-case `bot_playtest_*.cfg` files.
  - Writes `bot_multiplayer_playtest.md` for manual observation.
  - Writes `bot_multiplayer_playtest.json` as structured evidence.
  - Follow-up triage tooling now also writes
    `bot_multiplayer_playtest_notes_template.json`; see
    `docs-dev/q3a-botlib-playtest-evidence-triage-2026-06-29.md`.
  - Resets all current mode-specific bot policy cvars before enabling each
    case's intended FFA, Duel, TDM, or CTF policy gates.
  - Uses only canonical `bot_` cvars and Q3-style commands in generated configs.
- Added `tools/bot_playtest/test_generate_bot_playtest.py`.
  - Guards required mode coverage.
  - Verifies generated markdown, JSON, and cfg artifacts.
  - Verifies generated cfg text does not contain legacy prefixed or smoke-only
    bot cvar names.
  - Verifies the checklist calls out the observed failure signals: spinning in
    place, wall-sticking, point-blank overlap, and blaster-only suicide pushes.
- Added `tools/bot_playtest/README.md`.
- Added `docs-user/bot-playtest.md` and linked it from the bot and server docs.
- Extended `tools/bot_release/run_bot_acceptance.py`.
  - Requires `docs-user/bot-playtest.md`.
  - Adds a `playtest_plan` acceptance check that confirms the generated plan
    covers FFA, Duel, TDM, and CTF, exercises `bot_min_players`, uses release
    AAS-gated maps, and passes the generator's own cvar-surface validation.

## Generated Case Intent

`ffa_practice` targets the most common live complaints first: bots should roam
when they do not see an enemy, pick items, avoid standing nose-to-nose with a
nearby player, and retreat when weak instead of forcing a poor blaster fight.

`duel_rotation` focuses on two-bot active Duel behavior, item-denial pacing,
spawn pressure, and queue boundaries when a human joins.

`tdm_roles` checks team spread, role routes, resource denial, friendly-fire
avoidance, and point-blank spacing around teammates and enemies.

`ctf_objectives` checks flag pickup/drop/return transitions, carrier support,
base return, and item roles on the way to objectives.

## Validation

Planned commands for this round:

```powershell
python tools\bot_playtest\generate_bot_playtest.py --output-dir .tmp\bot_playtest
python -m pytest tools\bot_playtest\test_generate_bot_playtest.py tools\bot_release\test_run_bot_acceptance.py tools\bot_surface\test_audit_bot_surface.py -q
python tools\bot_release\run_bot_acceptance.py --scenario-report .tmp\bot_scenarios\implemented_hazard_context.json --format json --output .tmp\bot_release\bot_release_acceptance.json
```

No native executable build is part of this documentation/tooling slice.

## Follow-Up

- Use the generated plan as the operator scaffold for the next Duel and CTF
  live-server play-depth passes.
- After a fresh build refreshes `.install/`, generate the configs into
  `.install\basew` and attach `bot_multiplayer_playtest.json` to the release
  acceptance evidence.
