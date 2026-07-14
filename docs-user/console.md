# In-Game Console

WORR's console supports modern completion, smooth scrolling, mouse interaction,
and normal text-selection shortcuts while keeping classic Quake command syntax.
Open or close it with the console key (usually `` ` `` or `~`).

## Completion while typing

Start typing a command, cvar, alias, map name, filename, or another supported
command argument. A suggestion panel appears above the input line.

- `Tab` accepts the highlighted suggestion and cycles forward.
- `Shift+Tab` cycles backward.
- The arrow keys and mouse wheel move through suggestions.
- `PgUp`, `PgDn`, `Home`, and `End` move through a long list quickly.
- `Enter` or a left click accepts the highlighted suggestion.
- The small panel scrollbar can be dragged when there are many results.

Matching starts with normal prefix completion. If there is no exact prefix,
WORR also looks for sensible substring, abbreviated, and typo-tolerant command
matches. Command-specific suggestions, such as map and file names, continue to
come from the command itself.

Set `con_completion_popup 0` if you prefer the classic behavior where `Tab`
prints matches into the console log.

## Scrolling

`PgUp`, `PgDn`, and the mouse wheel scroll through console history. Hold `Ctrl`
to move by a visible page. The scroll bar at the right edge can also be clicked
or dragged.

- `con_scroll_lines` sets the normal step size. The default is `8` lines.
- `con_scroll_smooth 1` animates manual scrolling and newly printed lines.
- `con_scroll_smooth_speed` controls that animation in lines per second.
- Set `con_scroll_smooth 0` for immediate classic scrolling.

## Selecting and reusing text

The input line behaves like a normal text field:

- Drag to select text; use `Shift` with arrows, `Home`, or `End` to extend it.
- `Ctrl+Shift+Left` and `Ctrl+Shift+Right` select by word.
- `Ctrl+A`, `Ctrl+C`, `Ctrl+X`, and `Ctrl+V` select, copy, cut, and paste.
- `Shift+Insert` also pastes.

You can drag across console output to select it. `Ctrl+C` copies the selection
without console color codes. After focusing the log, hold `Ctrl+Shift` and use
the arrows, page keys, `Home`, or `End` to extend the selection by keyboard.
Selected input or log text can be dragged back into the input line.

## Layout and appearance

These settings are saved in your config:

- `con_screen_extents 0` uses the full screen width; `1` centers the console in
  a 4:3-width area on ultrawide displays.
- `con_background_style 0` uses the normal WORR/textured background; `1` uses a
  flat color.
- `con_background_color` sets the flat background color, such as `#180000`.
- `con_background_opacity` sets background opacity from `0` to `1`.
- `con_line_color` sets separator, scroll marker, and accent colors.
- `con_version_color` sets the clock and version color.
- `con_show_version 0` hides the WORR version in the console.
- `con_clock 0` hides the clock, `1` uses 24-hour time, and `2` uses 12-hour
  AM/PM time.
- `con_fade 1` fades console content during its open/close transition.

WORR's existing `con_font`, `con_fontscale`, `con_fontsize`, `con_scale`,
`con_height`, timestamps, and history settings continue to work with these
features.

## Optional slashless chat

Commands normally begin with `/` or `\`. WORR can instead treat slashless text
as chat while you are in a game:

- `con_auto_chat 0`: slashless input remains a command (default).
- `con_auto_chat 1`: slashless input becomes global chat.
- `con_auto_chat 2`: slashless input becomes team chat.
- `con_say_raw 1`: send that chat as one quoted line, preserving spacing more
  closely. This is off by default.

Slash-prefixed commands and remote-console input are never changed by the raw
chat option.
