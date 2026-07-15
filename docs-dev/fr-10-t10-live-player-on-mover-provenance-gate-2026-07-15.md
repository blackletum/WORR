# FR-10-T10: live player-on-mover provenance gate

Date: 2026-07-15  
Task: `FR-10-T10`  
Status: implemented diagnostic acceptance coverage; the parent task remains in progress.

## Purpose

The sealed rewind scene already represented player mover-relative provenance,
but its moving-brush runtime acceptance gate did not prove that data came from
an actual spawned player. This change adds a headless, repeatable proof that a
real dedicated-server bot becomes a player-bounds candidate whose sealed pose
retains the selected brush mover identity and exact mover-relative origin and
angles.

It advances acceptance evidence for a player standing on a rotated mover. It
does not claim normal movement physics, live weapon damage fairness, or a
continuously rotating mover are complete.

## Fixture and execution

`assets/maps/worr_fr10_rewind_mover.bsp` remains a deterministic collision
fixture with a real rotating push brush and a water inline model. It also contains an
`info_player_deathmatch` entity so the dedicated engine can spawn a bot. The
generated asset is 1,448 bytes with SHA-256
`bdc1a88bd7c83ddc7e52bd674856594113b2f09e798d2401522c06b33d404d53`.

The original V3 gate started a fresh headless `worr_ded_x86_64.exe` process with:

```text
+set deathmatch 1 +set maxclients 2
+map worr_fr10_rewind_mover
+addbot RewindRider
+sv worr_rewind_mover_selftest
```

The bot is created through the normal engine game-client path rather than a
fabricated test `gentity_t`. The command is map-local, is invoked only by the
test runner, and the runner terminates each fresh dedicated process after it
reads the status cvar.

## Provenance proof

`LagCompensation_RunHistoricalBrushRuntimeProbe` selects the asymmetric live
brush and an eligible live player. The test-scoped setup:

1. rotates the mover by 90 degrees;
2. assigns the real bot's `groundEntity` to that mover, positions it on the
   brush, and retains a 15-degree yaw offset;
3. uses normal `LagCompensation_RecordFrame` and
   `LagCompensation_RecordMovers` capture entry points;
4. appends a strictly later, ABI-validated player and mover history sample to
   avoid selecting a same-timestamp startup pose; and
5. builds the normal canonical scene and finds the bot's bounds candidate.

The gate accepts the rider only when its sealed pose has
`WORR_REWIND_POSE_HAS_MOVER`, has the selected mover's generation-qualified
identity, has the later capture time, and bitwise matches the expected
mover-relative origin and angles. Schema
`worr.networking.rewind-mover-runtime.v3` publishes `rider_setup` and
`rider_provenance_sealed` alongside the existing rotation-sensitive
historical-brush evidence.

The probe restores mover and rider origin, angles, ground entity, and link
state with RAII. It also restores temporary player and mover history rings and
invalidates frozen scene caches. Its authority fingerprint now includes player
angles, velocity, and a generation-qualified eligible ground-mover reference,
in addition to existing collision-relevant state.

## Acceptance evidence

Three fresh dedicated processes produced the recorded V3 row:

```text
pass:1:1:1:1:1:1:1:1:1:1:1:3:1000000:401042:0
```

This means setup, scene construction, rotation, real-rider setup and sealed
provenance, fixture reference collision, unrotated negative control, clear
baseline, historical dispatch/block, and authority preservation all passed.
The candidate count is three: the bot plus the two inline movers. The
historical transformed-BSP trace blocks at quantized fraction `401042`, while
the matching current-world baseline remains clear at `1000000`.

The test runner records invocation, individual stdout/stderr hashes, and
parsed result at `.tmp/networking/rewind-mover-runtime.json`. The three stdout
logs had the same SHA-256 and all stderr logs were empty.

## Deliberate remaining work

The probe assigns the bot's riding state inside a diagnostic command. It does
not prove that ordinary player physics carries a rider over a mover through
many frames, interpolation samples a moving platform while a player moves, or
a continuously rotating mover preserves fair weapon outcomes. Those scenarios,
broader BSP/BSPX maps, engine damage/fairness, and CPU/memory/load budgets
remain open under `FR-10-T10` and `FR-10-T11`.

## Validation

- `ninja -C builddir-win sgame_x86_64.dll` — rebuilt and linked.
- `python tools/networking/test_run_rewind_mover_runtime_gate.py` — 4/4.
- `python tools/networking/test_lag_compensation_mover_capture_contract.py` —
  8/8.
- `python tools/test_package_assets.py` — 14/14.
- `meson test -C builddir-win --no-rebuild
  network-rewind-collision-real-bsp-parity` — 1/1.
- `python tools/refresh_install.py --build-dir builddir-win --install-dir
  .install --base-game basew --platform-id windows-x86_64` — staged and
  validated the Windows payload.
- `python tools/networking/run_rewind_mover_runtime_gate.py --dedicated-exe
  .install/worr_ded_x86_64.exe --working-dir .install --output
  .tmp/networking/rewind-mover-runtime.json --repeat 3 --timeout 30` — 3/3.

`meson test -C builddir-win --no-rebuild` — 132/132 passed.

The V4 normal-frame continuation supersedes this runtime status; see
`docs-dev/fr-10-t10-normal-frame-rider-continuity-gate-2026-07-15.md`.

## Roadmap completion after this round

- Overall strategic roadmap: **68/180 complete (37.8%)**; **112 open**.
- `FR-10`: **3/16 complete (18.75%)**; **13 open**.
