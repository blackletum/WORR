# Code Safety Improvements

Date: 2026-06-26

Task IDs: `DV-04-T03`, `DV-04-T04`, `DV-04-T05`

## Purpose
Reduce first-party command, filesystem, prompt, and Windows service maintenance risk with a focused pass of small hardening fixes. The scope deliberately avoids `q2proto/`, renderer backend redirection, and the active bot worktree changes.

## Implemented Improvements
1. `Cmd_ArgsRange()` now returns an empty string for inverted ranges instead of accidentally reading the starting argument.
2. `Cmd_ArgsRange()` now uses bounded `Q_strlcat()` assembly and stops appending when the destination reaches `MAX_STRING_CHARS`.
3. Command macro keyword expansion now copies `$qt` through `Q_strlcpy()` instead of unbounded `strcpy()`.
4. Command macro keyword expansion now copies `$sc` through `Q_strlcpy()` instead of unbounded `strcpy()`.
5. Unknown command macro expansion now clears the working buffer directly instead of using `strcpy(buf, "")`.
6. `exec` fallback probing now appends `.cfg` with `Q_strlcat()` and reports `ENAMETOOLONG` if the normalized path cannot fit.
7. Filesystem write mode construction now uses `Q_strlcpy()` / `Q_strlcat()` for append/write/exclusive/read-write/binary mode strings.
8. Windows service installation now uses bounded appends for the generated service command line and detects an oversized module path before calling `CreateServiceA()`.
9. Console completion now uses the correctly spelled `com_completion_threshold` cvar while preserving the old `com_completion_treshold` spelling as a non-archived compatibility fallback.
10. The in-place filesystem normalization test setup now uses bounded copy, keeping test code aligned with first-party string safety policy.

## Notes
- The compatibility fallback prefers `com_completion_threshold` when it differs from default. If only the legacy misspelled cvar is changed, that value is still honored.
- The old completion cvar is registered with `CVAR_NOARCHIVE` so new configs converge on the corrected spelling.
- Direct `strcpy()` / `strcat()` hits were removed from the touched first-party files.

## Verification
- `meson compile -C builddir-win worr_engine_x86_64 worr_ded_engine_x86_64` succeeded.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64` succeeded and validated the staged payload.
- `meson test -C builddir-win --list` reported no tests defined for this builddir.
- `clang -c src\common\tests.c -o .tmp\common_tests_syntax.obj ... -DUSE_TESTS=1` succeeded as a syntax compile for the tests-only source.
