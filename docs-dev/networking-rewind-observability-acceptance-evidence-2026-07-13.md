# Player rewind observability and acceptance evidence (2026-07-13)

Task scope: **FR-10-T10**, **FR-10-T11**, and **FR-10-T14**.

This change adds a bounded, versioned observation seam to the production
server-game rewind path and a deterministic acceptance-evidence runner for the
player-bounds portion of lag compensation. It does not close any of the three
tasks by itself. Movers, a live client/server weapon-damage harness, sustained
load, and multi-machine evidence remain required.

## Production behavior

`worr_rewind_observation_v1` is a pointer-free 160-byte record containing the
weapon policy, selected path, policy and history reasons, target times, frozen
scene hash, hit identity, fallback reason, trace fraction, elapsed query time,
and an authoritative-state before/after fingerprint. A caller-owned 256-entry
ring in sgame retains observations and saturating telemetry without allocating
during weapon traces.

The existing `sg_lag_compensation_debug` cvar controls the feature:

- `0` (default): no observation, timing, hashing, journal writes, or per-query
  output. Existing gameplay cost and behavior are retained.
- `1`: existing once-per-second aggregate diagnostics only.
- `2`: retain one detailed journal record per compensated collision query,
  including a fingerprint proving whether live player collision state changed
  during the query, without printing each query.
- `3` or greater: retain the same bounded journal and also print one detailed
  `lagcomp trace` line per query for short interactive investigations.

The authoritative fingerprint covers live client-slot identity generation,
link/solid/damage state, health, origin, and bounds. Historical poses and proxy
scratch are deliberately excluded. The production bridge continues to clip
unlinked proxies and returns before damage, knockback, death, or pierce-state
callbacks execute.

Every currently integrated player weapon now supplies an explicit policy tag:

| Policy | Production query route |
| --- | --- |
| Machinegun | `fire_lead` / `pierce_trace` |
| Chaingun | `fire_lead` / `pierce_trace` |
| Shotgun | `fire_lead` / `pierce_trace` |
| Super Shotgun | `fire_lead` / `pierce_trace` |
| Railgun | `fire_rail` / `pierce_trace` |
| Disruptor convergence | point and expanded bounds queries |
| Plasma Beam | main/water-retrace beam queries |
| Thunderbolt | main, water-retrace, and side-ray queries |

Muzzle convergence traces use the same tag as the ensuing weapon query. The
record describes a collision query, not a client-authored hit claim, and never
contains a live entity pointer.

## Deterministic acceptance matrix

The checked-in matrix is
`tools/networking/scenarios/rewind_player_acceptance_matrix.json`. Its normal
cross-product covers all eight weapon policies at 0, 50, 100, and 200 ms. The
boundary cases cover:

- stale/too-old and future command rejection;
- the 200 ms target-window cap;
- empty history fallback;
- teleport discontinuity flooring;
- death/respawn generation isolation;
- client-slot reuse generation isolation; and
- master-disabled current-world fallback.

`rewind_acceptance_probe` calls the same production
`Worr_RewindPolicyResolveV1`, `Worr_RewindHistoryQueryV1`, frozen-scene, and
observation-journal implementations linked into sgame. The authoritative pose
used as the test oracle is hashed before and after every case. The runner
executes each matrix entry three times, rejects any semantic output mismatch,
and packages the raw child report and its hashes.

The evidence envelope schema is
`worr.networking.acceptance-evidence.v1`. It records source revision and file
hashes, dirty-tree state, platform/machine fingerprint, build/compiler and
binary hashes, manifest identity, workload and impairment profiles, p50/p95/p99
informational subprocess timings, thresholds, gates, privacy declarations, and
explicit limitations. Evidence is written beneath `.tmp/networking/` and is
not a source artifact.

## Reproduction

With a configured Meson build:

```text
meson compile -C builddir-win rewind_acceptance_probe rewind_observation_test sgame_x86_64
meson test -C builddir-win --suite networking --no-rebuild --print-errorlogs
meson compile -C builddir-win networking-rewind-acceptance
```

The direct runner form is:

```text
python tools/networking/run_rewind_acceptance.py \
  --probe-exe builddir-win/rewind_acceptance_probe.exe \
  --matrix tools/networking/scenarios/rewind_player_acceptance_matrix.json \
  --output .tmp/networking/rewind-acceptance/evidence.json \
  --repeat 3 \
  --platform-id windows-x86_64 \
  --build-type debug \
  --compiler-id clang \
  --sgame-module builddir-win/sgame_x86_64.dll
```

The first local run exercised 40 cases over 120 invocations with zero outcome
mismatches, zero deterministic-repeat mismatches, and zero authoritative pose
mutations. The generated envelope intentionally reports
`release_gate_complete: false`.

## Remaining acceptance gap

This is the strongest deterministic seam available without booting an engine
session: it reaches production policy, history selection, frozen scenes, and
the exact observation journal used by sgame. It does **not** invoke
`gi.trace`/`gi.clip`, apply weapon damage, traverse a real network transport,
or measure a sustained multiplayer load. Consequently, FR-10-T14 still needs:

1. a live dedicated-server/client harness that fires each weapon through the
   engine collision bridge and verifies damage/hit outcomes;
2. packet loss, jitter, reordering, and burst-loss scenarios in that live
   harness;
3. sustained memory/CPU/latency measurements with soak duration and player
   population recorded; and
4. comparable evidence from at least one additional machine/platform.

These gaps are machine-readable in the evidence envelope and keep the live
weapon-damage release gate false rather than overstating this slice.
