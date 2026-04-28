# TTF Fullscreen Font Pixel-Scale Refresh

Date: 2026-04-28

Task ID: `FR-06-T03`

## Summary

- Fixed fullscreen/resize handling so TTF-backed client fonts keep their assigned font type while refreshing the rasterized pixel height from the current framebuffer size.
- Removed a stale-cache path where some client font users could keep an older typeface/fallback decision until a later reload happened for unrelated reasons.
- Corrected shared screen/cgame multiline font stepping so TTF-backed text advances by the active font line height instead of legacy `8px` rows.

## Findings

1. The client font pixel-scale helpers in screen/UI/console code were derived from the integer virtual-screen scale bucket (`base_scale_int`).
   - That preserved virtual layout, but it also meant fullscreen/window-size changes inside the same bucket did not refresh TTF raster size.
   - The result was resolution-dependent behavior where the visible font raster did not consistently track the real framebuffer size.
2. Font identity and font raster refresh were not invalidated consistently across all client consumers.
   - `screen` and `ui_font` already had better reload coverage.
   - `console` did not react to the shared font settings generation.
   - The legacy client weapon-bar font cache did not react to shared font settings generation either.
   - This could leave mixed font-kind state in-session until a later reload happened because of an unrelated resize or cvar path.
3. The shared `SCR_DrawStringMultiStretch(...)` bridge still advanced multiline rows using `CONCHAR_HEIGHT * scale` even when the active path was TTF-backed.
   - Measurement used font metrics, but drawing still stepped with legacy row height.

## Implementation

- `src/client/client.h`
  - Added `CL_CalcFontPixelScale(...)` as a shared client-side helper for TTF raster pixel-scale calculation.
  - The helper preserves the existing `cl_font_skip_virtual_scale` override behavior, but otherwise uses the real framebuffer-derived base scale instead of the integer virtual bucket.
- `src/client/screen.cpp`
  - Updated `SCR_GetFontPixelScale()` to use the shared helper.
  - Updated `SCR_DrawStringMultiStretch(...)` to advance rows using `Font_LineHeight(...)` whenever the active screen font path is in use, keeping draw spacing aligned with TTF measurement.
- `src/client/ui_font.cpp`
  - Updated UI font reload sizing to use the shared framebuffer-derived pixel-scale helper.
- `src/client/console.cpp`
  - Updated console font reload sizing to use the shared framebuffer-derived pixel-scale helper.
  - Added `con_font_settings_generation` tracking so console fonts also reload when shared typeface/fallback/hinting settings change, rather than only when the console font cvars themselves change.
- `src/client/weapon_bar.cpp`
  - Updated the legacy client weapon-bar font cache to use the shared framebuffer-derived pixel-scale helper.
  - Added shared font-settings-generation invalidation so cached weapon-bar fonts do not hold onto an old typeface/fallback choice after global font-setting changes.
- `src/client/font.cpp`
  - Centralized fallback cvar initialization with `font_ensure_fallback_cvars()`.
  - Expanded `Font_SettingsGeneration()` so it now tracks:
    - `ui_high_visibility_text`
    - `ui_text_typeface`
    - `cl_font_fallback_kfont`
    - `cl_font_fallback_legacy`
    - `cl_font_ttf_hinting`
  - This gives all font consumers a more accurate shared invalidation signal for actual font-kind-affecting changes.

## Result

- Resize/fullscreen changes now refresh TTF raster pixel height based on the current framebuffer profile instead of waiting for an integer virtual-scale bucket boundary.
- The assigned font kind for a given path stays stable across resize events unless the user actually changes a font-affecting setting.
- Multiline shared HUD/cgame text now uses the active font's line height consistently during draw as well as measure.

## Validation

- `meson compile -C builddir-win worr_x86_64 worr_engine_x86_64 cgame_x86_64`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `.install\worr_x86_64.exe +set basedir E:\Repositories\WORR\.install +set r_renderer opengl +set r_fullscreen 1 +set win_fullscreen_capture_friendly 1 +set logfile 1 +set logfile_flush 1 +set logfile_name font_fullscreen_resize_smoke +set cl_debug_fonts 1 +wait 120 +quit`
  - Completed successfully.
  - `E:\Repositories\WORR\.install\basew\logs\font_fullscreen_resize_smoke.log` confirmed SDL3_ttf initialization plus TTF loads for:
    - `fonts/NotoSansKR-Regular.otf`
    - `fonts/RussoOne-Regular.ttf`
    - `fonts/AtkinsonHyperLegible-Regular.otf`
    - `fonts/RobotoMono-Regular.ttf`
