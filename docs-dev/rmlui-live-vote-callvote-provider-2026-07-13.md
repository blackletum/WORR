# RmlUi Live Vote and Callvote Provider

Date: 2026-07-13

Tasks: `FR-09-T08`, `FR-09-T09`, `FR-07-T01`, `FR-07-T02`,
`DV-03-T07`, `DV-04-T02`, and `DV-07-T04`

## Outcome

The active vote route and seven callvote routes now truthfully declare and
consume the existing live sgame-owned cvar and command contracts through the
native RmlUi bridge:

- `vote_menu`
- `callvote_main`
- `callvote_ruleset`
- `callvote_timelimit`
- `callvote_scorelimit`
- `callvote_unlagged`
- `callvote_random`
- `callvote_map_flags`

All eight documents are version 2 `live-provider` routes. Their central
migration phase deliberately remains `controller_stub` until connected action
automation, broad navigation/input validation, localization/large-text review,
and native Vulkan/RTX RmlUi renderer parity are accepted.

## Live provider contract

The focused provider checker locks 41 sgame-published values:

- seven active-vote values for proposal text, readiness, eligibility, and time
  remaining;
- thirteen callvote availability values;
- the current time limit;
- the current score limit and eight gametype-sensitive score labels;
- the current unlagged state; and
- ten tri-state map-flag labels.

The checker also locks all twenty registered vote/callvote commands, each
route's live ownership metadata, pre-show cvar hydration, condition handling,
label-cvar handling, capture registration, accessibility rules, and exact
source/staged route coverage.

No sgame authority moved into the client. Sgame still decides which choices
exist, publishes current values and labels, owns tri-state map flags, launches
votes, and refreshes or closes the active vote lifecycle. RmlUi only presents
that state and dispatches the existing commands.

## UX and behavior corrections

The route family now follows the shared design language more closely:

- every page has one canonical top-left backplate; duplicate footer Back and
  Close actions were removed;
- the vote's Yes action uses the canonical primary treatment;
- Yes/No and readiness controls require an active vote, so an idle direct
  route cannot leak stale actions or countdowns;
- the remaining time is presented as `Time left: Ns` instead of a bare number;
- the callvote empty state appears only when all thirteen server-published
  choices are unavailable;
- every callvote choice surface uses the same bounded two-column grid with a
  36px minimum hit target;
- all twenty random ranges fit as ten two-column rows at the 960x720 reference
  viewport; and
- Map Flags explains that changes are optional modifiers retained while the
  player returns to choose a map. There is no invented Apply action because
  each toggle already updates the server-owned state immediately.

The map-flag capture covers Default, Enabled, and Disabled labels. Time-limit,
score-limit, and unlagged captures cover live current-value publication, and
the score capture proves gametype-sensitive option labels replace authored
fallbacks.

## Close-command correctness

`vote_menu` must run `worr_vote_close` after its RmlUi back-pop so sgame clears
the per-client vote-menu lifecycle. Runtime visual testing found two edge cases
in that path:

1. popmenu close tails did not pass through the existing disconnected-command
   filter; and
2. the filter treated the main-menu `ca_cinematic` state as a server
   connection because it used an open-ended enum comparison.

Both click-driven and Escape/Mouse2 popmenu tails now use the connection-aware
helper. Remote cleanup commands are retained only for the bounded
`ca_connected` through `ca_active` server states; they are suppressed for
disconnected, connecting, and cinematic-only captures. Connected matches still
send the authoritative cleanup command.

The runtime capture tool also now derives its default executable from
`--install-dir`. This prevents an isolated asset staging tree from silently
running the canonical `.install` executable and DLLs. An explicit
`--engine-exe` continues to override that default.

## Validation

The final build and canonical staging sequence was:

```text
meson compile -C builddir-win worr_engine_x86_64
python tools/refresh_install.py --build-dir builddir-win
```

The build passed, `.install/` refreshed 308 packaged assets, 214 RmlUi
package/loose files, and 31 bot package/loose files, and SHA-256 checks matched
all eight source/staged documents, `session.rcss`, `session/routes.json`, and
the built/staged engine DLL.

Focused validation:

```text
python tools/ui_smoke/check_rmlui_vote_callvote_provider.py
python -m pytest -q tools/ui_smoke/test_check_rmlui_vote_callvote_provider.py
python -m pytest -q tools/ui_smoke/test_check_rmlui_runtime_capture.py
python tools/ui_smoke/check_rmlui_metadata_sync.py
python tools/ui_smoke/check_rmlui_route_metadata_shape.py
python tools/ui_smoke/check_rmlui_phase_consistency.py
python tools/ui_smoke/check_rmlui_manifest.py
python tools/ui_smoke/check_rmlui_runtime_assets.py
```

The provider check passes for eight routes and 41 published cvars. Twelve
provider regression tests and the expanded runtime-capture regression suite
pass. The complete `tools/ui_smoke` suite passes 292 tests.

## Installed visual evidence

Eleven clean 960x720 reduced-motion OpenGL frames were captured from the exact
canonical `.install` executable, DLL, and loose assets under
`.tmp/rmlui/runtime-capture/vote-callvote/`:

- `rmlui_vote_menu_live_provider_20260713`
- `rmlui_vote_menu_ready_live_provider_20260713`
- `rmlui_vote_menu_idle_live_provider_20260713`
- `rmlui_callvote_main_live_provider_20260713`
- `rmlui_callvote_main_empty_live_provider_20260713`
- `rmlui_callvote_ruleset_live_provider_20260713`
- `rmlui_callvote_timelimit_live_provider_20260713`
- `rmlui_callvote_scorelimit_live_provider_20260713`
- `rmlui_callvote_unlagged_live_provider_20260713`
- `rmlui_callvote_random_live_provider_20260713`
- `rmlui_callvote_map_flags_live_provider_20260713`

The route montage is
`.tmp/rmlui/runtime-capture/vote-callvote/rmlui_vote_callvote_live_provider_20260713.png`;
the active/idle/empty state montage is
`.tmp/rmlui/runtime-capture/vote-callvote/rmlui_vote_callvote_states_20260713.png`.
Every final log records the intended route, Quake II Rerelease font source,
complete rendered frames, synthetic input, and inactive back-close state, with
no warning, error, failed, or unknown-command hit.

## Remaining gates

- Automate connected Yes/No and callvote mutation flows with safe fixture
  restoration.
- Add broad keyboard/gamepad focus-order, navigation, and scroll assertions.
- Complete large-text and localization review for long server-provided labels.
- Implement and validate native RmlUi renderer bridges in Vulkan and RTX/vkpt;
  neither path is redirected to OpenGL.

