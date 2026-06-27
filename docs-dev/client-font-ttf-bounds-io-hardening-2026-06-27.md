# Client Font TTF Bounds and I/O Hardening

Date: 2026-06-27

Task IDs: `DV-04-T03`

## Summary

This pass hardens `src/client/font.cpp` around KFONT token parsing, SDL3_ttf
glyph extraction, TTF atlas packing, external font file reads, and glyph-dump
output path creation. The work is client/runtime defensive cleanup and does not
change user-facing font selection behavior.

## Implemented Improvements

1. Added checked `size_t` multiply/add helpers for font buffer sizing.
2. Added saturating 26.6 fixed-point advance conversion instead of raw signed
   shifts.
3. Rejected overflowing KFONT unsigned token values instead of truncating them
   into `uint32_t`.
4. Checked TTF atlas pixel and RGBA byte counts before allocating page buffers.
5. Checked atlas page-index conversion before casting `pages.size()` to `int`.
6. Checked both persistent atlas pixel allocation and renderer-upload allocation
   before writing/copying.
7. Hardened TTF alpha blitting against signed `x + w` / `y + h` overflow and
   invalid source pitch.
8. Checked blit source and destination row byte offsets before copying.
9. Rejected invalid SDL glyph surfaces with non-positive dimensions, missing
   pixels, impossible fixed-point advances, or undersized pitch.
10. Checked TTF glyph bitmap allocation size and SDL surface lock failure.
11. Replaced atlas packing addition checks with subtraction-style bounds that
    cannot overflow on extreme glyph metrics.
12. Checked atlas alpha vector sizing before chunk allocation proceeds.
13. Hardened external TTF disk reads for `int`, `size_t`, and `streamsize`
    conversion limits, short reads, and bad stream state.
14. Checked `font_dump_glyphs` output filename truncation before opening files.

## Verification

- `meson compile -C builddir-win worr_engine_x86_64`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `git diff --check`
- `meson test -C builddir-win --list` reported `No tests defined.`
