# Vulkan In-Session Confirmation Popup Parity

Date: 2026-07-15

Task IDs: `FR-01-T15`, `FR-02-T05`

Status: the guarded in-session `leave_match_confirm` and `forfeit_confirm`
RmlUi routes now have retained OpenGL-versus-native-Vulkan visual evidence.
This extends popup coverage; it does not establish parity for all session
flows.

## Change

The default native renderer overlay parity runner now includes
`leave_match_confirm` and `forfeit_confirm`, mapped to retained scenes in
`assets/renderer_parity/fr01_rmlui_overlay_manifest.json`. Each route opens its
native session document through the RmlUi bridge, waits for a deterministic
reduced-motion frame sequence, captures a 960 x 720 TGA image, then executes
synthetic key, character, pointer, button, and wheel input before asserting the
route closed. Vulkan runs with `VK_LAYER_KHRONOS_validation`. Each isolated
route launch retains its diagnostic output; a nonzero launch exit receives one
delayed retry, with both attempts preserved in the route log.

The retained manifest uses threshold 8, maximum mean absolute RGB
`0.13 / 0.12 / 0.10` for leave-match and `0.14 / 0.12 / 0.10` for forfeit, and
at most `0.4%` pixels over threshold. Those limits are deliberately close to
the measured reference rather than acting as a broad visual smoke test.

## Evidence

The completed focused headless run was:

```text
python tools/renderer_parity/run_rmlui_overlay_parity.py \
  --install-dir .install \
  --capture-root .tmp/renderer-parity/fr01-rmlui-leave-match-confirm-final \
  --route-id leave_match_confirm
```

Both renderers opened and synthetically closed the route after 372 rendered
frames. The focused forfeit confirmation followed the same route lifecycle.
Each comparison covers all 691,200 pixels:

| Route | Maximum RGB | Mean absolute RGB | Pixels above threshold 8 | Vulkan validation |
|---|---:|---:|---:|---:|
| Leave match | `54 / 51 / 48` | `0.104 / 0.094 / 0.077` | `2,359` (`0.341%`) | `0` |
| Forfeit | `57 / 51 / 48` | `0.113 / 0.093 / 0.077` | `2,349` (`0.340%`) | `0` |

## Boundary

This protects two native session confirmation popups and their close input. It
does not cover tournament confirmation state, live match data and controller
focus, download progress, or the rest of the gameplay HUD. Those remain in the
`FR-02-T05` nightly parity sequence.
