# Game, Cgame, and PFM Safety Hardening

Task: `DV-04-T03` Add static analysis and warning-as-error policy for first-party code in CI.

Date: 2026-06-27

## Summary

This pass continues the first-party safety cleanup by replacing the remaining clean game/cgame formatter and copy call sites, exposing the shared bounded variadic formatters to bgame/cgame callers, then hardening the optional RTX PFM image dump helper against silent partial output and allocation failures.

## Implemented Improvements

1. Sgame missing-userinfo FOV fallback formatting now uses `G_FmtTo()`.
2. Cgame weapon-wheel `CG_Snprintf()` now uses the shared `Q_vsnprintf()` helper.
3. Cgame weapon-wheel `CG_Scnprintf()` now uses the shared `Q_vscnprintf()` helper.
4. Bgame now declares the shared `Q_vsnprintf()` and `Q_vscnprintf()` helpers with C linkage for game-module callers.
5. Sgame worldspawn gamemod name storage now uses `Q_strlcpy()` instead of `std::strncpy()` plus manual termination.
6. RTX PFM dumps now keep the half-float conversion helper local to the optional image-dump block.
7. RTX PFM dumps now reject zero image dimensions before opening an output file.
8. RTX PFM dumps now guard pixel-buffer size calculations against overflow.
9. RTX PFM dumps now report output file open failures instead of silently doing nothing.
10. RTX PFM dumps now use checked zero-initializing allocation for the pixel buffer.
11. RTX PFM dumps now check header chunk writes.
12. RTX PFM dumps now check pixel payload writes and close failures before reporting success.

## Files Changed

- `src/game/sgame/client/client_session_service_impl.cpp`
- `src/game/bgame/q_std.hpp`
- `src/game/cgame/cg_wheel.cpp`
- `src/game/sgame/gameplay/g_spawn.cpp`
- `src/rend_rtx/vkpt/vk_util.c`

## Verification

- `rg -n "std::snprintf\(|std::vsnprintf\(|std::strncpy\(|\bsnprintf\(|\bvsnprintf\(|\bstrncpy\(" src/game/sgame/client/client_session_service_impl.cpp src/game/cgame/cg_wheel.cpp src/game/sgame/gameplay/g_spawn.cpp src/game/bgame/q_std.hpp src/rend_rtx/vkpt/vk_util.c` returned no matches.
- `meson compile -C builddir-win worr_rtx_x86_64 cgame_x86_64 sgame_x86_64` passed after the build exposed and this pass fixed missing cgame-visible `Q_vsnprintf()` / `Q_vscnprintf()` declarations and C-linkage.
- `clang ... -DVKPT_IMAGE_DUMPS -fsyntax-only ..\src\rend_rtx\vkpt\vk_util.c` passed with the renderer compile include/define set.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64` refreshed and validated `.install/`.
- `meson test -C builddir-win --list` reported `No tests defined.`
