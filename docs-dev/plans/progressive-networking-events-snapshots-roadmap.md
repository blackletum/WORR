# Progressive Networking, Events, Snapshots, Prediction, and Lag Compensation Roadmap

Date: 2026-07-11

Status: Living roadmap. The N0 architecture contract is accepted and its first
wire-compatible foundation is implemented. `FR-10-T01` is complete; the
remaining feature tasks are intentionally progressive and still open.

Primary tasks: `FR-10-T01`, `FR-10-T02`, `FR-10-T03`, `FR-10-T04`,
`FR-10-T05`, `FR-10-T06`, `FR-10-T07`, `FR-10-T08`, `FR-10-T09`,
`FR-10-T10`, `FR-10-T11`, `FR-10-T12`, `FR-10-T13`, `FR-10-T14`, and
`FR-10-T15`.

Strategic parent:
`docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`.

Historical predecessor:
`docs-dev/proposals/event-system-migration.md`.

## Purpose

This is the go-forward engineering plan for a progressive WORR networking
architecture that supports deterministic client-side prediction, resilient
snapshot delivery, typed sequenced events, and extensive but fair server-side
lag compensation.

The intended result is not a direct Quake 3 port and not a wholesale protocol
rewrite. WORR will use a hybrid design:

- Preserve the proven Quake II and Quake II Rerelease compatibility paths.
- Adopt the useful idTech3 separation between shared simulation, snapshots,
  commands, events, and client presentation.
- Introduce a modern canonical state model above the wire protocol layer.
- Progressively replace cgame's direct access to mutable engine state with
  validated, immutable snapshot, input, and event ranges.
- Leave room for a future WORR-native transport without making it a prerequisite
  for the prediction, event, timeline, or lag-compensation improvements.

This document records intended work and acceptance gates. The bounded cgame
timeline, event-deduplication seam, shared-PMove route, validated snapshot clock
mapping, full-pose history, and non-mutating player historical trace scene now
exist at foundation scope. They do not imply that the canonical wire model,
typed authoritative events, complete prediction surface, impairment gates, or
WORR transport are complete.

## Mandatory Constraints

- `q2proto/` is read-only for this initiative unless a later explicit request
  authorizes changes there.
- A future WORR-native wire transport must live outside `q2proto/` and negotiate
  independently.
- WORR clients must retain compatibility with legacy Quake II servers and legacy
  demos throughout the migration.
- Existing server protocol paths must remain available for legacy clients where
  they are supported today.
- Temporary reports, packet captures, fuzz corpora, and benchmark evidence belong
  under `.tmp/networking/`.
- Every build that changes binaries or packaged assets must end by refreshing
  `.install/`.
- New public controls must follow subsystem ownership: `net_` for shared network
  behavior, `cl_` for client-engine policy, `sv_` for server-engine policy,
  `cg_` for cgame presentation/prediction controls, and `sg_` for sgame lag-
  compensation or gameplay fairness controls. New `g_` names are reserved for
  legacy compatibility aliases.

## Corrected Current Baseline

The older event migration proposal describes snapshot deltas as if WORR always
deltas from the immediately preceding server frame. That is stale. The current
engine already selects snapshot delta bases from frames acknowledged by each
client and falls back to a non-delta frame when the requested base is invalid or
too old. This roadmap does not propose reimplementing that existing behavior.

The work still needed is to put a validated canonical snapshot model above the
wire decoder, make its lifetime and ownership explicit, expose it safely to
cgame, add deterministic testing and telemetry, and allow a future transport to
feed the same model.

Other established foundations include:

- Movement prediction and prediction-error handling have already migrated into
  `src/game/cgame/cg_predict.cpp` behind the cgame entity API.
- Entity interpolation, effects, and temp-entity handling are already primarily
  cgame responsibilities, although `FR-10-T01` must verify the remaining live
  engine fallbacks and direct-pointer dependencies against current source.
- `src/game/bgame/` is compiled into cgame and sgame and is the correct home for
  deterministic shared simulation rules.
- Legacy protocol decoding and encoding are centralized through the existing
  engine and `q2proto` integration.

The following are not established and must not be assumed complete:

- A versioned canonical snapshot/input/event API with immutable ranges.
- A typed multi-event journal with prediction-aware identifiers and replay
  suppression.
- General deterministic replay of all locally controlled gameplay state.
- Bounded timestamped full-pose server rewind and weapon integration.
- A WORR-native dual-stack transport.
- Automated latency, loss, jitter, reorder, duplication, or malicious-input
  regression gates.

## Architecture Decision

### Hybrid Above-Wire Canonical Model

The stable architectural seam is above the wire protocol, not inside cgame and
not inside `q2proto`.

```text
legacy Q2/Q2R wire                 future WORR wire
        |                                |
 existing q2proto adapter        separate WORR adapter
        |                                |
        +------ engine validation -------+
                       |
           canonical immutable model
        snapshots | inputs | events | time
                       |
        +--------------+---------------+
        |                              |
 deterministic bgame              engine/server
 simulation rules                 history/QoS
        |
 cgame transition, interpolation,
 prediction, reconciliation, and event playback
```

Both wire families must decode into the same canonical model. Cgame must not
branch on protocol field layouts. Sgame and shared simulation must not write
wire messages directly for new functionality.

### Ownership Boundaries

| Layer | Owner | Responsibilities | Must not own |
|---|---|---|---|
| Wire adapters | Engine protocol layer | Decode/encode negotiated wire formats and normalize protocol-specific representations | Prediction, presentation, or weapon fairness policy |
| Validation and canonicalization | Engine common/client/server | Bounds, counts, sequence windows, timestamps, entity identity, immutable storage, lifetime and generation checks | Cgame effects or sgame damage decisions |
| Shared deterministic simulation | `bgame` | Command interpretation, deterministic movement/state transitions, shared event derivation rules and state comparison | Transport negotiation or rendering |
| Server authority | Server engine plus `sgame` | Command acceptance, authoritative simulation, pose history, rewind queries, damage validation and fairness policy | Client visual smoothing |
| Client game | `cgame` | Snapshot transitions, time interpolation, bounded extrapolation, local prediction/replay, reconciliation, event matching and playback | Raw packet parsing or mutable server storage |
| Presentation | Cgame/render/audio/UI | Visual smoothing, effects, sound and diagnostics derived from canonical state/events | Authority or hit acceptance |

### Immutable Module API

The cgame bridge will progressively replace direct pointers to mutable engine
containers with immutable, generation-checked views. Final names and layouts are
an output of `FR-10-T01`; the required semantic contracts are:

- **Snapshot view:** server tick/time, acknowledged input sequence, validity and
  discontinuity flags, player state, sorted entity-state range, area/PVS data,
  and the event range associated with the transition.
- **Snapshot pair/timeline view:** explicit previous/current generations and
  interpolation fraction inputs. Missing or invalid prior state is represented
  explicitly rather than through a null pointer into engine storage.
- **Input range:** ordered immutable user commands with sequence numbers,
  simulation duration, client sample time, and acknowledgement state.
- **Event range:** ordered typed events with stable identity, source tick,
  subject, parameters, delivery class, and prediction-matching metadata.
- **Lifetime:** a view is valid only for the documented callback or acquired
  generation. Cgame may copy durable state but may not retain raw engine pointers
  across generation changes.
- **Validation:** counts, indexes, ranges, sequence arithmetic, timestamps, and
  all protocol-derived values are validated before a view becomes observable to
  a module.

The migration is progressive. Existing bridge fields remain until their
consumers have parity coverage; removal is a later task gate, not a first-slice
goal.

### Time and Sequence Model

- Internal canonical time uses monotonic server ticks plus a sufficiently wide
  monotonic duration representation. Wire wraparound is normalized at the
  adapter boundary.
- Input, snapshot, and event sequences use explicit wrap-safe comparison
  helpers. Raw signed subtraction is not an accepted ordering test.
- The client maintains a filtered server-time estimate separately from render
  time and local input sample time.
- Teleports, respawns, map changes, large mover discontinuities, and authoritative
  corrections carry explicit discontinuity markers. They never interpolate or
  rewind through an invalid state boundary.

### Typed Sequenced Events

Events are data, not transient writes into presentation code. The canonical
event model must distinguish at least:

- **Predictable events:** derivable from local deterministic input and matched
  against later authority without replaying audiovisual side effects.
- **Authoritative transient events:** snapshot-associated and sequenced for
  at-most-once client playback.
- **Durable/reliable control events:** important state transitions that require
  acknowledgement or representation in durable replicated state.
- **Cosmetic events:** explicitly eligible for coalescing or loss under a defined
  budget; they must never be silently confused with gameplay authority.

Every event needs a stable type, source tick, source/subject identity, sequence
or unique key, bounded typed payload, and declared delivery/prediction class.
Cgame owns matching, deduplication, transition dispatch, and presentation. Wire
adapters only serialize or normalize it.

### Snapshot Model

The canonical snapshot is a complete logical view even when the wire message is
a delta. The engine owns reconstruction and validation before cgame sees it.

Required properties include:

- Client-acknowledged delta bases remain the normal legacy behavior.
- Invalid, stale, missing, or overwritten bases trigger a safe full/key frame;
  they never expose a partially reconstructed snapshot.
- Entity creation, update, removal, visibility changes, and identity reuse have
  explicit semantics.
- Snapshot history is bounded by count, age, entity storage, and per-client rate.
- Oversized snapshots use application-level prioritization/fragment policy and
  do not rely on IP fragmentation for correctness.
- Cgame receives immutable entity ranges and explicit transition metadata rather
  than the engine's mutable packet-entity ring.

### Prediction and Reconciliation

Full client-side prediction means replaying all locally controlled state that is
both gameplay-relevant and deterministic, not merely extrapolating an origin.
The target includes movement, view state, stance, predictable weapon state,
selected predictable interactions, and their event identities where shared
simulation rules exist.

The client:

1. Starts from the last authoritative state and acknowledged input sequence.
2. Replays unacknowledged immutable input commands through shared `bgame` rules.
3. Matches predictable events by identity instead of replaying their effects.
4. Classifies reconciliation differences by cause and magnitude.
5. Applies authority immediately to collision/gameplay state while optionally
   smoothing only the rendered presentation offset.
6. Hard-resynchronizes when command history, map generation, entity identity, or
   deterministic invariants are no longer valid.

### Bounded Full-Pose Server Rewind

Lag compensation is an authoritative historical query, not client-authorized
damage and not a general rollback of the live world.

The history records timestamped full poses needed for a fair collision query:

- Origin, orientation, bounds, stance, solidity and relevant collision flags.
- Ground and mover attachment plus the mover transform needed to reconstruct the
  player's pose.
- Teleport, respawn, death, map-generation and other discontinuity markers.
- A stable entity identity/generation so a reused slot cannot be rewound as the
  wrong actor.

The server maps a validated command timestamp into the bounded history window,
interpolates only between compatible samples, clamps or rejects invalid/future/
excessively old requests, performs the historical trace, restores no mutable
live state, and applies damage only through current authoritative sgame rules.

Hitscan is the first integration target. Projectile, melee, radius damage,
movers, deployables, triggers, and cooperative interactions follow only after
their fairness semantics and validation scenarios are explicit.

## Current First Implementation Slice

The completed first slice covers `FR-10-T01` architecture and narrow foundations
under `FR-10-T02`, `FR-10-T05`, `FR-10-T07`, `FR-10-T10`, and `FR-10-T11`.

This slice is intentionally constrained:

- Inventoried current event, snapshot, prediction, command, and module-pointer
  ownership and recorded the accepted contracts (`FR-10-T01`).
- Routed cgame prediction through the same C++ PMove implementation as sgame,
  with cross-DLL layout guards and the remaining typed-ABI work tracked under
  `FR-10-T02`.
- Added bounded snapshot metadata and event journal seams without changing the
  live wire format (`FR-10-T05`/`FR-10-T07` foundations).
- Added server-validated snapshot-to-simulation clock mapping, timestamped
  full-pose history, discontinuity handling, and a non-mutating historical
  player collision scene (`FR-10-T10` foundation).
- Routed supported hitscan convergence and collision queries through the
  historical scene while applying damage only to current authoritative state
  (`FR-10-T11` foundation).

It does **not** authorize edits under `q2proto/`, a new wire protocol, removal of
legacy parsing, or broad direct-pointer removal. It preserves the existing
`g_lag_compensation` compatibility switch/default while adding a bounded 200 ms
policy cap. Completing foundation code does not complete the five open parent
tasks until their full Definitions of Done are met.

## Delivery Phases

| Phase | Tasks | Outcome | Exit gate |
|---|---|---|---|
| N0: Baseline and safe foundations | `FR-10-T01`, foundations of `FR-10-T02`, `FR-10-T05`, `FR-10-T07`, `FR-10-T10`, `FR-10-T11` | Accepted architecture, inventories, wire-compatible APIs and historical query foundation | Existing server/demo behavior passes; no wire change; foundation logs and evidence exist |
| N1: Deterministic model and test harness | `FR-10-T02`, `FR-10-T03`, `FR-10-T05` | Shared deterministic state/event rules and repeatable impairment tests | State/event replay is deterministic and event loss/duplication cases are covered |
| N2: Canonical snapshots and cgame timeline | `FR-10-T06`, `FR-10-T07` | Validated immutable snapshots, transitions, interpolation and event playback | Packet loss/base invalidation/visibility/discontinuity tests pass on legacy transport |
| N3: Full local prediction and input delivery | `FR-10-T08`, `FR-10-T09` | Command replay, reconciliation, predictable-event matching and adaptive input delivery | Bounded corrections and command recovery pass the impairment matrix |
| N4: Extensive authoritative lag compensation | `FR-10-T10`, `FR-10-T11`, `FR-10-T12` | Full-pose rewind, hitscan then extended gameplay coverage with fairness controls | Weapon-specific deterministic scenarios, abuse cases and performance budgets pass |
| N5: Dual-stack ecosystem and release hardening | `FR-10-T04`, `FR-10-T13`, `FR-10-T14`, `FR-10-T15` | Optional WORR transport, demos/spectators, telemetry, staged rollout and operator documentation | Compatibility matrix, soak/load, rollback and staged release gates pass |

The phases overlap only where interfaces are stable enough to keep work
independent. A future transport is deliberately late: the canonical model,
events, cgame timeline, prediction, and rewind must prove value over the existing
legacy transport first.

## Task Register and Definitions of Done

### `FR-10-T01` Current-state audit and architecture contract

- Area: `inc/client`, `inc/common`, `inc/server`, `src/client`, `src/server`,
  `src/game/bgame`, `src/game/cgame`, `src/game/sgame`.
- Priority: P0.
- Dependencies: none.
- Current state: Done on 2026-07-11.
- Evidence: `docs-dev/progressive-networking-foundation-2026-07-11.md`.
- Definition of Done:
  - Live data flows and mutable-pointer dependencies are inventoried from source.
  - The above-wire canonical model, ownership matrix, time/sequence rules,
    compatibility promises, budgets, security boundary, rollout gates, and
    rollback conditions are accepted and documented.
  - Stale statements in predecessor documents are identified without rewriting
    history as if the new system already exists.

### `FR-10-T02` Deterministic shared `bgame` simulation and state schema

- Area: `src/game/bgame`, cgame and sgame integration.
- Priority: P0.
- Dependencies: `FR-10-T01`.
- Current state: In Progress at shared-movement/ABI-guard foundation scope.
- Progress: cgame prediction now calls the same C++ PMove implementation linked
  into sgame; initial configuration parity and layout drift guards are present.
  The changed public layout advances the game/cgame module APIs to `2024`/`2027`
  so mixed stale binaries fail closed. A canonical exactly typed PMove ABI,
  state hashing, and replay parity harness remain required.
- Definition of Done:
  - Shared state and command rules use deterministic time and explicit inputs.
  - Client and server replay produce matching state hashes for the supported
    prediction surface across sequence wrap and correction cases.
  - Nondeterministic presentation, allocation, wall-clock, and renderer data are
    excluded from the simulation contract.

### `FR-10-T03` Deterministic impairment, fault-injection, and baseline harness

- Area: `tools/networking`, engine test hooks, `.tmp/networking` evidence.
- Priority: P0.
- Dependencies: `FR-10-T01`.
- Current state: Backlog.
- Definition of Done:
  - Tests can deterministically inject latency, jitter, loss, burst loss,
    reordering, duplication, throttling, stale acknowledgements, and sequence
    wrap.
  - Baseline artifacts record legacy behavior and future runs are repeatable.
  - The harness has unit tests and a non-interactive CI entrypoint.

### `FR-10-T04` Negotiated WORR transport envelope and legacy adapter

- Area: engine common/client/server networking outside `q2proto/`.
- Priority: P0.
- Dependencies: `FR-10-T01`, `FR-10-T03`.
- Current state: Backlog; not part of the first wire-compatible slice.
- Definition of Done:
  - Capability negotiation cannot reinterpret a legacy stream as WORR traffic.
  - A separate WORR adapter feeds the same canonical model as legacy adapters.
  - Negotiation downgrade, unknown versions, malformed messages, and reconnect
    behavior are tested.
  - `q2proto/` is unchanged and legacy server/demo paths remain operational.

### `FR-10-T05` Typed sequenced event journal

- Area: shared event schema, engine canonicalization, `bgame`, cgame and sgame.
- Priority: P0.
- Dependencies: `FR-10-T02`, `FR-10-T04` for full completion; foundation work
  may precede both without changing the wire.
- Current state: In Progress at foundation scope only.
- Progress: a 512-entry cgame-owned journal assigns epoch/snapshot/entity/raw
  event identities and suppresses duplicate legacy presentation. Typed payloads,
  authoritative sequence IDs, delivery classes, and acknowledgement remain.
- Definition of Done:
  - Event types, payloads, source time, identity, ordering, delivery class and
    prediction class are explicit and bounds-checked.
  - Multiple events per source/tick are retained within declared capacity.
  - Loss, replay, duplicate, late arrival, predicted matching and sequence-wrap
    tests prove at-most-once presentation where required.
  - Legacy temp/entity events map into the canonical journal without breaking
    legacy traffic or demos.

### `FR-10-T06` Acknowledged-baseline canonical snapshot system

- Area: server snapshot construction, client reconstruction, canonical storage.
- Priority: P0.
- Dependencies: `FR-10-T02`, `FR-10-T04` for the final dual-stack form.
- Current state: Backlog.
- Definition of Done:
  - Existing acknowledged-base behavior is retained and tested rather than
    redundantly reimplemented.
  - Reconstructed snapshot views are complete, immutable and generation-safe.
  - Invalid base, keyframe, entity add/remove/reuse, visibility, overflow,
    fragmentation and rate-budget cases pass deterministic tests.
  - History memory and encode/decode work remain within the declared budgets.

### `FR-10-T07` Cgame snapshot timeline, interpolation, and event playback

- Area: cgame entity API, `cg_entities`, `cg_predict`, effects and temp events.
- Priority: P0.
- Dependencies: `FR-10-T05`, `FR-10-T06`; aligned with `DV-04-T02` ownership
  cleanup.
- Current state: In Progress at immutable-view/timeline foundation scope only.
- Progress: cgame retains 128 immutable accepted-snapshot metadata records,
  classifies continuity/discontinuity, records inferred transport input acks,
  and exposes bounded debug telemetry without changing rendering behavior.
- Definition of Done:
  - Cgame consumes immutable snapshot and event ranges without retaining mutable
    engine pointers across generations.
  - Remote entities interpolate on an explicit timeline and use bounded,
    policy-driven extrapolation only when valid.
  - Discontinuities, missing snapshots, pauses, rate changes and demo playback do
    not blend invalid states.
  - Event dispatch is ordered, deduplicated and separated from raw parsing.

### `FR-10-T08` Full client prediction and reconciliation

- Area: `bgame`, `cg_predict`, cgame player/weapon state and client command history.
- Priority: P0.
- Dependencies: `FR-10-T02`, `FR-10-T05`, `FR-10-T07`.
- Current state: Backlog.
- Definition of Done:
  - All declared predictable local state replays from the last acknowledged
    authority through unacknowledged input.
  - Predictable audiovisual events match authority without duplicate playback.
  - Corrections are classified and instrumented; collision state is immediately
    authoritative while visual smoothing remains presentation-only.
  - History overflow and invariant failure trigger a safe hard resync.

### `FR-10-T09` Adaptive input delivery and command acknowledgement

- Area: client command production, server command intake, network pacing.
- Priority: P1.
- Dependencies: `FR-10-T04`, `FR-10-T06`, `FR-10-T08`.
- Current state: Backlog.
- Definition of Done:
  - Commands have validated order, duration, sample time and acknowledgement.
  - Batching/redundancy recovers bounded loss without double simulation.
  - Input sampling, simulation, rendering and packet send cadence are explicitly
    decoupled.
  - Flood, future-time, stale, duplicate, malformed-duration and sequence-wrap
    cases are rejected or clamped with telemetry.

### `FR-10-T10` Bounded timestamped full-pose server rewind

- Area: server history, sgame collision query bridge, time synchronization.
- Priority: P0.
- Dependencies: `FR-10-T02`, `FR-10-T06`, `FR-10-T09` for full completion.
- Current state: In Progress at history/query foundation scope only.
- Progress: server-validated acknowledgements map to authoritative simulation
  frames and validated contiguous-snapshot intervals; first/suppressed gaps use
  an explicit no-interpolation sentinel. Sgame retains timestamped full player
  poses and clips historical player proxies without mutating/relinking the live
  world. Mover-relative pose, explicit per-command render time, load gates, and
  deterministic tests remain.
- Definition of Done:
  - Bounded pose history includes collision-relevant player and mover state,
    stable identity and discontinuity markers.
  - Timestamp mapping, interpolation, clamp/reject reasons and history misses are
    deterministic and observable.
  - Queries do not mutate or restore the live world and cannot cross invalid
    identity, teleport, respawn or map boundaries.
  - Memory, query count and server-frame CPU budgets pass load tests.

### `FR-10-T11` Authoritative hitscan lag compensation

- Area: sgame weapon traces, server rewind query policy and damage validation.
- Priority: P0.
- Dependencies: `FR-10-T10`.
- Current state: In Progress at decision-boundary foundation scope only.
- Progress: machinegun, chaingun, shotgun, super shotgun, railgun, disruptor,
  plasma beam, and thunderbolt convergence/trace queries use the historical
  scene. Damage, knockback, death, piercing, effects, and radius damage execute
  against current authoritative state. Weapon/fairness scenarios remain.
- Definition of Done:
  - Each supported hitscan weapon uses one authoritative rewind policy and
    records requested/applied time plus clamp/reject reason.
  - The client never supplies a hit result or arbitrary rewind target.
  - Shooter, target, spectators, movers, spawn protection, death/respawn and
    discontinuity semantics have deterministic scenarios.
  - Fairness caps, opt-out/disable behavior and uncompensated fallback are
    documented and tested before default enablement.

### `FR-10-T12` Extended gameplay lag compensation

- Area: projectile, melee, radius damage, movers, deployables, triggers and coop
  interactions.
- Priority: P1.
- Dependencies: `FR-10-T10`, `FR-10-T11`.
- Current state: Backlog.
- Definition of Done:
  - Each mechanic has an explicit policy: rewind, forward estimate, hybrid, or
    deliberately uncompensated.
  - Projectile ownership, lifetime, collision, splash occlusion, mover-relative
    poses and multi-target fairness are covered independently.
  - No mechanic reuses hitscan rewind blindly where its time semantics differ.

### `FR-10-T13` Demo, MVD, spectator, and replay compatibility

- Area: client demos, server streaming/MVD, spectator timelines.
- Priority: P1.
- Dependencies: `FR-10-T04`, `FR-10-T05`, `FR-10-T06`, `FR-10-T07`.
- Current state: Backlog.
- Definition of Done:
  - Legacy demos still parse and play with the existing adapter.
  - New recordings identify their schema/transport and reproduce canonical
    snapshot/event order deterministically.
  - Seek, pause, timescale, packet loss in recorded streams, spectator switches
    and sequence reset do not duplicate events or interpolate discontinuities.

### `FR-10-T14` Networking telemetry, performance, load, and security gates

- Area: client/server/cgame diagnostics, tooling and CI evidence.
- Priority: P1.
- Dependencies: `FR-10-T03`, `FR-10-T06`, `FR-10-T08`, `FR-10-T10`,
  `FR-10-T11`, `FR-10-T12`.
- Current state: Backlog.
- Definition of Done:
  - The telemetry catalog below is implemented with low-overhead counters and
    focused opt-in detail.
  - Representative player/entity/rate/load matrices meet declared CPU, memory,
    bandwidth and correction budgets.
  - Decoder fuzzing, malformed-range tests, timestamp abuse, amplification,
    command flood and rewind-cheat scenarios pass.
  - Reports are machine-readable and written under `.tmp/networking/`.

### `FR-10-T15` Progressive rollout, documentation, and release acceptance

- Area: capability defaults, server policy, `.install`, CI/release and docs.
- Priority: P1.
- Dependencies: `FR-10-T05` through `FR-10-T14` as applicable to the release
  milestone.
- Current state: Backlog.
- Definition of Done:
  - Rollout gates and rollback triggers below are exercised, not merely
    documented.
  - Legacy and WORR-native compatibility matrices pass on supported platforms.
  - Server operators and users have approachable `docs-user/` guidance for
    public controls, fairness behavior and diagnostics.
  - `.install/` is refreshed and validated, release payloads contain the current
    modules/assets, and the canonical roadmap is updated with truthful status.

## Initial Engineering Budgets

These are first-pass guardrails. `FR-10-T01` must confirm or revise numeric
values using measured baselines before any new behavior becomes default.

| Area | Initial budget or invariant |
|---|---|
| Datagram sizing | Target application datagrams at or below 1200 bytes. Larger logical snapshots use explicit application fragmentation/prioritization; correctness must not depend on IP fragmentation. |
| Snapshot rate | Stay within each client's configured rate. When constrained, prioritize authority and explicitly defer/coalesce eligible state rather than emitting an invalid partial snapshot. |
| Snapshot history | Bound by count, age and entity storage. An overwritten/invalid base produces a full/key frame, never a partial reconstruction. |
| Event retention | Gameplay-critical and reliable events cannot be dropped by cosmetic pressure. Cosmetic coalescing requires an explicit type policy and counter. |
| Prediction history | Keep a bounded command ring sized for the supported impairment window; overflow causes an instrumented hard resync rather than replaying ambiguous commands. |
| Rewind window | Competitive default target: 200 ms; hard public-policy ceiling target: 250 ms. Requests outside policy are clamped or rejected and counted. Coop policy may differ only through an explicit bounded server rule. |
| Rewind sampling | At least one full collision pose per authoritative simulation step, with compatible-sample interpolation and discontinuity rejection. |
| CPU | At supported player/entity counts, snapshot build/encode and rewind queries each target no more than 10% of the authoritative server-frame budget at p95; combined networking must leave the simulation within its frame deadline. |
| Memory | All per-client input/snapshot/event rings and server pose history have configured hard caps and publish current/high-water byte counts. No unbounded growth from an unacknowledging client. |
| Allocation | No steady-state heap allocation is permitted in per-entity snapshot iteration, command replay, event playback or individual rewind trace hot paths after initialization/warmup. |
| Correction | Deterministic replay should produce zero gameplay-state error for the declared prediction surface. Nonzero corrections require categorized telemetry; visual smoothing must never delay authoritative collision state. |
| Time validation | Future, stale and non-monotonic client times are bounded by server-derived clock policy. Client timestamps never directly select arbitrary world history. |
| Failure behavior | Invalid canonical ranges, missing bases, generation mismatch or sequence ambiguity fail closed through drop, keyframe, hard resync or disconnect policy; never unchecked memory access. |

Performance gates must record the machine, build type, map, player/entity count,
tick/snapshot rates and impairment profile. Relative regressions are evaluated
against a stored same-machine baseline, not an undocumented absolute number.

## Compatibility Matrix

| Client / producer | Server / consumer | Required behavior | Modern features |
|---|---|---|---|
| WORR client | Legacy Quake II-compatible server | Connect and play through the existing negotiated `q2proto` path | Canonical model and local cgame improvements may run; WORR-only wire features remain off |
| WORR client | Current WORR server using a legacy protocol | Preserve current connection, acknowledged-delta snapshots and demo behavior | Enable only features representable safely above the existing wire |
| Legacy-compatible client | WORR server | Preserve currently supported legacy protocol paths | WORR-only fields/events/transport remain off |
| WORR client | Future WORR-native server | Explicit capability/version negotiation selects the separate WORR adapter | Full canonical event/snapshot/input feature set when mutually supported |
| Legacy demo | WORR client | Parse and play through the existing legacy adapter | No reinterpretation as a WORR-native stream |
| Future WORR demo | WORR client | Versioned recording feeds the canonical timeline deterministically | Typed events, modern snapshots and seek metadata supported by recorded version |
| MVD/spectator stream | WORR client/tool | Version and capability are explicit; player-view switches reset timeline/prediction correctly | Modern stream optional; legacy stream remains independently parseable |

No mid-stream packet may be guessed as another protocol. A transport change
requires a negotiated connection boundary or reconnect.

## Security and Abuse Model

The network is untrusted; cgame and sgame should receive validated canonical
data rather than parser-owned buffers.

Required controls include:

- Hard bounds on message length, range count, entity number, payload size,
  fragment count, decompressed size, sequence window and history references.
- Checked arithmetic for sizes, offsets, time conversion, interpolation and
  sequence wrap.
- Capability/version negotiation resistant to downgrade ambiguity and reflection
  or amplification before address validation.
- Command rate, batch size and duration limits; duplicate commands may be
  acknowledged but never simulated twice.
- Server-derived timestamp mapping. A client cannot request an arbitrary rewind
  age, future shot, target identity or hit result.
- Rewind policy caps by mode/server, with counters for clamp, reject, history
  miss and discontinuity rejection.
- Stable entity generations in snapshot and history lookups to prevent slot-
  reuse confusion.
- Fuzz/property tests for wire adapters and canonical range constructors, plus
  deterministic malformed-input corpora.
- Low-cost default telemetry and rate-limited detailed logging so diagnostics do
  not create a denial-of-service path.
- No raw input history, authentication data, or personally identifying network
  address data in ordinary telemetry artifacts.

## Telemetry Catalog

### Transport and input

- RTT, smoothed RTT, jitter, packet loss, burst loss, reorder and duplicate
  counts.
- Bytes/packets sent and received by class, rate-limit deferrals, fragment and
  reassembly counts.
- Latest input sequence, acknowledgement lag, command batch/redundancy depth,
  duplicate/stale/future/rejected command counts and input-buffer high water.

### Snapshots

- Full versus delta snapshots, selected base age, invalid/missing/overwritten
  base fallbacks and keyframe requests.
- Snapshot bytes, entity create/update/remove/defer counts, visible entity count,
  canonical storage bytes and history high water.
- Encode, decode, validate, reconstruct and cgame transition timings.

### Events

- Events emitted/received/played/matched/deduplicated/late/dropped/coalesced by
  class.
- Sequence gaps, queue occupancy/high water, prediction mismatch and reliable
  acknowledgement age.

### Prediction and timeline

- Commands replayed per frame, authoritative error by state field, correction
  magnitude/cause, visual smoothing duration and hard-resync reason.
- Render interpolation delay, extrapolated frames/duration, missing transition,
  discontinuity reset and server-time adjustment.

### Lag compensation

- Requested, mapped and applied rewind age; clamp/reject/history-miss reason.
- Pose samples/interpolations/discontinuity rejects, candidate counts and query
  time.
- Hits accepted/rejected with compensated versus uncompensated result metadata,
  without trusting or logging client-supplied hit conclusions.
- History bytes, samples and high water by entity class.

Diagnostics should expose compact operator status plus machine-readable reports.
Presentation controls belong to `cg_`; engine transport/status controls belong
to `net_`, `cl_`, or `sv_`; gameplay fairness controls belong to `sg_`.

## Validation Matrix

Each implementation task selects the relevant rows and records exact commands
and artifacts in its `docs-dev/` implementation log.

| Class | Required coverage |
|---|---|
| Determinism | Shared client/server state hashes; command/event sequence wrap; repeated identical seed/run |
| Impairment | 0/50/100/200 ms latency baselines; jitter; 1/5/10/20% loss; burst loss; reorder; duplicate; throttled rate; ack stall |
| Snapshot | Full/key/delta; invalid and overwritten base; add/remove/reuse; PVS transitions; oversize/fragment; reconnect/map change |
| Events | Multiple same-tick events; loss/replay/late/duplicate; predicted match/mismatch; reliable control; cosmetic pressure |
| Prediction | Movement, stance, view and supported weapon state; collision correction; history overflow; pause; tick/rate change |
| Rewind | Exact sample/interpolation; cap; future/stale time; teleport/respawn/death; mover attachment; slot reuse; history miss |
| Gameplay | Weapon-by-weapon hitscan, projectile, melee, radius and coop/mover scenarios as their tasks mature |
| Compatibility | Legacy servers, current WORR legacy protocol, legacy clients where supported, legacy demos, future demo/MVD versions |
| Security | Fuzzed adapters/ranges, malformed lengths/counts, decompression/fragment limits, command flood, timestamp abuse, amplification |
| Performance | Player/entity/rate matrix, CPU p50/p95/p99, allocation count, bandwidth, history/ring memory and long soak |

## Rollout and Rollback

### Rollout stages

1. **R0: Inventory only.** Accept architecture, baselines and budgets. No runtime
   behavior or wire changes.
2. **R1: Shadow canonicalization.** Build immutable canonical views beside the
   current path, compare state/hashes, and discard them from authority.
3. **R2: Cgame range cutover.** Move consumers to immutable ranges one subsystem
   at a time behind a developer gate, retaining the current bridge until parity
   evidence is complete.
4. **R3: Prediction/event cutover.** Enable canonical event playback and expanded
   prediction over legacy transport for controlled testing, then opt-in servers.
5. **R4: Lag compensation opt-in.** Enable bounded hitscan rewind per mode/server
   after fairness, abuse and performance gates; extend mechanics independently.
6. **R5: WORR transport experimental.** Negotiate the separate transport only
   between explicitly capable peers; keep dual-stack legacy paths.
7. **R6: Release candidate/default review.** Promote individual features only
   when compatibility, impairment, soak, telemetry and user-doc gates pass.

### Rollback rules

- Every promoted subsystem has a server/client capability or policy kill switch
  until its release gate is complete.
- A disabled modern feature falls back to the last validated legacy/canonical
  behavior at a connection or snapshot boundary; it does not reinterpret an
  in-flight stream.
- Protocol rollback requires reconnect using explicit negotiation.
- Lag-compensation disablement returns to authoritative current-world traces; it
  never accepts cached client hit results.
- Canonical range or prediction invariant failures request a full snapshot/hard
  resync or disconnect according to severity.
- Rollback triggers include crash, memory corruption, persistent state hash
  divergence, unbounded queue/history growth, missed critical events, fairness
  bypass, compatibility regression, or failure of the declared frame budget.
- Removal of the previous internal module bridge waits until the replacement has
  parity evidence across normal play, demos and impairment scenarios.

## Documentation and Evidence Rules

Each significant slice must add a focused implementation log under
`docs-dev/`, preferably under a networking subsystem directory, and put its
task IDs near the top. The log records:

- Scope and non-goals.
- Ownership/API changes.
- Compatibility and security effects.
- Exact build/test/runtime commands.
- Budget measurements and impairment profile.
- Evidence paths under `.tmp/networking/`.
- Remaining task work, without marking a parent task complete for a foundation
  slice.

Public cvars, operator policy, visible diagnostics, or fairness changes also
require approachable updates under `docs-user/` before release promotion.

## Build, Stage, and Current Test Commands

### Windows Clang/LLVM (`builddir-win`)

The configured local workflow used by the repository's VS Code tasks is:

```powershell
tools\meson_setup.cmd setup --native-file meson.native.ini --reconfigure builddir-win -Dbase-game=basew -Ddefault-game=basew -Drmlui=enabled
meson compile -C builddir-win
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64
```

For a fresh build directory, omit `--reconfigure` on the setup command.

### Windows MSVC

Do not use `meson.native.ini` with MSVC:

```powershell
tools\meson_setup.cmd setup -Dwrap_mode=forcefallback builddir
meson compile -C builddir
python tools\refresh_install.py --build-dir builddir --install-dir .install --base-game basew --platform-id windows-x86_64
```

### Linux/macOS

```bash
meson setup builddir -Dbase-game=basew -Ddefault-game=basew
meson compile -C builddir
python3 tools/refresh_install.py --build-dir builddir --install-dir .install --base-game basew
```

The refresh step replaces generated WORR runtime files, game modules,
`shader_vkpt`, release notices, and `basew/pak0.pkz`, while preserving
user-managed local data such as licensed `pak*.pak`, configs, saves, demos and
screenshots. It repackages current repository assets and validates the staged
Windows payload when `--platform-id windows-x86_64` is supplied.

The current top-level build has no registered Meson tests:

```powershell
meson test -C builddir-win --print-errorlogs
```

currently reports `No tests defined.` The existing top-level `-Dtests=true`
option enables dangerous in-engine test code and must not be treated as a normal
unit-test suite or enabled in release builds. `FR-10-T03` must establish a
non-interactive network test entrypoint and register appropriate tests before
this plan claims automated Meson coverage.

## Immediate Next Actions

1. Implement the deterministic impairment/baseline harness (`FR-10-T03`) and
   turn the published budgets into executable gates.
2. Replace the mirrored PMove callback boundary with a canonical exactly typed
   ABI and add client/server state-hash replay parity (`FR-10-T02`).
3. Add authoritative command-consumption and render-time watermarks
   (`FR-10-T09`) instead of relying on packet-level inferred timing.
4. Advance typed authoritative event sequences and immutable snapshot ranges
   (`FR-10-T05` through `FR-10-T07`).
5. Run the weapon/fairness/load matrix, then extend declared compensation
   policies to movers/projectiles/melee/radius interactions (`FR-10-T11/T12`).
