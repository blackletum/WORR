# Console Font TTF + Kfont Fallback Repair (2026-03-28)

Task ID: `FR-06-T03`

## Summary
- Repaired the fixed-width TTF console path so `con_font` TTF/OTF fonts render through the TTF atlas path instead of silently degrading into kfont fallback output.
- Switched client font fallbacks from `fonts/qfont.kfont` to `fonts/qconfont.kfont` for console, screen, and UI font loads.
- Hardened SDL3_ttf bitmap extraction so glyph alpha is copied from a known ARGB8888 surface format.
- Fixed string measurement so active-font advances are used before kfont or legacy fallback widths.

## Problem Statement
Three issues were stacked together:

1. Console TTF loads succeeded, but the fixed-width draw path no longer had a direct per-codepoint TTF renderer once the SDL3_ttf layout fast path was skipped.
2. The fallback chain still pointed at `fonts/qfont.kfont`, which is not the console-style kfont the client already loads elsewhere (`fonts/qconfont.kfont`).
3. Measurement fallback logic skipped the active font and only measured fallback widths in some paths, which could drift alignment and wrapping.

The result was that default console text looked wrong even though TTF/OTF assets were present and initialized.

## Implementation Details

### 1) Restored fixed-width TTF rendering
- `src/client/font.cpp`
  - Added `font_uses_ttf_layout_fast_path(...)` so SDL3_ttf text-object layout is only used for proportional TTF rendering.
  - Added a direct per-codepoint TTF draw path for fixed-width fonts (`font_draw_ttf_glyph(...)`).
  - Split TTF cache keys into two domains:
    - Unicode codepoints
    - SDL3_ttf shaped glyph indices
  - This avoids cache collisions between monospace codepoint rendering and shaped text rendering.

### 2) Corrected client kfont fallback choice
- `src/client/console.cpp`
- `src/client/screen.cpp`
- `src/client/ui_font.cpp`
  - Updated explicit fallback kfont paths from `fonts/qfont.kfont` to `fonts/qconfont.kfont`.
  - This matches the already-loaded console-style kfont asset and restores readable fallback output when TTF/OTF is unavailable or incomplete.

### 3) Hardened glyph extraction and measurement
- `src/client/font.cpp`
  - Converts glyph surfaces to `SDL_PIXELFORMAT_ARGB8888` before extracting alpha coverage.
  - `Font_MeasureString(...)` now measures the active font first, then the kfont fallback, then legacy.

## Validation
- Rebuilt with `meson compile -C builddir-win`.
- Restaged runtime binaries and verified the staged engine DLL was updated.
- Verified console output with synchronous screenshots:
  - Default console TTF (`fonts/RobotoMono-Regular.ttf`) now renders readable monospace text.
  - Explicit `fonts/qconfont.kfont` fallback remains readable.
  - Explicit `conchars.png` legacy fallback remains readable.

## Files Updated
- `src/client/font.cpp`
- `src/client/console.cpp`
- `src/client/screen.cpp`
- `src/client/ui_font.cpp`
- `docs-dev/console-font-ttf-kfont-fallback-repair-2026-03-28.md`
