# Q3A BotLib Utility and Deployable Inventory Policy

Date: 2026-06-20

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-07-T06`

## Summary

This pass extends the carried non-weapon inventory policy into a conservative
utility/deployable layer. The action layer can now use environment and support
items when the bot is already under clear pressure, while continuing to avoid
risky placement or random-movement items.

The new logic still flows through `BotActions_EnrichInventoryUse(...)`, normal
action arbitration, and the validated `use_index_only` dispatch path. No new
gameplay-side item callbacks or legacy Q2R bot hooks were introduced.

## Policy

New utility cases:

- `IT_POWERUP_ENVIROSUIT` is used while the bot is in lava/slime.
- `IT_POWERUP_REBREATHER` is used while the bot is underwater.
- `IT_IR_GOGGLES` are used when the bot has an enemy fact but the enemy is not
  currently visible.
- `IT_POWERUP_SILENCER` is used for actionable combat when no silencer shot
  counter is already active.

New deployable cases:

- Defender sphere may launch for combat or survival pressure.
- Hunter sphere may launch for actionable combat.
- Vengeance sphere may launch when the bot has enemy pressure and low
  health/armor.

The policy defers if a timed powerup/counter is already active, if the bot
already owns a sphere, or if a case still needs placement-aware validation.
This utility/sphere round did not enable nuke, doppelganger, or personal
teleporter; the follow-on escape/deployable round covers doppelganger and
teleporter while nuke remains pending.

## Code Changes

- `bot_actions.*`
  - Adds active powerup-count checks alongside active timer checks so silencer
    use does not repeatedly consume inventory while the counter is active.
  - Adds water/hazard pressure helpers based on `waterLevel`/`waterType`.
  - Expands inventory scoring for enviro suit, rebreather, IR goggles,
    silencer, and the three sphere items.
  - Adds counters for utility uses, environment uses, deployable uses, and
    owned-sphere deferrals.

- `bot_brain.cpp`
  - Extends compact and detailed `q3a_bot_action_status`/detail output with
    the new inventory-policy counters.

- `tools/bot_scenarios/run_bot_scenarios.py`
  - Registers the new counters in the optional inventory-policy field family.

## Follow-Up

The follow-on escape/deployable round adds placement-aware doppelganger probes
and strict personal teleporter escape gates. Future rounds should add
friendly-fire, objective-carrier, and blast-radius checks before using nuke-like
inventory.

## Validation

Commands run:

- `ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_bots_bot_actions.cpp.obj sgame_x86_64.dll.p/src_game_sgame_bots_bot_brain.cpp.obj`
- `python tools/bot_scenarios/test_run_bot_scenarios.py`
- `ninja -C builddir-win sgame_x86_64.dll`
- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas`
- `git diff --check`

Result: passed. Ninja printed the existing `premature end of file; recovering`
warning during build steps, and `git diff --check` only reported the existing
CRLF normalization warnings.
