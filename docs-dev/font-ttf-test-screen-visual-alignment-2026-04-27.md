# TTF Test Screen + Visual Alignment Repair (2026-04-27)

Task ID: `FR-06-T03`

## Summary
- Added a bespoke in-engine TTF diagnostic screen for OpenGL/Vulkan/RTX renderer runs.
- Reworked TTF vertical alignment to use printable ASCII visual extents instead of typographic line-skip metrics.
- Kept SDL3_ttf shaped-text placement consistent by applying a text-engine Y offset equal to `visual_baseline - SDL_ascent`.
- Taught the SDL3_ttf fast path to honor `TTF_CopyOperation.src` rectangles when drawing cached glyph images.

## Test Screen
- Toggle with `font_test` or `set cl_font_test 1`.
- Select a font with `font_test <path>` or `set cl_font_test_font <path>`.
- The screen draws proportional and fixed-width TTF paths at sizes `8, 10, 12, 16, 24, 32`.
- Each row includes top, baseline, and bottom guides:
  - gray: row top
  - green: baseline
  - brown: row bottom
- The sample string includes ASCII, digits, punctuation, accented Latin, Greek, Cyrillic, Korean, Japanese, and Chinese characters.

## Alignment Fix
- The earlier TTF path was still driven by SDL_ttf typographic metrics (`ascent + descent` or `line_skip`). For fonts with generous line gaps this left common Latin glyph ink visually low inside WORR's compact line boxes.
- `src/client/font.cpp` now computes a visual alignment box from printable ASCII glyph metrics. That keeps menu/console/HUD Latin text aligned to the ink users actually see.
- Wider Unicode coverage remains visible in the test screen, but unsupported glyphs no longer distort the core Latin baseline by contributing tofu extents to the font-wide alignment metric.
- The proportional SDL text-engine path still uses SDL's shaped copy destinations, but adds `font->ttf.text_y_offset` so it lands on the same visual baseline as the direct fixed-width glyph path.

## Validation
- `meson compile -C builddir-win`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- OpenGL overlay smoke:
  - `.install\worr_x86_64.exe +set basedir E:\Repositories\WORR\.install +set r_renderer opengl +set cl_font_test 1 +set cl_font_test_font fonts/RobotoMono-Regular.ttf +wait 120 +quit`
- OpenGL screenshot:
  - `.install\basew\screenshots\font_test_overlay_final.png`
- TTF dump probe for `fonts/RobotoMono-Regular.ttf` at `16` now reports `baseline=15`, `extent=19`, and `text_y_offset=-2`, replacing the previous typographic placement that used a taller line-skip box.

## Files Updated
- `inc/client/font.h`
- `src/client/font.cpp`
- `src/client/screen.cpp`
- `docs-dev/font-ttf-test-screen-visual-alignment-2026-04-27.md`
