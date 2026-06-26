# Q3A BotLib Competitive Server Tools Documentation Round

Date: 2026-06-21

Tasks: `FR-07-T05`, `DV-07-T04`, `DV-07-T06`

## Summary

This round adds the operator-facing documentation pass for the competitive
server tooling that has been hardened through the recent bot and match-system
smokes. The new guide is practical user documentation under `docs-user/` and
collects the expected cvars, commands, and operational notes for:

- warmup and bot practice gates,
- player voting and vote limits,
- MyMap, queued nextmap, and map selector behavior,
- Duel queue behavior,
- tournament veto and replay flow,
- admin commands used by competitive operators,
- match logging exports and schema names.

## Files

- `docs-user/competitive-server-tools.md`: new operator guide for competitive
  server setup and troubleshooting.
- `docs-user/server-quickstart.md`: links the competitive guide from the
  practical server setup path.
- `docs-user/server.asciidoc`: points the long server manual at the focused
  practical guides.
- `docs-dev/plans/q3a-botlib-aas-port.md`: advances the Phase 8 documentation
  checklist and outstanding-work summary.
- `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`: marks
  `FR-07-T05` complete and updates `DV-07-T04` user-doc parity progress.
- `docs-dev/q3a-botlib-aas-credits.md`: records this as WORR-native
  documentation/provenance work.

## Operator Contract

The guide deliberately documents the public operator surface instead of the
internal `sv_bot_*_smoke` cvars. It names the cvars and commands operators are
expected to use directly:

- `g_allow_voting`, `g_allow_vote_mid_game`, `g_allow_spec_vote`,
  `g_vote_limit`, and `g_vote_flags`.
- `g_maps_selector`, `g_maps_mymap`, `g_allow_mymap`,
  `g_maps_mymap_queue_limit`, `g_maps_pool_file`, and `g_maps_cycle_file`.
- `g_allow_duel_queue`.
- `match_setup_type`, `match_setup_bestof`, and `g_tourney_cfg`.
- `g_statex_enabled`, `g_statex_humans_present`, and
  `g_statex_export_html`.
- Client/admin commands including `callvote`, `vote`, `mymap`,
  `tourney_status`, `tourney_pick`, `tourney_ban`, `replay`, `lock_team`,
  `unlock_team`, `set_map`, `next_map`, `start_match`, and `reset_match`.

The guide also calls out the bot boundaries proven by the scenario suite:
bots cannot call or cast votes, cannot cross the admin boundary, are rejected
from tournament veto identity, and do not satisfy human-presence requirements
for match logging when `g_statex_humans_present 1` is active.

## Validation

This is a documentation-only implementation round. No runtime code, build
files, generated assets, or `.install/` payloads were changed.

Validation performed:

- `rg` source checks for documented cvars and commands in `g_main.cpp`,
  `command_voting.cpp`, `command_client.cpp`, `command_admin.cpp`, and
  `match_logging.cpp`.
- Documentation link/progress grep after patching.
- `git diff --check`.

## Provenance

No upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were imported
or modified. This is WORR-native user and development documentation based on
the existing local implementation.
