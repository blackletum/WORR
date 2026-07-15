# FR-10-T10: normal-frame canonical rider-scene gate

Date: 2026-07-15  
Task: `FR-10-T10`  
Status: implemented diagnostic acceptance coverage; the parent task remains in progress.

## Purpose

The V4 moving-rider gate proved ordinary rotating-pusher frames produce
coherent player and mover history pairs. This V5 increment proves the next
authoritative boundary: the newest real pair can be frozen by `CanonicalScene`
as an exact immutable rewind scene before the command-scoped historical trace
diagnostic makes any test-local mutation.

## Exact normal-frame scene proof

The dedicated fixture and command order remain unchanged: a real bot is armed
on the rotating `SOLID_BSP` `MoveType::Push` brush, then `wait 12` permits
ordinary physics, player end-frame capture, and mover capture to execute. V5
selects the newest paired player/mover timestamp from those normal history
rings and creates a fully accepted exact-time decision for that timestamp.

`CanonicalScene(riderIndex, normalFrameDecision)` must return a sealed scene
containing both bot bounds and selected brush candidates. The gate rejects the
process unless both candidates are exact query results at the selected time,
the rider has the selected generation-qualified mover reference, and stored
relative origin and angles bitwise equal the corresponding sealed player-minus-
mover transforms.

The check runs before the pre-existing diagnostic changes the mover by 90
degrees or creates a later test sample. It therefore proves ordinary
server-frame history itself feeds the immutable authoritative scene, rather
than only a direct ring inspection or injected sample.

## Acceptance evidence

Schema `worr.networking.rewind-mover-runtime.v5` records this identical row in
three fresh dedicated processes:

```text
pass:1:1:1:1:1:1:1:1:1:1:1:1:1:3:12:1000000:374756:0
```

The first thirteen true fields include `rider_frame_continuity=1` and
`rider_frame_scene_sealed=1`; later values record three candidates, twelve
paired normal-frame samples, a clear current baseline, historical block
fraction `374756`, and no failure. Each process had empty stderr and the same
stdout SHA-256 `125b9e5b52036514eb3827986797e181e5ce872d679119f44708b1ca9c0037a3`.

The scoped transformed-BSP proof remains active after this assertion: it
verifies the clear current baseline, local-axis rotation negative control,
historical brush block, and unchanged authority fingerprint.

## Deliberate remaining work

This one rotating brush and one stationary rider do not cover translating
elevators or trains, multiple riders, rider input during motion,
discontinuities, complex BSP/BSPX maps, actual hitscan damage fairness, or
CPU/memory/load budgets. Those remain open under `FR-10-T10` and `FR-10-T11`.

## Validation

- `ninja -C builddir-win sgame_x86_64.dll` — rebuilt and linked.
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

- `meson test -C builddir-win --no-rebuild
  network-rewind-collision-real-bsp-parity` — 1/1.
- `meson test -C builddir-win --no-rebuild` — 132/132.

## Roadmap completion after this round

- Overall strategic roadmap: **68/180 complete (37.8%)**; **112 open**.
- `FR-10`: **3/16 complete (18.75%)**; **13 open**.
