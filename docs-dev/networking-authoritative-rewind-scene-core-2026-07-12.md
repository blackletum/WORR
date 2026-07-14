# Authoritative rewind history and frozen scene core (2026-07-12)

## Project tasks and scope

This document records the common, allocation-free foundation for
`FR-10-T10` (bounded timestamped full-pose server rewind) and `FR-10-T11`
(authoritative hitscan lag compensation).

The implementation is intentionally below the live sgame integration layer:

- `inc/common/net/rewind.h` defines the pointer-free policy, pose, history,
  frozen-scene, ignore-set, and trace-view contracts;
- `src/common/net/rewind.c` implements their deterministic validation and
  state transitions;
- `tools/networking/rewind_core_test.c` covers policy, history, scene, mover,
  hostile-alias, and repeated trace-session behavior; and
- the C and C++ schema probes pin pointer-free ABI layout.

No live weapon, edict, collision-world, protocol, demo, or `q2proto/` path is
changed by this foundation. It is not sufficient by itself to mark either
project task complete.

### Subsequent live integration status

This foundation has since replaced the private live player-proxy path on the
canonical command route. Sgame records the common 512-pose player history,
resets it at map/client/lifecycle boundaries, and builds one sealed immutable
player-bounds scene per command. Supported hitscan, convergence, beam water
retrace, and thunderbolt side-ray queries reuse that scene. Piercing uses
generation-checked per-ray ignore identities; current edicts are not made
non-solid or relinked. Historical identity is revalidated against current
authority before damage, while damage/effects/radius work remains current.
Canonical rejected and synthesized-gap contexts cannot fall back to the legacy
estimate. `g_lag_compensation` remains default-off. See
`docs-dev/networking-live-canonical-rewind-scene-and-hitscan-2026-07-12.md`.

## Trust and policy boundary

`Worr_RewindPolicyResolveV1` consumes a canonical command, a trusted
server-built snapshot-time view, and an authenticated mapping proof. It derives
one immutable decision containing the command ID, current and source snapshot
IDs, watermark provenance, requested/mapped/applied time, clamp state, legacy
error bound, and explicit accept/reject reason.

The default policy is:

- a 200 ms target window;
- a hard 250 ms rejection ceiling;
- 25 ms of future-render tolerance, clamped back to current server time;
- at most 100 ms of server clock skew;
- at most 50 ms of legacy packet-shared mapping error; and
- a requirement that the authoritative snapshot cursor says the command was
  actually consumed, rather than merely packet-acknowledged.

Progression state rejects command replay, invalid epoch jumps, exhausted
sequences, snapshot regression, mutation of an already observed snapshot ID,
clock abuse, and unmarked map/hard-resync boundaries. Valid command sequence
rollover is accepted only from `{epoch, UINT32_MAX}` to `{epoch + 1, 1}`.
Cross-map/reset transitions require adjacent snapshot and command epochs plus
sequence one.

All policy counters saturate at `UINT64_MAX`. A policy rejection is a successful
evaluation with an explicit reason and committed telemetry; malformed or
overlapping call storage returns `false` and leaves state/output byte-identical.

### Live authentication adapter status

The common policy validates and requires a `worr_rewind_mapping_proof_v1`, but
it does not own the canonical snapshot timeline that creates that proof. A live
legacy adapter now binds the command watermark to the exact server-owned frame
record acknowledged by that client, projects source/current snapshot IDs from
the nonzero server map epoch and simulation ticks, and exports the complete
pointer-free tuple only during that command's game callback. It never treats a
merely well-formed client tuple as authenticated. See
`docs-dev/networking-authenticated-command-context-2026-07-12.md`.

That adapter is not the final canonical snapshot-store lookup. The native path
must materialize the issued source snapshot in the server canonical timeline.
Post-callback consumed-cursor publication is now live in negotiated snapshots;
packet
acknowledgement remains source-frame authentication, not consumed-command
authority.

For `LEGACY_PACKET_SHARED`, the adapter must calculate the actual uncertainty
and corrected mapped time for that individual command from its carrier position
and durations. The raw packet-shared render time remains the requested time for
diagnostics; policy operates on the proof's mapped time. The core does not
assume “two ticks”: that is only defensible for the oldest member of an ordinary
three-command MOVE and is unsafe for a Q2PRO/Q2REPRO batch containing up to 124
commands. A missing/zero proof or a bound above policy fails closed.

Canonical-store lookup remains a `FR-10-T06`/`FR-10-T10` integration
dependency. Native exact render-watermark transport remains `FR-10-T09` work.

## Historical collision pose

Each 160-byte pose is pointer-free and contains:

- stable entity and mover `{index, generation}` identities;
- map epoch, server tick, and microsecond server time;
- lifecycle, linked/damageable state, solidity, and clip flags;
- collision shape plus a map-local immutable brush collision-asset ID;
- world origin, angles, velocity, and bounds; and
- mover-relative origin and angles.

The collision shape is explicit. Bounds use no asset ID. Brush movers require a
nonzero map-local asset ID; map epoch and entity generation prevent accidental
reuse across map/entity lifetimes. This gives the future collision bridge enough
stable input to clip a historical brush transform without reading a mutable live
transform.

Append automatically marks map, time-gap, generation, mover, collision identity,
death, respawn, and teleport boundaries. Teleport detection occurs in
mover-relative space when the same mover remains attached, preventing ordinary
platform motion from being misclassified as a player teleport.

Equal server time is legal only for an explicitly paused sample. Server time
cannot advance while the server tick is unchanged. These rules are enforced by
both append and full history validation.

## Interpolation and discontinuities

History is a caller-owned fixed-capacity ring. It performs no heap allocation.
Queries select exact samples or interpolate continuous values:

- world/mover-relative origin;
- world/mover-relative angles using shortest-arc interpolation; and
- velocity.

Bounds, solidity, clip flags, collision asset, lifecycle, and other discrete
state come from the temporally nearest endpoint, with ties choosing the older
sample. Query interpolation cannot cross a map, generation, mover, collision,
teleport, time-gap, pause, respawn, death, or manual discontinuity. At such a
boundary the result deterministically uses the older discontinuity floor and
reports that reason. Map, generation, lifecycle, history-gap, too-old, and
future misses remain distinct.

Generation is part of every query identity. A reused entity index cannot match
history from its previous generation.

## Frozen scene and repeated traces

One accepted policy decision initializes one `worr_rewind_scene_v1`. The caller
queries rewindable entities at `decision.applied_time_us` and adds linked results
in strictly ascending entity-index order. Ascending capture matches the normal
server entity scan and keeps construction O(candidate count), with no sorting or
steady-state allocation.

Sealing the scene:

1. verifies every pose and pose hash;
2. requires every referenced mover generation to exist in the same scene;
3. rejects mover cycles or chains deeper than `WORR_REWIND_MAX_MOVER_DEPTH`
   (eight);
4. freezes the accepted command/snapshot target; and
5. calculates a fieldwise scene hash independent of pointers and storage
   addresses.

After sealing, the common API rejects candidate changes. A trace view exposes
only const candidate pointers, count, map epoch, target time, scene hash, command
ID, and snapshot ID.

Each ray or pellet owns a separate fixed-capacity sorted ignore set. A piercing
ray adds each already-hit entity identity to its set and rebuilds its view; other
pellets continue using their own ignore sets against the exact same sealed scene.
No piercing step removes solidity, relinks an edict, or changes shared scene
state.

The intended live flow is therefore:

```text
consume command -> authenticate watermark/build mapping proof -> resolve once
  -> query players/movers -> seal one historical scene
  -> trace static world + const historical candidates
       -> per-ray ignore-set updates for pierce
  -> revalidate current entity generation/lifecycle -> apply current damage
```

The historical scene decides collision convergence only. Damage, knockback,
death, effects, spawn protection, team rules, and other mutations remain current
server authority after the hit identity is revalidated.

## Transaction and alias contract

Public mutating operations validate schema, alignment, size multiplication, and
pairwise range overlap before committing. Invalid/overlapping calls preserve
all inputs, envelopes, and caller storage byte-for-byte. Rejected history
samples commit only their documented telemetry and reason, not ring slots or
cursors.

Ring indexing uses widened arithmetic, so `head + count` cannot wrap before the
capacity modulus. A partially filled ring must have head zero; only a full ring
may have a rotated head. Pose alignment gaps are represented by explicit zeroed
fields, including the aggregate tail, so C and C++ pose bytes are fully defined.

Semantic hashes are fieldwise and collapse signed floating-point zero. Telemetry
and process-local storage pointers are intentionally excluded.

## Resource model

The caller chooses hard capacities; the core never grows them.

- One history sample: 160 bytes.
- One frozen scene candidate: 184 bytes.
- One ignore identity: 8 bytes.
- One mapping proof and one resulting decision: 80 bytes each.
- Policy state: 176 bytes per client/policy stream.

For illustration, 64 rewindable players/movers with 32 samples each consume
327,680 bytes of pose storage. A 64-candidate frozen scene consumes 11,776 bytes,
and a 64-entry per-ray ignore set consumes 512 bytes. These are examples, not
approved release budgets; live load tests must set capacities from server tick
rate, hard rewind window, player/mover limits, and the roadmap's p95 frame budget.

History query is linear in retained samples for one entity. Scene construction
is linear in candidates. Seal validation is bounded by candidate count, binary
mover lookup, and the eight-level mover-depth ceiling. Ignore insertion is
linear in the usually small number of pierced identities.

## Verification

The standalone matrix was run with strict C/C++ warnings and strict floating
point settings:

```powershell
clang -std=c17 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror `
  -Iinc -fsyntax-only tools/networking/rewind_core_test.c `
  src/common/net/rewind.c

clang -std=c17 -O2 -ffp-contract=off -fno-fast-math `
  -Wall -Wextra -Wpedantic -Wshadow -Werror -Iinc `
  tools/networking/rewind_core_test.c src/common/net/rewind.c `
  src/common/net/command_abi.c src/common/net/command_canonical.c `
  src/common/net/snapshot_abi.c src/common/net/event_abi.c `
  -o .tmp/rewind-audit/rewind_core_test.exe

.tmp/rewind-audit/rewind_core_test.exe
```

The test was repeated 20 times. The same corpus also passed Clang AddressSanitizer
and UndefinedBehaviorSanitizer. Separate C17 and C++20 schema executables passed,
and Clang's static analyzer reported no findings in the core implementation.

Coverage includes exact/legacy/clamped/rejected policy decisions; authenticated
source-snapshot proofs; large-batch legacy error bounds; command and snapshot
epoch/sequence exhaustion; immutable snapshot identity; canonical snapshot
projection; interpolation and angle wrap; map/generation/teleport/pause/mover/
collision boundaries; mover-local teleport detection; scene order, hashing,
closure and cycle rejection; independent pellet/pierce ignore sets; signed-zero
hashing; ring rotation invariants; and hostile overlap calls.

## Remaining work before task completion

`FR-10-T10` and `FR-10-T11` still require all of the following:

- replace the authenticated legacy frame projection with canonical server
  snapshot-store lookup;
- extend live capture and the sealed scene from player bounds to brush movers,
  mover-relative poses, and stable collision asset IDs;
- finish bounded reason/age/history-miss/load telemetry and operator policy
  documentation;
- add deterministic weapon/fairness, discontinuity, spawn/death, spectator,
  abuse, malformed-input, and mover scenarios; and
- pass CPU, memory, query-count, latency, and release/runtime gates before any
  default enablement.

The landed player scene, per-ray ignore, scene reuse, and current-authority
damage revalidation are prerequisites, not completion claims.
