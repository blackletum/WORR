# Q3A BotLib Safe Nuke Inventory Policy

Date: 2026-06-20

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-07-T06`

## Summary

This pass adds the first safe-use policy for carried nuke inventory. The action
layer can now request `IT_AMMO_NUKE`, but only after a conservative safety
screen passes. The item still flows through the normal `UseInventory` decision
and validated `use_index_only` dispatch path; the gameplay `Use_Nuke(...)` and
`fire_nuke(...)` callbacks remain authoritative.

The policy deliberately treats nuke as a high-value combat utility item rather
than an escape item. Because `RadiusNukeDamage(...)` ignores walls and nuke
friendly fire is not scaled out by the normal friendly-fire damage scale, the
action layer avoids teammate exposure and self-pressure before it asks to use
the item.

## Policy

Nuke use requires all of the following:

- deathmatch inventory semantics;
- the bot is not carrying a CTF objective item;
- an actionable visible/shootable enemy;
- enemy distance in a conservative mid-to-long band;
- no low-health, low-armor, hazard, or underwater self-pressure;
- a mostly clear launch trace in the current view direction;
- no live teammate within the nuke falloff-risk radius;
- enough estimated or observed enemy health/armor value to justify the risk.

Rejected nuke candidates still report deferral reason strings such as
`nuke_objective_carrier_deferred`, `nuke_target_too_close`,
`nuke_launch_blocked`, `nuke_friendly_exposure`, or `nuke_low_enemy_value`.

## Code Changes

- `bot_actions.*`
  - Adds nuke-specific mode, objective-carrier, launch-clear, teammate-exposure,
    enemy-value, and self-pressure gates.
  - Converts the prior unconditional nuke deferral into a scoring branch that
    can select `nuke_safe_combat` when every gate passes.
  - Adds counters for nuke safety checks, friendly deferrals, self deferrals,
    use selections, and general nuke deferrals.

- `bot_brain.cpp`
  - Emits the new nuke-policy counters in compact and detailed
    `q3a_bot_action_status` output.

- `tools/bot_scenarios/run_bot_scenarios.py`
  - Registers the new counters in the optional inventory-policy field family.

## Follow-Up

The policy is intentionally narrow. Future command ownership can improve nuke
timing by pairing use with explicit retreat or area-denial route goals, and team
modes can eventually reason about objective state beyond the immediate carrier
and teammate-exposure checks.

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
