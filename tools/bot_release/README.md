# WORR Bot Release Acceptance

`run_bot_acceptance.py` is the executable M8 release-readiness dry run for the
bot feature. It ties together the checks that used to live as separate manual
steps:

- public bot cvar/command surface audit;
- first-party profile validation;
- `botfiles/bots.txt` min-player roster exposure;
- authored botfile package contract;
- staged `.install/basew` archive and loose botfile mirror;
- staged generated AAS files for the current reference maps;
- user documentation presence, including the public bot cvar/default reference;
- multiplayer playtest plan coverage;
- multiplayer playtest triage category coverage;
- bot perf per-run and repeated-soak variance budget validity; and
- scenario report evidence for the current implemented catalog.

Run:

```powershell
python tools\bot_release\run_bot_acceptance.py --format json --output .tmp\bot_release\bot_release_acceptance.json
python tools\bot_release\run_bot_acceptance.py --scenario-report .tmp\bot_scenarios\implemented_hazard_context.json
python -m pytest tools\bot_release\test_run_bot_acceptance.py -q
```

The default gate expects at least 114 implemented scenario rows and a clean
scenario summary. Use `--allow-missing-scenario-report` only for environments
that have not generated local scenario artifacts yet.
