# FnQuake3 In-Game Console Integration

Date: 2026-07-12

Tasks: `FR-11-T01`, `FR-11-T02`, `FR-11-T03`, `FR-11-T04`, `FR-11-T05`,
`FR-11-T06`, `FR-11-T07`

## Result

WORR now incorporates every user-visible in-game console capability identified
as unique to the current `../FnQuake3/` reference while retaining WORR's
stronger UTF-8, variable-width TTF/KFont, persistent-history, timestamp,
remote-console, download/status, and Quake II command-generator behavior.

This is an architectural adaptation rather than a direct file transplant.
FnQuake3 assumes Quake III fixed-cell fields and completion callbacks; WORR now
implements the same experience over its native `inputField_t`, `genctx_t`,
registered command/cvar/alias generators, shared renderer API, and platform
input abstraction.

## Implemented behavior

### Smooth scrolling (`FR-11-T02`)

- Added `con_scroll_lines`, clamped to the visible page, with Ctrl-modified
  full-page navigation.
- Added frame-time-independent `con_scroll_smooth` interpolation controlled by
  `con_scroll_smooth_speed`.
- The visual line follows manual navigation and naturally trails newly printed
  output while the integer scroll target continues to own authoritative range
  clamping and circular-buffer wrapping.
- Setting `con_scroll_smooth 0` restores immediate legacy movement.

### Live and fuzzy completion (`FR-11-T03`)

- Added a live popup driven by WORR's `Cmd_Command_g`, `Cvar_Variable_g`,
  `Cmd_Alias_g`, and `Com_Generic_c` paths. Command-specific map, file, mod,
  cvar-value, and other argument generators therefore remain authoritative.
- Strict prefix matching sorts alphabetically. If first-token prefix matching
  is empty, ranking falls back through case-insensitive substring,
  abbreviation/subsequence, and bounded edit-distance matches.
- Replacement tracks the active quoted/semicolon-delimited command segment,
  token offset, cursor, suffix, optional quoting, and leading slash.
- Tab/Shift+Tab, arrows, page/home/end keys, Ctrl+P/N, wheel, click, and a
  draggable popup scrollbar navigate or accept candidates.
- Cvar rows show current values. `con_completion_popup 0` preserves classic
  printed `Prompt_CompleteCommand()` behavior.
- `con_test` exercises quoted compound segmentation, fuzzy scoring, registered
  command lookup, and scroll-step bounds. `con_test_visual` is the deterministic
  screenshot/visual-QA variant.

### Mouse, selection, clipboard, and drag (`FR-11-T04`)

- Console activation now owns captured mouse motion and suppresses gameplay
  look input. Ungrabbed absolute events and captured relative motion share the
  same console coordinate path.
- The console renders the shared `/gfx/cursor.png` pointer, with a simple shared
  renderer fallback if the asset is unavailable.
- The scrollback bar gained animated hover width/brightness, click paging, and
  continuous thumb dragging. The completion popup bar is independently
  draggable.
- Existing UTF-8 keyboard input selection remains intact and now supports mouse
  positioning/drag selection.
- Scrollback supports mouse selection, Ctrl+Shift keyboard extension,
  Ctrl+A, and Ctrl+C. Copied text trims padding and removes renderer color
  escapes.
- Selected input or scrollback text can be dragged into the input field; moving
  an input selection adjusts source and destination offsets without round trips
  through the system clipboard.
- SDL and X11 cursor visibility was aligned with console ownership. Windows and
  Wayland already hide the system pointer while captured.

### Layout and appearance (`FR-11-T05`)

- `con_screen_extents` selects full-width or centered 4:3 console space while
  preserving WORR's existing scale/font pipeline.
- `con_background_style`, `con_background_color`, and
  `con_background_opacity` extend the existing textured/rerelease background
  with a configurable flat path.
- `con_line_color`, `con_version_color`, `con_show_version`, and `con_fade`
  control accents, footer/clock color, version visibility, and transition
  alpha.
- `con_clock 1` renders 24-hour time and `con_clock 2` renders 12-hour AM/PM.
- All geometry and drawing use shared renderer entry points. No OpenGL-specific
  call or Vulkan-to-OpenGL redirect was introduced; the native Vulkan target
  remains valid.
- Opening the console from an active RmlUi route now closes that route through
  `UI_CloseMenu()` before transferring key and pointer ownership. Legacy menus
  already honor the cleared `KEY_MENU` destination.

### Raw chat (`FR-11-T06`)

- `con_say_raw 1` sends opt-in slashless global/team input as one quoted client
  command when `con_auto_chat` is enabled.
- Default command execution, slash commands, classic tokenized chat, remote
  console mode, legacy Q2 servers, and demos remain unchanged.

## Public cvars

All new cvars follow WORR lowercase snake-case conventions and are archived:

| Cvar | Default | Purpose |
| --- | --- | --- |
| `con_scroll_lines` | `8` | Normal line/wheel scroll increment. |
| `con_scroll_smooth` | `1` | Animate scrollback and new output. |
| `con_scroll_smooth_speed` | `72` | Animation speed in lines per second. |
| `con_completion_popup` | `1` | Enable live/fuzzy completion UI. |
| `con_screen_extents` | `0` | Full width (`0`) or centered 4:3 (`1`). |
| `con_background_style` | `0` | Existing/textured (`0`) or flat (`1`). |
| `con_background_color` | `#180000` | Flat background color. |
| `con_background_opacity` | `0.8` | Background opacity. |
| `con_line_color` | `#566c42` | Separator, marker, and accent color. |
| `con_version_color` | `#ffc840` | Clock/version color. |
| `con_show_version` | `1` | Show footer version. |
| `con_fade` | `0` | Fade during open/close motion. |
| `con_say_raw` | `0` | Use quoted raw slashless chat. |

## Files and boundaries

- Core console: `src/client/console.cpp`, `src/client/client.h`
- Input/key routing: `src/client/input.cpp`, `src/client/keys.cpp`
- UI ownership bridge: `inc/client/ui.h`, `src/client/ui_bridge.cpp`
- SDL/X11 pointer visibility: `src/unix/video/sdl.c`,
  `src/unix/video/x11.c`
- Automated contract: `tools/console/validate_console_integration.py`, Meson
  `console-integration-check` test/run target
- User guide: `docs-user/console.md`
- Project plan: `docs-dev/plans/fnquake3-console-integration-roadmap.md`

`q2proto/` was not modified.

## Validation evidence (`FR-11-T07`)

- `python tools\console\validate_console_integration.py --root .`
  - Passed; 13 cvars, 14 core feature symbols, native completion-generator
    integration, both mouse routes, and renderer neutrality validated.
- `meson compile -C builddir-win worr_engine_x86_64 console-integration-check`
  - Passed after the current shared networking sources were included by Meson.
- `meson compile -C builddir-win worr_vulkan_x86_64`
  - Passed (`ninja: no work to do`), confirming the current native Vulkan
    renderer target remains valid.
- Staged runtime launch:
  - `.install/worr_x86_64.exe +con_test +quit` exited successfully and printed
    `con_test: all console integration checks passed`.
- Visual capture:
  - `.tmp/console-runtime-home/screenshots/console_integration_visual_deterministic.png`
    first confirmed the centered console geometry, flat background, fuzzy
    `togglcon -> toggleconsole` row, prompt, footer, and scrollbar. The later
    RmlUi-ownership fix is covered by the same deterministic
    `con_test_visual` path.
- Staging:
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir
    .install --base-game basew --archive-name pak0.pkz --platform-id
    windows-x86_64`
  - Passed runtime copy, `pak0.pkz` creation, botfile/RmlUi payload validation,
    and Windows x86_64 staged-payload validation.

The configured Windows build reports optional zlib/libpng unavailable; PNG
renderer capture therefore failed with `Operation not permitted`, while the
always-supported TGA path succeeded. QA TGA artifacts were converted to PNG
under `.tmp/` for pixel inspection only. This does not affect the console
implementation or packaged runtime.
