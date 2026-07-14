# RmlUi Live Match Stats Provider

Date: 2026-07-13

Tasks: `FR-09-T05`, `FR-09-T08`, `FR-09-T09`, `FR-07-T02`,
`DV-03-T07`, `DV-04-T02`, and `DV-07-T04`

## Outcome

The `match_stats` session route now truthfully declares version 2
`live-provider` status. Sgame remains authoritative for the current player's
match counters and refresh lifecycle. The RmlUi document consumes a semantic
fixed snapshot and no longer presents compatibility text lines as a console-
shaped report.

This completes the Wave C provider pass: all 25 session routes now report
`live_provider`. The central migration phase remains `controller_stub` until
connected action automation, localization/navigation review, and native
renderer parity are accepted.

## Native ownership and compatibility

`OpenPlayerMatchStatsMenu()` still validates `g_matchstats`, sets per-client
active/refresh state, publishes immediately, and opens `match_stats` through
the normal `pushmenu` bridge. `UpdateCgameUiMenus()` refreshes the snapshot once
per second while the route is active and closes it if the feature is disabled.

The publisher now exposes:

- current player;
- kills, deaths, and K/D ratio;
- damage dealt, damage received, and damage ratio;
- shots fired, shots on target, and total accuracy.

Undefined ratios are published as `N/A`, so a zero-death or zero-shot player
gets an explicit value instead of a missing row. Sgame continues to publish all
16 `ui_matchstats_line_*` cvars for the legacy JSON fallback, but RmlUi performs
no duplicate calculation and uses the ten semantic values.

`worr_matchstats_menu` and `worr_matchstats_close` remain available to dead,
spectator, and intermission clients. The client RmlUi bridge suppresses the
sgame-only close tail when no server is connected, keeping direct capture and
probe routes warning-free while preserving authoritative cleanup in a live
session.

## UX corrections

- One standard top-left backplate owns Back/Escape behavior; the duplicate
  footer Back button was removed.
- The current player has a clear identity panel rather than occupying an
  unlabeled report row.
- Combat, Damage, and Accuracy values are grouped into responsive cards with
  aligned monospace numbers and plain-language labels.
- Long player names wrap within the 620px bounded content area.
- A direct route with no server snapshot shows a mutually exclusive
  `Stats unavailable` state instead of empty or stale values.
- The summary states that values refresh while the page is open.

## Regression contract

`tools/ui_smoke/check_rmlui_match_stats_provider.py` validates both route
registries, the native cvar/condition/close bridge, disconnected command
hygiene, all ten semantic publisher values, all 16 compatibility lines, the
one-second per-client refresh lifecycle, command flags, fixed snapshot markup,
the three semantic cards, exact bindings, empty/live exclusion, single-back
ownership, responsive styles, metadata, and capture registration.

`tools/ui_smoke/test_check_rmlui_match_stats_provider.py` contains ten focused
regressions covering semantic publication, refresh drift, disconnected close,
raw-report restoration, stat binding drift, empty-state inversion, duplicate
Back, metadata, and capture registration. The complete `tools/ui_smoke` suite
passes 348 tests.

The runtime-capture harness now starts evidence processes with `s_enable 0`.
The harness does not test audio, and disabling it avoids machine/user-config-
dependent OpenAL device and optional EAX preset diagnostics without changing
game runtime defaults. The existing harness regressions lock this command.

## Build, staging, and evidence

The affected `sgame_x86_64`, `worr_engine_x86_64`, and `worr_x86_64` targets
compiled and linked successfully. During the build, Meson's archive mode had
changed and `llvm-ar` rejected stale regular archives when asked to create thin
ones. Only generated `.a` outputs under `builddir-win` were removed; Meson then
recreated them and completed all three requested targets. No networking,
q2proto, or third-party source was changed for this remediation.

The canonical distributable was refreshed with:

```text
python tools/refresh_install.py --build-dir builddir-win \
  --install-dir .install --base-game basew --platform-id windows-x86_64
```

The refresh packed 308 assets and validated 214 RmlUi plus 31 bot
package/loose files. SHA-256 comparisons match source/build to canonical stage
for `match_stats.rml`, `session.rcss`, `session/routes.json`,
`sgame_x86_64.dll`, and `worr_engine_x86_64.dll`.

Two accepted 960x720 OpenGL captures are under
`.tmp/rmlui/runtime-capture/match-stats/`:

- `rmlui_match_stats_live_provider_20260713`
- `rmlui_match_stats_unavailable_live_provider_20260713`

The populated capture uses a long tournament player name plus realistic
combat, damage, and accuracy values. It proves the responsive two-card first
row and full-width Accuracy card without clipping or label/value collisions.
The direct-route capture proves the unavailable state with all live cards
hidden. Both final 240-frame captures pass route, font, geometry, synthetic-
input, inactive-close, frame, route-counter, input-counter, and screenshot
gates. Final logs contain no warning, error, failed, or unknown-command hit.

Manifest, route-contract, controller-stub-completion, focused-provider, and
complete pytest gates pass. The session feature inventory reports 25
`live_provider` routes and the central manifest reports 33 live providers.

## Remaining gates

- Automate connected counter mutation and one-second refresh/close behavior.
- Add broad keyboard/gamepad traversal and large-text/localization evidence.
- Complete native Vulkan and RTX/vkpt RmlUi renderer parity without an OpenGL
  redirect.
