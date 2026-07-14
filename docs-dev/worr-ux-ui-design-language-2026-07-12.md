# WORR UX/UI Design Language (2026-07-12)

**Status:** Canonical. This document steers all future menu/UI design decisions.
**Supersedes:** ad-hoc styling decisions; complements
`docs-dev/rmlui-grimy-metal-theme-2026-07-11.md` (asset pipeline + engine
constraints) and `docs-dev/plans/rmlui-ui-migration-roadmap.md` (execution
gates). Where documents conflict on *visual/UX intent*, this one wins.

**Inspiration source:** 23 Quake Champions menu screenshots
(`QC-menu` reference set: main/champion screen, leaderboards, all six settings
tabs, Play Now mode cards in three interaction states, custom game editor with
open dropdown, quit dialog, contacts overlay, player profile across five tabs).
QC is treated as a *structural* reference — its layout discipline, navigation
model, and material language — **not** a palette or tone reference. QC reads
clean, red, e-sports. WORR must read scavenged, grimy, slime-green military
salvage, equally at home fronting a single-player campaign and a multiplayer
session.

---

## 1. Design pillars

1. **Scavenged military metal.** Every surface is a physical thing: plates,
   wells, rivets, rails. Nothing floats; chrome is bolted on. Grime and rust
   accumulate at edges and corners, never over text.
2. **One glance, one action.** Each screen has exactly one obvious primary
   action. Everything else is quieter. Clutter is debt: if an element doesn't
   help the player decide or act, cut it.
3. **Weighty but instant.** Motion communicates mass (short, decisive,
   opacity/position based) and never gates input. No transition may make the
   menu feel slower than the legacy UI.
4. **Campaign and arena are one family.** Single-player screens are calm,
   editorial, exploratory; multiplayer screens are denser and status-driven —
   but they share chrome, type, palette, and motion. A player moving between
   them should never feel a theme switch.
5. **Degrade gracefully.** Every treatment must survive: decorators off
   (high-visibility), motion off (reduced-motion), texture load failure
   (flat-color fallback), long localized strings, and 4:3 through ultrawide.

---

## 2. What we take from QC — decision table

| QC pattern (observed) | Decision | WORR adaptation |
|---|---|---|
| Persistent top bar: brand mark left, section icon-tabs, huge center PLAY CTA, account/settings/quit right | **Adopt (phased)** | Shell-wide `worr-topbar`: brand plate left, center **PLAY** slab (slime-green ramp, context-aware label: PLAY / IN MATCH / CONNECTING…), right cluster = Settings gear + Quit power. Icon-tabs only when their sections exist (no placeholder icons). |
| Persistent bottom status bar (contacts, build string center, notification icons right) | **Adapt** | Thin `worr-statusbar`: player identity left, version string center (mono, muted), download/notification chip right when active. No social chrome until social features exist. |
| Big square metal **back plate** (`<`) beside page title | **Adopt** | 40×40 riveted plate button, top-left of every non-popup page, always the same position — this *is* the Back affordance; footer Back buttons become secondary/optional. |
| Page title: huge all-caps, tight, top-left, content starts below | **Adopt** | Already close; formalize: H1 30px uppercase +2px tracking, kicker/summary 760px measure beneath. |
| Settings: horizontal tab strip under title | **Adopt** | Replace the Options *hub-of-pages* long-term with one Settings surface + tab strip (Game / Controls / Video / Audio / HUD / Accessibility). Tabs = flat plates, active = slime-green underline bar + brighter plate. Until the tab controller exists, hub cards remain acceptable. |
| Settings rows: full-width alternating dark slabs; label left (small caps, muted), control right-aligned at fixed column; slider = value readout LEFT of track; section headers as brighter slab captions | **Adopt** | Matches our `.setting-row` contract. Enforce: control column right-aligned at a fixed offset; numeric readout left of slider track; section header slabs (not floating h2s). Keep our per-control-type colored left accents (they aid scanning; QC lacks them). |
| Persistent bottom-right DEFAULT + SAVE | **Adapt** | WORR applies cvars live, so SAVE is meaningless. Standard footer-right pair: **Defaults** (quiet) + **Back** (quiet). Destructive "Defaults" requires a confirm popup. |
| Play Now: 3–4 tall full-bleed key-art **mode cards**; hover tooltip; selected card gets glowing gold border and **expands in place** revealing options + red CTA | **Adopt — flagship pattern** | `worr-card` archetype for Play flows. SP Play: CAMPAIGN / LOAD GAME / START SERVER cards. MP Play: QUICK JOIN / SERVER BROWSER / HOST MATCH / MATCH HUB (in-session). Key art = levelshots/episode art (we own these). Selection = gold ninepatch glow frame; expansion = in-place reveal (height/opacity, no transforms). CTA inside expanded card = green slab (SEARCH / BEGIN / JOIN). |
| Custom game editor: left column (arena card w/ image + name + rules + recommended players), right column labeled dropdown grid + advanced checkbox grid, bottom CANCEL/OK | **Adopt** | Target layout for `startserver` + `mymap`: left = map preview card (levelshot, map name, gametype chip, description, player-count band), right = the settings grid we already have. Bottom-center Cancel / Begin. |
| Dropdown open state: opaque dark panel, active item accent-highlighted, scrolls within panel | **Keep ours** | Already implemented (drop ninepatch). Add: checked item gets accent text + left bar (done) and the panel opens with a 0.1s fade (new). |
| Tables (leaderboard): row slabs with generous height, tiny all-caps column headers, own-row highlighted with bright border, pagination chevron plates | **Adopt** | Server browser / demos / stats tables: 36px row slabs, 12px caps column headers, hover row = brass edge, "you/selected" row = slime-green edge glow. Pagination plates when providers land. |
| Modal dialogs: heavy dim scrim, compact metal plate, title band, message well, two slab buttons, colored underglow along dialog bottom edge | **Adopt** | Our popup anatomy matches; add the **intent underglow**: a 3px bottom edge band — green (confirm), red (destructive), gold (attention). Scrim stays translucent (`#080706d8`). |
| Profile: left rail list + tabbed content; section dividers as center-banner bars; circular gauges for percentages; medal grid with counts | **Adapt (later)** | Reserve for match-stats/player pages when providers land: rail list = our list-table; banner dividers become riveted rail strips (use the reserved `rail` sprite); circular gauges are feasible as sprite ring + text (no shader arcs) — spec in §8.12. |
| 3D character showcase behind menus, smoky void backdrop | **Reject (for now)** | No menu 3D-scene path in the RmlUi bridge. Our equivalent atmosphere: tiled plate backdrop + edge grime + slow `image-color` ember drift on accents. Revisit if a menu-scene bridge ever exists. |
| Currencies, lootboxes, XP boost, battlepass chrome | **Reject** | No monetization surfaces. Top bar stays functional. |
| Clean flat-grey e-sports minimalism | **Reject** | Surfaces keep grime, wear, bevels, rivets. Flatness only in high-visibility mode. |
| Red as brand accent | **Translate** | QC-red roles map to **slime green** (primary/CTA/active) in WORR; red is reserved for danger/destructive only. Gold = focus/selection. Orange = live-session/match energy. |

---

## 3. Layout architecture

### 3.1 Canvas & grid

- Reference canvas **960×720** logical units (engine-scaled). Design at 16:9
  ≥1280 wide but must hold at 4:3 960×720 and stay composed at ultrawide
  (cap text measures at 760px; center or left-anchor content blocks).
- **4px base unit.** Approved spacing steps: 4 / 8 / 12 / 16 / 24 / 32.
  No other pixel values for gaps/padding without a documented reason.
- Screen padding: 18px (10px under 640px height — accessibility sheet owns
  this fallback).
- Vertical rhythm: header band → 8 → content → 8 → footer band. Section gap
  within content: 12. Row gap: 6.

### 3.2 Global chrome (target end-state)

```
┌──────────────────────────────────────────────────────────────────────┐
│ [BRAND] [tabs…]            ███ PLAY ███             [gear] [power]   │ topbar 48px
├──────────────────────────────────────────────────────────────────────┤
│ [<] PAGE TITLE                                    context control    │ title row
│     kicker / summary (≤760px)                                        │
│ ──────────────────────────────────────────────────────────────────── │
│                                                                      │
│                        content region (scrolls)                      │
│                                                                      │
│ ──────────────────────────────────────────────────────────────────── │
│ footer actions (left: contextual · right: Defaults/Back)             │ footer band
├──────────────────────────────────────────────────────────────────────┤
│ player · WORR r#### (mono, muted) ·                    [notif chip]  │ statusbar 24px
└──────────────────────────────────────────────────────────────────────┘
```

Rules:

- The **back plate** is always top-left at the same coordinates on every
  standard page. Escape and the plate do the same thing (`ui.back`).
- The topbar PLAY slab is the *only* element allowed to glow at idle.
- Popups render over the current page with the translucent scrim; they never
  get topbar/statusbar chrome.
- The in-match hub keeps its own translucent floating-shell chrome (already
  established) — it must not adopt the opaque topbar.

### 3.3 Page archetypes

Every menu must be one of these. New archetypes require updating this doc.

| Archetype | Use | Structure |
|---|---|---|
| **Hero** | Main menu | Centered brand column on framed plate; 3–5 large actions; version chip; no scroll. |
| **Card select** | Play flows, episode/skill select | Title row + horizontal row of 3–5 `worr-card`s; selected card expands in place with options + CTA. |
| **Hub** | Options landing (interim), Game menu | Title row + wrapping framed section cards of left-aligned action rows. |
| **Form** | Settings pages | Title row (+ tab strip when available) + 1–3 columns of `.setting-row` slabs grouped in section slabs; footer Defaults/Back. |
| **Table** | Server browser, demos, leaderboard-likes | Title row + toolbar band + column-headed row slabs + status bar; selection/pagination chrome. |
| **Editor** | Start server, MyMap, callvote compound pages | Two-panel: preview/summary card column (left ~40%) + settings grid (right); bottom-center Cancel/primary CTA. |
| **Dialog** | Confirms, popups | Scrim + plate ≤420px + title band + message well + ≤3 slab buttons + intent underglow. |
| **Session hub** | dm_join / match hub | Established floating translucent shell: brand band, tab strip, content splits, tools band, footer. |

Anti-overlap covenant: absolutely no absolutely-positioned overlays inside
content flow except the scrim/dialog pair and dropdown panels. Every text
element must live in a flex flow with `min-width: 0` and long-string wrapping;
fixed-width text containers require an overflow rule at authoring time.

---

## 4. Color system

Base identity: slime green over rusted browns/greys with worked-metal
neutrals, orange for live-match energy. QC's red is *not* imported as brand.

### 4.1 Tokens

| Token | Hex | Role |
|---|---|---|
| `bg/void` | `#12110f` | Screen base, statusbar |
| `bg/plate` | `#1b1916` | Panels, cards, section slabs |
| `bg/well` | `#171511` | Inputs, tracks, row slabs |
| `bg/raised` | `#24211c` – `#2e2a24` | Buttons, tabs, thumb chrome |
| `edge/steel` | `#756a5d` | Primary borders |
| `edge/steel-dark` | `#5f5548` / `#4b4235` | Secondary/quiet borders, dividers |
| `text/primary` | `#f3eee4` | Body, values |
| `text/muted` | `#c9beb0` | Labels, kickers |
| `text/faint` | `#8d8375` | Disabled, hints, statusbar |
| `accent/slime` | `#83d18f` (deep `#1f271d`, edge `#5f8765`, hot `#a6e3ad`) | **Primary.** CTAs, confirm, active tab underline, "you" markers, campaign progress |
| `accent/gold` | `#ffd967` (brass `#b99b5b`) | **Focus & selection only.** Focus rings/pulse, hover edges, selected card frame |
| `accent/orange` | `#ed842e` (muted `#dc8a47`) | **Live session energy.** Match hub, live-state chips, in-match tab underline |
| `accent/red` | `#ee6758` (deep `#281b18`, edge `#895047`) | **Danger only.** Destructive actions, errors, forfeit/quit |
| `accent/teal` | `#7ed4d8` (deep `#426f72`) | System/info: progress fills, meters, info notes |
| `rust` | `#7a4326` / `#a05c32` / `#4f2d1a` | Texture-layer grime only — never UI semantics, never text |

### 4.2 Usage rules

1. **60/30/10:** ~60% neutrals/void, ~30% plate/steel chrome, ≤10% accent.
   One accent family dominates per screen (green for menus, orange for
   in-match, red only inside danger contexts).
2. Green means *go/primary/yours*. Gold means *where you are* (focus,
   selection, hover heat). Orange means *a match is live*. Red means *this
   will hurt*. Teal means *the system is telling you something*. Never
   substitute between them.
3. Rust hues live in textures only. If a rust hex appears in a `color:`
   property, it's a bug.
4. Text contrast floors: primary text ≥ 7:1 against its slab; muted ≥ 4.5:1;
   faint only for non-essential info. Accent-on-plate must hold ≥ 4.5:1
   (all current accents pass on `bg/plate`).
5. New near-duplicate hexes are forbidden — extend this table or reuse.

---

## 5. Typography

Families (runtime-resolved): **WORR Display** (headings, buttons, tabs),
**WORR UI** (body, controls), **WORR Mono** (version, stats, command chips).

| Level | Spec | Usage |
|---|---|---|
| H1 | Display 30px bold, UPPERCASE, +2px tracking | Page titles |
| H2 | Display 21px bold, +1px tracking | Content headings |
| H2-dense | Display 18px bold, gold, brass left-bar | Section headers in forms/hubs |
| Body | UI 16px | Copy, controls |
| Label | UI 14–16px bold muted | Row labels |
| Microlabel | Display 12px bold, UPPERCASE, +1px tracking, muted | Column headers, kickers, chips, tab captions |
| Mono | Mono 11–13px | Version, usage strings, stat values |

Rules: sentence case for copy; Title Case for button labels and row labels;
UPPERCASE reserved for H1, microlabels, tabs, and state chips. One exclamation
mark budget: zero. Numbers in stats/values right-aligned in mono or tabular
context. Never split a sentence across sibling elements (localization).

---

## 6. Materials & asset rules

The generated sprite system (`tools/ui_gen_metal_skins.py` →
`skins/metal/ui-metal.png` + seamless `backdrop.png`/`plate.png`) is the
single source of widget chrome. Rules:

1. **Ninepatch pairs** (`sprite`, `sprite-inner`) for anything that stretches;
   fixed sprites for glyph-bearing chrome (checkboxes, thumbs, arrows).
   Texture detail concentrates in corners/edges; stretch regions stay
   low-frequency.
2. State variants share one base plate per family (same seed) so decorator
   swaps read as lighting changes. Any new widget family ships all states:
   normal / hover / focus / active / disabled (+ checked pair where relevant).
3. Large-area texture comes from the seamless tiles via
   `image(<file> repeat)`; sprites cannot tile (RmlUi limitation).
4. Grime creeps from screen edges (vignette ninepatch) and never sits behind
   body text at more than ~25% alpha.
5. Flat-color `background/border` declarations stay in RCSS beneath every
   decorator: they are the degrade path and the high-visibility base.
6. New raster art: author at 2× (sheet `resolution: 2x`), through the
   generator only — no hand-edited PNGs. SVGs (icons, fallback skins) stay
   within the bridge subset: flat fills/strokes, rect/circle/line/poly/path.
7. Iconography: single-weight flat glyphs, `text/muted` at rest, accent on
   state; 16px in rows, 20–24px in tabs/cards. Icons always pair with a text
   label somewhere in the flow (icon-only buttons need tooltips once the
   tooltip primitive exists; until then, no icon-only actions). Icons are
   authored as flat-shape SVGs (source of truth) but **ship as generated
   PNGs**: the generator renders each SVG 8x-supersampled to 2x its largest
   display size, so shipped glyphs are properly antialiased instead of
   relying on the runtime's flat rasterizer.
8. Key art (cards) uses owned assets: levelshots, episode art, brand marks.
   Card art gets a bottom-up dark gradient band (geometry `vertical-gradient`)
   so titles always sit on ≥ 60% darkened backing.

---

## 7. Motion system

Constraint honesty (see engine doc): **no transforms, no filters/blur** —
motion = opacity, margins/padding, size, and animatable `image-color` tints.
This is sufficient: QC-style motion is mostly fades, underline slides, and
in-place reveals.

### 7.1 Timing tokens

| Token | Value | Tween | Use |
|---|---|---|---|
| `t/state` | 0.12s | cubic-out | hover/focus/active color+border+padding shifts |
| `t/route` | 0.18s | cubic-out | screen fade-in |
| `t/band` | 0.30s | cubic-out | header slide-fade |
| `t/content` | 0.45s (30% hold) | cubic-out | content stagger |
| `t/footer` | 0.55s (55% hold) | cubic-out | footer stagger |
| `t/popup` | 0.14s | cubic-out | dialog drop-in (opacity + margin) |
| `t/reveal` | 0.20s | cubic-out | card expansion, dropdown panel fade |
| `t/pulse` | 1.1s | cubic-in-out, infinite alternate | focus pulse (image-color) |
| `t/breathe` | 1.8s | cubic-in-out, infinite alternate | progress fill life |

### 7.2 Choreography rules

1. **Enter, never exit.** Entrances are choreographed (header → content →
   footer via percentage-hold keyframes); exits are instant (engine swaps
   documents). Never delay input on entrance — elements are interactive from
   frame one.
2. **Hover = 3-property shift** (surface, edge, text) + at most one physical
   move (4px padding slide on left-aligned action rows). Focus = gold ring +
   slow warm pulse. Active = pressed plate + hot edge, instant.
3. **Selection** (cards, tabs, rows) = gold frame + `image-color` warmth; the
   *consequence* of selection (expansion, underline) animates with `t/reveal`.
4. Idle animation budget per screen: the PLAY slab glow, focus pulse, and
   at most one status element (progress breathe / live-match chip). Nothing
   else moves at idle.
5. Reduced-motion kills **all** animation and transitions (ladder +
   enumerated guards in accessibility.rcss). Any new animated selector with
   >100k specificity must add its own guard there — checklist item.

---

## 8. Widget specifications

Baseline height 36px; hit targets ≥ 36px (44px with large-text mode).

1. **Button / slab** — ninepatch `btn*`; variants: default, `quiet`
   (back/close/footer), `primary` (green family: Begin/Join/Search/Save),
   `danger` (red family), **`cta`** (new: topbar PLAY / card CTA — green ramp,
   46–52px tall, Display 18–20px, idle edge-glow). Text Title Case. Min width
   96px; footer buttons never wrap their label.
2. **Field / select** — recessed `field*` well; select adds arrow plate;
   dropdown panel = `drop` ninepatch, options with accent left-bar +
   padding slide on hover; panel fades in `t/reveal`.
3. **Checkbox** — 32×28 fixed sprites, green-lit check when on. Binary
   settings must be checkboxes, never 2-option selects.
4. **Slider** — recessed track + riveted thumb; **numeric readout left of the
   track** (QC pattern; already our `.setting-value`, enforce placement);
   focus = gold track rim.
5. **Progress** — channel + teal fill with breathe; determinate only (no
   fake indeterminate spinners; use status text).
6. **Scrollbar** — 12px steel thumb, brass hover, gold-tinged active.
7. **Tabs** (strip under H1) — flat plates, microlabel caps; active: brighter
   plate + 3px accent underline (green in menus, orange in-match); hover:
   text brightens; keyboard: gold ring. Underline is part of the tab element
   (border-bottom), not a floating element.
8. **Card** (`worr-card`) — 3–5 per row, ~200–280px wide, 3:4-ish; layers:
   key art (cover), bottom gradient band, title + microlabel description;
   rest state: steel 1px frame; hover: brass frame + art `image-color` lift;
   selected: gold ninepatch glow frame + in-place expansion revealing option
   list + CTA slab. Unavailable card: desaturated art (pre-authored variant),
   lock microlabel, tooltip-copy line.
9. **Table** — header row of microlabels; 36px row slabs (`bg/well`,
   alternate ±2% tone); hover: brass left edge; selected: green left edge +
   brighter slab; "you" row: green frame. Columns fixed-width except one
   flexible name column (`min-width: 0`, ellipsis). Pagination: 32px chevron
   plates + mono page indicator.
10. **Keybind chip** — small slab pair (primary/secondary) right-aligned in
    row; capture state: gold pulsing frame + "PRESS A KEY…" microlabel;
    conflict: red edge + conflict row below.
11. **Status chip** — mono microlabel plate with intent left-bar (used for
    match state, NOT ACTIVE-style flags, notification counts).
12. **Radial gauge** (deferred until stats providers) — sprite ring
    (empty/quarter/half/three-quarter/full pre-baked states at 2×) + centered
    mono value; no arbitrary-angle arcs (no shaders). Use for accuracy/health
    percentages on stats pages.
13. **Empty / loading / error states** — every data surface ships all three:
    empty = muted sentence + optional quiet action; loading = teal status
    line (text, not spinner); error = red status slab with retry action.
    Dev-scaffold copy is forbidden in shipped RML.

---

## 9. Navigation & UX rules

1. **Back vs Close:** Back (plate, Escape) pops one level. Close dismisses
   the whole menu stack (gameplay contexts only). Labels must match the
   semantic. Every page has a reachable Back; no dead ends (verified by
   smoke checks).
2. **Focus:** runtime focuses the first actionable element on open; focus
   order follows visual order; disabled elements leave the nav ring. Gamepad
   D-pad = spatial nav; the gold ring + pulse must be visible on every
   focusable at 4:3 brightness floor.
3. **Primary action placement:** hero/cards = inside the selected card;
   forms = footer-right; editors = bottom-center pair; dialogs = left-most
   button is the safe option, destructive styled red and never default-focused.
4. **Confirms:** destructive/irreversible actions (quit, forfeit, leave,
   defaults reset, delete) always confirm via Dialog archetype with
   consequence copy ("The match will end and count as a loss.").
5. **Declutter checklist** (apply to every screen): every visible element
   maps to decide/act/inform-now; merge labels with values where possible;
   no duplicate affordances for the same action within one region (topbar
   back plate replaces footer Back except on forms with Defaults); max one
   kicker line under H1; developer/version info lives in the statusbar only.
6. **Copy voice:** terse, concrete, military-maintenance flavor ("Field
   Systems", "Match Tools") without lore-bloat. Terminology is fixed by the
   normalization pass (Back, Time Limit, Vote:, Multi-Monitor Setup…) — new
   strings follow it. All strings localizable: no concatenated sentence
   fragments across elements.
7. **Session vs shell:** entering the match hub keeps the game visible
   (translucent shell, orange accent). Shell menus are opaque worlds
   (backdrop + grime, green accent). Never mix the two chromes on one screen.

---

## 10. Accessibility contract (non-negotiable)

- **High-visibility** strips decorators to black/white/yellow — every new
  family-scoped chrome selector gets a mode-prefixed entry in
  `accessibility.rcss` (specificity note in §12).
- **Reduced-motion** disables every animation/transition (ladder + explicit
  guards). New animations at >100k selector specificity add guards.
- **Large-text** mode: min-heights scale to 48px — new widgets must not
  hard-clip at 21px text.
- Focus visible everywhere; hit targets ≥ 36px; long-string wrapping on all
  data-driven labels; short-canvas (≤640px) fallback maintained in the
  accessibility sheet.

---

## 11. Screen-by-screen adoption backlog (priority order)

1. ~~**Topbar + statusbar** shells~~ — **DONE 2026-07-12** (brand plate,
   PLAY CTA slab with `cta*` sprites, gear/power icon buttons, statusbar
   with identity/version/download chip on all shell/settings/singleplayer/
   multiplayer/utility pages; session keeps its own chrome).
2. ~~**Back plate** standardization~~ — **DONE 2026-07-12** (`backplate*`
   sprites in a `worr-titlerow` on every standard + session page; footer
   Back buttons removed, Close retained).
3. ~~**Play card flow**~~ — **DONE 2026-07-12** (`shell/play.rml` route with
   Campaign / Load Game / Host Match / Servers cards; `multiplayer.rml`
   converted to its own card set: Servers / Host Match / Demos / Profile;
   expansion via `ui_play_card`/`ui_mp_card` cvar conditions; `card*` frame
   sprites; generated key-art tiles `cardart-{campaign,load,host,servers}.png`
   with baked hazard band + title gradient per 6.8 — hand-painted art can
   drop into the same slots later).
4. ~~**Settings tab strip**~~ — **DONE 2026-07-12** (worr-tabstrip on the
   eight primary settings pages: Video / Screen / Sound / Input / Effects /
   Performance / Access / Language; tabs replace-navigate via
   `popmenu; pushmenu <route>` so the stack never grows; active tab is a
   static green-underlined plate — no runtime controller needed).
   Crosshair/rail-trail/multi-monitor remain sub-pages by design.
5. ~~**Editor layout**~~ — **DONE 2026-07-12** for startserver: left
   preview column (`worr-editor-card`) with a live levelshot image driven by
   the new `data-src-cvar`/`data-src-prefix` runtime binding
   (`levelshots/<map>` through the engine image pipeline, card-art fallback
   when absent) plus the bound arena name. Session `mymap` flow adopts the
   same card when its map cvar lands.
6. ~~**Table dress**~~ — **DONE 2026-07-12** (microlabel headers, slab rows,
   hover/selected edge states) — providers still pending.
7. ~~**Dialog underglow** + dropdown-panel fade~~ — **DONE 2026-07-12**
   (intent-colored bottom band via `data-confirm-kind`, selectbox fade,
   safe-button-first ordering in all confirms, slider readouts left of
   track, settings section header slabs).
8. **Stats surfaces** — gauge primitive **DONE 2026-07-12** (`gauge-q0..q4`
   ring sprites, `.worr-gauge` classes, runtime `data-gauge-cvar` quartile
   mapper; reference usage on download progress). Adoption on stats pages
   is data-gated: it happens when match/weapon stat providers publish cvars
   or models to bind — a data dependency, not open UI work.

Each item follows the standard slice loop: design per this doc → implement →
`ui_smoke` suite → docs-dev log → roadmap tick.

---

## 12. Engineering constraints (summary — full detail in the theme doc)

- Renderer bridge (GL): geometry + textures + scissor only. **No transforms,
  no filters/blur, no shader gradients, no box-shadow.** Vertex-color
  `horizontal/vertical-gradient` OK. `image-color` tints decorators and
  animates.
- RmlUi 6.2: no `!important`; **additive specificity** (tag 10k / class,
  attr, pseudo 100k / id 1M); equal specificity → later-imported sheet wins;
  `accessibility.rcss` imports last by contract.
- Sprites can't use repeat fit; seamless tiles are standalone PNGs
  (`IF_REPEAT` registered by the bridge). New small RmlUi textures request
  `IF_NOSCRAP`. Already-cached legacy scrap-atlas images are supported for
  non-repeating `<img>` content through per-handle UV remapping; do not use a
  scrap-backed legacy image as a repeating decorator.
- Custom SVG rasterizer: flat shapes only.
- Console macro `$cvar` does not expand inside quotes — empty-guard idiom is
  `if x$cvar == x then <seed>`.
- Validation: `tools/ui_smoke/check_rmlui_*.py` must stay green; sprite and
  keyframe references must resolve (validator in the theme workflow).

---

## 13. Review checklist for any menu change

- [ ] Fits one archetype (§3.3); no new overlap/absolute positioning.
- [ ] Spacing uses the 4px scale; text measures ≤ 760px.
- [ ] Colors from §4 tokens; one dominant accent; semantics respected.
- [ ] Type from §5 scale; casing rules; localizable strings.
- [ ] Widgets per §8 with full state coverage; binary = checkbox.
- [ ] Motion per §7 tokens; reduced-motion + high-visibility entries added
      for any new family chrome/animation.
- [ ] Back/Escape path exists; focus lands sensibly; confirms on destructive.
- [ ] Empty/loading/error states authored (no dev copy).
- [ ] `ui_smoke` suite green; sprite/keyframe validator clean; docs-dev log
      updated.
