# RmlUi Grimy-Metal Theme & Menu Production Pass (2026-07-11)

Related roadmap: `docs-dev/plans/rmlui-ui-migration-roadmap.md` (visual parity /
polish tasks), strategic project `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`.

## Summary

Full-menu-set production pass on the RmlUi UI: a generated grimy-rusty-metal
asset pipeline replaces the flat single-image SVG widget skins, plus
navigation/layout/content/transition fixes surfaced by a multi-agent audit of
all ~57 RML documents, 7 theme RCSS files, and the runtime/renderer bridges.

## Asset pipeline

New generator: `tools/ui_gen_metal_skins.py` (numpy + Pillow, deterministic
seed). Outputs:

- `assets/ui/rml/common/skins/metal/ui-metal.png` — 2x sprite sheet
  (`@spritesheet worr-metal` with `resolution: 2x`, block auto-rewritten in
  `common/theme/base.rcss` between GENERATED markers). Buttons, fields,
  checkboxes, range/progress/scroll chrome, panel/popup/dropdown ninepatch
  frames, screen grime vignette.
- `assets/ui/rml/common/skins/metal/backdrop.png` / `plate.png` — seamless
  tiles for screen and panel interiors, drawn with `image(<file> repeat)`.

Design rules encoded in the generator:

- State variants share one base plate per widget family (same seed), so RmlUi
  decorator swaps read as lighting changes; soft feel is preserved by the
  existing RCSS color/border transitions underneath (decorators paint the
  padding box by default, leaving RCSS borders visible).
- Ninepatch centers that must stretch are transparent (frames) or near-flat
  (buttons); texture detail concentrates in corners and edges.
- RmlUi rejects `repeat` fit on sprites, hence the standalone tile PNGs.

## Renderer bridge

`src/renderer/rmlui_bridge.cpp`: RmlUi file textures are now registered with
`IF_REPEAT` so decorator texcoords past [0,1] (repeat fit modes) wrap
correctly. Sprite-sheet sampling stays inside [0,1] and is unaffected.

Capability constraints confirmed during this pass (relevant to future
authoring): the bridge implements geometry, textures, and scissor only — no
SetTransform, clip masks, layers, filters, or shaders. Therefore:

- `transform` animations are silently dropped: entrance animations must be
  opacity/layout based (`route-enter` was rewritten fade-only).
- `box-shadow`, `filter`, `linear-gradient` decorators are unusable;
  `horizontal-gradient`/`vertical-gradient` (vertex-color based) are fine.
- The custom SVG rasterizer supports flat rect/circle/line/polyline/polygon/
  path fills+strokes only — no gradients, transforms, or groups.

## Theme changes

- `common/theme/base.rcss`: worr-metal spritesheet block; all widget
  decorators swapped from `image(*.svg)` to ninepatch/image sprites; screens
  layer grime vignette over tiled backdrop; panels/popups layer ninepatch
  frames over tiled plate; popup screens use a translucent scrim; popup enter
  fade animation (reduced-motion aware); heading letterspacing/uppercase;
  high-visibility mode extended to strip the new screen decorators.
- `common/theme/settings.rcss`, `session.rcss`, and component RCSS files:
  same decorator migration.

## Audit fixes

A 25-agent audit (8 mapping, 5 dimension reviewers, 12 adversarial verifiers)
across all 57 RML documents, 7 theme sheets, runtime, and renderer bridge
produced the fixes below. Note: the grimy-metal direction itself is a new
owner decision, not previously documented in the migration roadmap.

Functionality (C++, `src/client/ui_rml/ui_rml_runtime.cpp`):
- Disabled controls (attribute/pseudo, incl. ancestors) no longer dispatch
  data-commands on click or Enter/Space, and leave the tab/nav order
  (`pointer-events`/`tab-index`/`focus`/`nav` guards in accessibility.rcss).
- The per-frame condition pass only touches `display`/`disabled` on state
  changes instead of dirtying styles and forcing relayout every frame.
- `$$com_maplist` selects seed their bound cvar from the first option when
  empty, so `map $_ui_nextserver force` (Begin Game) no longer macro-collapses
  to `map force` on fresh configs.

Commands (RML/JSON):
- `multiplayer.rml` + legacy `worr.json` Start Server guards rewritten to the
  sentinel idiom `if x$match_setup_* == x then <seed>`: the engine does not
  macro-expand `$` inside quotes (src/common/cmd.c Cmd_MacroExpandString), so
  unset cvars used to collapse the argument and produce seven `unknown
  operator` errors while seeding nothing.
- `download_status.rml`: gained a Back button; Escape close command is now
  `popmenu download_cancel` so the page cannot trap the user when idle.

Layout:
- `box-sizing: border-box` on buttons/fields/panels/popup/setting rows;
  fixes the `.menu-list button { width: 100% }` clipping, setting-row
  overhang past section dividers, and popup width on small canvases.
- Singleplayer save/load two-across tiles 288px -> 284px (fit 604px stack).
- Short-canvas `@media (max-height: 640px)` fallback moved to
  accessibility.rcss (imported last) — it was dead code, silently overridden.
- Vote prompt/summary panels scroll instead of pushing footers off-screen;
  match-hub tabs clip overflow; header copy capped at 760px measure on
  ultrawide; shell header/footer rhythm aligned with the other families.

Accessibility/interactivity:
- High-visibility mode enforced with !important in accessibility.rcss —
  family-scoped selectors (hubs, lists, settings combos) previously kept
  their decorators and low-contrast colors under `.ui-high-visibility`.
- Reduced-motion contract enforced with !important (`animation`/`transition`).
- Focus is visibly distinct from hover everywhere (gold border enforced).
- Disabled buttons no longer light up on hover/active/focus (guard block).
- Pressed (`:active`) feedback added for session list/hub/tool buttons,
  save/load tiles, and scrollbar thumbs; sortable table headers got hover
  feedback; meter fills transition all hover-changed properties.

Content (batch across ~30 documents):
- Dev-scaffold copy removed from user-visible slots (server/demo browser
  status, ui_list fallbacks, fake match-stats rows, DEVELOPMENT VERSION).
- Terminology normalized: Back vs Return, Time/Score Limit, "Vote:" pages,
  Multi-Monitor Setup, Rail Trail, Slipgate/Stroyent/Vadrigar option case,
  "Begin Game" (no exclamation), Previous Item, Default dogtag label.
- Destructive confirms state consequences (forfeit counts as a loss; leaving
  returns to the lobby); quit prompt matches the "Are you sure" pattern.
- Split sentences merged for localization safety (tourney copy, set_team
  usage chip); missing header summaries added across callvote/dm pages;
  `<title>` aligned with on-screen h1 on drifting pages.
- Main menu logo now ships as a prescaled 2x asset
  (`common/brand/logo-menu.png`, Lanczos) instead of a 6.6x runtime downscale
  of `art/logo.png`.

Deliberately left to the existing roadmap (controller work, not menu polish):
server/demo browser data providers, save-slot metadata gating, dm_welcome
bindings publisher, localization pipeline, Vulkan/RTX native RmlUi bridges.

## Regeneration

```
python tools/ui_gen_metal_skins.py [--seed N]
python tools/ui_smoke/check_rmlui_manifest.py   # validates RML + imports
```
