# Live canonical rewind scene and hitscan integration

Date: 2026-07-12  
Project tasks: `FR-10-T10`, `FR-10-T11`; supporting authority from
`FR-10-T09` and `FR-10-T06`

## Outcome

The live sgame hitscan path now consumes the common bounded rewind history,
policy, frozen-scene, trace-view, and ignore-set APIs. The old sgame-private
pose interpolation implementation has been removed. Historical collision is a
read-only query over copied poses: it does not move, unlink, relink, or change
the solidity of an authoritative game entity.

For a command carrying authenticated canonical authority, the engine exposes a
callback-scoped, pointer-free command/snapshot/mapping record. Sgame resolves
the rewind policy once and seals one immutable scene for that command. Every
pellet and repeated trace reuses the same decision and scene. A policy reject,
history miss, malformed proof, or scene-build failure produces an
uncompensated authoritative trace; it can never fall through to the older
packet-ack time estimate. Scene construction is atomic for fairness: if any
currently eligible live player lacks a compatible historical identity or pose,
the whole rewind is abandoned and the trace is rerun against the current
authoritative scene. A partial scene can therefore never make a newly joined,
newly spawned, or history-starved player non-collidable.

Cache reuse is fail-closed rather than ID-only. A command decision is reused
only when the complete copied command/current-snapshot/mapping-proof authority
is byte-identical. A same-ID authority mutation is rejected immediately and
sticks for that mutated authority instead of receiving stale authority or
turning an earlier rejection into acceptance. A sealed
scene additionally requires the complete immutable policy decision and the
exact sorted roster of currently eligible `{entity index, life generation}`
identities to match. Eligibility or life changes during repeated traces force a
rebuild, which in turn applies the atomic history-miss fallback.

## Fail-closed command scope

The command-context import is version 2 and exposes three states:

- `INACTIVE_LEGACY`: no canonical callback is active, so the validated legacy
  packet-ack estimate may be used;
- `ACTIVE_VALID`: `GetCurrent()` returns the authenticated command context;
  and
- `ACTIVE_REJECTED`: a canonical callback is active but its mapping proof is
  unavailable or rejected, so historical compensation is forbidden.

The engine enters rejected scope when context construction fails and for
server-synthesized gap commands. Synthesized commands do not possess an exact
client-observed render instant and therefore cannot inherit a zero-error
mapping. Context state is cleared before and after callbacks, on map spawn,
and before game-module shutdown so authority cannot leak between clients or
module lifetimes.

## History and frozen-scene behavior

Each client slot owns fixed 512-pose storage initialized through
`Worr_RewindHistoryInitV1()`. End-frame capture records time, tick, entity
generation, origin, angles, velocity, bounds, solidity, clip flags, linkage,
damageability, lifecycle, and teleport discontinuities. The common history
core adds generation, collision-identity, death/respawn, map, clock-gap, and
large-correction discontinuities and refuses incompatible interpolation.

Player pose identity uses a dedicated per-client rewind life generation rather
than the general edict `spawn_count`. A client edict survives ordinary
deathmatch death and respawn, so `spawn_count` alone cannot distinguish the old
and new lives. The rewind generation advances only after `ClientSpawn()` has
selected a valid spawn, while the slot history and cached decision/scene state
are cleared at the same boundary. Pose capture, history queries, per-ray ignore
mapping, and live-entity revalidation all consume that same generation. An old
life can consequently never resolve to a newly respawned player, including
through an already sealed scene.

Life-generation exhaustion never wraps. Reaching `UINT32_MAX` clears the slot,
makes its rewind generation absent, and keeps historical compensation disabled
for that slot for the remainder of the map. The next authenticated map epoch
starts a new namespace. This preserves the canonical no-alias contract even at
the otherwise impractical exhaustion boundary.

Map and client-slot hooks reset history, decision, scene, and policy state.
Early provisional map history can be relabelled exactly once when the first
authenticated server map epoch arrives; an unexpected later epoch transition
discards history instead of aliasing poses across maps.
That one-time canonical pose relabel preserves the independent validated
ack-to-time ring, so adoption by a canonical client does not transiently erase
fallback history for a mixed legacy client.

An accepted command builds at most one sealed player-bounds scene. Before a
historical hit is returned, the bridge revalidates the live slot generation,
connection/lifecycle state, damageability, and ignore policy. Damage,
knockback, death, and all other gameplay mutations remain on the current
authoritative entity after the collision query returns.

Every eligible live player must produce a valid historical query result before
the scene is used. Missing history is not treated as an absent target: canonical
scene construction fails as a unit, while the legacy query path immediately
abandons its partial result, and both paths rerun one uncompensated native trace.
Historical evidence that explicitly says a player was unlinked or
non-damageable at the selected time remains a valid omission; only absent or
invalid evidence triggers the atomic fallback.

## Trace-local piercing

`pierce_args_t` now retains stable pointers only as a per-ray ignore list.
Subsequent traces convert those entries into generation-checked canonical
identities. The previous behavior temporarily set live entities to
`SOLID_NOT` and relinked them between trace iterations; that mutation has been
removed for rail, lead, laser, and other users of the shared piercing helper.

## Policy and diagnostics

The public-policy ceiling is 250 ms and the default target remains 200 ms.
Legacy packet-shared mappings are accepted only within
`sg_lag_compensation_legacy_error_ms`; canonical proofs are subject to the
common clock, future, age, consumed-command, discontinuity, and replay rules.
Diagnostics count canonical accept/reject/reuse, scene build/reuse/reject,
history misses, interpolation/discontinuity choices, clamps, append failures,
and ignore identities.

## Verification

The migrated translation unit compiled with the production Windows Clang
configuration:

```powershell
ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_network_lag_compensation.cpp.obj
```

The spawn-boundary integration also compiled together with the migrated unit:

```powershell
ninja -C builddir-win `
  sgame_x86_64.dll.p/src_game_sgame_network_lag_compensation.cpp.obj `
  sgame_x86_64.dll.p/src_game_sgame_gameplay_g_spawn_points.cpp.obj
```

The common rewind suite independently covers policy abuse, interpolation,
discontinuities, history exhaustion, frozen-scene sealing, trace views, and
ignore sets. The subsequent integration pass linked
`worr_engine_x86_64.dll`, `worr_ded_engine_x86_64.dll`,
`cgame_x86_64.dll`, and `sgame_x86_64.dll`; passed the complete Meson
networking suite `49/49`; and passed a ten-repeat high-risk subset `70/70`
including command-context and rewind-core coverage. The staged headless
runtime smoke also passed after `.install/` was refreshed and validated.

## Remaining scope

This slice freezes player bounds poses. The subsequent dedicated
`fire_rail` acceptance gate now proves one real legacy-ack weapon/damage
fairness case, including invalid-ack uncompensated fallback and post-query
current-authority damage; see
`docs-dev/fr-10-t11-headless-railgun-damage-fairness-gate-2026-07-15.md`.
Brush-model/mover weapon scenarios, remaining weapon-by-weapon fairness cases,
load profiles, projectile/melee/beam/splash policies, and the release
acceptance matrix remain open under `FR-10-T10` through `FR-10-T15`. The
legacy path is retained only for callbacks that are explicitly outside
canonical command scope.
