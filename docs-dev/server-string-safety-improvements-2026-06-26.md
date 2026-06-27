# Server String Safety Improvements

Date: 2026-06-26

Task IDs: `DV-04-T03`, `DV-04-T05`

## Purpose
Continue the first-party string-safety cleanup with a focused server/MVD pass. This pass keeps existing values and control flow intact while replacing fixed-buffer `strcpy()`, `strcat()`, and `sprintf()` writes with WORR's bounded string helpers.

## Implemented Improvements
1. Savegame listing fallback dates now use `Q_strlcpy()` when timestamp formatting fails.
2. Dummy MVD extra-userinfo construction now copies the primary info string with `Q_strlcpy()`.
3. Dummy MVD extra-userinfo construction now writes the secondary NUL-separated info string with a size-adjusted `Q_strlcpy()`.
4. Dummy MVD legacy userinfo construction now uses `Q_concat()` instead of `strcpy()` plus `strcat()`.
5. Server `airaccel` configstring formatting now uses `Q_snprintf()`.
6. Server non-deathmatch `airaccel` fallback now uses `Q_strlcpy()`.
7. Server map checksum configstring formatting now uses `Q_snprintf()`.
8. Server inline model configstring formatting now uses `Q_snprintf()`.
9. Server maxclient configstring formatting now uses `Q_snprintf()`.
10. MVD waiting-room fallback path, name, configstrings, recording-list placeholder, and seek-reset configstring copies now use bounded helpers.

## Notes
- The MVD extra-userinfo path still preserves the intentional two-string layout: the second info string starts after the first string's terminating NUL.
- The legacy dummy-MVD userinfo path still produces one concatenated info string followed by the extra terminating NUL expected by the existing callback path.
- The touched files now have no direct `strcpy()`, `strcat()`, or `sprintf()` hits.

## Verification
- `rg -n "strcpy\(|strcat\(|sprintf\(" src\server\save.c src\server\mvd.c src\server\init.c src\server\mvd\game.c src\server\mvd\client.c` found no remaining direct hits in the touched files.
- `meson test -C builddir-win --list` reported no tests defined for this builddir.
- `meson compile -C builddir-win worr_engine_x86_64 worr_ded_engine_x86_64` succeeded.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64` succeeded and validated the staged payload.
