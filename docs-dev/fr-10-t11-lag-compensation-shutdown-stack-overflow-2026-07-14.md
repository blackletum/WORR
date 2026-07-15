# Lag-Compensation Shutdown Stack-Overflow Repair

Date: 2026-07-14

Task ID: `FR-10-T11`

## Scope

Repair the client-hosted server-game shutdown crash recorded in
`.install/crashdump_134285190652660396.dmp`.

## Diagnosis

The staged Windows dump reports `c00000fd` (stack overflow) in
`LagCompensation_Shutdown`, reached through `ShutdownGame` and
`SV_ShutdownGameProgs` while handling `quit`. The exception occurred in the
MSVC stack-probe helper before the shutdown routine could complete.

The fault was caused by aggregate assignments such as `sceneCaches = {}` and
`poseTracks = {}`. Those static containers include every client history and
frozen-scene work set. MSVC lowered the assignment through a multi-megabyte
temporary on the game thread's stack, exhausting it during shutdown. The same
pattern was also present in map/history cache reset paths.

## Implementation

- Added in-place decision-cache and scene-cache invalidation helpers. Cache
  consumers already require `valid`, so invalidation is the correct reset
  boundary without physically copying their storage.
- Replaced bulk cache reset assignments in history reset, epoch adoption,
  client reset, and client-life reset paths.
- Removed large aggregate storage resets from `LagCompensation_Shutdown`.
  `LagCompensation_Init` and `LagCompensation_ResetMap` already recreate all
  policy, history, journal, lifecycle, and diagnostics state before any later
  use, so shutdown only needs to detach imports and invalidate caches.
- Added the repository rule that all automated test launches must be headless;
  use the dedicated server or an explicit no-window/headless mode with an
  isolated runtime directory.

## Validation

- Symbolicated the supplied dump against the matching staged PDBs. Its stack
  resolves to `LagCompensation_Shutdown` via `ShutdownGame` on the client
  shutdown path.
- `meson compile -C builddir-win sgame_x86_64` rebuilt and linked the server
  game module.
- `python tools/refresh_install.py --build-dir builddir-win --install-dir
  .install --base-game basew --platform-id windows-x86_64` refreshed and
  validated the distributable staging root. The staged `sgame_x86_64.dll`
  SHA-256 matches the rebuilt binary.
- A headless dedicated-server smoke loaded `base1` and exited through `quit`:
  `worr_ded_x86_64.exe +set basedir E:\Repositories\WORR\.install +set game
  basew +set dedicated 1 +map base1 +quit`. The log reached `Closing console
  log.` and no crash dump newer than the pre-fix dump was created.
- The headless rewind acceptance runner passed all 40 player-history cases
  three times each (120 probe invocations): zero deterministic mismatches,
  authoritative mutations, and failed assertions. Evidence is under
  `.tmp/networking/lagcomp-shutdown-fix/rewind-acceptance-evidence.json`.
- `python -m unittest tools.test_package_assets` passed 14 tests; `python -m
  unittest test_compare_captures` from `tools/renderer_parity` passed 3 tests.
