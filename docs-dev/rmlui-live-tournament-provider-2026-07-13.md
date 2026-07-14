# RmlUi Live Tournament Provider

Date: 2026-07-13

Tasks: `FR-09-T08`, `FR-09-T09`, `FR-07-T02`, `DV-03-T07`,
`DV-04-T02`, and `DV-07-T04`

## Outcome

The four tournament session routes now truthfully declare version 2
`live-provider` status:

- `tourney_info`
- `tourney_mapchoices`
- `tourney_veto`
- `tourney_replay_confirm`

The implementation keeps match and tournament authority in sgame. RmlUi
consumes the state published immediately before each route opens and dispatches
the already registered participant or admin commands. The shared live
`ui_list` provider remains the child picker for map Pick, map Ban, and Replay
Game selection.

The central migration phase remains `controller_stub` until connected action
automation, localization/navigation review, and native renderer parity are
accepted.

## Native ownership and behavior

`OpenTournamentInfoMenu()` opens the guidance route through the normal
`pushmenu` bridge. The route retains the registered `tourney_status` console
reference without presenting the non-interactive reference text as a button.

`OpenTournamentMapChoicesMenu()` publishes ten bounded
`ui_tourney_mapchoice_line_*` cvars. Before the veto finishes, the first two
lines explain that the order is pending. After completion, sgame publishes the
numbered display names from the authoritative map order. Unused rows are
cleared and suppressed.

`OpenTournamentVetoMenu()` publishes the complete per-client state:

- inactive versus active veto;
- current Home/Away side and team or participant identity;
- observer/waiting copy;
- whether the current client may Pick;
- whether another Ban remains mathematically valid;
- picks needed and maps remaining.

Pick and Ban do not embed or duplicate a map provider in the veto document.
They call `worr_tourney_pick` or `worr_tourney_ban`; the command opens the
shared `ui_list`, which excludes maps already picked or banned and publishes
the exact validated action command for each remaining map. This corrects the
old metadata claim that the veto page owned an embedded candidate list.

Replay remains admin-only across `worr_tourney_replay_menu`,
`worr_tourney_replay_confirm`, and `worr_tourney_replay`. The list publishes
one confirmation action per locked game. Sgame then publishes the selected
game number and exact Yes command. The final replay handler rejects invalid
state and game numbers; on success it rebuilds wins through the preceding
game, truncates match winners/IDs/maps from the selected game onward, and
queues the selected map.

## UX corrections

- Info, Map Choices, and Veto now use one canonical top-left backplate;
  duplicate footer Back buttons were removed.
- Map Choices renders all server-published rows as a contained, wrapping
  report and keeps a direct-route empty state.
- Veto presents distinct inactive, waiting, actionable, and Ban-locked states.
- The Ban-locked state is a disabled semantic control with no command, so it
  cannot accidentally submit a ban.
- The two veto statistics render as complete equal-width server strings. This
  fixes the previous duplicated labels and incorrect second-row wrap.
- Replay is marked as destructive. No is authored before Yes and receives
  safe default focus. Yes uses the destructive skin.
- The replay prompt now states that results from the selected game onward are
  discarded, matching the native truncation behavior.

## Regression contract

`tools/ui_smoke/check_rmlui_tournament_provider.py` validates the four route
registries, native cvar/condition/command bridge, sgame publishers, participant
versus admin command flags, shared list-provider cases, replay reset fields,
document bindings and conditions, single-back ownership, safe replay order,
destructive copy/style, accessibility/layout hooks, metadata, and capture
registration.

The focused test module
`tools/ui_smoke/test_check_rmlui_tournament_provider.py` contains eleven
regressions covering publisher loss, map-row drift, veto action drift, false
embedded-list ownership, an actionable Ban-locked regression, destructive
warning loss, admin-guard loss, duplicate Back, metadata, and capture registry
drift. The complete `tools/ui_smoke` suite passes 327 tests.

## Build, staging, and evidence

`meson compile -C builddir-win` completed cleanly, including the sgame change
and the native Vulkan/RTX target builds. The canonical distributable was then
refreshed with:

```text
python tools/refresh_install.py --build-dir builddir-win \
  --install-dir .install --base-game basew --platform-id windows-x86_64
```

The refresh packed 308 assets and validated 214 RmlUi plus 31 bot
package/loose files. SHA-256 comparisons match source to canonical stage for
all four documents, `session.rcss`, `session/routes.json`, and the built/staged
`sgame_x86_64.dll`.

Seven accepted 960x720 OpenGL captures are under
`.tmp/rmlui/runtime-capture/tournament/`:

- `rmlui_tourney_info_live_provider_20260713`
- `rmlui_tourney_mapchoices_live_provider_20260713`
- `rmlui_tourney_veto_inactive_live_provider_20260713`
- `rmlui_tourney_veto_waiting_live_provider_20260713`
- `rmlui_tourney_veto_actor_live_provider_20260713`
- `rmlui_tourney_veto_ban_locked_live_provider_20260713`
- `rmlui_tourney_replay_confirm_live_provider_20260713`

The evidence covers pending and five-map order reporting; inactive, observer,
Pick/Ban, and Ban-locked veto states; and a destructive game-2 replay prompt.
Manual QA rejected incomplete early waiting/Ban-locked frames and retained the
final 240/480-frame settled captures. The final frames are unclipped, keep the
header/backplate, use the intended visual hierarchy, and pass route, font,
geometry, synthetic-input, inactive-close, frame, route-counter,
input-counter, and screenshot gates. Final logs contain no warning, error,
failed, or unknown-command hit.

The session route inventory is now `23` `live_provider` and `2`
`starter_round3`; only `map_selector` and `match_stats` remain in the Wave C
live-provider pass.

## Remaining gates

- Automate a connected participant Pick/Ban cycle and restore the tournament
  fixture afterward.
- Automate connected admin Replay open, cancel, confirmation, truncation
  checks, and fixture restoration.
- Add broad keyboard/gamepad traversal and large-text/localization evidence.
- Complete native Vulkan and RTX/vkpt RmlUi renderer parity without an OpenGL
  redirect.
