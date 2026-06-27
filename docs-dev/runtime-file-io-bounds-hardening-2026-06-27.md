# Runtime File I/O and Bounds Hardening

Task: `DV-04-T03` Add static analysis and warning-as-error policy for first-party code in CI.

Date: 2026-06-27

## Summary

This pass hardens a small set of first-party runtime paths where debug-only checks, STB write callbacks, or temporary save helpers could hide failure states. The changes keep success behavior unchanged while turning more file and buffer edge cases into explicit runtime failures.

## Implemented Improvements

1. HTTP file-list receive callbacks now reject `size * nmemb` overflow at runtime instead of relying on a debug-only assert.
2. HTTP file-list receive callbacks now reject already-full download buffers at runtime before subtracting from the remaining capacity.
3. RTX screenshot STB write callbacks now record short writes and write failures in `screenshot_t::status`.
4. RTX TGA screenshot saving now returns a callback write error even if STB reports success.
5. RTX JPG screenshot saving now returns a callback write error even if STB reports success.
6. RTX PNG screenshot saving now returns a callback write error even if STB reports success.
7. RTX HDR screenshot saving now returns a callback write error even if STB reports success.
8. OpenGL STB PNG write callbacks now record short writes and write failures in `screenshot_t::status`.
9. OpenGL STB PNG conversion now rejects invalid zero or negative dimensions before allocation math.
10. OpenGL STB PNG conversion now guards row-byte and total converted-buffer size calculations before allocation.
11. Game3 proxy base85 save reads now use an explicit read loop instead of `while (!feof(...))`.
12. Game3 proxy base85 save reads now close the file and destroy the base85 context before reporting read failures.
13. Game3 proxy base85 save reads now report file close failures.
14. Game3 proxy base85 restore writes now closes the file and destroys the base85 context before reporting write failures.
15. Game3 proxy base85 restore writes now report file close failures separately from write failures.

## Files Changed

- `src/client/http.cpp`
- `src/rend_rtx/refresh/images.c`
- `src/rend_gl/images.c`
- `src/server/game3_proxy/game3_proxy.c`

## Verification

- `meson compile -C builddir-win worr_engine_x86_64 worr_ded_engine_x86_64 worr_opengl_x86_64 worr_rtx_x86_64` passed.
- `rg -n "assert\(size <= SIZE_MAX / nmemb\)|assert\(dl->position < MAX_DLSIZE\)|while\(!feof\(f\)\)|fwrite\(data, size, 1|fwrite\(data, 1, data_size, f\) != data_size" src/client/http.cpp src/rend_rtx/refresh/images.c src/rend_gl/images.c src/server/game3_proxy/game3_proxy.c` returned no matches.
- `git diff --check -- docs-dev/runtime-file-io-bounds-hardening-2026-06-27.md docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md src/client/http.cpp src/rend_rtx/refresh/images.c src/rend_gl/images.c src/server/game3_proxy/game3_proxy.c` passed.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64` refreshed and validated `.install/`.
- `meson test -C builddir-win --list` reported `No tests defined.`
