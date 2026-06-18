# Q3A BotLib Behavior Action Brain Telemetry Bridge - 2026-06-18

Task: `FR-04-T15`

## Summary

This follow-up wires the new WORR-native behavior/action dispatcher into the bot frame-command path as read-only telemetry. `BotBrain_BuildFrameCommand()` now samples `BotActions_BuildContext()` and `BotActions_Decide()` once per active bot command frame before route-command construction, then discards the decision.

This intentionally does not call `BotActions_ApplyDecision()`. The current smoke behavior remains owned by the existing route/fake-client command path, so this bridge cannot make bots fire weapons, use world triggers, switch weapons, use inventory, or alter movement.

## Code Boundary

- `bot_brain.cpp` includes `bot_actions.hpp` only for telemetry sampling and status reporting.
- The helper `Bot_CommandSampleActionDecision()` builds the current action context and evaluates it without mutating `usercmd_t`.
- `BotBrain_PrintFrameCommandStatus()` emits a separate `q3a_bot_action_status` line after the existing frame-command/nav policy status output.
- The action status line avoids a `commands=` field name so the existing server smoke status capture continues to match the primary `q3a_bot_frame_command_status` line first.

The dispatcher boundary still owns action vocabulary and counters only. Route selection, route refresh, recovery movement, item-goal reservation, and AAS navigation remain outside this slice.

## Telemetry Added

The new `q3a_bot_action_status` line reports aggregate dispatcher, item, and combat counters:

- Action evaluations, invalid/dead contexts, item/combat evaluations, decision counts, pending weapon/inventory intents, and applied command/button counts.
- Last sampled action client, intent, priority, item/entity, weapon item, and intent name.
- Item policy evaluations, invalid candidates, reservation deferrals, seek decisions, low-health/low-armor boosts, and last item decision fields.
- Combat policy evaluations, no-enemy/blocked-sight/fire/switch counters, withheld-fire counters, and last combat decision fields.

Expected current smoke behavior is telemetry-only: `action_applied_cmds=0`, `action_applied_attack_buttons=0`, and `action_applied_use_buttons=0`.

## Intentionally Not Enabled

- No call to `BotActions_ApplyDecision()` is made from runtime bot command generation.
- No item candidates are fed from nav/item-goal ownership yet.
- No visibility, aim, or shootability facts are enriched beyond the current safe context snapshot.
- No weapon-switch or inventory-use dispatch path is enabled.
- Existing route recovery may still press `BUTTON_USE` through its own path; action telemetry distinguishes that by keeping action-applied counters at zero.

## Validation

Commands run:

```powershell
meson compile -C builddir-win
python .\tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas
& .\.install\worr_ded_x86_64.exe +set basedir E:\Repositories\WORR\.install +set game basew +set net_port 27961 +set logfile 1 +set logfile_name q3a_bot_action_telemetry_frame_command_smoke +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sg_bot_debug_route 1 +set sg_bot_debug_goal 1 +set sv_bot_frame_command_smoke 17 +map mm-rage | Select-String -Pattern 'q3a_bot_frame_command_status|q3a_bot_action_status|pass=|action_applied'
ninja -C builddir-win sgame_x86_64.dll
python .\tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas
& .\.install\worr_ded_x86_64.exe +set basedir E:\Repositories\WORR\.install +set game basew +set net_port 27962 +set logfile 1 +set logfile_name q3a_bot_action_telemetry_frame_command_smoke_final +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sg_bot_debug_route 1 +set sg_bot_debug_goal 1 +set sv_bot_frame_command_smoke 17 +map mm-rage | Select-String -Pattern 'q3a_bot_frame_command_status|q3a_bot_action_status|pass=|action_applied'
```

Results:

- The broad build pass linked the updated `sgame_x86_64.dll`; Ninja still printed the existing `premature end of file; recovering` warning.
- A later no-op `meson compile -C builddir-win` retry timed out after 124 seconds without returning output, leaving child compiler/linker processes running. Those child processes were allowed to drain before any further build validation.
- The narrow `ninja -C builddir-win sgame_x86_64.dll` target passed, compiling `bot_combat.cpp`, `bot_items.cpp`, `bot_actions.cpp`, and `bot_brain.cpp`, then linking `sgame_x86_64.dll`.
- Install refresh passed after the binary relink, rebuilt `.install`, repacked `basew/pak0.pkz`, and confirmed `maps/mm-rage.aas` remained represented in the staged archive.
- Focused frame-command smoke passed before and after the final install refresh. The final smoke reported `q3a_bot_frame_command_status ... frames=92 commands=92 ... pass=1`.
- The final new action telemetry line reported `action_evaluations=92`, `action_noop_decisions=92`, `action_applied_cmds=0`, `action_applied_attack_buttons=0`, and `action_applied_use_buttons=0`.

## Residual Risks

- Action status counters currently accumulate like the existing process-local bot status counters; there is no dedicated runtime reset hook beyond the dispatcher reset API.
- The bridge samples only the base context. Real item, perception, aim, and inventory facts still need later owners to enrich the context before decisions become meaningful.
- Future runtime use must opt into `BotActions_ApplyDecision()` deliberately and preserve command ownership so action policy does not leak into nav code.
