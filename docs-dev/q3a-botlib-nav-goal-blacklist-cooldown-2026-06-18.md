# Q3A BotLib Nav Goal Blacklist Cooldown

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice adds a short per-bot item-goal blacklist cooldown to the WORR-native `bot_nav` route goal layer. When the stuck-progress watchdog fires while the bot still has a live active-pickup goal, the current item identity is blacklisted for that bot, the persistent item goal is cleared with a dedicated blacklist clear reason, and the route scan immediately looks for a different pickup.

The goal is to keep the existing stuck repath and short recovery movement from repeatedly selecting the same reachable-but-locally-blocked item. The cooldown stays per bot and per item identity, using entity number, spawn count, and item id so a respawned or changed pickup is not accidentally treated as the same blocked target.

## Implementation

- Added per-client item-goal blacklist state to `bot_nav` route slots: entity number, spawn count, item id, and expiry frame.
- Added `BotNavGoalClearReason::Blacklisted` and preserved the active short recovery window when a goal is cleared only because it was blacklisted.
- Stuck detections now activate both the existing recovery window and the item-goal blacklist when the current persistent goal is still an available pickup.
- Active-pickup scans skip a matching blacklisted item for that bot, record skip counters, and continue scoring other available pickups.
- `BotNavRouteStatus` and the dedicated frame-command smoke now report blacklist activations, skips, active cooldown count, last blacklisted entity, owning client, and frames remaining.

No new upstream Q3A source files were imported. The work is WORR-native navigation policy over the existing BotLib/AAS adapter and imported Q3A route-query primitives.

## Validation

- `meson compile -C builddir-win`
- `python .\tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas`
- Normal two-bot smoke:
  - `.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set net_port 27915 +set logfile 1 +set logfile_name q3a_bot_nav_goal_blacklist_normal_smoke +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sg_bot_debug_route 1 +set sg_bot_debug_goal 1 +set sv_bot_frame_command_smoke 3 +map mm-rage`
  - Result: `frames=17`, `commands=17`, `route_failures=0`, `stuck_detections=0`, `stuck_recovery_activations=0`, `item_goal_blacklist_activations=0`, `item_goal_blacklist_skips=0`, `item_goal_blacklist_active=0`, `pass=1`.
- Forced stalled-command smoke:
  - `.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set net_port 27915 +set logfile 1 +set logfile_name q3a_bot_nav_goal_blacklist_forced_smoke +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sg_bot_debug_route 1 +set sg_bot_debug_goal 1 +set sv_bot_frame_command_smoke 4 +map mm-rage`
  - Result: `frames=29`, `commands=29`, `route_failures=0`, `stuck_detections=2`, `stuck_repath_refreshes=2`, `stuck_recovery_activations=2`, `item_goal_blacklist_activations=2`, `item_goal_blacklist_skips=2`, `item_goal_blacklist_active=2`, `last_item_goal_blacklisted_entity=68`, `last_item_goal_blacklisted_by_client=1`, `last_item_goal_blacklist_frames_remaining=96`, `last_goal_clear_reason=5`, `pass=1`.

## Follow-Up

- Movement-state handling still needs jump/crouch/swim/door/teleporter-specific command support.
- Door/trigger retry should become a more precise recovery path before any last-resort debug-only reset behavior is added.
- Failed-goal reason debug should graduate from the current blacklist-specific clear reason into a fuller route/goal failure taxonomy.
