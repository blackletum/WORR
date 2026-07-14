# FR-10 Roadmap Completion Audit

Date: 2026-07-13
Last updated: 2026-07-14

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
| `FR-10-T04` | Negotiated native carrier, codecs for the accepted canonical models, exact receipts, live client/server adapters, downgrade/MTU/malformed/reconnect coverage, and dual-adapter parity | The default-off production adapters now complete the private `0x13` readiness proof while public masks remain `3`, select the newest command from one exact encoded legacy range, carry at most one 110-byte `WNC1` command as `WTC1(DATA(WNE1))` per private epoch, join it observationally with the authoritative legacy stream, and return an exact ACK-only receipt. Current plus one retired DRAIN bank, committed-duplicate rearm, 100 ms/three-handoff ACK liveness, reliable-queue point-of-no-return handling, pre-netchan admission clocking, async ACK wake, and exact 818/819 plus 976/977 production boundaries are covered. General command/event/snapshot adapters, mixed DATA/ACK packing, native authority, real reliable/async impairment proof, dual-adapter parity, load, and cross-platform evidence remain absent | Incomplete |
| `FR-10-T05` | Multiple typed events per tick, explicit delivery/prediction identity, reliable retention/receipt, predicted matching, and ordered at-most-once live presentation | Canonical journal, full legacy payload mapping, client V2 ranges, and durable cgame audit exist; direct multi-event producers and live predicted presenters remain absent | Incomplete |
| `FR-10-T06` | Complete acknowledged-base snapshots, immutable reconstruction, keyframe recovery, packet budgets, and at least 100,000 accepted live shadow snapshots with zero unexplained mismatch | Offline 100,000-frame final-emission/projector parity and a current-build 115,914-frame live target-count gate are exact; native serialized parity, recovery-promotion, load/budget, and release gates remain pending | Incomplete |
| `FR-10-T07` | Cgame-owned immutable timeline, valid interpolation/extrapolation/discontinuity behavior, and ordered deduplicated event dispatch on live presenters | Timeline, remote-transform audit/opt-in promotion, and present-once audit cursor exist; presenter cutover, impairment breadth, extrapolation and performance evidence remain absent | Incomplete |
| `FR-10-T08` | Full eligible movement/weapon prediction, authoritative consumed-command replay, predicted-side-effect suppression, correction telemetry, and hard-resync behavior | Movement replay and local-action transaction/audit cores exist; complete weapon catalog, live cgame/sgame transactions, side-effect suppression, and correction budgets remain absent | Incomplete |
| `FR-10-T09` | Canonical command identity/timing, server-consumed watermark, idempotent duplicate handling, legacy/native mapping, and shared prediction/rewind/event correlation | Legacy carrier and live consumed cursor drive prediction/rewind; the default-off native pilot now maps one exact written-range command with the shared usercmd converter, validates each transport bank's official command epoch, and joins either arrival order without changing authority. Retained-ring gaps have checked O(1) distance plus bounded O(capacity), transactional recovery, scoped live stress, and a connected 115,914-frame snapshot run. Repeated native mapping/authority, a dedicated command-gap report, complete event correlation, and full impairment/runtime acceptance remain absent | Incomplete |
| `FR-10-T10` | Server-authenticated time mapping, bounded player and mover pose history, non-mutating historical scenes, deterministic observability, and CPU/memory budgets | Player history and sealed player scenes are live default-off; immutable BSP provider now has real generated IBSP38 parity against `SV_Clip`. Mover capture/history, historical brush authority, complex-map breadth, and load budgets remain absent | Incomplete |
| `FR-10-T11` | Every supported hitscan weapon uses one fair authoritative rewind policy with shooter/target/mover/lifecycle scenarios, documentation, abuse/load gates, and safe fallback | Eight policy tags route through the sealed player scene and 120 common-core cases pass; live damage/fairness, mover and release gates remain absent | Incomplete |
| `FR-10-T12` | Explicit independent policies and deterministic scenarios for projectile, melee, beam, splash/radius, mover, deployable, trigger, and cooperative interactions | Continuous-beam and narrow convergence queries exist; most declared mechanics remain unimplemented | Incomplete |
| `FR-10-T13` | Legacy and modern demo/MVD/GTV/spectator record/play/seek/relay matrices reproduce canonical order and reset timelines safely | Client demo cursor/clock preservation and seek lineage exist; MVD/GTV, view switching, native recording, event reproduction, and full matrices remain absent | Incomplete |
| `FR-10-T14` | Complete telemetry catalog, 1/8/16/32-client performance profiles, 100,000 malformed cases per changed decoder/range, allocation/budget proof, stress/soak, and machine-readable evidence | Focused telemetry, short loopback, offline corpora, allocation-free codecs/carrier/dispatch/ACK retention, one-shot production lifecycle/epoch/queue/clock/budget properties, local-action/rewind/collision evidence, focused 14/14, full 104/104 plus 312/312 repeated networking, final production build, strict client x64/i686, sanitizer/analyzer, final 388/388 default plus 386/386 impaired staged smoke, and a hash-bound 115,914-frame report exist on Windows. Mandatory live load, bandwidth, 100,000 malformed, soak, and cross-platform gates remain absent | Incomplete |
| `FR-10-T15` | Three full matrices on every release platform, two-hour 32-client soaks, seven-day/100-server-hour opt-in evidence, rollback drills, current docs, and staged payload | User controls are current and the final Windows `.install` refresh/stage validates 16 root files, one dependency, 308 assets, 31 botfiles, and 214 RmlUi assets; platform matrices, durations, rollout hours, and drills remain absent | Incomplete |
| `FR-10-T16` | Fresh-input age, loss recovery, idempotence, bandwidth, command-flood, and cadence evidence on both legacy and native adapters | Deterministic controller and default-off legacy batched integration pass a short impaired loopback; one observational native command now has exact retained delivery and ACK-only retry over production seams, while over-retention gaps have a 4,096-command authenticated ceiling and bounded fast-forward. Repeated/adaptive native delivery, mixed DATA/ACK, real async-wake impairment, a dedicated accepted gap report, and full bandwidth/budget/load evidence remain absent | Incomplete |

## Current critical path

The project cannot close by independently polishing every subsystem. The
dependency-preserving critical path is:

1. Build on the completed default-off one-command WTC1 DATA/ACK observation by
   testing real reliable transfer and the rate/fragment-gated asynchronous ACK
   wake under deterministic loss, then add transactional mixed DATA-plus-ACK
   packing before repeated native commands or any authority promotion.
2. Connect direct authoritative and predicted local-action event production to
   the same validated journal/timeline used by both transports.
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
  byte mappings under a 65,536-byte payload ceiling; no capability is
  advertised and no live packet currently carries those bytes.
- The one-shot production pilot proves that default-off connections can carry
  one native command observation and return an exact ACK while leaving legacy
  authority intact. Its 14/14 focused matrix covers current/retired banks,
  duplicate rearm, reliable-queue commitment, clock ordering, one-shot drain,
  and exact packet boundaries; it does not prove repeated native delivery,
  native authority, mixed DATA/ACK packing, or real asynchronous wake under
  impairment.
- The final Windows stage contains 16 root runtime files, one dependency, 308
  assets, 31 botfiles, and 214 RmlUi assets. Its short runtime smoke accepts
  388/388 default and 386/386
  impaired cgame publications with zero mismatches/failures/rejections, but it
  but it is not a multi-client, long-session, or supported-platform release
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
