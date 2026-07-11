# Progressive Networking Foundation (2026-07-11)

Tasks: `FR-10-T01`, `FR-10-T02`, `FR-10-T05`, `FR-10-T07`, `FR-10-T10`,
and `FR-10-T11`.

Strategic project:
`docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`.

Living plan:
`docs-dev/plans/progressive-networking-events-snapshots-roadmap.md`.

## Outcome

WORR now has the first wire-compatible foundation of the progressive network
architecture:

- The engine maps a client-acknowledged network snapshot to the authoritative
  server simulation frame that produced it. Sgame never trusts a client-authored
  rewind timestamp.
- Cgame prediction uses the same C++ PMove implementation as authoritative
  sgame movement, including initial and live Quake 3 overbounce configuration.
- Cgame owns bounded accepted-snapshot metadata and legacy-event journal rings,
  with continuity classification, stable local identities, duplicate
  suppression, and prediction/reconciliation telemetry.
- Sgame records bounded timestamped full player poses and resolves historical
  queries using the sgame clock rather than assuming one simulation tick per
  engine frame.
- Hitscan queries run against a non-mutating historical collision scene made
  from the current static/non-player world, current non-rewindable client
  bodies, and unlinked historical living-player proxies. Live entities are
  never globally rewound or relinked.
- Historical convergence and collision queries are separated from damage:
  health, knockback, death, piercing, effects, and radius damage always execute
  against current authoritative entities.

`q2proto/` and all wire message formats remain unchanged. Existing legacy
server/demo parsing remains the compatibility adapter.

This is a foundation, not the completed FR-10 system. Authoritative typed event
sequences, full immutable snapshot payloads, explicit processed-input and
render-time watermarks, prediction state hashing, deterministic impairment
tests, movers, and the extended gameplay compensation matrix remain open.

## Architecture Decision

The selected design is a hybrid above-wire canonical model.

Useful idTech3 concepts were adopted at the ownership level:

- the engine validates and retains network inputs/snapshots;
- shared simulation owns deterministic movement rules;
- cgame owns snapshot transition, interpolation, prediction,
  reconciliation, and presentation-time event suppression;
- sgame owns authority, clock mapping, history, fairness policy, and damage.

WORR does not copy the Quake 3 wire protocol or its small event/snapshot limits.
The existing Quake II/Rerelease transport feeds the new seams now; a future
negotiated WORR transport can feed the same canonical model outside `q2proto/`.

## Source Audit Findings

The `FR-10-T01` audit found the following high-impact baseline facts:

1. Server snapshots already delta from a client-acknowledged valid frame. The
   new snapshot work must preserve and generalize that behavior, not replace it
   with a previous-frame-only design.
2. Authoritative movement used `src/game/sgame/player/p_move.cpp`, while the
   external cgame entity prediction callback was routed to legacy
   `src/common/pmove.c`. This allowed deterministic divergence even though the
   C++ movement source is linked into both cgame and sgame.
3. `usercmd.server_frame` was not populated by the engine, so the previous
   origin-only lag compensation had no trustworthy rewind clock.
4. Legacy entity events are one-byte, snapshot-state events without a durable
   sequence, typed payload, delivery class, or prediction correlation.
5. The current cgame extension still exposes mutable engine-owned state. The
   bounded value-only timeline is the migration seam, not the final immutable
   module API.
6. Quake 3 supplies useful snapshot/input ownership and command-watermark
   patterns, but it does not supply server-side rewind and is not a direct
   solution for WORR lag compensation.

The accepted architecture, budgets, compatibility matrix, security model,
telemetry catalog, rollout stages, and rollback rules are in the living plan.
The previous event proposal is retained as history and marked superseded.

## Server-owned Snapshot Clock Mapping

### Data flow

`client_frame_t` now stores the identifiers/timing that were previously
conflated:

- `number`: the per-client network snapshot/frame number used by the legacy
  transport and delta history;
- `server_frame`: the authoritative engine simulation frame captured when that
  snapshot was built;
- `server_frame_delta`: the interval in engine frames from the previous built
  snapshot only when the per-client network frame sequence is contiguous.
  Zero is a no-interpolation sentinel for the first snapshot and for gaps
  created by rate suppression or pending fragments.

When a move packet acknowledges a snapshot, `SV_SetLastFrame`:

1. rejects future and duplicate acknowledgements using existing rules;
2. verifies that the referenced slot still contains the exact acknowledged
   network frame;
3. copies its server-owned frame and sent-snapshot interval into the client's
   validated acknowledgement watermark;
4. clears the rewind watermark when the acknowledgement is invalid, missing
   from the validated ring, or explicitly non-delta.

Immediately before `ClientThink`, the engine overwrites
`usercmd_t.server_frame` and the internal `server_frame_delta` with those
validated values. The decoded client command cannot author either value used by
sgame. The interval is an engine/game ABI field, not a wire field. Because this
tail field changes both public module layouts, `GAME_API_VERSION` was advanced
from `2023` to `2024` and `CGAME_API_VERSION` from `2026` to `2027`; mixed stale
engine/module binaries now fail closed at load. Bots receive the current server
frame and remain uncompensated by policy.

### Current timing limitation

The legacy move packet has one packet-level snapshot acknowledgement, so backup
and new commands decoded from the same packet share that clock/interval. It is
not an authoritative per-command consumption or exact render-time watermark.
`FR-10-T09` must add explicit command sequences, processed-input
acknowledgements, and sample/render time.

## Shared Movement and Prediction Foundation

The cgame entity movement callback now calls the loaded cgame module's C++
`Pmove` export instead of legacy engine `Pmove`. This makes predicted and
authoritative movement execute the same `p_move.cpp` implementation.

Two associated divergence bugs were corrected:

- `CONFIG_Q3_OVERBOUNCE` is read during cgame initialization and on later
  configstring changes;
- rotated brush-model point-contents prediction uses the entity's actual angles
  instead of an uninitialized vector.

The current C `pmove_t` and C++ `PMove` mirrors have matching layouts on the
supported 32-bit and 64-bit Windows ABIs. A shared versioned layout description
and assertions now fail the engine or cgame build if size, alignment, or key
offsets drift. The callback types are still mirrored rather than one exactly
typed canonical POD interface; replacing them with explicit thunks/adapters is
part of `FR-10-T02` and is required before that task completes.

Prediction telemetry remains observational. It does not alter movement or
correction decisions in this slice.

## Cgame Snapshot Timeline

`cg_snapshot_timeline.cpp` owns fixed-capacity, allocation-free state:

- 128 accepted snapshot metadata records;
- 512 presented event records;
- a 1024-slot collision-safe event lookup table;
- prediction replay/correction counters.

Each accepted snapshot records:

- epoch;
- network snapshot ID and delta baseline ID;
- server and receive time;
- frame flags and entity count;
- the input command inferred from the acknowledged transport packet;
- continuity classification.

Continuity is classified as initial, contiguous, sequence gap, baseline jump,
full snapshot, duplicate, or rewind. A backwards snapshot starts a new epoch;
an equal snapshot preserves the epoch so duplicate events remain suppressible.

The inferred input acknowledgement is explicitly named and documented as
transport telemetry. It must not drive future canonical reconciliation until
the server reports an authoritative consumed-input sequence.

## Event Journal Foundation

Legacy entity events retain their existing wire representation and eligibility
checks. Immediately before presentation, cgame derives this local identity:

```text
(epoch, accepted snapshot id, entity id, raw legacy event)
```

The bounded journal reserves the identity on first presentation and suppresses
duplicates. Full tuple comparison makes hash collisions safe. Snapshot rewind
epochs and event serial wrap are handled without heap allocation.

This prevents repeated presentation on duplicate accepted snapshots, but it is
not the final event system. `FR-10-T05` still requires typed payloads, multiple
ordered events per source/tick, authoritative monotonic IDs, delivery classes,
retention/acknowledgement, and predictable-event correlation.

Developer telemetry is opt-in and rate-limited to at most one aggregate line
per second:

- `cg_net_debug 1` reports snapshot continuity and prediction replay/correction
  state;
- `cg_event_debug 1` reports accepted/duplicate event counts and the last local
  identity.

## Full-pose History and Clock Resolution

Sgame owns 512 fixed-capacity samples per client slot. Each sample contains:

- sgame time and engine server frame;
- entity spawn generation;
- origin, angles, mins/maxs, and velocity;
- solidity, clip mask, server collision flags, and linked state;
- damageable/dead lifecycle state;
- discontinuity marker.

History is cleared across map/time reversal, slot generation change, or
damageable/death lifecycle change. Teleports and corrections over 256 units are
recorded as discontinuities, and interpolation never crosses them. Origin,
angles, and velocity interpolate; collision bounds/stance and discrete flags use
the nearest compatible sample.

Multiple `g_frames_per_frame` simulation ticks may share one engine server
frame. History therefore uses monotonic `level.time` for sample ordering. To map
an acknowledged snapshot, sgame finds the newest shooter sample captured under
that exact validated server frame—the state that the outgoing snapshot
observed—and maps the validated prior-sent snapshot frame into the same history.
Reduced client snapshot rates retain their actual multi-engine-frame interval.
Rate-suppressed or fragment-stalled gaps carry the zero no-interpolation
sentinel, so they never manufacture a half-gap rewind. Exact history-time
subtraction also covers `g_frames_per_frame` without assuming engine-frame age
equals sgame time.

The default interpolation estimate is half the observed interval between
validated contiguous snapshots and is zero when no predecessor exists. It
remains a render-time approximation and is replaced by an explicit per-command
render timestamp in `FR-10-T09`.

## Non-mutating Historical Collision Scene

The initial relink/restore prototype was rejected during review because damage
inside a rewind could overwrite knockback/death state and even a pure relink can
perturb linkage ordering. The shipped foundation does not mutate live player
entities.

For each eligible historical trace, sgame:

1. clips against the static world;
2. broad-phase queries and individually clips current non-player solids plus
   current dead/non-damageable client bodies, preserving owner, dead-monster,
   and projectile filters;
3. omits only eligible living current player links;
4. resolves each current live/damageable player to a compatible historical
   sample;
5. clips an unlinked reusable proxy carrying that player's historical origin,
   orientation, bounds, solidity, mask, and server collision flags;
6. returns the actual authoritative player pointer in the winning trace.

No proxy is inserted into the server area tree. If a target has no compatible
pose/life at the requested time, it is excluded rather than substituting a newly
spawned current life for an older command. If the shooter's validated clock
cannot be resolved, the whole query falls back to current authoritative tracing.
A currently dead or non-damageable player is never resurrected from history;
its current body still participates when the native content mask includes it.

The default maximum rewind is 200 ms, with a hard policy ceiling of 500 ms:

- existing `g_lag_compensation` remains the compatibility master switch;
- `sg_lag_compensation_max_ms` defaults to `200` and clamps to `0..500`;
- `sg_lag_compensation_interp_ms` defaults to `-1` for the interval-derived
  estimate, or accepts an explicit bounded millisecond bias;
- `sg_lag_compensation_debug` enables one aggregate diagnostic line per second.

Invalid/future clocks fail to current authoritative tracing. Excessively old
valid requests are capped and counted. Debug counters include requests,
historical queries, proxy targets, interpolations, discontinuities, caps,
clock rejects, and history misses.

## Hitscan Integration

Historical muzzle convergence and collision queries are active for:

- machinegun;
- chaingun;
- shotgun;
- super shotgun;
- railgun;
- disruptor target acquisition;
- plasma beam;
- thunderbolt.

Projectile, melee, and utility `P_ProjectSource` callers retain live collision
unless their later collision policy is explicitly historical. Bullet/pellet and
rail piercing traces, beam water retraces, thunderbolt main/side rays, and the
disruptor point/expanded fallback use the new query API.

Thunderbolt resolves all three historical rays before applying any damage, so a
main-ray kill cannot alter the collision scene used by side rays. All weapon
damage/effects run after the query returns, against current state.

Railgun instagib splash remains a current-world radius query centered on the
historical direct-impact point. Projectile fast-forward, melee, radius/mover
semantics, continuous interactions, and coop policies are deliberately tracked
under `FR-10-T12` rather than inferred from hitscan behavior.

## Compatibility and Security

- No file under `q2proto/` changed.
- No legacy message, demo, configstring, snapshot, or usercmd wire field changed.
- The internal game and cgame ABIs advanced to `2024` and `2027` respectively,
  so binaries compiled against the former command/PMove layout are rejected.
- Legacy snapshot delta selection remains intact.
- The server, not the client, maps acknowledgements to simulation authority.
- Future or stale-unverifiable acknowledgement mappings fail closed.
- Rewind time is bounded by server policy and target lifecycle/generation.
- Fixed-capacity histories/journals avoid attacker-controlled hot-path
  allocation.
- The existing lag-compensation switch and server compatibility path remain
  available for rollback.

No `docs-user/` change is included: the new controls are developer/server
foundation controls and the feature has not passed the release/operator gates
required by `FR-10-T15`.

## Validation

Completed during implementation:

```text
meson compile -C builddir-win sgame_x86_64 -j 4
meson compile -C builddir-win cgame_x86_64 worr_engine_x86_64 \
  worr_ded_engine_x86_64 -j 4
meson compile -C builddir-win -j 4
python tools/refresh_install.py --build-dir builddir-win --install-dir \
  .install --base-game basew --platform-id windows-x86_64
git diff --check
```

The targeted sgame, cgame, client-engine, and dedicated-engine targets compile
and link successfully. The final ABI-forced workspace rebuild completed all 400
steps. The refresh repackaged 280 assets, validated 31 botfile and 186 RmlUi
payload entries, and passed the staged Windows payload audit.

Staged runtime acceptance also passed:

- `worr_ded_x86_64.exe +set game basew +map mm-rage +wait 10 +quit`
  loaded the staged `sgame_x86_64.dll`, initialized `mm-rage`, and exited `0`;
- the staged OpenGL client loaded both `cgame_x86_64.dll` and
  `sgame_x86_64.dll`, reported game API `2024`, connected over loopback, and
  transitioned `cs_primed -> cs_spawned` before exiting `0`;
- final dedicated and client stdout/stderr evidence is under
  `.tmp/networking/dedicated_smoke_final_abi.*.log` and
  `.tmp/networking/client_cgame_smoke_final_abi2.*.log`.

The build currently reports no registered Meson tests. Enabling the existing
top-level dangerous in-engine test option is not an acceptable substitute.
`FR-10-T03` must add deterministic, non-interactive latency/loss/jitter/reorder
and weapon-fairness tests before `FR-10-T10` or `FR-10-T11` can be completed.

## Required Next Work

1. `FR-10-T03`: deterministic network impairment and state/event/weapon
   regression harness with machine-readable `.tmp/networking/` evidence.
2. `FR-10-T02`: canonical exactly typed PMove ABI, deterministic predicted-state
   schema, state hashing, and client/server replay parity.
3. `FR-10-T09`: explicit command sequence, consumed-input acknowledgement,
   sample time, render time, pacing, batching, and redundancy.
4. `FR-10-T05/T06/T07`: authoritative typed events and immutable canonical
   snapshot/input/event range APIs that remove cgame's mutable engine pointers.
5. `FR-10-T11`: zero/low/high-latency weapon scenarios, discontinuity/death/
   spawn/mover fairness cases, security abuse cases, and declared 32-client
   shotgun/chaingun p95/p99 CPU/query/allocation load gates.
6. `FR-10-T12`: explicit projectile, melee, radius, mover, deployable, trigger,
   and cooperative interaction policies.
