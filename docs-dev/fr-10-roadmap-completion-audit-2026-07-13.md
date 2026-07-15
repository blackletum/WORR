# FR-10 Roadmap Completion Audit

Date: 2026-07-13
Last updated: 2026-07-15

Project tasks: `FR-10-T01` through `FR-10-T16`

## Purpose

This is the requirement-by-requirement completion audit for the progressive
events, snapshots, prediction, transport, and lag-compensation project. It is
not a substitute for the canonical task definitions in
`docs-dev/plans/progressive-networking-events-snapshots-roadmap.md`. It records
which evidence actually proves a task, which evidence is narrower than the
task, and which next dependency blocks the critical path.

The audit deliberately treats missing or indirect evidence as incomplete. A
green focused unit test cannot prove a live adapter, a short loopback cannot
prove a 100,000-frame or multi-client gate, and one Windows build cannot prove
the supported-platform matrix.

## Task audit

| Task | Completion proof required | Current authoritative evidence | Audit result |
|---|---|---|---|
| `FR-10-T01` | Accepted ownership, identity, time, compatibility, budget, security, rollout, and rollback contracts | `docs-dev/progressive-networking-foundation-2026-07-11.md` plus the canonical living plan | Complete |
| `FR-10-T02` | One deterministic client/server movement core, exact ABI/hash/replay parity, correction and wrap coverage | `docs-dev/networking-deterministic-prediction-core-2026-07-12.md` and ordinary networking tests | Complete |
| `FR-10-T03` | Deterministic impairment model covering the declared faults, checked-in golden, staging control, and repeatability | `docs-dev/networking-deterministic-impairment-harness-2026-07-12.md` and `networking-baseline` evidence | Complete |
| `FR-10-T04` | Negotiated native carrier, codecs for the accepted canonical models, exact receipts, live client/server adapters, downgrade/MTU/malformed/reconnect coverage, and dual-adapter parity | The native shadows now require negotiated private epoch cancellation bit `0x40`: command mode binds exact private `0x53`, event mode binds exact private `0x73`, and the public offer remains `0x03`. A fresh challenge transactionally cancels and counts all lower activated or pending-readiness epochs, then initializes fresh state after the normal readiness proof; no retired banks or epoch rings remain. Fully validated canceled carriers expose only their unchanged legacy prefix, and valid delayed old readiness records are consumed without state mutation and counted separately. The production server orders that classification before `SERVER_ACTIVE` reservation, so stale control is response-free while a current response still fails closed; this wrapper ordering has code-review/build coverage, while an end-to-end full-queue wrapper regression remains open. Corrupt, wrong-direction, or downgraded old traffic rejects. Server status separates current `transport_epoch` from `wire_committed_transport_epoch`. The production-wrapper schedule converges with exactly-once cgame admission and digest `be9724b38fb5f682`; its exhaustion/two-rotation row has zero retired receipt/retention, valid-old ACK/DATA stripped, corrupt-old traffic rejected, and no silent overwrite. Legacy commands, snapshots, and presentation remain authoritative. Native snapshot DATA, broader event families, dual-adapter parity, load, and cross-platform evidence remain absent | Incomplete |
| `FR-10-T05` | Multiple typed events per tick, explicit delivery/prediction identity, reliable retention/receipt, predicted matching, and ordered at-most-once live presentation | Canonical journal, payload mapping, cgame authority/prediction runtime, semantic descriptor, monotonic owner, and fresh-status admission/repeat fences now back a live per-peer shadow. Final-emission legacy-entity candidates remain ID-less until descriptor ACK, then reliable server DATA reaches cgame and exact fresh receipts authorize ACKs. The negotiated epoch barrier terminally counts retained sender records/backlog and client event RX/receipts before removing old transport state; delayed canceled DATA cannot submit to or reset cgame. The event-owner high-water persists across map changes, current-epoch repeats still require fresh cgame authority, and the wrapper schedule remains exactly-once under loss/reorder/duplication. Direct sgame multi-event families, temporary/muzzle/audio production, predicted local-action submission, native snapshot fences, and effect/audio presenters remain absent | Incomplete |
| `FR-10-T06` | Complete acknowledged-base snapshots, immutable reconstruction, keyframe recovery, packet budgets, and at least 100,000 accepted live shadow snapshots with zero unexplained mismatch | Offline 100,000-frame final-emission/projector parity and a current-build 115,914-frame live target-count gate are exact. The exact retained per-peer final emission now also supports transactional legacy-entity event candidate reconstruction/hash verification for the native event shadow; native serialized snapshot parity, recovery-promotion, load/budget, and release gates remain pending | Incomplete |
| `FR-10-T07` | Cgame-owned immutable timeline, valid interpolation/extrapolation/discontinuity behavior, and ordered deduplicated event dispatch on live presenters | Timeline, remote-transform audit/opt-in promotion, present-once audit cursor, snapshot-fenced authority ordering, hard-resync signaling, and default-off native event authority admission through a fresh-status ACK fence exist. The production-wrapper virtual link proves the currently transported event reaches cgame exactly once despite duplicate/reordered DATA and that corrupt or canceled-epoch DATA cannot invoke cgame. Legacy effect/audio presentation remains authoritative; native snapshot authority, presenter cutover, multi-event impairment breadth, extrapolation, and performance evidence remain absent | Incomplete |
| `FR-10-T08` | Full eligible movement/weapon prediction, authoritative consumed-command replay, predicted-side-effect suppression, correction telemetry, and hard-resync behavior | Movement replay and local-action transaction/audit cores now feed a tested prediction-key reconciliation/tombstone model with consumed-command retirement; complete weapon catalog, live cgame/sgame producer transactions, side-effect presentation, and correction budgets remain absent | Incomplete |
| `FR-10-T09` | Canonical command identity/timing, server-consumed watermark, idempotent duplicate handling, legacy/native mapping, and shared prediction/rewind/event correlation | Legacy carrier and live consumed cursor drive prediction/rewind; the default-off native pilot now maps a repeated sampled sequence from exact written ranges with the shared usercmd converter, validates each bank's official epoch, joins either arrival order, retires exact matches, and rejects fresh transport identities for already matched canonical commands without changing authority. Three process runs prove 453 mapped match/ACK/release cycles. Retained-ring gaps have checked O(1) distance plus bounded O(capacity), transactional recovery, scoped live stress, and a connected 115,914-frame snapshot run. Exhaustive native mapping, native authority, a dedicated command-gap report, complete event correlation, and full impairment/runtime acceptance remain absent | Incomplete |
| `FR-10-T10` | Server-authenticated time mapping, bounded player and mover pose history, non-mutating historical scenes, deterministic observability, and CPU/memory budgets | Player and live `SOLID_BSP` mover history, mover-relative provenance, sealed historical brush dispatch, and immutable BSP provider parity are now present. A three-run dedicated-server V5 gate loads a packaged rotating collision fixture, arms a real bot on its `MoveType::Push` brush, lets twelve normal frames invoke physical pusher motion and end-frame capture, then proves paired history has changed mover angle and rider origin with exact relative provenance. The newest real pair is also accepted as exact bot/brush candidates in a sealed immutable canonical scene. The scoped trace still proves the clear current baseline, rotation-sensitive negative control, historical provider block at fraction 0.374756, and restored authority state. Broader player-on-mover/continuous-rotation/complex-map cases, engine weapon damage/fairness, and CPU/memory/load budgets remain absent | Incomplete |
| `FR-10-T11` | Every supported hitscan weapon uses one fair authoritative rewind policy with shooter/target/mover/lifecycle scenarios, documentation, abuse/load gates, and safe fallback | Eight policy tags route through the sealed player scene and 120 common-core cases pass; a three-repeat real `fire_rail` gate proves invalid-ack no-damage fallback plus near, in-budget, and capped legacy acknowledgement historical-hit-to-current-damage rows (exactly 30 total damage and unchanged query authority). A separate three-repeat, real-command two-client fixture proves normal Railgun policy `5`/80-damage, machinegun policy `1`/8-damage, Chaingun policy `2`/18-damage (full three-round burst), Super Shotgun policy `4`/120-damage (two ten-pellet barrels), shotgun policy `3`/48-damage, Disruptor convergence policy `6`/45-damage after normal projectile and daemon lifecycle, and real held first, three-tick, bounded release, water-crossing, and bounded 32-tick modes for Plasma Beam policy `7`/8 then 24/4/256 damage and dry-world Thunderbolt policy `8`/8 then 24/4/256 de-duplicated-footprint damage, all at positive age with unchanged live query geometry. Remaining fairness, moving-target/multi-target, movers/lifecycle, abuse/load, platform, and release gates remain absent | Incomplete |
| `FR-10-T12` | Explicit independent policies and deterministic scenarios for projectile, melee, beam, splash/radius, mover, deployable, trigger, and cooperative interactions | Narrow convergence and continuous-beam query coverage exist, and three-repeat real-command first-tick, three-tick, bounded release, water-crossing, and bounded 32-tick modes prove Plasma Beam policy `7` and dry-world Thunderbolt policy `8` historical selection with exact live 8 then 24 then halved 4 then 256 damage, no post-release damage over 250 ms, and no received release during the sustained hold. Three further headless real-command repeats prove Thunderbolt's distinct current-authority underwater branch: a bounded world-water `pointContents` state, all eight Cells drained, explicit production-branch observation, and exact 70 self damage; no historical beam trace is claimed for the branch that returns before tracing. Indefinite holds, moving/multi-target and underwater-radius behavior, and most declared mechanics remain unimplemented | Incomplete |
| `FR-10-T13` | Legacy and modern demo/MVD/GTV/spectator record/play/seek/relay matrices reproduce canonical order and reset timelines safely | Client demo cursor/clock preservation and seek lineage exist; MVD/GTV, view switching, native recording, event reproduction, and full matrices remain absent | Incomplete |
| `FR-10-T14` | Complete telemetry catalog, 1/8/16/32-client performance profiles, 100,000 malformed cases per changed decoder/range, allocation/budget proof, stress/soak, and machine-readable evidence | Existing focused telemetry, corpora, Windows build/stage, and live snapshot/command slices remain valid. The cancellation milestone adds client/server monotonic floors plus saturating barrier, canceled transport/TX/RX/receipt/event, stale-canceled-carrier, and stale-canceled-readiness counters; server status also disambiguates the current and wire-committed transport epochs. Its final focused gate passes 8/8, the negotiation/readiness subset passes 100/100 across twenty repetitions, the complete suite passes 121/121, and three complete repetitions pass 363/363. The wrapper lifecycle row records three exhausted ACK handoffs followed by counted disposal, zero retired state, a second rotation without a retired bank, valid-old stripping, and two corrupt-old rejections. Separate client/server adapter regressions consume all four valid delayed old readiness record kinds without state mutation while malformed/downgraded variants reject; the server adapter distinguishes stale response-free COMMIT from current response-required COMMIT, and status preserves the prior wire-committed epoch across a replacement challenge. The production wrapper ordering has code-review/build coverage, not a full-queue parser regression. The new wrapper digest is `be9724b38fb5f682`; four production modules build and the refreshed Windows stage validates. This evidence does not replace the still-required `NetImpair` golden, distinct-event matrix, live multi-process/load profile, malformed corpus, soak, or cross-platform gates | Incomplete |
| `FR-10-T15` | Three full matrices on every release platform, two-hour 32-client soaks, seven-day/100-server-hour opt-in evidence, rollback drills, current docs, and staged payload | User controls are current and the final Windows `.install` refresh/stage validates 16 root runtime files, one dependency, a `pak0.pkz` packed from 342 source asset files, 31 botfiles, and 215 RmlUi assets; platform matrices, durations, rollout hours, and drills remain absent | Incomplete |
| `FR-10-T16` | Fresh-input age, loss recovery, idempotence, bandwidth, command-flood, and cadence evidence on both legacy and native adapters | Deterministic controller and default-off legacy batched integration pass a short impaired loopback, and the native command shadow retains its 453 exact sampled matches/releases. The private `0x73` event mode still uses live mixed packing and scheduler wakeups, while a fresh negotiated epoch now terminally cancels exhausted old DATA/receipt work instead of depending on unbounded ACK retries or a one-bank rollover. The wrapper gate proves bounded retry convergence for ordinary loss and explicit counted termination after all three ACK credits are spent; a second rotation has no bank overwrite. Adaptive native batching/redundancy, the full production impairment matrix, server feedback, a dedicated accepted gap report, and full bandwidth/budget/load evidence remain absent | Incomplete |

## Current critical path

The project cannot close by independently polishing every subsystem. The
dependency-preserving critical path is:

1. Extend the live per-peer descriptor/reliable stream beyond exact final-
   emission legacy entity events to temporary entities, muzzle flashes,
   spatial audio, direct sgame multi-events, and predicted local actions. Then
   bind those events to native snapshot fences before any presenter cutover.
2. Extend the implemented negotiated epoch-cancellation proof from the explicit
   wrapper schedule to the production `NetImpair` model/golden, distinct events,
   rate/fragment pressure, and multi-process coverage. Then add the native
   acknowledged-baseline snapshot adapter and dual-adapter budgets.
3. Complete the Rerelease local weapon/action catalog and run prediction and
   authority transactions in shadow using the now-measured cheap/deep audit
   split.
4. Build on proven generated-IBSP38 transformed collision parity to capture
   immutable mover poses and add historical brush queries without relinking
   live state.
5. Promote the canonical snapshot/event/cgame paths one subsystem at a time
   only after serialized-datagram, 100,000-frame live, impairment, demo, and
   correction evidence is exact.
6. Expand lag compensation by mechanic, then execute the mandatory security,
   load, allocation, compatibility, soak, rollback, and platform matrices.

This ordering keeps the native adapter downstream of one canonical gameplay
model and keeps mover rewind downstream of a proven non-mutating collision
primitive. It also prevents release-duration evidence from being collected
against interfaces that are still changing.

## Evidence that is valid but not sufficient

- `.tmp/networking/snapshot-parity/evidence.json` proves two identical
  100,000-case public-API projections with digest `7b185107eeb0f6e7`; it does
  not serialize datagrams or execute the live parser/cgame path.
- `.tmp/networking/impairment-runtime.json` proves a short one-client Windows
  loopback, including exact live cgame acceptance; it does not prove the
  100,000-frame, multi-client, long-session, or other-platform gates.
- `.tmp/networking/rewind-acceptance/evidence.json` proves 120 deterministic
  player-bounds policy invocations with zero live-pose mutation; its own
  `live_engine_weapon_damage_gate` remains false.
- `.tmp/networking/rewind_collision_real_bsp/report.json` proves real generated
  IBSP38 transformed trace parity for a narrow collision-only fixture; it does
  not prove mover authority, complex BSP/BSPX breadth, concurrency, or soak.
- The native codec tests prove exact transactional command, event, and snapshot
  byte mappings under a 65,536-byte payload ceiling. The base pilot carries a
  sampled command subset; the separately opted-in private `0x73` mode now also
  carries descriptor and final-emission legacy-entity event DATA. No capability
  is publicly advertised, and no live native snapshot packet exists yet.
- The repeated production pilot proves that default-off connections can reuse
  one bounded retained slot for a stop-and-wait sampled stream while legacy
  authority remains intact. Its three latency-only reports prove 453 exact
  match/ACK/release cycles, immediate match retirement, final quiescence, and
  stable report/log/runtime binding. The event mode now uses the mixed
  coordinator at live client/server hooks, but this older repeated-command
  evidence does not prove the event lane, exhaustive command coverage, native
  authority, directional packet loss/reorder/duplication, WAN behavior, or
  multi-client fairness. Exact reports, counters, hashes, and limitations are recorded in
  `docs-dev/fr-10-t04-repeated-command-shadow-and-mixed-carrier-core-2026-07-14.md`.
- The cgame event runtime proves transactional authoritative admission,
  selective receipt, prediction reconciliation/retirement, snapshot-fenced
  ordering, audit-off authority lifecycle, strict engine-owner epoch
  monotonicity, and stale-receipt suppression across DLL and snapshot-fence
  loss. The default-off event shadow now calls its export for exact final-
  emission legacy-entity events. Its presentation sink remains no-effects while
  legacy presentation is
  authoritative. Exact scope and the required fresh-status-before-ACK rule are
  recorded in
  `docs-dev/fr-10-t05-cgame-event-runtime-and-direct-authority-export-2026-07-14.md`.
- The canonical event-stream milestone proves an epoch-independent 24-byte
  descriptor, exact 56-byte codec image, monotonic connection owner,
  capability-bound completed-message admission, private transport precommit,
  fresh cgame receipt proof before ACK publication, and fail-closed lifecycle
  alignment. The later default-off `0x73` shadow now negotiates this privately,
  emits it per peer, and calls the admission path; the foundation milestone by
  itself did not prove those live properties or presentation. Exact scope is recorded in
  `docs-dev/fr-10-t04-t05-canonical-event-stream-admission-core-2026-07-14.md`.
- The semantic repeat/ACK-fence milestone proves packet-observed exact-history
  binding, any-fragment committed replay, generic commit/repeat rejection,
  exact-only receipt capture, fresh cgame proof, new-fragment arena rollback,
  and fail-closed retirement of all semantic ACKs on resync. The live per-peer
  event lane now routes current-bank admission and repeats through this fence;
  the focused foundation's exact scope is recorded in
  `docs-dev/fr-10-t04-t05-semantic-repeat-ack-fence-2026-07-14.md`.
- The live per-peer event-shadow milestone proves the separate default-off
  private `0x73` readiness path over an unchanged public `0x03` mask, exact
  final-emission candidate extraction, descriptor-before-ID reliable sending,
  full-duplex mixed packing, fresh cgame-status ACK authority, and
  client/server scheduler liveness. It leaves legacy commands, snapshots, and
  presentation authoritative and does not prove broader event families,
  native snapshot DATA, fault/load/soak breadth, or release promotion. Exact
  scope and final integrated totals are recorded in
  `docs-dev/fr-10-t04-t05-live-per-peer-event-stream-shadow-2026-07-14.md`.
- The deterministic event virtual-link milestone exercises the real production
  hook wrappers through accepted-but-lost bidirectional DATA, delayed/reordered
  and duplicate delivery, one-way ACK loss, mixed DATA/ACK packets, corruption,
  and cancellation lifecycle traffic. It converges ordinary faults, admits one
  event exactly once, strips valid canceled ACK/DATA to legacy-only behavior,
  rejects corrupt canceled traffic, and has stable digest
  `be9724b38fb5f682`. It is deliberately an explicit in-process one-event
  schedule, not yet the `NetImpair` golden, distinct-event reorder,
  multi-process, load, or cross-platform matrix.
- The map-quiesce follow-up identified the three-credit exhaustion plus second-
  rotation overwrite and ratified the correction. The implemented negotiated
  barrier now covers activated and pending-readiness epochs, counts every
  terminal disposition, removes retired banks, and preserves strict cgame and
  parser fences. Exact implementation and focused evidence are recorded in
  `docs-dev/fr-10-t04-t05-negotiated-epoch-cancellation-barrier-2026-07-14.md`.
- The final Windows stage contains 16 root runtime files, one dependency, 331
  assets, 31 botfiles, and 215 RmlUi assets. Its short runtime smoke accepts
  388/388 default and 386/386
  impaired cgame publications with zero mismatches/failures/rejections, but it
  is not a multi-client, long-session, or supported-platform release
  matrix.
- The networking suite and its repeat runs prove deterministic focused and
  integration behavior on the configured Windows Clang build. They do not
  replace live load, malformed-corpus, soak, or cross-platform acceptance.

## Completion rule

An `FR-10` task may be checked complete only when every item in its Definition
of Done has direct current evidence at the same scope. Parent completion is not
inferred from a foundation API, default-off shadow, short smoke test, or a
documented intention. The living and strategic roadmaps must be updated in the
same change that adds or invalidates completion evidence.
