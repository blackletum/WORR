# FR-10-T10 Rotated Moving-Brush Runtime Gate

Date: 2026-07-15

Project task: `FR-10-T10`

## Scope

This follow-up strengthens the headless historical-brush gate with an
orientation-sensitive real collision case.  It proves that a sealed mover pose
uses its historical angles at the engine's immutable transformed-BSP provider,
not the current brush's angles or merely its asset bounds.

The packaged `worr_fr10_rewind_mover` fixture has an asymmetric solid inline
brush: its local X span is 32 units and its local Y span is 48 units.  The
probe selects only an eligible live brush where the Y span exceeds X by at
least four units, applies a 90-degree yaw before capture, and traces at a
world-X extent that the unrotated asset cannot occupy.  It therefore cannot
pass merely because the asset was traced at the original transform.

## Probe sequence

1. The command finds an actual live `SOLID_BSP` push/stop mover from the
   packaged map, saves its origin/angles/link state, rotates it 90 degrees,
   and links it through the normal engine path.
2. It invokes the normal `LagCompensation_RecordMovers()` path.  Startup
   console commands can share a timestamp with the map's preceding regular
   capture, whose duplicate guard correctly keeps the earlier unrotated pose.
   The probe consequently appends one strictly later sample for every live
   scene mover through `BuildMoverPose` and `Worr_RewindHistoryAppendV1`.
   This preserves the all-movers scene requirement rather than allowing the
   non-selected brush to cause a future-history miss.
3. Those temporary histories and sealed-scene cache entries are restored by
   scoped cleanup before the command returns; the self-test cannot alter later
   gameplay history.
4. A direct engine clip against the rotated live brush must block.  The same
   sealed immutable asset and context, explicitly supplied with the original
   unrotated angles, must dispatch but remain clear.
5. The live brush is then moved +96 X and reset to its original angles.  The
   sealed context excludes that live brush from the current-world baseline,
   which must be clear; the sealed rotated pose must still block through
   `TraceTransformed` without an edict rewind or relink.
6. The authoritative collision hash is identical before and after the query,
   and RAII restores the map entity's original transform/link state.

## Machine-readable evidence

The status schema is now `worr.networking.rewind-mover-runtime.v5`. In
addition to the existing scene, baseline, dispatch, historical block, and
authority fields it requires `rotation_applied=1` and
`rotation_control_unblocked=1`. It also requires a real engine bot rider with
sealed mover-relative provenance and twelve ordinary rotating-pusher frame
pairs, including their exact sealed canonical scene; the external runner
rejects an incomplete, duplicate, stale, or non-deterministic row.

`.tmp/networking/rewind-mover-runtime.json` records three independent
dedicated-server processes.  All report the same passing status:

| Field | Value |
|---|---:|
| rotation applied | 1 |
| real rider setup/provenance | 1/1 |
| normal-frame continuity samples | 12 |
| normal-frame canonical scene sealed | 1 |
| unrotated negative control clear | 1 |
| current baseline fraction | 1.000000 |
| historical rotated fraction | 0.374756 |
| historical dispatch/block/authority flags | 1/1/1 |
| candidate count | 3 |
| failure code | 0 |

The run stdout digest is identical in all three runs, stderr is empty, and the
runner explicitly terminates each dedicated process.  No graphical client or
renderer is launched.

## Validation

- The updated `sgame_x86_64.dll` target built and linked through the configured
  Ninja graph.
- `python tools/networking/test_run_rewind_mover_runtime_gate.py` — 4/4.
- `python tools/networking/test_lag_compensation_mover_capture_contract.py` —
  8/8.
- `python tools/test_package_assets.py` — 14/14.
- `python tools/refresh_install.py --build-dir builddir-win --install-dir
  .install --base-game basew --platform-id windows-x86_64` — staged and
  validated the Windows payload.
- `python tools/networking/run_rewind_mover_runtime_gate.py --dedicated-exe
  .install/worr_ded_x86_64.exe --working-dir .install --output
  .tmp/networking/rewind-mover-runtime.json --repeat 3 --timeout 30` — 3/3.

This is still a focused collision proof, not full rotating-mover gameplay
fairness. Normal player-on-mover physics, continuously rotating movers,
complex BSP/BSPX maps, live weapon damage, load budgets, and release-platform
evidence remain open under `FR-10-T10`.

## Roadmap completion after this round

- Overall strategic roadmap: **68/180 complete (37.8%)**; **112 open**.
- `FR-10`: **3/16 complete (18.75%)**; **13 open**.

The gate advances the open T10 evidence only; no parent task is closed.
