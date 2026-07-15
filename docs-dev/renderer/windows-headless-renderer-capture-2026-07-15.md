# Windows Headless Renderer Capture Surface

Date: 2026-07-15

Task ID: `FR-01-T15`

Status: implemented; a hidden native Vulkan validation smoke passed. Paired
Vulkan/OpenGL evidence and performance budgets remain pending.

## Purpose

Windows renderer automation previously had no explicit client-side no-window
mode. Passing `r_fullscreen 0` only requested a normal visible window, so a
runtime parity or timing run could violate the repository rule that automated
tests must not launch an interactive client.

`win_headless 1` now supplies a Windows-only, non-archived capture mode. It
keeps a real, non-zero native HWND and therefore permits both native Vulkan and
OpenGL presentation-surface creation. It does not redirect any Vulkan work to
OpenGL.

The cvar is `CVAR_NOARCHIVE`: a one-shot automated launch cannot persist an
invisible normal client session.

## Window and activation contract

When `win_headless` is set, `src/windows/client.c`:

- applies the requested window geometry but uses `SWP_NOACTIVATE` rather than
  `SWP_SHOWWINDOW`, then explicitly hides the HWND;
- never calls the foreground/focus path for that hidden surface;
- marks the client active internally, because a hidden HWND does not receive
  the normal foreground activation notification and renderer scheduling must
  remain representative;
- forces `r_fullscreen` to `0`, preventing display-mode changes and retaining a
  concrete surface extent; and
- leaves desktop hardware gamma unchanged, even if `r_hwgamma` is enabled.

The bootstrap-HWND adoption path follows the same hiding and no-focus rules.

This is intentionally a hidden native rendering surface, not a synthetic
offscreen renderer mode. Native Vulkan still creates and presents through its
own Windows surface/swapchain, and OpenGL still creates its own Windows GL
context.

## Capture tooling

Both client-launching renderer tools now include `+set win_headless 1` and use
Windows `CREATE_NO_WINDOW` process creation:

- `tools/renderer_parity/run_capture_matrix.py` for paired scene captures;
- `tools/renderer_parity/run_vk_debug_smoke.py` for native Vulkan debug and
  telemetry validation.

Their isolated `homedir`, disabled audio, fixed window geometry, renderer
selection, and per-run output locations remain unchanged. The client-side cvar
is the authority that prevents the native window from being shown or focused;
`CREATE_NO_WINDOW` also suppresses a console window for the Python-launched
process.

## Validation

Structural coverage is in
`tools/renderer_parity/test_win_headless_renderer_source.py`. It checks that
the no-window cvar is non-archived, the window is hidden without activation,
fullscreen and desktop gamma are guarded, and every renderer client launcher
selects the mode.

Compile the Windows client and refresh the staged distribution before any
runtime run:

```text
ninja -C builddir-win worr_engine_x86_64.dll worr_vulkan_x86_64.dll
python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64
Push-Location tools/renderer_parity
python -m unittest test_win_headless_renderer_source.py test_run_vk_debug_smoke.py
Pop-Location
```

The explicit hidden Vulkan smoke is:

```text
python tools/renderer_parity/run_vk_debug_smoke.py --install-dir .install --run-root .tmp/renderer-parity/fr01-vk-debug-win-headless --vulkan-validation
```

The rebuilt 2026-07-15 Windows runtime passed this smoke from an isolated
packaged stage at `.tmp/renderer-parity/fr01-win-headless-stage`:

```text
exit status:              0
baseline and overlay:     produced
changed overlay pixels:   3067
maximum channel delta:    231
VK_STATS:                 gpu_valid=1, gpu_ms=1.400
Vulkan validation errors: none
```

The exact command, screenshots, and process log are retained beneath
`.tmp/renderer-parity/fr01-vk-debug-win-headless`. It is only an enabling
check, not paired Vulkan/OpenGL visual parity or a performance result. The
paired capture manifest and budget gates remain defined in
`vulkan-paired-performance-capture-contract-2026-07-15.md`.

## Paired capture proof

The same hidden native-surface stage also completed the existing
`FR-01-T06` bmodel/legacy-lightmap visual matrix. The retained report at
`.tmp/renderer-parity/fr01-bmodel-win-headless/results-runner.json` covers the
OpenGL and Vulkan captures of both `960x720` scenes:

```text
bmodel transformed first frame: 170000 cropped pixels, maximum RGB error 0,
                                mean absolute RGB error 0.0, mask IoU 1.0
legacy lightmapped world:       34000 cropped pixels, maximum RGB error 0,
                                mean absolute RGB error 0.0, mask IoU 1.0
Vulkan log error patterns:      none (including VUID/validation errors)
```

This demonstrates that the explicit no-window path supports real paired
OpenGL/Vulkan captures. It reconfirms only the already-closed `FR-01-T06`
scope; it does not promote any other partial parity row or establish a
performance budget.
