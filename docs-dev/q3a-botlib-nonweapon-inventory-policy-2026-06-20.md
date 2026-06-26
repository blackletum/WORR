# Q3A BotLib Non-Weapon Inventory Policy

Date: 2026-06-20

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-07-T06`

## Summary

This pass gives the WORR-native action layer a first conservative policy for
using carried, non-weapon inventory. It builds on the existing exact
`use_index_only` dispatch path: the new logic enriches `BotActionContext` with
an inventory-use request, then lets the normal action arbitration and command
request validation submit the item use.

The policy is intentionally narrow. It avoids environment-only utility items,
spheres/deployables, active timed powerups, weapon items, and power armor that
is already enabled. The goal is to make common combat/survival powerups useful
without creating noisy inventory churn.

## Policy

`BotActions_EnrichInventoryUse(...)` scans carried usable non-weapon items and
scores only cases with clear pressure:

- damage boosts such as Quad/Double are used for visible or shootable enemies;
- protection powerups are used for enemy pressure, especially when health or
  armor is low;
- invisibility and mobility powerups are used only against actionable enemies;
- regeneration is used when health is low, or while hurt during actionable
  combat;
- power armor is enabled only when it is off, cells are available, and there is
  enemy or survival pressure.

The selected request uses a higher context-local inventory priority than manual
inventory intents so combat powerups can beat ordinary attack frames, while the
legacy default inventory-use priority remains available for externally supplied
requests.

## Code Changes

- `bot_actions.*`
  - Adds `BotActions_EnrichInventoryUse(...)`, a carried non-weapon inventory
    scan and scorer that sets `inventoryUseRequested`, `inventoryItem`,
    `inventoryUsePriority`, and `inventoryUseReason`.
  - Adds status counters for scans, candidates, usable candidates, selections,
    combat/survival/power-armor uses, existing-request deferrals, active-effect
    deferrals, no-cell skips, no-candidate skips, no-usable skips, and last
    selected item/score/priority/reason.

- `bot_items.*`
  - Exposes small public wrappers for special-item classification and power
    armor detection so action policy can reuse the item taxonomy already used
    by pickup scoring.

- `bot_brain.cpp`
  - Calls non-weapon inventory enrichment after enemy facts and carried-arsenal
    weapon enrichment, before `BotActions_Decide(...)`.
  - Extends compact and detailed action status output with
    `action_inventory_policy_*` and `last_action_inventory_policy_*` fields.

- `tools/bot_scenarios/run_bot_scenarios.py`
  - Registers the inventory-policy status fields as optional scenario evidence.

## Follow-Up

The follow-on utility/deployable rounds add conservative use for rebreather,
enviro suit, IR goggles, silencer, spheres, doppelganger, and personal
teleporter. Remaining inventory work should focus on safe nuke/friendly-fire
policy, deeper timing rules, and broader behavior ownership.
This pass does not change route ownership, item acquisition, or team/coop role
consumption.

## Validation

Commands run:

- `ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_bots_bot_actions.cpp.obj sgame_x86_64.dll.p/src_game_sgame_bots_bot_items.cpp.obj sgame_x86_64.dll.p/src_game_sgame_bots_bot_brain.cpp.obj`
- `python tools/bot_scenarios/test_run_bot_scenarios.py`
- `ninja -C builddir-win sgame_x86_64.dll`
- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas`
- `git diff --check`

Result: passed. Ninja printed the existing `premature end of file; recovering`
warning during build steps, and `git diff --check` only reported the existing
CRLF normalization warnings.
