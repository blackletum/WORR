# RmlUi Live Session Entry Provider (2026-07-13)

## Scope and project tasks

This slice advances `FR-09-T05`, `FR-09-T08`, `FR-09-T09`, `FR-07-T01`,
`FR-07-T02`, `DV-03-T07`, `DV-04-T02`, and `DV-07-T04` by promoting the
five session-entry routes from static controller claims to the live native
RmlUi cvar/condition/command bridge:

- `dm_welcome` (legacy-compatible welcome/acknowledgement surface);
- `dm_join` (deathmatch/team match hub);
- `join` (generic/non-team match hub);
- `dm_hostinfo`; and
- `dm_matchinfo`.

The central migration phase deliberately remains `controller_stub`. Native
Vulkan/RTX RmlUi rendering, real connected-session action automation,
controller navigation, large text, localization, and the full renderer/layout
matrix remain release gates.

## Live owner contract

`src/game/sgame/menu/menu_page_welcome.cpp` remains the authoritative current
publisher for 49 `ui_dm_*`, `ui_hostinfo_*`, and `ui_matchinfo_*` cvars. It
publishes the route after the values so the generic native RmlUi bridge can
hydrate text, labels, conditions, enable state, and command-cvar actions before
the first visible frame. Registered sgame commands retain authority over team,
spectator, ready, menu cleanup, and information-page publication.

`OpenDmWelcomeMenu()` intentionally redirects the current first-connect flow to
`dm_join`; the `dm_welcome` document remains a functional compatibility route
for legacy servers that publish `ui_welcome_title`, `ui_welcome_hostname`, and
`ui_welcome_motd`. Its Continue and back-close paths preserve
`worr_welcome_continue` acknowledgement.

The two match hubs consume the live provider for:

- server/context/title/subtitle, map, mode, match state, player/spectator
  counts, ruleset, score/time limits, current status, MOTD, and join notice;
- team versus non-team participation branches;
- current join eligibility and action labels;
- dynamic spectate and ready commands;
- vote, host info, match info, stats, settings, tournament, MyMap, forfeit,
  admin, resume, and leave visibility; and
- the confirmation route for leaving a match.

## Close and modal behavior

The native back handler continues to respect document-owned close commands.
This slice adds a narrow match-hub rule:

- when `ui_dm_initial != 0`, Escape remains server-owned and cannot locally
  dismiss the first-connect modal before team/spectator selection;
- when `ui_dm_initial == 0` and `ui_dm_show_resume != 0`, Escape or Resume
  closes the local RmlUi shell immediately and still sends
  `worr_dm_join_close` to clear authoritative sgame state; and
- when disconnected, the bridge omits only the known server-only
  `worr_dm_join_close` and `worr_welcome_continue` tails, avoiding meaningless
  console errors while preserving the local close. Connected play continues
  to send both commands.

This keeps first-connect safety intact while removing a network-round-trip
delay from the resumable hub.

## UX/UI corrections

All five documents now declare `data-document-status="live-provider"` and
`data-controller="native-session-cvars"` with incremented route versions.

The established orange-accent translucent match-hub archetype remains intact.
Host Info and Match Info now use bounded 36px information rows with long-string
wrapping, explicit empty states, and high-visibility coverage. Their duplicate
footer Back actions were removed; the standardized top-left back plate is the
single back affordance required by the canonical UX/UI design language.

The first Match Info evidence frame exposed a direct-text RmlUi issue: flex
`<p>` rows retained geometry but did not render their text nodes. The final
style uses block rows, and a regression test rejects restoring flex display on
those direct-text elements.

## Validation and evidence

`tools/ui_smoke/check_rmlui_session_entry_provider.py` validates:

- all 49 current sgame-published session cvars;
- route publication and registered cleanup/info/acknowledgement commands;
- hydrate-before-show ordering, live labels, command-cvar resolution,
  conditions, enable state, and document-owned back handling;
- team/non-team join branching and leave-confirm routing;
- first-connect modal protection plus resumable local-close behavior;
- disconnected suppression of the two known remote-only cleanup tails;
- complete host/match bindings, explicit empty states, single-back navigation,
  block text rows, and long-string styling;
- high-visibility selectors;
- live-provider feature and central metadata; and
- guarded capture registration for all five routes.

Twelve focused positive/negative tests reject lost publisher state, broken team
conditions, hard-coded ready commands, unsafe first-connect close behavior,
disconnected remote-command noise, duplicate Back actions, missing captures,
invisible flex text, lost accessibility coverage, and scaffold metadata.

Installed reduced-motion OpenGL evidence at 960x720 is:

- `rmlui_dm_welcome_live_provider_20260713`;
- `rmlui_dm_join_live_provider_20260713`;
- `rmlui_join_live_provider_20260713`;
- `rmlui_dm_hostinfo_live_provider_20260713`; and
- `rmlui_dm_matchinfo_live_provider_20260713`.

The seeded frames cover compatibility welcome text, a populated team hub, a
populated non-team cooperative hub, host identity/MOTD, and the complete match
information set. Every harness run passes exact route, 960x720 geometry, Q2R
TTF source, synthetic keyboard/text/pointer/button/wheel input, and back-close
gates. Final logs contain no parser, property, missing-media, warning, error,
or unknown-command lines.

Final automated verification for this slice is:

- `12 passed` in the focused session-entry provider suite;
- `279 passed` across `tools/ui_smoke`;
- 58/58 required route documents present;
- passing metadata sync, metadata shape, phase consistency, manifest, runtime
  asset, and session-entry provider checks;
- a successful `worr_engine_x86_64` Windows build; and
- a refreshed `.install/` containing the current binary and 308 packaged
  assets, including 214 RmlUi and 31 bot files.

## Remaining migration work

- Add connected-session action automation that joins teams, spectates, readies,
  resumes, traverses child routes, leaves through confirmation, and restores
  the test session safely.
- Promote the vote/callvote, MyMap, tournament, admin, map-selector, and match-
  stats session families through the same live-provider and evidence loop.
- Run final large-text, localization, gamepad/controller-navigation, viewport,
  and native cross-renderer parity matrices.

No separate user guide is required for this slice. It completes and clarifies
the existing session menu behavior without introducing a new player command,
cvar, or workflow; the approachable session workflow already lives in
`docs-user/multiplayer-session-menu.md`.
