# RTX and Cgame Format Helper Normalization

Task: `DV-04-T03` Add static analysis and warning-as-error policy for first-party code in CI.

Date: 2026-06-27

## Summary

This pass replaces another set of direct first-party formatter calls with WORR's bounded formatting helpers. The cleanup focuses on RTX/path-tracing debug and asset-path strings plus one cgame crosshair image-name path.

## Implemented Improvements

1. RTX shader module path construction now uses `Q_snprintf()`.
2. RTX debug-output variadic formatting now uses `Q_vsnprintf()`.
3. Physical-sky sun color cvar name construction now uses `Q_snprintf()`.
4. RTX profiler current/average timing text now uses `Q_snprintf()`.
5. RTX profiler average-only timing text now uses `Q_snprintf()`.
6. RTX profiler no-data timing text now uses `Q_snprintf()`.
7. RTX blue-noise texture path construction now uses `Q_snprintf()`.
8. RTX tone-mapping adapted-luminance debug text now uses `Q_snprintf()`.
9. RTX vertex-buffer OBJ dump filename construction now uses `Q_snprintf()`.
10. Cgame crosshair image-name construction now uses `G_FmtTo()`.

## Files Changed

- `src/rend_rtx/vkpt/main.c`
- `src/rend_rtx/vkpt/physical_sky.c`
- `src/rend_rtx/vkpt/profiler.c`
- `src/rend_rtx/vkpt/textures.c`
- `src/rend_rtx/vkpt/tone_mapping.c`
- `src/rend_rtx/vkpt/vertex_buffer.c`
- `src/game/cgame/cg_draw.cpp`

## Verification

- `rg -n "std::snprintf\(|\bsnprintf\(|\bvsnprintf\(" src/rend_rtx/vkpt/main.c src/rend_rtx/vkpt/physical_sky.c src/rend_rtx/vkpt/profiler.c src/rend_rtx/vkpt/textures.c src/rend_rtx/vkpt/tone_mapping.c src/rend_rtx/vkpt/vertex_buffer.c src/game/cgame/cg_draw.cpp` returned no matches.
- `meson compile -C builddir-win worr_rtx_x86_64 cgame_x86_64` passed.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64` passed and refreshed `.install/`.
- `meson test -C builddir-win --list` reported `No tests defined.`
