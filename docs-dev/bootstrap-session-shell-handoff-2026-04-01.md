# Bootstrap Session-Shell Handoff Stabilization

Task Reference: `DV-08-T12`

## Summary
- Restored the Windows client bootstrap to a real single-window session-shell handoff so the updater splash and hosted engine now stay on the same native HWND instead of falling back to a second blank client window.
- Fixed the adopted-window startup activation path so the first engine/menu frame presents without requiring Alt+Tab to wake the client.
- Kept the updater splash UX improvements in-process: minimum five-second dwell, centered circular progress/spinner, centered status stack, centered legal footer, and the startup blend overlay once the engine begins drawing.
- Hardened `.install` staging so refreshed launcher/runtime binaries are explicitly replaced and hash-verified; VS Code debug launches now run the binaries that were just built instead of occasionally reusing stale staged executables.

## Problem
Two separate issues were masking each other on the Windows client bootstrap path.

First, the shared-window handoff regressed in installed launches and could silently fall back to a second native client window. That reintroduced the same black-screen/Alt+Tab behavior the session-shell work was supposed to eliminate, because the hosted engine either started minimized from the adopted-window path or never adopted the splash HWND at all.

Second, local validation through `.install/worr_x86_64.exe` was not always exercising the newest launcher binaries. The build output in `builddir-win/` could contain the fix while `.install/` still held older executables, which made the launcher appear unchanged even after a successful build.

## Implementation
### Shared-window handoff restoration
- `src/updater/bootstrap.cpp`
- `LaunchEngineAndWait()` now resolves the concrete `SplashUi` instance and performs the Windows shared-window handoff directly from that type instead of relying on the previous generic branch that could skip the splash handoff path in installed/windowed runs.
- The splash keeps the existing minimum display gate and then hands its native Win32 HWND to the hosted engine through `WORR_BOOTSTRAP_WIN32_HWND`.
- The splash shell now uses a bootstrap-owned native Win32 window on Windows and only wraps that HWND in SDL for splash rendering; during engine handoff the SDL renderer and SDL window wrapper are destroyed before the engine adopts the HWND, so the live desktop canvas no longer remains stuck on the last presented splash frame.

### Adopted-window activation and redraw fix
- `src/windows/client.c`
- The engine no longer forwards unhandled adopted-window messages back into SDL's old WndProc after taking ownership of the splash HWND.
- The adopted window is explicitly shown, activated, focused, invalidated, and redrawn at adoption time.
- The client also reasserts activation after later mode changes, which prevents the hosted engine from starting in the `ACT_MINIMIZED` path that previously suppressed the first rendered frame until a manual focus transition.

### Splash presentation cleanup
- `src/updater/bootstrap.cpp`
- The splash continues to render the centered WORR logo, circular progress indicator, centered status copy, and centered multiline legal footer.
- The layout now clamps the logo and content stack against the actual available vertical space so the spinner, status text, and legal block retain room even in smaller windowed shells.
- The splash renderer now uses a normal SDL renderer on the shell window instead of the earlier surface-backed software path.
- On Windows, the splash renderer now prefers SDL's software renderer on the wrapped native shell window to avoid graphics-API ownership conflicts before the OpenGL client takes over the same HWND.

### Engine-side transition blend
- `src/client/screen.cpp`
- Bootstrap-launched client sessions still use the existing one-shot startup blend overlay so the first in-engine frame eases out of the splash presentation instead of hard-cutting.

### Main menu presentation
- `src/game/cgame/ui/ui_menu.cpp`
- `src/game/cgame/ui/worr.json`
- The centered WORR logo/menu/footer presentation remains in place, including the centered `DEVELOPMENT VERSION` footer and the live `version` cvar subline in smaller grey text.
- The centered quit-confirmation popup remains wired from the main menu `Quit` action.
- The main menu now uses a dedicated engine-side backdrop instead of falling through to the global solid-black UI clear, which removes the "black screen" appearance while keeping the centered WORR logo/menu stack readable on installed launches.

### Runtime staging hardening
- `tools/stage_install.py`
- Runtime staging now removes the existing destination file before each copy and verifies the copied file with SHA-256.
- This closes the local workflow gap where `.install/worr_x86_64.exe`, `.install/worr_updater_x86_64.exe`, and `.install/worr_ded_x86_64.exe` could lag behind the freshly built `builddir-win/` binaries while `refresh_install.py` still reported success.

## Validation
- `meson compile -C builddir-win`
- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- Verified staged launcher hashes matched build outputs after the staging hardening (`.install/worr_x86_64.exe`, `.install/worr_updater_x86_64.exe`, `.install/worr_ded_x86_64.exe` all matched their `builddir-win/` counterparts).
- Windows client smoke launch through `.install/worr_x86_64.exe` in windowed OpenGL mode showed the same visible HWND before and after engine handoff, confirming the session shell stayed on one native window.
- Bootstrap trace with `WORR_BOOTSTRAP_TRACE=1` recorded `SplashUi shared_hwnd props_ok`, confirming the installed launcher resolved a native HWND from the splash shell on the restored handoff path.
- An engine-side `screenshotpng` capture from the installed launch path confirmed the live main menu rendered with the centered WORR logo, centered menu options, and centered `DEVELOPMENT VERSION` plus version subline.
- A delayed in-engine screenshot from the staged launcher confirmed the main menu no longer collapses to a pure-black background; the dedicated backdrop now renders behind the menu stack during installed startup.
- A delayed live `PrintWindow` capture from the staged launcher confirmed the desktop window itself now advances past the splash and shows the in-engine main menu, resolving the previous "audio starts but splash canvas remains visible" failure mode.

## Follow-up
- Do one manual visual pass on the splash shell on the target desktop profile to fine-tune the final logo/status/legal proportions, because desktop-compositor capture of the bootstrap window remained less trustworthy than in-engine framebuffer screenshots during local validation.
