# Sgame and IQM String Helper Normalization

Task: `DV-04-T03` Add static analysis and warning-as-error policy for first-party code in CI.

Date: 2026-06-27

## Summary

This pass replaces another set of direct first-party C library string formatting and copy calls with WORR's bounded helpers. The cleanup focuses on gameplay display helpers, generated sound paths, admin IP formatting, and RTX IQM loader metadata strings.

## Implemented Improvements

1. Rank-place display formatting now uses `G_FmtTo()` instead of `snprintf()`.
2. Match time formatting with hours and milliseconds now uses `G_FmtTo()`.
3. Match time formatting without hours but with milliseconds now uses `G_FmtTo()`.
4. Match time formatting with hours and without milliseconds now uses `G_FmtTo()`.
5. Match time formatting without hours or milliseconds now uses `G_FmtTo()`.
6. Gametype display-name storage now uses `Q_strlcpy()` instead of `std::strncpy()` plus manual termination.
7. Tesla grenade bounce sound path formatting now uses `G_FmtTo()`.
8. Foodcube pickup and flashlight toggle sound path formatting now uses `G_FmtTo()`.
9. Admin IPv4 dotted-quad formatting now uses `G_FmtTo()`.
10. RTX IQM mesh, surface, material, and animation names now use `Q_strlcpy()` instead of `strncpy()`.

## Files Changed

- `src/game/sgame/gameplay/g_utilities.cpp`
- `src/game/sgame/gameplay/g_weapon.cpp`
- `src/game/sgame/gameplay/g_items.cpp`
- `src/game/sgame/gameplay/g_svcmds.cpp`
- `src/rend_rtx/refresh/model_iqm.c`

## Verification

- `rg -n "Q_snprintf\(|std::snprintf\(|std::strncpy\(|\bsnprintf\(|\bstrncpy\(" src/game/sgame/gameplay/g_utilities.cpp src/game/sgame/gameplay/g_weapon.cpp src/game/sgame/gameplay/g_items.cpp src/game/sgame/gameplay/g_svcmds.cpp src/rend_rtx/refresh/model_iqm.c` returned no matches.
- `meson compile -C builddir-win worr_rtx_x86_64 sgame_x86_64` passed.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64` passed and refreshed `.install/`.
- `meson test -C builddir-win --list` reported `No tests defined.`
