# Bootstrap, Save Copy, and Steam I/O Hardening

Task: `DV-04-T03` Add static analysis and warning-as-error policy for first-party code in CI.

Date: 2026-06-27

## Summary

This pass continues the first-party runtime reliability cleanup by making bootstrap ready-file signaling, save-slot file copying, and Steam library metadata reads handle short writes, close failures, seek failures, and oversized files explicitly.

## Implemented Improvements

1. Windows bootstrap ready-file signaling now checks `WriteFile()` failure.
2. Windows bootstrap ready-file signaling now checks for short token writes.
3. Windows bootstrap ready-file signaling now checks `CloseHandle()` failure.
4. Windows bootstrap ready-file signaling now removes the ready file after UTF-8 conversion failure to avoid stale partial tokens.
5. Windows bootstrap ready-file signaling now removes the ready file after write or close failure.
6. Non-Windows bootstrap ready-file signaling now checks token `fwrite()` byte counts.
7. Non-Windows bootstrap ready-file signaling now checks `fclose()` failure.
8. Non-Windows bootstrap ready-file signaling now removes the ready file after write or close failure.
9. Save-slot file copying now uses an explicit read loop instead of relying on end-of-file loop shape.
10. Save-slot file copying now rejects short destination writes immediately.
11. Save-slot file copying now preserves output file close failures as copy failures.
12. Save-slot file copying now preserves input file close failures as copy failures.
13. Steam library metadata loading now checks the seek-to-end step before measuring the file.
14. Steam library metadata loading now rejects failed `ftell()` results.
15. Steam library metadata loading now rejects files too large to allocate with a terminating byte.
16. Steam library metadata loading now checks the rewind seek before reading.
17. Steam library metadata loading now reads using a checked `size_t` file size.
18. Steam library metadata loading now reports close failures after reading `libraryfolders.vdf`.

## Files Changed

- `src/common/bootstrap.c`
- `src/server/save.c`
- `src/common/steam.c`

## Verification

- `meson compile -C builddir-win worr_engine_x86_64 worr_ded_engine_x86_64` passed.
- `rg -n "WriteFile\(file, token_utf8, \(DWORD\)|fwrite\(token, 1, strlen\(token\), file\)|while \(!feof|ret \|= fclose|Z_Malloc\(len \+ 1\)|file_read != len" src/common/bootstrap.c src/server/save.c src/common/steam.c` returned no matches.
- `git diff --check -- docs-dev/bootstrap-save-steam-io-hardening-2026-06-27.md docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md src/common/bootstrap.c src/server/save.c src/common/steam.c` passed.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64` refreshed and validated `.install/`.
- `meson test -C builddir-win --list` reported `No tests defined.`
