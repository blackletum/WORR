# RmlUi Live Save/Load Provider (2026-07-13)

Task IDs: `FR-09-T05`, `FR-09-T07`, `FR-09-T09`, `DV-03-T07`

## Summary

The RmlUi `loadgame` and `savegame` routes now hydrate their authored slot
rows from the engine's live save metadata instead of presenting static slot
numbers. This closes the controller gap for the complete save/load list while
preserving the existing server-owned save format and commands.

This is a live-provider slice, not a claim that every remaining RmlUi browser
or session list has reached parity.

## Runtime behaviour

- `UI_Rml_PopulateSaveSlots()` queries every `.save-slot[data-slot]` row when
  a document opens.
- Slot metadata comes from `SV_GetSaveInfo()`, the same engine API used by the
  legacy menus. The UI therefore presents the engine-formatted map and date
  label without duplicating save parsing in the RmlUi layer.
- Occupied rows are marked `data-save-state="ready"`; missing or unreadable
  rows are marked `data-save-state="empty"`.
- Empty load rows receive the real `disabled` attribute. The command listener
  already rejects disabled controls for pointer and keyboard activation, and
  initial focus skips them.
- Empty save rows remain enabled so players can create the slot.
- Slot commands remain `load "saveN"` and
  `save "saveN"; forcemenuoff`, matching the legacy `SaveGameWidget` flow.
- Returned save metadata is released with `Z_Free()` after it is copied into
  the document.
- Dynamic route providers now finish before `ElementDocument::Show()`. The
  document is shown only after list expansion, save-slot hydration, cvar
  binding, accessibility classes, and keybind labels are final. This prevents
  visible compiled geometry from being invalidated while load rows acquire
  their disabled state and makes the first presented frame complete.

## Presentation and accessibility

Live occupied slots receive a quiet slime-green state edge. Empty rows retain
muted readable copy and the shared disabled treatment where applicable. Both
new state selectors are explicitly covered by the high-visibility sheet.

Live inspection also exposed two shared-chrome defects:

- the topbar's equal flex spacers allowed its right system cluster to extend
  beyond a 960px canvas. The PLAY CTA is now explicitly centred and the
  92px settings/quit cluster is anchored inside the right edge;
- generated 32-40px widget PNGs entered the legacy scrap atlas, which cannot
  provide the standalone texture coordinates RmlUi expects. The existing SVG
  generator now emits every widget PNG at a minimum of 64px, preserving the
  authored SVGs while producing native RmlUi texture handles. This removes the
  white missing-texture settings and quit placeholders.

## Validation coverage

`tools/ui_smoke/check_rmlui_save_load_provider.py` verifies:

- the engine provider uses `SV_GetSaveInfo()`, applies ready/empty state,
  disables only empty load rows, frees metadata, and runs before the document
  is shown or receives initial focus;
- all 16 load slots and 15 manual save slots remain present and ordered;
- every authored row retains the correct live command; and
- normal and high-visibility state selectors remain available.

The runtime capture harness now also has
`ui_rml_runtime_capture_route <route_id>`, allowing non-engine-entrypoint
routes such as `loadgame` and `savegame` to produce the same screenshot,
font, frame, input, close, and status evidence as the original guarded menu
set. The older `ui_rml_runtime_capture_menu` path remains reserved for proving
`UI_OpenMenu()` entrypoints.

Capture runs now cap render pacing at 60fps, wait 120 rendered frames, and use
the supported reduced-motion mode while recording. This produces a settled,
deterministic visual frame; the harness restores the reduced-motion cvar before
exit. The synthetic keyboard, text, pointer, button, wheel, and back-close
sequence still runs against the live route after the screenshot.

## Build repair found during validation

The RmlUi OpenGL bridge's repeat/no-scrap texture lookup combined two typed
`imageflags_t` values as an integer. Clang rejected that call. The combined
flags are now explicitly converted back to `imageflags_t`; texture behaviour
is unchanged and no renderer fallback was introduced.

## Verification

- `python tools/ui_smoke/check_rmlui_save_load_provider.py`
- focused runtime/provider tests: `21 passed`
- complete UI smoke suite: `230 passed`
- RmlUi-enabled Windows build through `builddir-win` (engine, cgame/sgame,
  OpenGL, Vulkan, and RTX targets)
- `.install/` refreshed after the final source and asset changes; validation
  confirmed 308 packaged assets and 214 RmlUi assets
- guarded 960x720 OpenGL captures passed for `loadgame` and `savegame`, with
  engine exit, guarded renderer identity, Quake II Rerelease font source,
  frame counters, all synthetic input counters, close/back counters, and
  screenshot dimensions validated

Live OpenGL evidence is stored under `.tmp/rmlui/runtime-capture/`:

- `rmlui_save_load_live_20260713_loadgame.{tga,png,log}`
- `rmlui_save_load_repeat_20260713_savegame.{tga,png,log}`
- `loadgame-live.json` and `savegame-repeat-live.json`

Both final screenshots were inspected at native 960x720 resolution: the
topbar, PLAY CTA, settings/quit icons, backplate, title rail, populated slot
states, close action, and status bar remain inside the canvas and render with
the shared metal skin. The broader goal remains active until the remaining
browser/session providers and native-renderer matrix close.
