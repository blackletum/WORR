# Renderer, Save, and Anticheat String Safety Cleanup

Task: `DV-04-T03` Add static analysis and warning-as-error policy for first-party code in CI.

Date: 2026-06-26

## Summary

This pass removes another focused set of direct first-party `strcpy()`, `strcat()`, and `sprintf()` call sites from renderer, path-tracing, savegame, and anticheat code. The goal is to keep shrinking the code surface that future warning-as-error or static-analysis gates need to special-case.

## Implemented Improvements

1. OpenGL default `NOTEXTURE` image naming now uses `Q_strlcpy()` against `image_t::name`.
2. OpenGL default `SKYTEXTURE` image naming now uses `Q_strlcpy()` against `image_t::name`.
3. RTX image CSV format-string construction now uses `Q_snprintf()` instead of `sprintf()`.
4. RTX loaded-image filepath capture now uses `Q_strlcpy()` against `image_t::filepath`.
5. RTX override image-name construction now uses `Q_concat()` instead of separate unbounded copy/append calls.
6. Path-tracing feedback material name capture now uses bounded copies for both base and override material fields.
7. Path-tracing PFM screenshot output now bounds both generated filenames and resolution header text.
8. Anticheat missing-hashlist fallback names now use bounded copies.
9. Anticheat missing client-file hash text now uses a bounded fallback copy.
10. Savegame JSON string restore now removes unbounded string copies for dynamic and fixed-width saved strings.

## Files Changed

- `src/rend_gl/texture.c`
- `src/rend_rtx/refresh/images.c`
- `src/rend_rtx/vkpt/main.c`
- `src/rend_rtx/vkpt/vk_util.c`
- `src/server/ac.c`
- `src/game/sgame/gameplay/g_save.cpp`

## Verification

- `rg -n "strcpy\(|strcat\(|sprintf\(|vsprintf\(" src/rend_gl/texture.c src/rend_rtx/refresh/images.c src/rend_rtx/vkpt/main.c src/rend_rtx/vkpt/vk_util.c src/server/ac.c src/game/sgame/gameplay/g_save.cpp` returned no matches.
- `meson compile -C builddir-win worr_opengl_x86_64 worr_rtx_x86_64 sgame_x86_64` passed.
- `clang ... -DUSE_AC_SERVER=USE_SERVER -fsyntax-only ..\src\server\ac.c` passed for the optional anticheat server path, which is disabled in the current Meson builddir.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64` passed and refreshed `.install/`.
- `meson test -C builddir-win --list` reported `No tests defined.`
