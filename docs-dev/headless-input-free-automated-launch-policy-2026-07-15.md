# Headless, input-free automated launch policy

Date: 2026-07-15  
Project tasks: `FR-10-T11`, `FR-10-T14`, `FR-10-T15`

## Policy

Automated test launches must not interfere with a developer's desktop. The
workspace rule now requires a dedicated server or an explicit no-window mode,
an isolated runtime directory, and no client input initialization or mouse
capture. Visual assessment remains a separate, deliberately requested activity.

For Windows client automation the required launch settings are:

```text
+set win_headless 1
+set in_enable 0
+set in_grab 0
```

`win_headless` supplies a hidden native surface where a renderer-backed test
needs one. `in_enable=0` exits the input initialization path before platform
mouse setup, while `in_grab=0` is a second explicit no-capture guard. Launchers
also use `stdin=DEVNULL` and `CREATE_NO_WINDOW` on Windows. Dedicated-only
tests continue to use `worr_ded_x86_64.exe` and do not take a client path.
The client additionally fails closed at `IN_Init()`: `win_headless` prevents
platform mouse initialization even if a future launcher omits `in_enable=0`.
`IN_GetCurrentGrab()` also fails closed when this deliberate opt-out leaves the
grab cvar unset; map activation can still call it after the client transitions
to gameplay. This keeps the policy no-window and input-free through the full
connection lifecycle.

## Applied launchers

The rule is enforced by command-building contracts for networking live snapshot
and native-shadow runs. The same settings are applied to staged impairment,
renderer-parity, Vulkan debug, shadowmapping, and RmlUi capture tools. RmlUi's
`ui_rml_runtime_synthetic_input` remains engine-generated test input, not OS
pointer input, and continues to work with physical input disabled.

The canonical two-client weapon gate additionally sets `cl_async=1`: command
finalization stays on its independent physics cadence while the hidden renderer
is absent from the input clock. Its sustained-beam modes issue one ordinary
`+attack` only; they never synthesize release/press refresh edges.

## Validation

```powershell
python -m unittest tools/networking/test_run_staged_impairment_smoke.py tools/networking/test_run_native_shadow_runtime_smoke.py tools/networking/test_run_native_shadow_repeated_runtime_smoke.py tools/networking/test_run_live_snapshot_acceptance_gate.py
python -m unittest tools/renderer_parity/test_run_renderer_perf_capture.py
python -m unittest test_run_vk_debug_smoke.py  # run from tools/renderer_parity
python -m pytest tools/ui_smoke/test_check_rmlui_runtime_capture.py -q
python -m unittest tools/networking/test_headless_input_contract.py
python -m unittest tools/networking/test_run_canonical_rail_damage_runtime_gate.py
```

These tests passed: networking `69/69`; renderer performance `4/4`; Vulkan
debug `2/2`; and RmlUi capture `28/28`.
