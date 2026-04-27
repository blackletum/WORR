# Font Horizontal Alignment + Main Menu Footer Review (2026-04-27)

Task ID: `FR-06-T03`

## Summary
- Restored low-level left/center/right handling in `Font_DrawString(...)`.
- Fixed TTF measurement for the SDL3_ttf shaped-text path by forcing `TTF_UpdateText(...)` before reading `TTF_TextData::w`, draw ops, or copy glyph data.
- Updated callers that already pre-position text to strip alignment bits before handing strings to `Font_DrawString(...)`, avoiding double alignment.
- Extended the `font_test` overlay with explicit left, center, and right anchor rows.

## Main Menu Version Text
- The main menu footer/version text is defined in `src/game/cgame/ui/worr.json` under the `main.footer` block.
- It is drawn by `src/game/cgame/ui/ui_menu.cpp` through `UI_FontDrawStringSized(...)`.
- It uses the UI font cvar, currently defaulting to `fonts/NotoSansKR-Regular.otf` in `src/client/ui_font.cpp`.
- The footer label uses size `8`; the product/version cvar line uses `subtextSize` `6`, which routes through the small UI font handle.
- It is not a separate bespoke product-version font. The remaining visual difference was from very small Noto sizing and from the same width-measurement failure that affected other center/right TTF text.

## Root Cause
- `TTF_CreateText(...)` creates lazy text objects.
- The previous draw path fell back to direct per-codepoint rendering when the text object had no generated draw ops yet.
- The measurement path still treated the not-yet-laid-out text object as valid and read `internal->w == 0`.
- Any caller that centered or right-aligned by measuring first therefore computed no horizontal offset, so text started at the anchor and appeared left-aligned.

## Implementation Notes
- `Font_DrawString(...)` now computes an aligned start X for each line when `UI_CENTER` or `UI_RIGHT` is present, including multiline strings.
- The black-background pass uses the same per-line aligned X so contrast bars remain under the text.
- `SCR_DrawStringStretch(...)`, cgame `UI_DrawString(...)`, and sized list text strip alignment bits after their own pre-adjustment.
- The TTF fast path now calls `TTF_UpdateText(...)` before using `TTF_TextData`; measurement and drawing now share the same shaped layout data.
- `font_test` now draws colored anchor guides:
  - blue: left anchor
  - green: center anchor
  - gold: right anchor

## Validation
- `meson compile -C builddir-win`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- OpenGL TTF alignment screenshot:
  - `.install\basew\screenshots\font_test_alignment.png`
- OpenGL main-menu footer screenshot:
  - `.install\basew\screenshots\font_menu_alignment_after.png`

## Files Updated
- `src/client/font.cpp`
- `src/client/screen.cpp`
- `src/game/cgame/ui/ui_core.cpp`
- `src/game/cgame/ui/ui_list.cpp`
- `docs-dev/font-horizontal-alignment-and-menu-footer-2026-04-27.md`
