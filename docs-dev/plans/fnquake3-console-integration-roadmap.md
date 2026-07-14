# FnQuake3 Console Integration Roadmap

Date: 2026-07-12

Task IDs: `FR-11-T01` through `FR-11-T07`

## Objective

Port every user-visible in-game console capability that is unique to
`../FnQuake3/` into WORR without regressing WORR's stronger existing console
foundation. The result must retain UTF-8 field editing, TTF/KFont rendering,
persistent history, timestamps, remote-console mode, configurable height, and
Quake II command/completion compatibility while gaining FnQuake3's modern
interaction model.

## Source and ownership boundary

- Reference implementation: `../FnQuake3/code/client/cl_console.cpp` and
  `../FnQuake3/docs/CONSOLE.md`.
- WORR implementation owners: `src/client/console.cpp`, the shared prompt/field
  helpers under `src/common/`, and platform input routing only where required.
- This is an adapted port, not a wholesale source transplant. FnQuake3 uses a
  fixed-cell Quake III console, while WORR has variable-width UTF-8 font and
  Quake II command-generator infrastructure.
- `q2proto/` is outside this project and remains read-only.

## Baseline feature matrix

| Capability | FnQuake3 | WORR before this project | Integration decision |
| --- | --- | --- | --- |
| Smooth manual and new-output scroll | Yes | No | Port with frame-time-independent line interpolation. |
| Configurable scroll step and page modifier | Yes | Fixed 2/6 lines | Add a bounded `con_scroll_lines`; preserve Ctrl page behavior. |
| Live completion popup | Yes | Classic printed completion list | Port against WORR command/cvar/alias and command-specific generators. |
| Fuzzy completion fallback | Yes | Prefix only | Port bounded substring/subsequence/edit-distance ranking. |
| Completion keyboard/mouse navigation | Yes | Tab only | Add Tab/Shift+Tab, arrows, page/home/end, wheel, click, and popup scrollbar. |
| Input selection/clipboard | Yes | Keyboard selection and clipboard already present | Preserve WORR's UTF-8 implementation; add mouse selection. |
| Log selection/copy | Yes | No | Port with wrapped-line and timestamp-aware extraction. |
| Selection drag into input | Yes | No | Port without weakening clipboard behavior. |
| Interactive scrollback scrollbar | Yes | Draw-only scrollbar | Add hover animation, click/drag, and correct range mapping. |
| In-game console cursor/capture | Yes | No console-specific pointer | Route absolute mouse position and render the shared cursor asset. |
| Full-width versus centered extents | Yes | Full-width | Add renderer-neutral console extents without altering HUD/UI scaling. |
| Uniform console scaling mode | Yes | Stronger TTF/KFont pixel/virtual scaling controls | Keep WORR's existing font system; do not duplicate an inferior fixed-cell mode. |
| Background style/color/opacity | Yes | Textured or WORR rerelease gradient plus `con_alpha` | Extend the existing background path with explicit style/color controls. |
| Accent/version color, clock/version visibility | Yes | Clock and version already rendered | Add explicit visibility/color controls while preserving `con_clock`. |
| Transition fade | Yes | Height animation only | Add opt-in content/background alpha coupled to open/close motion. |
| Slashless chat and raw chat mode | Yes | Existing `con_auto_chat` modes | Preserve the safer opt-in policy and add raw quoted chat mode. |
| Persistent history/search | No unique advantage | WORR already supports history file and Ctrl+R/S | Preserve WORR behavior. |
| UTF-8/variable-width input | No unique advantage | WORR already supports it | Treat as a non-regression gate throughout. |

## Tasks

- [x] `FR-11-T01` Audit both console implementations and ratify the adaptation
  matrix, ownership boundaries, cvar naming, and non-regression requirements.
- [x] `FR-11-T02` Implement smooth scrollback/new-output motion and configurable,
  page-aware scroll increments.
- [x] `FR-11-T03` Implement live and fuzzy completion with keyboard navigation,
  command-specific argument generators, cvar context, and classic fallback.
- [x] `FR-11-T04` Implement console mouse routing, interactive scrollbars, input
  and log selection/copy, selection drag reuse, and in-game cursor rendering.
- [x] `FR-11-T05` Integrate centered extents, configurable background/accent
  appearance, clock/version visibility, and opt-in transition fading with both
  OpenGL and native Vulkan renderer paths through shared 2D APIs.
- [x] `FR-11-T06` Add raw quoted chat mode while preserving legacy server and
  slash-command behavior.
- [x] `FR-11-T07` Add focused automated/source-level coverage, build the client,
  refresh `.install/`, perform runtime interaction checks, publish approachable
  user documentation, and close the roadmap/implementation record.

## Acceptance gates

1. Completion uses the same registered command, cvar, alias, and argument
   generators as classic completion; map/file/mod-specific suggestions remain
   available.
2. Completion never corrupts quoted input, semicolon-separated commands,
   trailing arguments, UTF-8 text, or the cursor/selection state.
3. Smooth scrolling is deterministic across frame rates, clamps across circular
   scrollback wrap, and can be disabled for immediate legacy behavior.
4. Mouse capture, pointer display, clicking, dragging, wheel input, and release
   behave on Windows and SDL-backed platforms without affecting gameplay or
   menus when the console is closed.
5. Console input and log copy preserve valid UTF-8 and do not include internal
   color bytes or padding.
6. Shared renderer APIs implement all visuals; Vulkan is never redirected to
   OpenGL.
7. Existing command execution, remote mode, history persistence/search,
   timestamps, downloads, loading status, notification overlay, and console
   open/close behavior remain intact.
8. The normal build refreshes `.install/` with the resulting binary and assets.

## Validation plan

- Compile focused client targets from the existing configured Windows build.
- Add deterministic tests for completion token/ranking/replacement helpers and
  smooth-scroll range/step behavior where those helpers can be isolated.
- Run source guard checks for cvar names and native shared-renderer usage.
- Launch the staged client with a deterministic console test configuration and
  exercise commands, cvars, map arguments, quoted arguments, multiple command
  segments, history, UTF-8 input, scrollback, selection, clipboard, and mouse
  navigation.
- Refresh `.install/` through the repository build/staging workflow and record
  exact evidence in the final implementation log under `docs-dev/`.
