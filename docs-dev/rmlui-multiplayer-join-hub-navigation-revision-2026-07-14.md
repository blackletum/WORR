# RmlUi Multiplayer Join-Hub Navigation Revision

Date: 2026-07-14

Task: `FR-09-T12`

Related tasks: `FR-09-T08`, `FR-09-T11`, `DV-03-T07`, `DV-07-T04`

## Outcome

The generic Join and deathmatch Join routes now behave as one concise in-game
match hub. The revision keeps the level visible behind the existing focused
partial-screen frame while replacing the previous oversized navigation,
duplicated identity, detached tool row, and persistent footer instructions.

The primary navigation is always **Overview**, **Server Info**, **Voting**,
**MyMap**, **Admin**. Feature availability changes enabled state, not position.
The active route has an orange underline and filled indicator, keyboard focus
uses the gold accessibility ring, and unavailable routes use a dim hollow
indicator. This preserves spatial memory across servers and match phases.

## Information architecture and presentation

- The standard WORR wordmark replaces the hand-built W monogram plus duplicate
  WORR/Multiplayer text. `logo-session.png` is a faithful 192x144 derivative of
  the existing official menu wordmark, sized for reliable live texture upload.
- Server identity, gametype, and map remain adjacent to the wordmark.
- Match state is a compact intent-edged **Session phase** signal with a live
  dot rather than a large disabled-looking tab or banner.
- A small 24-hour local clock and date sit under the phase signal. Client-owned
  ROM cvars refresh once per second and do not depend on server time.
- Overview presents an Arena Brief, compact player/spectator/ruleset state,
  current participation status, and only the secondary context actions that
  apply. Match Details, Stats, and tournament information no longer form a
  competing tools row.
- The participation column remains a stable decision surface at the right.
  Join/team/queue/spectate/ready commands retain the live sgame conditions and
  labels used by the previous provider.
- The lower-left footer is a live tooltip region. Pointer hover and keyboard
  focus both publish per-control context; blur or pointer exit restores one
  quiet default help sentence.
- Settings, Resume, Leave, and Forfeit stay at footer-right and are present
  only when the provider says they apply. Single-button footers retain their
  natural width instead of stretching across the footer.

## Voting behavior

When no vote exists, the Voting tab opens the server-owned callvote choices.
While a vote is active:

1. Overview alone yields to an embedded live-vote panel.
2. The caller, command plus argument, eligibility status, and seconds remaining
   refresh through the same sgame cvar provider.
3. Yes and No use the existing `worr_vote_yes` and `worr_vote_no` commands and
   remain disabled until the server says this client may vote.
4. The join/spectate column and shell remain mounted and do not move.
5. A vote starting while Overview is open refreshes the provider in place;
   it no longer reopens the route and produces a layout flash. A vote launched
   from a nested Voting page still returns to the hub explicitly.

The shared vote predicate now describes whether a server vote session exists,
independent of one client's eligibility. The legacy standalone vote menu still
owns presentation when the match hub is closed.

The proposal binding uses a stable child span inside its flex presentation
container. This avoids replacing the flex container's children during cvar
hydration and makes proposal text and surrounding geometry deterministic.

## Runtime and ownership changes

- `menu_page_welcome.cpp` publishes the fixed-tab availability, active-vote
  state, caller, proposal, eligibility copy, countdown, and inline actions.
- `g_utilities.cpp` exposes the server-session vote predicate while preserving
  the existing per-client `Vote_Menu_Active` eligibility check.
- `p_view.cpp` keeps active-vote presentation under match-hub ownership and
  performs an in-place refresh on the transition.
- `command_client.cpp` returns successful callvotes launched from a nested hub
  route to Join and leaves inline Yes/No submissions inside the hub.
- `ui_rml_runtime.cpp` publishes the client-local clock/date and installs the
  generic hover/focus tooltip listener after route hydration.
- `session.rcss` and `accessibility.rcss` define the compact tab, state signal,
  vote, tooltip, responsive, high-visibility, large-text, and reduced-motion
  treatments.
- `routes.json`, the canonical UX language, the migration roadmap, strategic
  roadmap, and player documentation record the resulting contract.

## Validation evidence

- Build: `meson compile -C builddir-win worr_engine_x86_64 cgame_x86_64 sgame_x86_64` passed.
- Focused provider regression: `14 passed`.
- Live session-entry provider: five routes and 56 sgame cvars passed.
- Canonical design compliance: 58/58 routes, 1,131/1,131 localization
  hooks, and zero errors passed.
- Runtime UX services: all 15 keyboard, gamepad, back, localization,
  accessibility, responsive, focus, and native-renderer services passed.
- Complete UI smoke suite: `366 passed`.
- Installed active-vote route capture at 960x720 passed route open, font,
  renderer, synthetic input, close/back, and screenshot checks:
  `.tmp/rmlui/runtime-capture/mp-join-revision-20260714/dm_join_active_vote_complete.png`.
- Live-world 960x720 OpenGL capture confirms the frame, standard wordmark,
  arena focus, full Overview, fixed tab availability, local time/date, compact
  state, participation, tooltip footer, and correctly bounded Leave action:
  `.tmp/rmlui/runtime-capture/mp-join-revision-20260714/mp_join_live_final.png`.
- Final `.install/` refresh validated 16 root runtime files, one root runtime
  dependency, 329 packaged assets, 31 botfile paths, and 215 RmlUi paths for
  Windows x86-64.

`git diff --check` reported no whitespace errors. These provider, design,
runtime-service, and complete UI smoke commands remain the regression gate for
the contract.
