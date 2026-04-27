# Font TTF + KEX `.kfont` Alignment Pass (2026-04-27)

Task ID: `FR-06-T03`

## Summary
- Fixed proportional TTF glyph placement in the SDL3_ttf text-engine path by treating `TTF_CopyOperation.dst` as a top-origin destination instead of adding WORR's cached baseline a second time.
- Restored the intended TTF letter-spacing behavior for draw and measure paths so UI/screen text tracking stays consistent.
- Hardened KEX `.kfont` loading in the shared client font layer and renderer-side ASCII KFont paths.
- Aligned the default fallback chain on `fonts/qconfont.kfont`, matching the console-style KEX font used by screen, console, and UI code.

## Findings
1. The proportional TTF fast path was vertically offset because WORR added `font->ttf.baseline` to SDL3_ttf copy-operation coordinates. SDL3_ttf already emits copy destinations relative to the text object's top-left placement, so the extra baseline pushed glyphs down.
2. The direct fixed-width TTF path still needs its baseline math because it draws cached glyph bitmaps from FreeType-style `top`/baseline metrics, not SDL text-object copy rectangles.
3. `Font_SetLetterSpacing(...)` was being called by screen/UI font setup, but the current shared draw/measure logic no longer applied the stored tracking value.
4. KEX `.kfont` support in the high-level font loader was present but brittle: it assumed a texture path shape, accepted malformed numeric rows, and could report success for an empty map.
5. Renderer-side `SCR_LoadKFont(...)` implementations stored ASCII KFont rows after subtracting `KFONT_ASCII_MIN` and checking against `KFONT_ASCII_MAX`. That let some out-of-range codepoints index past `chars[]` and also skipped the valid `~` slot semantics.

## Implementation
- `src/client/font.cpp`
  - Added shared fallback constants for `fonts/qconfont.kfont` and `conchars.png`.
  - Set SDL3_ttf hinting from `cl_font_ttf_hinting` immediately after opening a font.
  - Applied TTF tracking in `Font_DrawString(...)` and `Font_MeasureString(...)`.
  - Used SDL3_ttf text-object width for proportional TTF measurement, with the same copy-operation count used by drawing for added tracking.
  - Removed the duplicate baseline offset from the proportional SDL3_ttf copy-operation path.
  - Hardened `.kfont` parsing by validating numeric tokens, accepting explicit `{ ... }` map blocks, registering texture paths consistently, and requiring both an atlas and at least one glyph row before load success.
- `src/rend_gl/draw.c`
  - Added null/atlas/size guards to OpenGL KFont drawing.
  - Fixed KFont ASCII bounds before indexing `chars[]`.
  - Avoided dereferencing a missing KFont image when computing atlas scale.
- `src/rend_vk/vk_main.c`
  - Fixed KFont ASCII bounds and EOF handling in the Vulkan KFont parser.
- `src/rend_rtx/vkpt/draw.c`
  - Matched the KFont bounds and draw guards used by the OpenGL path.

## Validation
- `meson compile -C builddir-win`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- OpenGL smoke:
  - `.install\worr_x86_64.exe +set basedir E:\Repositories\WORR\.install +set r_renderer opengl +set logfile 1 +set logfile_flush 1 +set logfile_name font_opengl_smoke +wait 120 +quit`
  - Log confirmed `Loaded renderer 'opengl'.`, `Font: SDL3_ttf initialized`, TTF loads, and `SCR_LoadKFont "fonts/qconfont.kfont"` with texture `fonts/qconfont.png`.
- KFont parser probe:
  - Ran `font_dump_glyphs fonts/qconfont.kfont 16` under the OpenGL client.
  - Created `.install\basew\fontdump\glyphs_001.txt`, confirming the shared client `.kfont` loader reports `kind: kfont line_height=14`.

## Follow-Up
- Add a small automated font QA command/script for `FR-06-T05` that screenshots representative HUD/menu/console strings with TTF, KEX `.kfont`, and legacy fallback fonts.
