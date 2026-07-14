# Progressive Networking Foundation (2026-07-11)

Tasks: `FR-10-T01`, `FR-10-T02`, `FR-10-T05`, `FR-10-T07`, `FR-10-T10`,
and `FR-10-T11`.

Strategic project:
`docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`.

Living plan:
`docs-dev/plans/progressive-networking-events-snapshots-roadmap.md`.

Update (2026-07-12): this document remains the implementation record for the
initial foundation. Its provisional mirrored-PMove boundary was subsequently
removed and `FR-10-T02` completed. The authoritative replacement and validation
record is `docs-dev/networking-deterministic-prediction-core-2026-07-12.md`.

Subsequent integration update (2026-07-12): several limitations recorded below
describe this foundation revision, not the current runtime. Negotiated legacy
command/cursor sidebands now carry canonical command identity and the
authoritative server-consumed cursor without changing `q2proto/`; the live
client snapshot shadow attaches that cursor, maintains a stateful canonical
server clock across FPS changes and demo seeks, and feeds parity-qualified
immutable views to external cgame. Cgame replays retained local input from the
exact consumed cursor and fails closed on ambiguous history. Sgame now consumes
an API-v2 tri-state command context and traces one common sealed, immutable
player-bounds rewind scene per command. The current implementation records are
the T06 through T11 documents linked by the living plan. The native WORR packet
envelope, canonical presentation cutover, mover history, fairness/load gates,
and full demo/MVD matrix remain open.

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

At this foundation revision, `q2proto/` and all wire message formats were
unchanged. Existing legacy server/demo parsing remained the compatibility
adapter. Later work kept `q2proto/` unchanged while adding negotiated
engine-owned setting sidebands around the legacy command and snapshot paths.

This is a foundation, not the completed FR-10 system. At this revision,
authoritative typed event sequences, full immutable snapshot payloads, explicit
processed-input and render-time watermarks, prediction state hashing,
deterministic impairment tests, movers, and the extended gameplay compensation
matrix remained open. Typed event/shadow ranges, canonical snapshot storage and
live client projection, authoritative consumed-input identity, prediction
hashing/replay, deterministic impairment tests, and the common rewind scene
have since landed in progressive default-off slices; the remaining promotion
and coverage work is tracked in the living plan.

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
sgame. The interval is an engine/game ABI field, not a wire field. The
foundation first advanced `GAME_API_VERSION` from `2023` to `2024` and
`CGAME_API_VERSION` from `2026` to `2027`. The completed `FR-10-T02` work then
removed the unsafe mirrored PMove export and advanced the APIs again to
`2025`/`2028`. Mixed stale engine/module binaries fail closed at load. Bots
receive the current server frame and remain uncompensated by policy.

### Foundation timing limitation

At this foundation revision, the legacy move packet had one packet-level
snapshot acknowledgement, so backup
and new commands decoded from the same packet share that clock/interval. It is
not an authoritative per-command consumption or exact render-time watermark.
`FR-10-T09` subsequently added explicit command identity, validated sample-time
provenance, and a negotiated server-consumed cursor. Native exact render
watermarks remain open; packet acknowledgement is no longer used as the replay
watermark after canonical cursor authority is established.

## Shared Movement and Prediction Foundation

The foundation initially routed the cgame movement callback into the same
`p_move.cpp` source as sgame and guarded a provisional C/C++ mirrored layout.
That intermediate callback no longer exists. `FR-10-T02` now builds one
strict-FP, position-independent C++20 prediction core and links it whole into
cgame and sgame. Cross-DLL calls use the versioned, pointer-free records in
`inc/shared/prediction_abi.h`, with validated callback thunks and atomic failure.

The completed path retains the two original divergence fixes:

- `CONFIG_Q3_OVERBOUNCE` is read during cgame initialization and on later
  configstring changes;
- rotated brush-model point-contents prediction uses the entity's actual angles
  instead of an uninitialized vector.

Canonical command, state, configuration, collision-transcript, and replay-chain
hashes now have checked deterministic evidence. Prediction telemetry remains
observational; only presentation position error is smoothed, while collision
state accepts authority immediately.

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

The inferred input acknowledgement was explicitly named and documented as
transport telemetry in this foundation. It did not drive canonical
reconciliation. The live client now imports an authoritative consumed-command
cursor and cgame replays only verified canonical successors, with an explicitly
tagged packet-ACK fallback limited to negotiated bootstrap and legacy peers.

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

The foundation's default interpolation estimate was half the observed interval between
validated contiguous snapshots and is zero when no predecessor exists. It
was a render-time approximation. Current live snapshot projection instead
maintains a stateful canonical server clock across FPS changes and carries the
server-consumed command cursor into prediction and rewind context. A native
exact render watermark is still not claimed and remains `FR-10-T09` work.

## Non-mutating Historical Collision Scene

Current-status note: this section records the initial private-proxy scene. It
has since been superseded on the canonical path by the common 512-pose history
and one sealed immutable player-bounds scene per accepted command, with
generation-checked live revalidation and per-ray ignore sets. Rejected or
synthesized-gap canonical contexts cannot fall back to the legacy estimate.

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

The configured maximum rewind defaults to 200 ms, with a hard policy ceiling
of 250 ms:

- existing `g_lag_compensation` remains the compatibility master switch and is
  default-off until the `FR-10-R4` fairness, abuse, and performance gates pass;
- `sg_lag_compensation_max_ms` defaults to `200` and clamps to `0..250`;
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
- At the foundation revision, no legacy message, demo, configstring, snapshot,
  or usercmd wire field changed. Later negotiated setting sidebands preserve the
  legacy payload formats and still do not modify `q2proto/`.
- The internal game and cgame ABIs are now `2025` and `2028` respectively, so
  binaries compiled against the removed mirrored PMove layout are rejected.
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
  `sgame_x86_64.dll`, reported game API `2024` at the original foundation
  revision, connected over loopback, and
  transitioned `cs_primed -> cs_spawned` before exiting `0`;
- final dedicated and client stdout/stderr evidence is under
  `.tmp/networking/dedicated_smoke_final_abi.*.log` and
  `.tmp/networking/client_cgame_smoke_final_abi2.*.log`.

That historical smoke was superseded during the 2026-07-12 T02/T03 closeout.
The current staged modules report API 2025; the safe ordinary `networking`
suite is registered independently of the dangerous in-engine test option and
passed 33/33 across three repetitions. The refreshed staged control and
impaired loopback profiles reached `cs_spawned` with empty stderr and zero queue
overflow. Exact current evidence is recorded in the T02 and T03 implementation
documents.

## Current Required Next Work

1. `FR-10-T04/T06/T07`: implement the native WORR envelope and server peer
   snapshot shadow, accumulate the required live parity/load evidence, and
   promote immutable cgame rendering/event presentation behind rollback gates.
2. `FR-10-T05/T08/T09`: complete authoritative event ordering/prediction
   correlation, predictable weapon/gameplay state, side-effect suppression,
   native exact render watermarks, pacing, batching, and redundancy.
3. `FR-10-T10/T11`: add mover/brush collision history and zero/low/high-latency
   weapon, discontinuity, death/spawn, abuse, and declared 32-client CPU/query/
   allocation acceptance gates before default enablement.
4. `FR-10-T12/T13`: complete explicit projectile, melee, radius, mover,
   deployable, trigger, and coop policies plus MVD/GTV/spectator/native-demo and
   full record/play/seek/relay matrices.
