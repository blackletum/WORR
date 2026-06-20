# Q3A BotLib Carried Arsenal Selection

Date: 2026-06-20

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-07-T06`

## Summary

This pass expands weapon selection from a current-vs-pending comparison into a
small carried-arsenal scan. After the action context has been enriched with
blackboard/smoke enemy facts, the action layer scans weapons already present in
the bot inventory and asks the existing combat scorer whether each candidate
beats the current weapon by the normal switch margin.

The scanner is conservative:

- it only runs for valid, alive bots with a known combat target;
- it defers when the weapon system already has a pending weapon switch;
- it only recommends a switch when the combat scorer selects the carried
  candidate over the current weapon;
- range, ammo, readiness, splash safety, and enemy health/armor estimates stay
  centralized in `bot_combat.*`.

## Code Changes

- `bot_actions.*`
  - Adds `BotActions_EnrichCombatInventory(...)`, which scans carried weapon
    items and updates `BotCombatContext::preferredWeapon*` to the best carried
    candidate when the scorer recommends a switch.
  - Adds status counters for inventory scans, carried candidates, ready
    candidates, selections, switch recommendations, keep-current results,
    pending-switch deferrals, no-enemy skips, and no-candidate skips.

- `bot_brain.cpp`
  - Calls the carried-arsenal enrichment after perception/smoke enemy facts are
    applied and before `BotActions_Decide(...)`.
  - Extends compact `q3a_bot_action_status` output with action weapon-inventory
    scan counters and the last selected carried weapon/reason.

- `tools/bot_scenarios/run_bot_scenarios.py`
  - Registers the new action weapon-inventory fields as optional action
    dispatch telemetry.

## Follow-Up

This is weapon-only inventory policy. Non-weapon inventory and powerup use still
need broader decision ownership, including when to trigger power armor, timed
powerups, and utility items without interfering with movement/combat decisions.

## Validation

Commands run across the carried-arsenal and follow-on inventory-policy stack:

- `ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_bots_bot_actions.cpp.obj sgame_x86_64.dll.p/src_game_sgame_bots_bot_brain.cpp.obj`
- `python tools/bot_scenarios/test_run_bot_scenarios.py`
- `ninja -C builddir-win sgame_x86_64.dll`
- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas`
- `git diff --check`

Result: passed. Ninja printed the existing `premature end of file; recovering`
warning during build steps, and `git diff --check` only reported the existing
CRLF normalization warnings.
