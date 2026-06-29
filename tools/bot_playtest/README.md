# WORR Bot Multiplayer Playtest Generator

`generate_bot_playtest.py` writes a repeatable operator checklist for the
current multiplayer bot behavior gates. It covers FFA, Duel, TDM, and CTF with
canonical `bot_` cvars and Q3-style bot commands.

`triage_bot_playtest.py` reads the generated checklist plus operator notes and
groups observed failures into scenario-candidate categories.

Run:

```powershell
python tools\bot_playtest\generate_bot_playtest.py --output-dir .tmp\bot_playtest
python tools\bot_playtest\generate_bot_playtest.py --output-dir .install\basew
python tools\bot_playtest\triage_bot_playtest.py --plan .tmp\bot_playtest\bot_multiplayer_playtest.json --notes .tmp\bot_playtest\bot_multiplayer_playtest_notes_template.json
python -m pytest tools\bot_playtest\test_generate_bot_playtest.py tools\bot_playtest\test_triage_bot_playtest.py -q
```

The default output under `.tmp\bot_playtest` is best for evidence and review.
When validating a staged build, generate into `.install\basew` so the produced
`bot_playtest_*.cfg` files can be launched from `.install` with `+exec`.

Generated artifacts:

- `bot_multiplayer_playtest.md`: human checklist with observations and failure
  signals.
- `bot_multiplayer_playtest.json`: machine-readable release evidence.
- `bot_multiplayer_playtest_notes_template.json`: operator notes template for
  recording pass/fail outcomes and failure signals.
- `bot_multiplayer_playtest_triage.*`: triage output after notes are processed.
- `bot_playtest_*.cfg`: per-mode setup configs.

The generator intentionally resets all mode-specific bot policy cvars before
enabling the cvars for a case. That keeps FFA, Duel, TDM, and CTF observations
independent when they are run in sequence.
