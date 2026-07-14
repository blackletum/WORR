# RmlUi Live Map Selector Provider

Date: 2026-07-13

Tasks: `FR-09-T05`, `FR-09-T08`, `FR-09-T09`, `FR-07-T01`,
`DV-03-T07`, `DV-04-T02`, and `DV-07-T04`

## Outcome

The `map_selector` session route now truthfully declares version 2
`live-provider` status. Sgame remains authoritative for the three candidates,
per-client ballots, majority/timeout finalization, the chosen next map, and
the menu lifecycle. RmlUi consumes the fixed cvar snapshot and dispatches the
existing validated vote and close commands.

The central migration phase remains `controller_stub` until connected action
automation, localization/navigation review, and native renderer parity are
accepted.

## Native ownership and lifecycle

`OpenMapSelectorMenu()` publishes the current prompt, three bounded candidate
labels and visibility flags, acknowledgement state, a numeric seconds-remaining
label, and a 28-segment remaining-time bar. It resets the client's dismissal
state only when a new selector opens.

`MapSelector_CastVote()` continues to reject an out-of-range index, bots, and
an empty candidate. The map manager synchronizes vote counts, finalizes early
on a strict majority or at the deadline, closes every active selector, and
performs the authoritative transition.

Per-frame menu ownership now distinguishes active, dismissed, and globally
finished state. Closing the selector sets `mapSelectorDismissed`, so the
global five-second vote does not reopen the route on the next server frame.
The flag is cleared after the global selector finishes, allowing the next
independent vote to open normally.

The client RmlUi close bridge suppresses the sgame-only
`worr_mapselector_close` tail when no server is connected. In a connected
session the command is preserved, so authoritative dismissal still reaches
sgame. This keeps direct guarded captures warning-free without weakening the
live close path.

## UX corrections

- The page has a stable `Next Map Vote` heading. The server prompt is a
  separate summary, so clearing it after a ballot cannot blank the title.
- All three candidates are full-width semantic buttons with exact indexed
  vote commands and server-published labels.
- After voting, the acknowledgement replaces the candidate group. The direct-
  route empty state cannot appear beside the acknowledgement.
- The Countdown panel shows both whole seconds and a bar that shrinks with
  remaining time. The earlier bar grew as the deadline approached and had no
  numeric time reference.
- One standard top-left backplate owns Close/Escape behavior; the duplicate
  footer Close button was removed.
- A client's explicit Close persists for the current selector instead of
  reopening one frame later.

## Regression contract

`tools/ui_smoke/check_rmlui_map_selector_provider.py` validates the route
registries, native cvar/condition/command bridge, disconnected close hygiene,
sgame publisher, per-frame dismissal lifecycle, intermission command flags,
map-manager validation/finalization, stable heading, exact three-option
contract, acknowledgement and empty-state exclusion, countdown bindings,
single-back ownership, theme hooks, metadata, and capture registration.

`tools/ui_smoke/test_check_rmlui_map_selector_provider.py` contains eleven
regressions covering countdown publication, reopen suppression, sgame
dismissal, disconnected cleanup, blankable headings, command drift,
post-vote empty-state overlap, duplicate Close, metadata, and capture drift.
The complete `tools/ui_smoke` suite passes 338 tests.

## Build, staging, and evidence

The sgame-bearing all-target build completed cleanly before the client close
bridge correction. The corrected `ui_rml_runtime.cpp` object and
`worr_engine_x86_64.dll` then compiled and linked through the isolated engine
target. A later all-target invocation was blocked by unrelated concurrent
`native_carrier_session` header/source ABI drift; no networking files were
changed as part of this work.

The canonical distributable was refreshed after each accepted build with:

```text
python tools/refresh_install.py --build-dir builddir-win \
  --install-dir .install --base-game basew --platform-id windows-x86_64
```

The final refresh packed 308 assets and validated 214 RmlUi plus 31 bot
package/loose files. SHA-256 comparisons match source/build to canonical stage
for `map_selector.rml`, `session.rcss`, `session/routes.json`,
`sgame_x86_64.dll`, and `worr_engine_x86_64.dll`.

Two accepted 960x720 OpenGL captures are under
`.tmp/rmlui/runtime-capture/map-selector/`:

- `rmlui_map_selector_candidates_live_provider_20260713`
- `rmlui_map_selector_acknowledged_live_provider_20260713`

The first shows the live prompt, three candidate maps, four seconds remaining,
and the longer countdown bar. The second deliberately clears the prompt and
candidate rows, then proves the stable heading, recorded-vote acknowledgement,
two-second label, and shorter bar. Both final 240-frame captures are unclipped
and pass route, font, geometry, synthetic-input, inactive-close, frame,
route-counter, input-counter, and screenshot gates. Final logs contain no
warning, error, failed, or unknown-command hit.

At this slice's acceptance point the session inventory was `24`
`live_provider` and `1` `starter_round3`. The subsequent Match Stats slice
completed Wave C at all 25 session routes live.

## Remaining gates

- Automate a connected human ballot, early-majority/timeout completion, Close
  persistence, map transition, and fixture restoration.
- Add broad keyboard/gamepad traversal and large-text/localization evidence.
- Complete native Vulkan and RTX/vkpt RmlUi renderer parity without an OpenGL
  redirect.
