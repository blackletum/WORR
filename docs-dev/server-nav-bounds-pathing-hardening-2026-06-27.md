# Server Navigation Bounds and Pathing Hardening - 2026-06-27

Task: `DV-04-T03`

## Summary

This pass hardens the server-side Kex navigation loader and path helpers. It focuses on file-controlled count validation, allocation-size checks, malformed-link rejection, safer loaded-state handling, and registration table bounds while preserving existing navigation behavior for valid `.nav` files.

## Implemented Improvements

1. Added checked byte-size helpers for navigation allocations.
2. Added checked packed-context size accumulation for `Nav_AllocCtx()`.
3. Made `Nav_AllocCtx()` reject zero-node contexts and report allocation failure.
4. Delayed `nav_data.loaded = true` until a navigation file is successfully opened.
5. Added a null/empty map-name guard to `Nav_Load()`.
6. Added lower and upper bounds for supported `.nav` file versions.
7. Validated node, link, and traversal counts before allocation, with 16-bit limits matching stored node/link ids.
8. Replaced node/link/traversal/conditional-node/edict allocations with checked byte counts.
9. Allocated a one-entry link sentinel for zero-link nav files so zero-link nodes keep valid pointer ranges.
10. Replaced signed `first_link + num_links` validation with subtraction-based extent checks.
11. Rejected negative traversal indices other than the `-1` sentinel.
12. Validated nav edict counts against `MAX_EDICTS`.
13. Checked node-link bitmap byte sizing and converted bitmap row offsets to `size_t`.
14. Added a null path/request guard in `Nav_Path()`.
15. Fixed closest-node radius selection to use `nodeSearch.radius` instead of `nodeSearch.maxHeight`.
16. Added null and capacity guards for nav edict registration.
17. Fixed `Nav_UnRegisterEdict()` tail compaction to avoid unsigned underflow.

## Files Changed

- `src/server/nav.c`

## Verification

- `meson compile -C builddir-win worr_engine_x86_64 worr_ded_engine_x86_64` passed.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64` passed and refreshed `.install/`.
- `git diff --check` passed.
- `meson test -C builddir-win --list` reported `No tests defined.`
