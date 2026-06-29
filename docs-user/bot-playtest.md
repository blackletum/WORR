# Bot Multiplayer Playtest

Use this checklist when you want a quick, repeatable pass over bot behavior in
FFA, Duel, TDM, and CTF. It is meant for release candidates, local practice
server checks, and bug reports about roaming, close-range combat, retreating, or
objective play.

## Generate The Checklist

From the repository root:

```powershell
python tools\bot_playtest\generate_bot_playtest.py --output-dir .tmp\bot_playtest
```

For a staged build that should run the generated configs directly:

```powershell
python tools\bot_playtest\generate_bot_playtest.py --output-dir .install\basew
```

The generator writes:

- `bot_multiplayer_playtest.md`
- `bot_multiplayer_playtest.json`
- `bot_multiplayer_playtest_notes_template.json`
- one `bot_playtest_*.cfg` file per mode

The generated configs use the supported `bot_` cvars and Q3-style bot commands.
Use `botlist` after each map starts to confirm the bot slots and profile names.

## Run A Case

From `.install/`, after generating configs into `.install\basew`:

```powershell
.\worr_ded_x86_64.exe +set basedir . +set game basew +exec bot_playtest_ffa_practice.cfg
```

Replace the config name with the case you want to run:

- `bot_playtest_ffa_practice.cfg`
- `bot_playtest_duel_rotation.cfg`
- `bot_playtest_tdm_roles.cfg`
- `bot_playtest_ctf_objectives.cfg`

## What To Watch

A healthy bot pass should show bots moving through the map, collecting useful
items, reacting to nearby players, and changing plans when they are weak.

Treat these as failures worth reporting:

- a bot spins in one place for more than a few seconds with no visible target
- bots repeatedly stick to the same wall or corner after the map has AAS data
- bots walk into another player at point-blank range without strafing, backing
  away, or choosing a different target
- a weak bot keeps attacking a stacked enemy with only a poor weapon
- CTF bots ignore flag pickup, drop, return, or carrier-support moments
- `bot_min_players` does not fill to the target after the map has been running
  for several seconds

## Useful Manual Checks

After the generated case reaches its target population:

```text
botlist
addbot vanguard
removebot all
```

For auto-filled practice servers, leave `bot_profile` empty to rotate through
the bundled first-party bot profiles. Set a specific profile only when you want
to isolate one character during diagnosis.

## Report Notes

When reporting a bot behavior issue, include:

- map and mode
- generated config name
- `botlist` output
- profile names involved
- whether the bot was healthy, weak, stacked, carrying a flag, or near a teammate
- the visible failure signal and how long it lasted

## Triage Notes

After a playtest, fill in the generated
`bot_multiplayer_playtest_notes_template.json`:

- set each case `outcome` to `pass`, `fail`, `blocked`, `pending`, or `skip`
- paste useful `botlist` output into `botlist`
- copy any matching visible problems into `failure_signals`
- put new symptoms in `custom_failure_signals`
- add short reproduction steps when a failure repeats

Then run:

```powershell
python tools\bot_playtest\triage_bot_playtest.py --plan .tmp\bot_playtest\bot_multiplayer_playtest.json --notes .tmp\bot_playtest\bot_multiplayer_playtest_notes_template.json
```

The triage output groups failures into route, close-threat, weak-retreat,
min-player, Duel queue, CTF objective, and team-spacing categories. Repeated
or release-blocking failures are marked as scenario candidates for follow-up
automation.
