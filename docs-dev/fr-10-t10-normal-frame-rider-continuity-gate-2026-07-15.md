# FR-10-T10: normal-frame moving-rider continuity gate

Date: 2026-07-15  
Task: `FR-10-T10`  
Status: implemented diagnostic acceptance coverage; the parent task remains in progress.

## Purpose

The preceding rider-provenance gate put a real bot on a mover inside the final
diagnostic probe. This V4 increment proves a stronger, separate boundary: the
engine's ordinary pusher physics moves a real bot through normal server frames
and the ordinary end-frame player and mover capture paths retain coherent
paired history for that moving ride.

This is narrow gameplay-physics evidence, not a claim that all player-on-mover
or weapon-fairness scenarios are complete.

## Real fixture and frame sequence

The generated collision fixture's asymmetric solid inline model is now a
`func_rotating` with `spawnflags=1`, `speed=45`, and `dmg=0`. It is a normal
`SOLID_BSP` `MoveType::Push` mover that starts rotating without damaging the
test rider. The fixture also contains the existing deathmatch spawn for a real
engine bot. Its generated identity is 1,448 bytes, SHA-256
`bdc1a88bd7c83ddc7e52bd674856594113b2f09e798d2401522c06b33d404d53`.

The headless runner uses the ordered dedicated commands:

```text
+map worr_fr10_rewind_mover
+addbot RewindRider
+sv worr_rewind_mover_arm_rider
+wait 12
+sv worr_rewind_mover_selftest
```

The console-only arm command locates the real eligible bot and active
asymmetric mover, places the bot at a non-central local offset on the brush,
and sets its `groundEntity`. The following twelve server frames are not
simulated by the diagnostic: ordinary `G_Physics_Pusher` moves the standing
bot, then `ClientEndServerFrames` calls `LagCompensation_RecordFrame` and the
normal final-frame path calls `LagCompensation_RecordMovers`.

The runner owns and terminates each fresh dedicated process after reading the
status. The map-local arm state therefore cannot escape into a live server
session; no graphical client or renderer is launched.

## Pair validation

Before the existing historical trace diagnostic mutates local test state, the
V4 probe walks the real bot and mover history rings. For every rider pose with
`WORR_REWIND_POSE_HAS_MOVER`, it finds the generation-qualified brush pose at
the exact same `server_time_us` and `server_tick`. A paired sample is valid
only when both stored mover-relative arrays bitwise equal the difference of
that frame's sealed player and mover transforms.

The gate requires at least two paired samples, a changed mover angle, and a
changed rider world origin. A static rider assignment, manually appended pose,
missed mover sample, or disconnected player/mover capture cadence therefore
cannot satisfy the proof. The scoped historical trace retains its 90-degree
transform, current-world exclusion, immutable transformed-BSP collision,
negative control, and authority-fingerprint checks. Its negative control ray
now derives from captured local axes, so it remains discriminating after the
ordinary frame sequence rotates the fixture.

## Acceptance evidence

Schema `worr.networking.rewind-mover-runtime.v4` records this identical row in
three fresh dedicated processes:

```text
pass:1:1:1:1:1:1:1:1:1:1:1:1:3:12:1000000:374756:0
```

`rider_frame_continuity=1` and `rider_continuity_samples=12` prove paired
ordinary-frame evidence. The candidate count remains three (bot plus two
inline models), the current baseline is clear, and the historical brush blocks
at quantized fraction `374756`. All three stderr streams were empty and all
three stdout logs had SHA-256
`769edb4300c2653b0ab7d0ddacd80c163cb2f2bb16a468f92e9964ff379de273`.

## Deliberate remaining work

This does not cover a translating elevator/train, multiple riders, a rider
walking or jumping during motion, discontinuities, complex BSP/BSPX geometry,
or real weapon damage and fairness outcomes. Broader player-on-mover,
continuous-motion, load-budget, and release gates remain open in `FR-10-T10`
and `FR-10-T11`.

## Validation

- `ninja -C builddir-win sgame_x86_64.dll` — rebuilt and linked.
- `python tools/networking/test_run_rewind_mover_runtime_gate.py` — 4/4.
- `python tools/networking/test_lag_compensation_mover_capture_contract.py` —
  8/8.
- `python tools/test_package_assets.py` — 14/14.
- `meson test -C builddir-win --no-rebuild
  network-rewind-collision-real-bsp-parity` — recorded after the fixture hash
  update.
- `python tools/refresh_install.py --build-dir builddir-win --install-dir
  .install --base-game basew --platform-id windows-x86_64` — staged and
  validated the Windows payload.
- `python tools/networking/run_rewind_mover_runtime_gate.py --dedicated-exe
  .install/worr_ded_x86_64.exe --working-dir .install --output
  .tmp/networking/rewind-mover-runtime.json --repeat 3 --timeout 30` — 3/3.

`meson test -C builddir-win --no-rebuild` — 132/132 passed.

The V5 canonical-scene continuation supersedes this runtime status; see
`docs-dev/fr-10-t10-normal-frame-canonical-scene-gate-2026-07-15.md`.

## Roadmap completion after this round

- Overall strategic roadmap: **68/180 complete (37.8%)**; **112 open**.
- `FR-10`: **3/16 complete (18.75%)**; **13 open**.
