# RmlUi Live Fixed-List Provider (2026-07-13)

Task IDs: `FR-09-T05`, `FR-09-T07`, `FR-09-T09`, `FR-03-T08`,
`DV-03-T07`, `DV-04-T02`, `DV-07-T04`

## Summary

The generic RmlUi `ui_list` route is now a truthful live presentation of the
existing sgame-owned list publication surface. It covers Callvote map,
gametype, and arena lists; MyMap selection; tournament pick and ban lists; and
tournament replay selection.

The provider was already capable of publishing labels and commands, but the
authored route treated actionless status copy as focusable rows, could stack
twelve items into a nested scrolling region at 960x720, showed empty toolbar
and footer slabs, and duplicated the standard top backplate with a footer Back
button. This slice closes those gaps without moving game-state authority into
the client runtime.

It does not claim that the remaining session-specific list surfaces or the
native Vulkan/RTX RmlUi lanes are complete.

## Ownership and state contract

`UiList_Refresh()` remains the authority for list kind, entries, commands,
extras, and page state. The sgame provider now publishes:

- `ui_list_state` as `ready`, `empty`, or `error`;
- `ui_list_status` with useful empty/error copy;
- eight visible rows per page; and
- the existing extra-action and Previous/Next cvars.

The provider still clears all twelve historical item cvar slots on every
refresh. This prevents stale values from an older document or prior page from
becoming visible while allowing the live 960x720 presentation to use an
eight-row page size.

Empty Callvote, MyMap, arena, tournament, and replay cases no longer create
buttons whose command is empty. Tournament veto inactivity is an explicit
error; ordinary no-results cases use the empty state. Loading markup is also
authored as part of the shared data-surface contract even though the current
sgame publication is synchronous.

## Runtime interaction

The generic compiled-runtime bridge continues to refresh `data-label-cvar`,
`data-bind="cvars.*"`, visibility, and enablement every frame. Each item uses
its command cvar as an enable condition, and the event listener rejects
disabled controls before dispatch, so keyboard activation cannot bypass the
actionless-row guard.

The standard top backplate now follows the same document close path as Escape
and Mouse2. A `ui.back` click consults `data-close-command`, pops the RmlUi
route, and then runs `worr_ui_list_close` so sgame clears its active list kind.
Back/close sequences use `Cbuf_InsertText()` as one ordered unit. This makes
the pop and owner cleanup execute before already-buffered commands while
avoiding the reversed ordering that separate insert calls would create.

## Layout and accessibility

The route now uses a 720px fixed-list column within the 960x720 canvas:

- eight 36px minimum-height row slabs fit without a nested page scroll;
- long labels wrap instead of clipping;
- brass hover/focus treatment and the left focus edge remain visible;
- the extra-action toolbar disappears when no extras are published;
- the paging footer disappears for a single page;
- Previous, the monospaced page label, and Next have a stable reading order;
- loading, empty, and error states use the shared semantic classes; and
- the footer Back button is removed because the top backplate owns that action.

The large-text accessibility theme can still raise controls to its 48px
minimum; the content region remains the bounded overflow owner for that
preference.

## Capture harness

The guarded runtime capture tool now registers `ui_list` and accepts repeated
`--seed-cvar NAME=VALUE` arguments. Seeds are applied after configuration and
reduced-motion setup but before the route opens, making any cvar-published
surface deterministic without a live match. Names are syntax-validated,
values preserve spaces, and newline/control injection is rejected.

This is reusable beyond `ui_list`; no route-specific fixture code is embedded
in the runtime. Focused tests cover values with spaces and invalid names.

## Validation and evidence

`tools/ui_smoke/check_rmlui_ui_list_provider.py` validates:

- the eight-row/twelve-cleared-slot sgame contract;
- explicit ready/empty/error status publication;
- registered Previous, Next, and close commands;
- per-frame cvar display/condition refresh and disabled-command guarding;
- ordered backplate owner cleanup;
- exactly eight authored item controls with matching label, command,
  visibility, and enable cvars;
- the absence of a duplicate footer Back button; and
- the 720px, 36px-row, wrapping, focus, and page-label style contract.

Negative tests reject a twelfth authored row, actionless placeholder rows, and
a duplicate footer Back control. The capture harness tests also reject malformed
seed names.

Installed guarded OpenGL evidence at 960x720 includes:

- `rmlui_ui_list_populated_20260713`: eight map rows, two extra actions,
  focused-row styling, and Page 1 / 2 with Next;
- `rmlui_ui_list_empty_clean_20260713`: a truthful no-other-arenas empty
  state with no empty toolbar or paging slab; and
- `rmlui_ui_list_error_20260713`: a visible tournament-veto error state.

All three runs report the exact `ui_list` route, Q2R TTF font source, 960x720
dimensions, input counters, ordered synthetic back-close, and a normal engine
exit. Visual inspection used lossless PNG conversions under `.tmp/`; direct
pixel checks confirmed that the previewer's unchanged-band masking did not
exist in the captured files (zero pure-black pixels in each inspected frame).

The RmlUi-enabled Windows engine and sgame compile and link successfully. The
staged sgame copy is current, and `.install/` packages 308 assets while
validating 214 RmlUi and 31 bot files.

Final automated verification for this slice is:

- `4 passed` in the focused fixed-list provider suite;
- `26 passed` across the fixed-list provider and runtime-capture suites;
- `247 passed` across `tools/ui_smoke`;
- 58/58 required route documents present with route-contract, metadata-sync,
  phase-consistency, and parity-manifest checks passing;
- successful RmlUi-enabled Windows engine and sgame builds; and
- a refreshed `.install/` with the staged binaries and 308 current assets.

## Remaining work

- Add action-level automation for extras, item activation, both paging
  directions, and owner cleanup against a live sgame session.
- Apply the same explicit state and bounded-list rules to remaining
  session-specific providers.
- Complete native Vulkan and RTX/vkpt RmlUi rendering and evidence without an
  OpenGL redirect.
- Run the final localization, large-text, controller-navigation, and
  cross-renderer parity gates before changing the central migration phase.
