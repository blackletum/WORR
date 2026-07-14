# RmlUi Grimy-Metal Theme & Menu Production Pass (2026-07-11)

Related roadmap: `docs-dev/plans/rmlui-ui-migration-roadmap.md` (visual parity /
polish tasks), strategic project `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`.

**Design intent is governed by `docs-dev/worr-ux-ui-design-language-2026-07-12.md`**
(canonical UX/UI design language, QC-inspired layout architecture, tokens,
widget/motion specs). This document covers the asset pipeline and engine
constraints that implement it.

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

## Motion system (AAA polish round)

All motion works within the bridge constraints (no transforms): opacity,
layout properties (margins/padding), and the animatable `image-color`
property, which tints decorator quads (verified: DecoratorNinePatch
multiplies by `computed.image_color()`).

- Entrance choreography: screens fade (route-enter 0.18s); headers slide
  down and fade (header-enter, margin-top -8px -> 0); content containers and
  footers stagger in via percentage-hold keyframes (content-enter holds 30%,
  footer-enter holds 55%) since the shorthand delay would need per-element
  values.
- Popups drop in (popup-enter: opacity + margin-top 14px -> 0) over the
  route-fade scrim.
- Focused buttons pulse a warm tint (focus-pulse, image-color white ->
  #ffd9a8, 1.1s infinite alternate); progress fills breathe teal
  (fill-breathe, 1.8s).
- Microinteractions: left-aligned action lists (hub sections, callvote,
  menu-list, save/load tiles) slide 4px right on hover via padding-left
  transitions; dropdown options slide and light a gold left accent.
- Hero composition: the main menu action column sits on a framed metal
  plate with larger type; hub sections are framed metal cards with
  brass-barred headings; the version block is monospace.
- Reduced-motion: the [class] ladder plus explicit guards
  (button:focus pulse, headers/footers/popup-dialog/progress fill) disable
  every animation and transition.

## Regeneration

```
python tools/ui_gen_metal_skins.py [--seed N]
python tools/ui_smoke/check_rmlui_manifest.py   # validates RML + imports
```

## Design-language compliance round (2026-07-12)

Implemented the non-gated backlog of the design language across all menus:

- Global chrome: `worr-topbar` (brand plate, green PLAY CTA slab, gear/power
  icon buttons) and `worr-statusbar` (identity, mono version, download chip)
  on every shell/settings/singleplayer/multiplayer/utility page; session
  pages keep their own translucent chrome per the doc.
- `worr-backplate` (riveted chevron plate, new sprites) in a `worr-titlerow`
  on every standard and session page; footer Back buttons removed (Close
  retained); Escape and the plate share `ui.back`.
- New `shell/play.rml` Card-select route (`pushmenu play`): Campaign / Load
  Game / Host Match / Servers cards with in-place expansion driven by
  `ui_play_card` cvar conditions and `card`/`card-hover`/`card-selected`
  hollow frames; registered in ui_rml.cpp, shell/routes.json, and the smoke
  manifest.
- New sprites: `cta*` slime-green CTA ninepatches, `backplate*`, `card*`;
  new `gear.svg`/`power.svg` icons within the rasterizer subset.
- QC-pattern details: dialog intent underglow (bottom band colored by
  `data-confirm-kind`), safe-button-first ordering in all confirm dialogs,
  dropdown panel fade, slider readouts moved left of the track, settings
  section headers as brass-barred slabs, table dress (microlabel headers,
  36px slab rows, hover/selected edges).
- All chrome covered in high-visibility and reduced-motion modes; 11/11
  ui_smoke checks pass; engine rebuilt with the new route.

Chrome buttons carry document-scoped ids (semantics check requires ids on
command buttons); the transform script lives in the session scratchpad and is
one-shot — future pages should author the chrome directly per the design doc.

### Compliance round 2 (same day)

Closed the remaining non-data-gated backlog:

- Settings tab strip on the eight primary settings pages (replace-navigation
  via `popmenu; pushmenu <route>`; static is-active plate; green underline).
- `shell/play.rml` and `multiplayer.rml` card sets now use generated key-art
  tiles (`cardart-*.png`: themed washes, glyphs, hazard band, baked title
  gradient) behind hollow card frames; hover lifts art via image-color.
- Editor preview column on startserver: new runtime binding `data-src-cvar`
  (+`data-src-prefix`/`-suffix`) rebinds <img> src from a cvar so the
  levelshot follows the map select through the engine image pipeline.
- Gauge primitive: `gauge-q0..q4` ring sprites + `.worr-gauge` + runtime
  `data-gauge-cvar` (+min/max) quartile class mapper; reference usage on the
  download page. Stats pages adopt when providers publish data.
- High-visibility/reduced-motion coverage extended to tabs/gauge/editor card.
- 11/11 ui_smoke checks pass; engine rebuilt (two new runtime bindings).

### Icon raster pass (2026-07-12)

All rendered SVGs replaced with generated high-quality PNGs: the generator
now includes an SVG-subset renderer (xml parse -> 8x supersampled Pillow
draw -> LANCZOS downscale) that produces a `.png` sibling for every icon in
`common/icons/**` (21 sources incl. the brand mark), sized at 2x each
icon's largest display size (32px widget glyphs, 40px topbar gear/power,
126px brand mark). 229 `<img>` references across 34 documents now point at
the PNGs; zero SVG renders remain in shipped documents. The SVGs stay as
editable sources and the renderer-neutral fallback set, as do the legacy
`skins/widgets/*.svg` decorator fallbacks (unreferenced by shipped RCSS).
Engine support for the icon pass: new `IF_NOSCRAP` image flag
(inc/renderer/renderer.h, honored in src/rend_gl/texture.c) — the RmlUi
bridge registers file textures with `IF_REPEAT | IF_NOSCRAP` so sub-64px
icon PNGs own their GL texture instead of landing in the scrap atlas, whose
sub-rect texcoords require special handling. The 2026-07-13 Player Setup
follow-up adds per-texture atlas rectangles and draw-time UV remapping for
legacy images that were already cached in the scrap before RmlUi requested
them. Authored repeat decorators still use standalone assets; the cached-atlas
path is for non-repeating legacy `<img>` content such as player icons and
dogtags.
