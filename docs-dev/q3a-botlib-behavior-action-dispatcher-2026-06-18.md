# Q3A BotLib Behavior Action Dispatcher Slice - 2026-06-18

Task: `FR-04-T15`

## Summary

This slice creates the first WORR-native behavior/action boundary above the fake-client route-command path without taking ownership of navigation or current `bot_brain.*` movement policy. The new files compile into `sgame` and expose small, explicit APIs for future item, weapon, combat, inventory, and world-use decisions.

The inherited Q2R bot export/action helper layer is gone, so future bot behavior needs a dedicated action dispatcher instead of embedding item or weapon policy in `bot_nav.*`. This slice adds that boundary but leaves it intentionally unwired until the owner of `bot_brain.*` can choose the frame point where action decisions should combine with route movement.

## Files

- `src/game/sgame/bots/bot_items.*`: item-candidate scoring and item decision counters.
- `src/game/sgame/bots/bot_combat.*`: combat/weapon intent scoring and combat decision counters.
- `src/game/sgame/bots/bot_actions.*`: public dispatcher, entity snapshot helper, command-button application helper, and aggregate action counters.
- `meson.build`: adds the three new implementation units to `sgame_src`.

## Public API

`bot_actions.hpp` exports the integration surface expected by a later `bot_brain.*` slice:

- `BotActions_ResetStatus()`
- `BotActions_BuildContext(const gentity_t *bot)`
- `BotActions_Decide(const BotActionContext &context)`
- `BotActions_ApplyDecision(const BotActionDecision &decision, usercmd_t *cmd)`
- `BotActions_GetStatus()`
- `BotActions_IntentName(BotActionIntent intent)`

The context is deliberately policy-neutral. It can carry health, armor, weapon readiness, generic item candidate information, enemy/perception facts, world-use requests, and inventory-use requests without depending on any `bot_nav.*` route type.

`bot_items.hpp` exports:

- `BotItems_ResetStatus()`
- `BotItems_Evaluate(const BotItemContext &context)`
- `BotItems_GetStatus()`
- `BotItems_DecisionName(BotItemDecisionKind kind)`

`bot_combat.hpp` exports:

- `BotCombat_ResetStatus()`
- `BotCombat_Evaluate(const BotCombatContext &context)`
- `BotCombat_GetStatus()`
- `BotCombat_DecisionName(BotCombatDecisionKind kind)`

## Current Behavior

The dispatcher can represent these intents:

- `MoveToItem`
- `SwitchWeapon`
- `Attack`
- `UseWorld`
- `UseInventory`
- `None`

`BotActions_BuildContext()` snapshots safe bot-local state from `gentity_t`: alive/valid bot status, client index, health, armor, current weapon, pending/preferred weapon, weapon ammo, weapon readiness, and whether an assigned enemy is alive. It does not perform visibility, aiming, perception, inventory-use, trigger-use, or nav-item candidate discovery.

`BotActions_Decide()` evaluates item and combat policy, then chooses the highest-priority action. Combat can request a weapon switch or attack when a caller supplies a shootable visible enemy. Item policy can request movement toward a generic item candidate when a caller supplies candidate entity/item/score data. World-use and inventory-use requests are represented as explicit context flags.

`BotActions_ApplyDecision()` only applies command buttons that are safe to express through `usercmd_t` today: `BUTTON_ATTACK` and `BUTTON_USE`. Weapon switching and inventory use are counted as pending intent because they still need a validated bot command/inventory dispatch path and should not resurrect the removed legacy helper surface.

## Hardening Follow-Up

A narrow header/API hardening pass documents the ownership invariants future callers need before wiring this boundary into `bot_brain.*`:

- `bot_actions.hpp` now states that action intents are vocabulary only, route ownership remains in `bot_nav.*`, `BotActionContext` is caller-owned frame data, `BotActions_ApplyDecision()` only mutates `usercmd_t` buttons, and status references are borrowed process-local counters.
- `bot_items.hpp` now states that item candidates come from the current item/nav owner and that item decisions do not discover, reserve, mutate, or clear route goals.
- `bot_combat.hpp` now states that perception facts are caller-owned, visibility and shootability are separate gates, weapon switching is intent-only, and firing does not own aim or movement.

The implementation also adds local invariants:

- Compile-time checks keep each `None` enum value at zero so default-constructed contexts, decisions, and statuses remain no-op safe.
- `BotActions_ApplyDecision()` rejects malformed command-button combinations. Only `Attack` may press `BUTTON_ATTACK`; only `UseWorld` may press `BUTTON_USE`; `SwitchWeapon`, `UseInventory`, and `MoveToItem` remain intent/status-only.
- Priority selection now ignores candidates with `None` intent or non-positive priority.

## Intentionally Not Wired

This slice does not edit `bot_brain.*`, `bot_nav.*`, `bot_think.*`, `botlib_adapter.*`, or `src/server/*`.

The following work remains for later integration:

- Call `BotActions_BuildContext()`, enrich the context with perception/nav facts, and call `BotActions_Decide()` from the frame-command path.
- Feed item candidates from the current route/item-goal owner without moving item-goal ownership out of `bot_nav.*`.
- Add validated weapon-switch and inventory-use command dispatch.
- Add perception and aim policy so `enemyVisible` and `enemyShootable` become real bot facts rather than caller-provided fields.
- Decide how action counters should be surfaced in the existing frame-command smoke/status output.

## Validation

Commands run:

```powershell
meson compile -C builddir-win
python .\tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas
```

Results:

- The first full build after adding the new units passed.
- After the no-op item-decision fix, the broad default build recompiled `bot_actions.cpp`, `bot_combat.cpp`, and `bot_items.cpp`, linked `sgame_x86_64.dll`, and then stopped later while writing the unrelated `worr_updater_x86_64.pdb` with a filesystem I/O error.
- The hardening follow-up full build passed, including `bot_actions.cpp`, `bot_combat.cpp`, `bot_items.cpp`, and `sgame_x86_64.dll`; Ninja still reported the existing `premature end of file; recovering` warning.
- Install refresh passed and repackaged the staged runtime under `.install`.
- `maps/mm-rage.aas` remained represented in `.install\basew\pak0.pkz` with SHA-256 `6459585e3c15eaa4170e23ca7465fc8255bd95b9b59d42e8615c39a67b707f9c`.

## Residual Risks

- The action boundary is compile-ready but unused until a later owner wires it into `bot_brain.*`.
- The first weapon preference model only distinguishes current and pending/preferred weapon state; real preference scoring from profiles and situational weapon utility is still pending.
- `SwitchWeapon` and `UseInventory` remain intent-only because the safe command path for those actions has not been implemented yet.
- Item candidate scoring accepts generic candidate facts but does not discover or reserve item goals itself; nav ownership stays with the existing route/item-goal code.
