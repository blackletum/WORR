# FR-10 Roadmap Completion Audit

Date: 2026-07-13
Last updated: 2026-07-17

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
| `FR-10-T04` | Negotiated native carrier, codecs for the accepted canonical models, exact receipts, live client/server adapters, downgrade/MTU/malformed/reconnect coverage, and dual-adapter parity | Private command/event modes, cancellation, mixed carrier scheduling, semantic event ACK fencing, and narrow live event producers remain valid over the unchanged public `0x03` offer. Private snapshot mode binds exact `0x57` plus an independent snapshot epoch, queues exact final projections in a bounded two-bank sender, admits them through the real client hook after the exact legacy expectation, publishes through the sole native cgame timeline owner, and returns semantic ACKs through the reverse carrier. A fixed-seed two-run production-path corpus now carries 100,000 positive projections through WNC1/WNE1/WTC1, one production `NetImpair` profile, receiver/admission, cgame timeline, locally constructed prediction resolver/selector, ACK, and release with golden `c6aee48df85341ab`; all positives accept/ACK/release and three corrupt carriers reject. The local-action continuation adds a fieldwise 64-byte private receipt as event payload kind `12`; its production-wrapper virtual link passes descriptor admission, exact cgame command correlation, semantic ACK, and release with zero presentation. Three live two-process schema-v32 repeats disconnect/reconnect the shooter and match 45/45 exact action receipts after epoch-3 rebase with zero reconciliation errors. Exact private combined `0x77` now fairly schedules event and snapshot DATA with disjoint ACK-identity lanes; three schema-v33 repeats match 99/99 action receipts and show both snapshot peers ACKing/releasing with zero endpoint failure. Public advertisement, full dual-adapter parity, complete impairment/load, broader real-socket, and cross-platform evidence remain absent | Incomplete |
| `FR-10-T05` | Multiple typed events per tick, explicit delivery/prediction identity, reliable retention/receipt, predicted matching, and ordered at-most-once live presentation | Canonical journal, cgame authority/prediction runtime, semantic descriptor, exact receipts, epoch cancellation, and the live default-off per-peer event shadow are present. Narrow production covers final-emission legacy entities, visible or explicit-positional spatial audio, and bounded ordered temporary/player-muzzle/monster-muzzle/Rerelease-muzzle sequences. Snapshot ACK authority now uses the healthy exact cgame event-runtime fence in the live default-off snapshot adapter, including zero-event snapshots and event-carrying legacy-inferred snapshots with `cg_event_runtime_audit=0`. Audit-off legacy ranges validate without consuming the diagnostic join table. Private local-action receipt payload kind `12` is reliable ordered, authoritative only, critical, entity-free, and terminally skipped without presentation; command mismatch/conflict degrades the same authority runtime. Other direct service families, reliable/positionless audio, combined/reliable muzzle traffic, predicted local-action presentation, and presenter cutover remain absent | Incomplete |
| `FR-10-T06` | Complete acknowledged-base snapshots, immutable reconstruction, keyframe recovery, packet budgets, and at least 100,000 accepted live shadow snapshots with zero unexplained mismatch | Offline 100,000-frame projector parity and the 115,914-frame live target-count gate remain exact. The separate serialized production corpus records 100,000 requested/accepted/acknowledged/released positive frames plus three corrupt probes across four epochs, using the complete WNC1 view, real server/client hooks, deferred expectation, semantic admission, sole cgame timeline publication, prediction selection, reverse ACK, and retention release. Sender retention now preserves an attempted frame through ordinary semantic-ACK latency, coalesces newer projections, and uses bounded expiry recovery. Three target-only private `0x57` live repeats show positive queue/ACK/release and actual safe remote-transform authority with zero semantic or transform errors. The larger matrices remain single-platform/focused; broader recovery, allocation/packet/CPU/load budgets, public authority, platform, and release gates remain pending | Incomplete |
| `FR-10-T07` | Cgame-owned immutable timeline, valid interpolation/extrapolation/discontinuity behavior, and ordered deduplicated event dispatch on live presenters | Timeline, remote-transform audit/opt-in parity promotion, present-once audit cursor, default-off event authority, and hard-resync signaling exist. Private `0x57` gives the native receiver sole canonical timeline publication ownership for the readiness epoch, with fresh receipts gating ACKs and uncertainty quarantining the epoch. The corpus extends immutable admission and pure prediction-authority selection from four focused views to 100,000 positives across four epochs. Source-gated mode 3 now uses that engine-owned timeline as actual safe remote-transform presentation authority; three hidden-client repeats record 303/303/261 samples, 84/287/189 native promotions, and zero clock/pair/alignment/parity/event-fence/endpoint/transform failures. Previous-only entities, event/effect/audio presenters, adaptive extrapolation, broad correction/load, demos, and performance evidence remain absent. Evidence: `docs-dev/fr-10-t07-t08-t09-canonical-snapshot-prediction-authority-2026-07-16.md`, `docs-dev/fr-10-t04-t06-t07-t08-t09-t14-serialized-production-snapshot-corpus-2026-07-17.md`, `docs-dev/fr-10-t04-t06-t07-t08-t09-t14-production-v2-pmove-replay-boundary-2026-07-17.md`, and `docs-dev/fr-10-t06-t07-t14-native-snapshot-presentation-authority-2026-07-18.md` | Incomplete |
| `FR-10-T08` | Full eligible movement/weapon prediction, authoritative consumed-command replay, predicted-side-effect suppression, correction telemetry, and hard-resync behavior | Existing movement replay, fail-closed recovery, Hook request/authority receipt, and local-action audit cores remain valid. The corpus selects 100,000 exact canonical authorities and resolves 250,500 locally fabricated commands. The production-linked focused gate now executes the extracted shipped engine V2 request constructor, real `CL_PredictMovement`, and shared PMove from one admitted receipt. It proves pending/finalized state/collision/replay-hash parity, a legal 127-command replay, 128-command fail-closed ring clearing, one bounded correction, and one threshold hard reset (`v2_replays=4`, `oracle_matches=3`, digest `abf2723d5f03cbe9`). A pointer-free catalog now freezes all 22 ordinary weapons with exact sgame `Weapon`/item/ammo mappings, legacy-driver class, separate Grapple ownership, shadow-only status, per-catalog observation counters, and digest `e857eec08cfa9c00`. A second profile with digest `4f723f6fddf5bf52` freezes the complete legacy frame skeleton and is consumed by all 22 production wrappers. A third exact callback-capability descriptor with digest `a5d823b554b31ee8` classifies trigger/emission families, ammo/emission bounds, direct dependencies, and per-weapon V2 blocker masks; its audit proves zero of 22 are V2-representable. A bounded observation-only lease now gives the latest authenticated command at most one later server-frame weapon advance, expires on all frame exits, retains exact scoped/leased records, and joins them only across semantic-command equality, exact non-timer state, and equal bounded relative-timer decay. Three live hidden-client Blaster repeats prove offers, supersedes, claims, expiries, exact scoped/leased/joined records, and zero rejection. A descriptor-complete 528-byte shared shadow model binds each joined record to the exact catalog/profile/semantics facts, digests, comparable ammo delta, blocker mask, and hashes; its 22-entry golden is `1270244792631122`, and three schema-v30 live repeats prove Blaster catalog `1`, flags `127`, exact blocker mask `4367`, and nonzero record hashes. A compact 64-byte default-off receipt now crosses a bounded backpressure-safe/reconnect-reset engine mailbox and private native event payload kind `12`; the 128-entry cgame audit owner accepts command-first and receipt-first order only on exact canonical command-hash equality. A production-wrapper native virtual link proves exact match, zero presentation, ACK, and server release. Three schema-v32 live repeats now prove in-session reconnect rebase, epoch-3 exact joined Blaster shadows, 45/45 cross-process receipt matches, zero reconciliation errors, and normal 15 damage. Audiovisual suppression/de-duplication, sustained correction budgets, and legacy/native gameplay/presenter promotion remain incomplete. Evidence: `docs-dev/fr-10-t08-private-hook-authority-receipt-native-virtual-link-2026-07-16.md`, `docs-dev/fr-10-t07-t08-t09-canonical-snapshot-prediction-authority-2026-07-16.md`, `docs-dev/fr-10-t04-t06-t07-t08-t09-t14-serialized-production-snapshot-corpus-2026-07-17.md`, `docs-dev/fr-10-t04-t06-t07-t08-t09-t14-production-v2-pmove-replay-boundary-2026-07-17.md`, `docs-dev/fr-10-t08-t09-rerelease-weapon-catalog-boundary-2026-07-17.md`, `docs-dev/fr-10-t08-t09-weapon-semantics-v2-blocker-audit-2026-07-17.md`, `docs-dev/fr-10-t08-t09-post-command-weapon-observation-lease-2026-07-17.md`, `docs-dev/fr-10-t08-t09-post-command-weapon-lease-runtime-acceptance-2026-07-17.md`, `docs-dev/fr-10-t08-t09-descriptor-complete-local-action-shadow-2026-07-17.md`, `docs-dev/fr-10-t04-t05-t08-t09-local-action-shadow-authority-receipt-2026-07-17.md`, and `docs-dev/fr-10-t04-t05-t08-t09-t14-live-reconnect-local-action-authority-2026-07-18.md` | Incomplete |
| `FR-10-T09` | Canonical command identity/timing, server-consumed watermark, idempotent duplicate handling, legacy/native mapping, and shared prediction/rewind/event correlation | Legacy carrier and live consumed cursor drive prediction/rewind; the default-off native pilot retains 453 exact mapped command/ACK/release cycles, and the dedicated large-gap report covers the reproduced 161/401-command ranges. The corpus binds 100,000 admitted snapshots to server-consumed cursors and resolves continuous ranges through 127 commands. The focused runtime continuation now builds engine history through the production identity owner, passes the exact admitted cursor into the shipped V2 resolver, replays 127 commands through cgame/PMove, and rejects 128 without packet-ACK fallback. The local-action cgame owner additionally correlates a fieldwise private authority receipt against the exact semantic hash of the canonical command in either arrival order; mismatch/conflict/expiration fail closed into shared resync. Three live reconnect repeats prove one real correlation row with epoch-3 exact command records and 45/45 receipt matches. Exhaustive native mapping, wrap/flood, long-horizon live cursor evidence, complete event correlation, and full runtime/load acceptance remain absent. Evidence: `docs-dev/fr-10-t09-t16-headless-command-gap-acceptance-gate-2026-07-15.md`, `docs-dev/fr-10-t07-t08-t09-canonical-snapshot-prediction-authority-2026-07-16.md`, `docs-dev/fr-10-t04-t06-t07-t08-t09-t14-serialized-production-snapshot-corpus-2026-07-17.md`, `docs-dev/fr-10-t04-t06-t07-t08-t09-t14-production-v2-pmove-replay-boundary-2026-07-17.md`, `docs-dev/fr-10-t04-t05-t08-t09-local-action-shadow-authority-receipt-2026-07-17.md`, and `docs-dev/fr-10-t04-t05-t08-t09-t14-live-reconnect-local-action-authority-2026-07-18.md` | Incomplete |
| `FR-10-T10` | Server-authenticated time mapping, bounded player and mover pose history, non-mutating historical scenes, deterministic observability, and CPU/memory budgets | Player and live `SOLID_BSP` mover history, mover-relative provenance, sealed historical brush dispatch, and immutable BSP provider parity are now present. A three-run dedicated-server V5 gate loads a packaged rotating collision fixture, arms a real bot on its `MoveType::Push` brush, lets twelve normal frames invoke physical pusher motion and end-frame capture, then proves paired history has changed mover angle and rider origin with exact relative provenance. The newest real pair is also accepted as exact bot/brush candidates in a sealed immutable canonical scene. The scoped trace still proves the clear current baseline, rotation-sensitive negative control, historical provider block at fraction 0.374756, and restored authority state. A separate three-repeat real-command Railgun gate moves the live brush out of the lane after authenticated mapping, requires a clear substituted current baseline, then proves the concrete production pierce hit stops on the generation-matched sealed mover and leaves the target at exact zero damage. Broader player-on-mover/continuous-rotation/complex-map cases, engine weapon/mover fairness, and CPU/memory/load budgets remain absent | Incomplete |
| `FR-10-T11` | Every supported hitscan weapon uses one fair authoritative rewind policy with shooter/target/mover/lifecycle scenarios, documentation, abuse/load gates, and safe fallback | Eight policy tags route through the sealed player scene and 120 common-core cases pass; a three-repeat real `fire_rail` gate proves invalid-ack no-damage fallback plus near, in-budget, and capped legacy acknowledgement historical-hit-to-current-damage rows (exactly 30 total damage and unchanged query authority). A separate three-repeat, real-command two-client fixture proves normal Railgun policy `5`/80-damage, machinegun policy `1`/8-damage, Chaingun policy `2`/18-damage (full three-round burst), Super Shotgun policy `4`/120-damage (two ten-pellet barrels), shotgun policy `3`/48-damage, Disruptor convergence policy `6`/45-damage after normal projectile and daemon lifecycle, and real held first, three-tick, bounded release, water-crossing, and bounded 32-tick modes for Plasma Beam policy `7`/8 then 24/4/256 damage and dry-world Thunderbolt policy `8`/8 then 24/4/256 de-duplicated-footprint damage, all at positive age with unchanged live query geometry. A distinct real-command Railgun mover mode passes three times at 56 ms: 64 mover samples, clear current baseline, explicit production pierce hit on the sealed mover, unchanged collision authority, and exact zero target damage. Remaining fairness, moving-target/multi-target, broader movers/lifecycle, abuse/load, platform, and release gates remain absent | Incomplete |
| `FR-10-T12` | Explicit independent policies and deterministic scenarios for projectile, melee, beam, splash/radius, mover, deployable, trigger, and cooperative interactions | Narrow convergence and continuous-beam query coverage exist, and three-repeat real-command first-tick, three-tick, bounded release, water-crossing, and bounded 32-tick modes prove Plasma Beam policy `7` and dry-world Thunderbolt policy `8` historical selection with exact live 8 then 24 then halved 4 then 256 damage, no post-release damage over 250 ms, and no received release during the sustained hold. Three further headless real-command repeats prove Thunderbolt's distinct current-authority underwater branch: a bounded world-water `pointContents` state, all eight Cells drained, explicit production-branch observation, and exact 70 self damage; no historical beam trace is claimed for the branch that returns before tracing. Three staged real-command Disruptor repeats additionally prove a 100 ms-capped mapped-command-age spawn forward through a current-world projectile-hull trace: every run records authenticated/advanced 56 ms, no current-world block, historical convergence, and normal exact 45 current-authority delayed damage. Three more staged real-command Rocket Launcher repeats prove its separate policy `9`: after history capture the live target moves 32 units along the aim ray; each report records `canonical_historical_hit=0`, a clear authenticated/advanced 56 ms current-world hull sweep, and exact 100 current-authority direct damage. A further three-repeat Rocket splash gate creates a current-world BBOX impact blocker after history capture, moves the target off the ray, and requires the normal Rocket touch plus RadiusDamage path to cause exact reduced 58 splash damage; it binds the accepted Rocket entity and requires no historical hit plus the same unblocked 56 ms forward proof. Three staged real-command Plasma Gun direct-hit repeats prove separate policy `10`: ordinary muzzle clearance first, no historical impact, clear authenticated/advanced 56 ms current-world hull spawn-forward, and normal exact 20 direct damage. A distinct three-repeat Plasma Gun radius-execution mode creates a present-world blocker after history capture, requires exact normal Plasma touch, and stages a small clear-side damageable current-world target for unchanged `RadiusDamage`. Every run retains no historical impact, authenticated/advanced 40–56 ms current-world forward, and exact seven-damage falloff. This proves no player-hull splash, map/BSP occlusion, mover, water, multi-target, fairness, or load behavior. Evidence: `docs-dev/fr-10-t12-plasma-gun-current-world-splash-acceptance-2026-07-16.md`. Three standard-Blaster direct-hit repeats prove shared Blaster/HyperBlaster policy `11`: ordinary muzzle clearance, no historical impact, clear authenticated/advanced 56 ms current-world hull spawn-forward, and normal exact 15 direct damage. Finally, three Chainfist hybrid-melee repeats prove policy `12`: one historical reach/FOV player candidate under the accepted canonical command, then a 64-unit current displacement guard plus ordinary current-world `CanDamage`/`Damage`; each run records 56 ms authenticated age, historical eligibility, accepted 64-unit displacement, unchanged live geometry, and exact 15 normal damage. HyperBlaster cadence/radius, moving/multi-target melee, other melee weapons, mover contacts, indefinite holds, underwater-radius behavior, and most declared mechanics remain unimplemented | Incomplete |
| `FR-10-T13` | Legacy and modern demo/MVD/GTV/spectator record/play/seek/relay matrices reproduce canonical order and reset timelines safely | Client demo cursor/clock preservation and seek lineage exist; MVD/GTV, view switching, native recording, event reproduction, and full matrices remain absent | Incomplete |
| `FR-10-T14` | Complete telemetry catalog, 1/8/16/32-client performance profiles, 100,000 malformed cases per changed decoder/range, allocation/budget proof, stress/soak, and machine-readable evidence | Existing corpus evidence remains valid: 100,003 serialized views, exact positive ACK/release, 250,500 resolved commands, bidirectional impairment coverage, three corrupt rejections, and golden `c6aee48df85341ab`. The focused replay continuation adds engine-import result/source/flag/range diagnostics and deterministic replay/collision/correction counters, including one withheld receipt, four V2 replays, three oracle matches, 127 success, and 128 reset. The local-action receipt adds fixed 32-entry server mailboxes and 128-entry cgame correlation storage with fail-closed capacity/conflict/expiration coverage. Schema-v32 adds three machine-readable live reconnect rows with exact joined-shadow fields, receipt parity, and lifecycle counters. The final production build, 157/157 headless networking suite in 383.93 seconds (corpus row 289.24 seconds), 16/16 package tests, 1/1 release contract, and refreshed validated 524-asset Windows stage pass. The corpus still uses one profile, dynamic vectors, synthetic within-burst receive sequencing, eight entities/four area bytes, and no real sockets; allocation proof, cross-snapshot reorder, 100,000 malformed cases, 1/8/16/32 clients, load/soak, and cross-platform gates remain absent | Incomplete |
| `FR-10-T15` | Three full matrices on every release platform, two-hour 32-client soaks, seven-day/100-server-hour opt-in evidence, rollback drills, current docs, and staged payload | User controls are current and the final Windows `.install` refresh/stage validates 16 root runtime files, one dependency, a 483-member `pak0.pkz`, 31 botfiles, 215 RmlUi assets, one loose q2aas reference BSP, and five release notice sidecars; platform matrices, durations, rollout hours, and drills remain absent | Incomplete |
| `FR-10-T16` | Fresh-input age, loss recovery, idempotence, bandwidth, command-flood, and cadence evidence on both legacy and native adapters | Deterministic controller and default-off legacy batched integration pass a short impaired loopback, and the native command shadow retains its 453 exact sampled matches/releases. The private `0x73` event mode still uses live mixed packing and scheduler wakeups, while a fresh negotiated epoch now terminally cancels exhausted old DATA/receipt work instead of depending on unbounded ACK retries or a one-bank rollover. The wrapper gate proves bounded retry convergence for ordinary loss and explicit counted termination after all three ACK credits are spent; a second rotation has no bank overwrite. Adaptive native batching/redundancy, the full production impairment matrix, server feedback, a dedicated accepted gap report, and full bandwidth/budget/load evidence remain absent | Incomplete |

## 2026-07-16 `FR-10-T04/T05/T06/T07` Stage D addition

The native snapshot path now has a bounded semantic-admission transaction.
WNC1 snapshot V2 remains one complete reconstructed view, including when
`base_id` is nonzero; that field records lineage and is not a decode
dependency. The existing 1,200-byte datagram contract is unchanged, while the
opaque WNE1 message limit is now 131,072 bytes across at most 128 fragments.
This covers the full legal 80,869-byte projection with 512 event references.

Admission requires the exact independently observed snapshot ID plus five
cross-endpoint proofs: legacy parity and player, entity, area, and event
semantic hashes. The endpoint hash is local chronology/provenance and is
validated against the decoded native cgame receipt after consumption.
Session, retained-slot, and ACK-ledger changes remain staged until cgame
consumes the immutable view and returns a fresh ABI-V2 timeline receipt. The
event runtime must report the
same snapshot epoch healthy and not require resync, including for a snapshot
with no event references. Legacy-inferred event ranges also pass the
correctness fence with `cg_event_runtime_audit=0`: structure, dense ordering,
semantic revision, and event hash are validated while the audit-only join
table remains untouched. Exact committed repeats revalidate that receipt
without duplicate consumption. Uncertainty clears the consumer, retires every
snapshot/event/descriptor receipt, quarantines the epoch, and requires a
keyframe.

An actual `SV_SnapshotShadowViewV1` event-carrying keyframe and
base-referenced complete update pass reversed multi-fragment WNE1/WTC1
delivery through the real canonical cgame timeline/event runtime with exact
cross-endpoint parity and local native endpoint receipt. Endpoint-only
expectation divergence remains legal; an exact-ID, legacy-parity, or semantic
mismatch cannot ACK and leaves transport and cgame uncommitted. This is a
production-shaped in-process foundation, not live production server TX/client
RX authority. Legacy q2proto snapshots and presentation remain authoritative,
the public offer remains unchanged, and `q2proto/` is untouched. Evidence:
`docs-dev/fr-10-t04-t06-native-snapshot-semantic-admission-2026-07-16.md`.

## 2026-07-16 `FR-10-T04/T06/T07` production adapter continuation

The default-off private snapshot lane now connects the exact final per-peer
server projection to the real server TX hook and real client RX hook. Exact
private mode `0x57` carries the canonical snapshot epoch independently from
the transport epoch in a 15-pair readiness record. At this milestone event
`0x73` and snapshot `0x57` were mutually exclusive and combined `0x77` was
defined but unsupported; the 2026-07-18 combined-lane addition below supersedes
that historical limitation.

The server owns one supersedable retained slot and two immutable encoded
payload banks so a newer projection can coalesce without mutating an active
multi-fragment dispatch. The client owns bounded reassembly, a rolling
16-entry exact expectation cache, semantic admission, the real cgame consumer,
and the semantic ACK ledger. The cache protects active-RX expectations,
evicts only safe older entries, and returns retry-later rather than draining
when no safe victim exists. Native DATA may complete before the legacy
observation, but it cannot ACK or publish until the exact independently
reconstructed expectation exists. Complete core-RX expiry is reconciled with
wrapper pending descriptors before later DATA is interpreted.

During the private epoch the native receiver is the sole canonical cgame
timeline publisher. The legacy frame still renders and remains gameplay
authority, but its canonical projection is comparison-only. Diagnostic DRAIN
and hook corruption/loss keep that ownership latched rather than introducing
a second producer mid-epoch. The transport fails closed and detaches only
exact owned hooks; map/serverdata or matching connection boundaries release
the latch.

The registered production-hook virtual link passes with deterministic digest
`7176afa3d4eb62b2`. It reports `fragmented=8`, `s2c_loss=1`,
`reordered=13`, `duplicates=1`, `ack_loss=1`, `repeat_revalidate=1`,
`cgame_once=4`, `hash_quarantine=1`, `wrong_epoch=1`, `real_domains=4`,
`expectation_rollovers=1`, `timeout_recoveries=1`, and
`prediction_ready=4`. Coverage includes high identities 1023/8191, 24 legacy
expectations before DATA, complete-timeout slot reuse, one lost reverse ACK,
exact repeat revalidation, exactly-once cgame publication, server retention
release, five-proof hash quarantine, wrong-snapshot-epoch rejection, and four
exact admitted snapshot-to-prediction-authority selections. The complete
headless networking suite passes 147/147, the
full Windows build passes, and the refreshed Windows stage validates 16 root
runtime files, one dependency, one loose q2aas reference BSP, a 483-member
archive, 31 botfiles, 215 RmlUi assets, and five notice sidecars. Evidence:
`docs-dev/fr-10-t04-t06-t07-production-native-snapshot-adapters-2026-07-16.md`.

## 2026-07-16 `FR-10-T07/T08/T09` canonical prediction-authority continuation

Cgame can now resolve one exact immutable canonical snapshot and the exact
server-consumed command cursor carried by it as one prediction-authority
transaction. A generation-checked timeline helper copies the retained
snapshot/player pair by value, and the engine's V2 prediction-input import
resolves a canonical-only command range beginning at that copied cursor. It
cannot substitute packet acknowledgement when canonical authority is required.

The pure selector verifies complete and promotion-eligible snapshot state,
snapshot/player hashes, exact epoch/sequence/tick/time/control alignment,
controlled-entity generation and provenance, consumed-cursor provenance, and a
complete continuous canonical input range. The non-archived
`cg_prediction_snapshot_authority` control remains default `0`; mode `1` audits
against legacy authority, while mode `2` promotes only after every invariant
passes. Promotion failure blocks legacy replay, clears retained prediction
rings, and records a classified hard resync rather than silently mixing
authorities.

The production snapshot-hook link now carries a deterministic server-consumed
cursor through final projection, native codec/transport, semantic admission,
cgame timeline publication, immutable copy-out, V2 input resolution, and the
selector. Each of four successful admissions becomes prediction-ready exactly
once. The result reports `prediction_ready=4` with digest
`7176afa3d4eb62b2`; focused prediction-contract coverage passes 4/4, the
production target passes 1/1, and the then-current registered networking suite
passed 147/147. This proves exact authority availability with a zero-pending
production range, not sustained replay under long impairment. Default
promotion, weapon/action prediction, presentation deduplication, correction
budgets, long-horizon command identity, and, at that milestone, the
100,000-frame serialized production corpus remain open. Evidence:
`docs-dev/fr-10-t07-t08-t09-canonical-snapshot-prediction-authority-2026-07-16.md`.

Task status and accounting are unchanged: overall **74/190 complete (38.9%)**
with **116 open**, and FR-10 **3/16 complete (18.75%)** with **13 open**.
`FR-10-T07`, `FR-10-T08`, `FR-10-T09`, and `FR-10-T14` remain incomplete.

## 2026-07-17 `FR-10-T04/T06/T07/T08/T09/T14` serialized production corpus

The fixed-seed corpus connects the exact server final projection and snapshot
shadow to the bounded native sender, WNC1/WNE1/WTC1, production `NetImpair`,
the real client receiver and semantic-admission owners, sole native cgame
timeline publication, locally constructed prediction-input resolution, the
pure prediction-authority selector, reverse semantic ACK, and exact server
retention release.

Across four activated epochs, each of two normalized runs records 100,000
requested, accepted, acknowledged, released, and prediction-selected positive
frames. It resolves 250,500 commands across 33,334 pending and 66,666
non-pending ranges, includes four `{epoch,0}` bootstraps, 99,996 nonzero
cursors, four 127-command ranges, one deliberate 128-command rejection, and 43
history resets. Three additional corrupt-carrier probes make 100,003 serialized
views; all three corrupt carriers reject, and accepted positive abandonment is
zero. The checked-in golden is `c6aee48df85341ab`, normalized JSON SHA-256 is
`a35973b39947387d7454a45650d5f9489e0ed136158e55177d9770c904444d38`,
and the final registered corpus row passes inside the 149/149 headless suite in
382.30 seconds. Evidence:
`.tmp/networking/native_snapshot_production_corpus/evidence.json` and
`docs-dev/fr-10-t04-t06-t07-t08-t09-t14-serialized-production-snapshot-corpus-2026-07-17.md`.

This is resolver/selector evidence over locally fabricated command history. It
does not execute `cg_predict`, PMove, weapon prediction, or the engine V2
request-construction boundary. It uses one impairment profile, dynamic harness
vectors, within-burst fragment scheduling with synthetic incoming sequencing,
and a fixed eight-entity/four-area-byte snapshot. It therefore does not prove
allocation-free operation, cross-snapshot netchan reorder, real sockets,
multi-client load/soak, the 15-profile matrix, full semantic breadth, or
cross-platform acceptance. The final production build, 149/149 headless
networking suite, 11/11 qualifier unit tests, 16/16 package tests, 1/1 release
headless contract, and refreshed/validated Windows stage pass. The stage has
16 root runtime files, one dependency, a 500-file pak, 31 botfile payloads,
215 RmlUi assets, and one q2aas reference map; these focused Windows results do
not satisfy the missing release-platform, load, soak, or malformed matrices.

Task status and accounting remain unchanged: overall **74/190 complete
(38.9%)** with **116 open**, and FR-10 **3/16 complete (18.75%)** with **13
open**. `FR-10-T04`, `FR-10-T06`, `FR-10-T07`, `FR-10-T08`, `FR-10-T09`, and
`FR-10-T14` remain Incomplete. This milestone closes zero project tasks.

## 2026-07-17 `FR-10-T04/T06/T07/T08/T09/T14` production V2 PMove boundary

The focused production virtual-link gate now carries one exact native snapshot
past resolver/selector availability and through the shipped engine/cgame replay
boundary. The V1/V2 resolver was extracted from `src/client/cgame.cpp` into one
production-owned module used by both `CG_GetExtension` and the gate. Cgame's
movement configuration was likewise extracted so the real replay path and
independent oracle consume the same production settings.

Canonical prediction copy-out now requires an immutable per-slot receipt bound
to the timeline generation, admission generation, snapshot ID/hash, consumed
cursor, tick/time, and controlled-entity generation/provenance. The receipt is
created only after both timeline publication and the event fence succeed. A
forced wrong-event-epoch case proves that a timeline-only publication remains
available to diagnostic snapshot copy while prediction copy-out fails with
`NOT_FOUND`.

The hidden, stdin-disabled, input-free gate executes four real cgame replay
calls and records pending/finalized parity, three state/collision oracle
matches, one bounded correction, one threshold hard reset, one legal
127-command replay, and one fail-closed 128-command exhaustion that clears all
prediction rings. The result is `receipt_fence_blocks=1`, `v2_replays=4`,
`oracle_matches=3`, `state_hash=f49cc087878cc954`,
`collision_hash=0f918c82efaaa180`, `replay_hash=c5830959d33890a6`, and digest
`abf2723d5f03cbe9`. The shared-source 100,000-frame corpus target now links the
same production replay dependencies and builds independently; a 1,000-frame
smoke retained exact positive acceptance/ACK/release/authority totals.

Final integration validation passed: the complete production build, 149/149
headless networking tests in 224.8 seconds (corpus row 209.08 seconds), 16/16
package tests, 1/1 release headless contract, and the refreshed/validated
Windows stage. The stage contains 16 root runtime files, one dependency, a
507-file pak, 31 botfile payloads, 215 RmlUi assets, and one q2aas reference
map. `q2proto/` remained untouched.

This is focused movement replay/reconciliation evidence. It does not close the
weapon/action catalogue, predicted presentation de-duplication, sustained
correction budgets, dual-adapter impairment/load, demo, soak, malformed, or
platform matrices. Evidence:
`docs-dev/fr-10-t04-t06-t07-t08-t09-t14-production-v2-pmove-replay-boundary-2026-07-17.md`.

Task status and accounting remain unchanged: overall **74/190 complete
(38.9%)** with **116 open**, and FR-10 **3/16 complete (18.75%)** with **13
open**. This milestone closes zero project tasks.

## 2026-07-16 `FR-10-T10/T11` acceptance addition

A new real-command two-client Railgun mode proves one bounded historical-mover
occlusion seam. After authenticated command mapping, the fixture moves only
the live rotating `SOLID_BSP` brush out of the shot lane. The unchanged
production Railgun path then requires a clear substituted current baseline,
and its concrete pierce-hit callback must terminate on the generation-matched
sealed historical mover. The preliminary `P_ProjectSource` convergence trace
cannot satisfy the terminal proof. Collision authority remains unchanged and
the target behind the mover takes exact zero damage.

Three hidden, input-disabled repeats pass at 56 ms applied age with 64 mover
history samples. Participant disconnect or respawn also fails the fixture and
restores its live mover immediately. This narrows the missing
engine-weapon/mover evidence but does not satisfy moving
shooter/target/rider, multiple-mover, broader mover class, piercing ordering,
lifecycle, fairness, abuse/load, platform, or release requirements. Evidence:
`docs-dev/fr-10-t10-t11-canonical-rail-historical-mover-occlusion-acceptance-2026-07-16.md`.

## 2026-07-16 `FR-10-T12` acceptance additions

Chainfist policy `12` now proves a bounded hybrid seam: one historical
reach/FOV player candidate under the accepted canonical command, a 64-unit
live displacement guard, and normal current-world `CanDamage`/`Damage`
authority. Three headless repeats record 56 ms authenticated age and exact 15
normal damage.

ETF flechette policy `13` now proves a distinct bounded current-world
spawn-forward seam: no historical impact, a clear authenticated 40–56 ms
advance after the ordinary muzzle probe, and exact 10 normal direct damage in
three headless repeats. These results narrow two interaction classes only;
the task remains incomplete pending broader projectile, melee, mover, radius,
trigger, cooperative, fairness, and load matrices. Evidence:
`docs-dev/fr-10-t12-chainfist-hybrid-melee-acceptance-2026-07-16.md` and
`docs-dev/fr-10-t12-etf-flechette-spawn-forward-acceptance-2026-07-16.md`.

Plasma Gun policy `10` now also has a narrow current-world radius-execution
seam. A real bounded spawn-forward projectile must touch an exact blocker
created after history capture; only then does the fixture create a small
clear-side damageable current-world target for unchanged `RadiusDamage`.
Three hidden, input-disabled repetitions retain no historical impact and
record authenticated unblocked 40–56 ms forward plus exact seven-damage
falloff. This is not player-hull splash or map/BSP occlusion evidence.
Evidence:
`docs-dev/fr-10-t12-plasma-gun-current-world-splash-acceptance-2026-07-16.md`.

HyperBlaster now has a narrow held-cadence acceptance seam for shared policy
`11`. Three headless real-command repeats prove the first normal 6–11
gun-frame bolt with no historical impact, a clear authenticated/advanced 56 ms
current-world forward sweep, unchanged current geometry, and exact 15 direct
damage. Its production cadence, collision, damage, and optional radius
authority remain unmodified; sustained cadence and ruleset-radius matrices
remain open. Evidence:
`docs-dev/fr-10-t12-hyperblaster-cadence-spawn-forward-acceptance-2026-07-16.md`.

Phalanx policy `14` now proves a bounded current-world shell-spawn seam.
The post-command Generic callback can consume only a short-lived,
server-authenticated decision bound to the same shooter life, weapon, policy,
map epoch, expiry, and bounded launch count. It transports no historical
collision or damage result. The current-world forward hull sweep remains
capped at 100 ms; normal `phalanx_touch` keeps direct damage and `RadiusDamage`
splash authority. Three headless repeats retain six target-history captures,
claim no historical impact, record clear authenticated/advanced 56–72 ms
current-world sweeps, and apply exact 80 normal direct damage. A shared
current-world splash fixture now moves the target off the shell ray, creates a
non-damageable blocker only after history capture, and requires the normal
shell identity to strike it before `phalanx_touch`/`RadiusDamage` applies the
exact two-barrel 93-damage splash total (48 then 45). Three headless repeats
retain no historical impact and clear 56–72 ms forward proof. Broader
occlusion and interaction matrices remain open. Evidence:
`docs-dev/fr-10-t12-phalanx-current-world-spawn-forward-acceptance-2026-07-16.md`
and
`docs-dev/fr-10-t12-phalanx-current-world-splash-acceptance-2026-07-16.md`.

Grenade Launcher policy 15 now proves a bounded current-world ballistic spawn
seam and first normal splash impact. Only accepted, 100 ms-capped
canonical-command age may be consumed through stepped gravity-before-move
current-world hull sweeps. Any contact fails closed to the ordinary Toss spawn;
the resolver cannot invent a bounce, touch, or historical impact. A
post-history, present-world damageable BBOX blocker and off-axis target require
the accepted normal grenade to use unchanged touch/explosion/RadiusDamage
authority. Three headless repeats record no historical impact, clear
authenticated 40–56 ms progress, and 58 normal splash damage within the
closed 57–60 production range. Bounce, fuse, mover, water, deployable, trigger,
cooperative, fairness, and load matrices remain open. Evidence:
`docs-dev/fr-10-t12-grenade-launcher-ballistic-current-world-splash-acceptance-2026-07-16.md`.

Hand Grenade policy 16 now proves a separate release-only bounded
current-world ballistic spawn-forward seam. Only the first authentic
attack-to-no-attack edge may leave a short-lived authorization, and it is bound
to the original shooter, life, command, weapon, policy, map epoch, expiry, and
single ordinary non-held throw. Three dedicated/headless, input-disabled
repeats prove no historical impact or fallback, unchanged live geometry, and
clear authenticated/advanced 40–56 ms current-world gravity-stepped progress.
This no-contact fixture intentionally makes no splash or damage claim; bounce,
fuse, contact, splash, mover, water, deployable, trigger, cooperative,
fairness, and load matrices remain incomplete. Evidence:
`docs-dev/fr-10-t12-hand-grenade-release-ballistic-current-world-forward-acceptance-2026-07-16.md`.

Proximity Launcher policy 17 now also proves one bounded current-world
deployable lifecycle. Accepted command age advances only the new mine's clear
initial gravity path; any contact during that advance still fails closed to
ordinary Bounce behavior. After the clear advance, a non-damageable
current-world Push surface permits normal `prox_land`; only after its actual
trigger is linked is the isolated target staged. Three dedicated/headless,
input-disabled repeats record no historical impact, authenticated/advanced
56 ms flight, normal land/arm-delay trigger/visibility selection/delayed
explosion, and exact 61 ordinary `RadiusDamage`. The fixture supplies no
trace, contact, landing, target-selection, trigger, explosion, or damage
result. Arbitrary surfaces, water/lava, destruction, chain reactions,
multiple targets, movers, cooperative rules, fairness, and load matrices
remain incomplete. Evidence:
`docs-dev/fr-10-t12-prox-launcher-ballistic-deploy-forward-acceptance-2026-07-16.md`
and
`docs-dev/fr-10-t12-prox-launcher-lifecycle-current-world-acceptance-2026-07-16.md`.

BFG policy 18 now proves only its fresh projectile's bounded current-world
launch. Its normal frame-9-to-17 wind-up can consume one accepted
shooter/life/weapon/policy/map-epoch/command-bound authorization for at most
1.25 seconds, while the actual advance remains capped at 100 ms and traces the
current world only. Three input-disabled headless repeats record 664, 744, and
680 ms authenticated age, clean unblocked clamped advances, no historical
impact, and unchanged current geometry. BFG laser, contact, staged explosion,
radius, and damage remain current-world production authority and are not
claimed by this narrow seam. Evidence:
`docs-dev/fr-10-t12-bfg-current-world-spawn-forward-acceptance-2026-07-16.md`.

Ion Ripper policy 19 now proves only the initial bounded current-world advance
of all fifteen bolts created by one normal randomized burst callback. The
live-shooter/weapon/policy/map-epoch/command-bound authorization permits
exactly those fifteen launches; each remains capped at 100 ms and selects no
historical impact, collision, target, damage, effect, or lifetime result.
Three input-disabled headless repeats record all 15/15 clear 56 ms advances,
no historical impact, unchanged current geometry, and explicit termination of
the dedicated server and both clients. Contact, damage, effects, lifetime, and
broader interaction/fairness/load coverage remain current-world production
authority and are not claimed by this narrow seam. Evidence:
`docs-dev/fr-10-t12-ion-ripper-burst-current-world-forward-acceptance-2026-07-16.md`.

Tesla Mine policy 20 now proves only a fresh mine's initial clear
current-world gravity advance after a real attack-to-release edge. Its
single-launch authorization is bound to the live shooter, weapon, policy, map
epoch, accepted release command, and expiry; another prime invalidates it.
Three input-disabled headless repeats record no historical impact, unchanged
current geometry, no block, and 56, 56, and 40 ms advances, with explicit
dedicated-server and client termination. Landing, activation, target scans,
chains, damage, effects, destruction, expiration, and broader interaction,
fairness, and load coverage remain current-world production authority and are
not claimed by this narrow seam. Evidence:
`docs-dev/fr-10-t12-tesla-mine-release-ballistic-deploy-forward-acceptance-2026-07-16.md`.

Trap policy 21 now proves only a fresh Trap's initial clear current-world
gravity advance after a real attack-to-release edge. Its single-launch
authorization is bound to the live shooter, weapon, policy, map epoch,
accepted release command, and expiry; another prime invalidates it. Three
input-disabled headless repeats record no historical impact, unchanged current
geometry, no block, and 56 ms advances, with explicit dedicated-server and
client termination. Landing, capture/release, destruction, expiration, and
broader interaction, fairness, and load coverage remain current-world
production authority and are not claimed by this narrow seam. Evidence:
`docs-dev/fr-10-t12-trap-release-ballistic-deploy-forward-acceptance-2026-07-16.md`.

Grapple policy 22 now proves only a fresh normal hook's clear current-world
advance after its normal frame-six callback. One
shooter/life/weapon/policy/map-epoch/command-bound authorization can remain
eligible for 750 ms to accommodate the ordinary wind-up, while the actual
physical advance is still capped at 100 ms. Three input-disabled headless
repeats record 40, 56, and 56 ms advances, no historical impact, unchanged
current geometry, no block, and explicit dedicated-server and client
termination. Hook contact, attachment, pull, cable, damage, reset, detach,
lifecycle, mover, and cooperative behavior, plus the off-hand `Hook` command,
remain current-world production authority and are not claimed by this narrow
seam. Evidence:
`docs-dev/fr-10-t12-grapple-hook-current-world-spawn-forward-acceptance-2026-07-16.md`.

ProBall policy 23 now proves only a fresh ball's clear current-world gravity
advance after the ordinary Chainfist-held attack-to-release edge. Its
single-launch authorization is bound to the live ProBall carrier,
Chainfist-plus-ball state, weapon policy, map epoch, and accepted release
command; a later prime invalidates it, and direct item-use passes remain
unchanged. Three input-disabled headless ProBall repeats record 72, 88, and
56 ms advances, no historical impact, unchanged current geometry, no block,
and explicit dedicated-server and client termination. Possession, pickup,
touch, re-grab, bounce, goals, scoring, teams, resets, and lifecycle behavior
remain current-world production authority and are not claimed by this narrow
seam. Evidence:
`docs-dev/fr-10-t12-proball-held-throw-current-world-ballistic-acceptance-2026-07-16.md`.

Native off-hand Hook policy 24 now proves only a fresh clear current-world
spawn-forward. Unlike the legacy `hook` string command, this path has a mapped
`+hook` user-command edge, which must be received from a live playing client
with off-hand Hook enabled and no active hook. It permits one authenticated,
100 ms-capped present-world hull advance only. The ordinary hook callback
retains contact, attachment, pull, damage, reset, detach, and lifecycle
authority; no historical collision or damage result is available. One debug
gate and three input-disabled headless dedicated-server/two-client repeats
record policy 24, an unblocked authenticated/advanced 56 ms sweep, no
historical hit/damage, and termination of every launched WORR process. This
narrows one input-identity seam only; the legacy string command and all broader
interaction, fairness, load, mover, trigger, and cooperative matrices remain
open. Evidence:
`docs-dev/fr-10-t12-offhand-hook-authenticated-current-world-acceptance-2026-07-16.md`.

## 2026-07-18 `FR-10-T04/T06/T14` combined native-lane addition

The default-off private readiness pilot now activates exact `0x77` when event
and snapshot controls are enabled at both endpoints. The public offer remains
`0x03`. A fair DATA scheduler alternates eligible event and snapshot work;
low-half event and high-half snapshot message-sequence lanes let ACK handling
classify records without ambiguity and prevent one class from releasing the
other. An ordinary packet whose legacy prefix is larger than the prefix frozen
for an active asynchronous dispatch now returns output-too-small and defers the
native append instead of draining the pilot.

Snapshot retention preserves an already-attempted frame through ordinary
semantic-ACK latency, coalesces newer projections into one pending frame, and
promotes that pending frame after release. A bounded 1,000-tick abandonment
rule recovers when the receiver has expired incomplete work and can no longer
ACK it. Snapshot epoch rebinding preserves admitted history and q2proto delta
bases, and server projection canonicalizes q2proto's direction-specific unions
before hashing and serialization.

Three schema-v33 two-process combined runs pass with 99/99 exact local-action
receipt matches. Both snapshot peers report positive semantic ACK and release
counts in every run; all server/client failure counters are zero. This closes
the simultaneous private event-plus-snapshot carrier proof only. Public
advertisement, sustained combined load, the full impairment/platform matrix,
and release promotion remain open. Evidence:
`docs-dev/fr-10-t04-t06-t14-combined-native-event-snapshot-shadow-2026-07-18.md`.

## 2026-07-18 `FR-10-T06/T07/T14` snapshot-presentation authority addition

Cgame now has source-gated `cg_snapshot_timeline_render=3`. The engine exposes
the read-only `cl_worr_native_snapshot_timeline_owned` latch only while its
private snapshot receiver owns the timeline epoch; mode 3 refuses promotion
without that ownership. It samples no later than the newest admitted native
server time, promotes safe remote transforms independently, retains per-entity
fallback, and leaves controlled-player prediction unchanged. Modes 0 through 2
retain their prior behavior.

Three hidden-client, input-disabled, two-process target-only private `0x57`
runs pass. They record 303/303/261 samples, 84/287/189 native-authority samples,
and exactly the same promotion counts. Snapshot queue/ACK/release, epoch, clock,
and pair counters are positive in every run. Clock, pair, alignment, parity,
event-fence, endpoint, and transform-error failures are all zero. This proves
actual remote-transform authority, not previous-only entity rendering,
event/effect/audio presenters, extrapolation/load breadth, demos, or release
promotion. Evidence:
`docs-dev/fr-10-t06-t07-t14-native-snapshot-presentation-authority-2026-07-18.md`.

Neither addition completes a parent task. The project accounting therefore
remains 74/190 Definition-of-Done checks complete, 116 open, and 3/16 parent
tasks complete.

## Current critical path

The project cannot close by independently polishing every subsystem. The
dependency-preserving critical path is:

1. Extend the live per-peer descriptor/reliable stream beyond the implemented
   legacy entities, visible/explicit-positional audio, and bounded mixed
   temporary/muzzle sequences to the remaining direct service families,
   reliable/positionless audio, combined/reliable muzzle traffic, and predicted
   local actions. Bind production events to snapshot fences before presenter
   cutover.
2. Extend the implemented negotiated epoch-cancellation proof from the explicit
   wrapper schedule to the production `NetImpair` model/golden, distinct events,
   rate/fragment pressure, and multi-process coverage. The snapshot lane now
   has one accepted two-run 100,000-frame production `NetImpair` corpus; expand
   it through broader semantic/keyframe recovery, engine-import and real
   prediction execution, dual-adapter recovery, allocation, load, and bandwidth
   budgets. Any true delta codec requires an explicit codec revision and
   retained-base reconstruction contract.
3. Complete the Rerelease local weapon/action catalog and run prediction and
   authority transactions in shadow using the now-measured cheap/deep audit
   split.
4. Build on proven generated-IBSP38 transformed collision parity to capture
   immutable mover poses and add historical brush queries without relinking
   live state.
5. Promote the canonical snapshot/event/cgame paths one subsystem at a time
   only after the accepted serialized corpus is joined by broad 100,000-frame
   live, full impairment, demo, real prediction/correction, budget, and
   presenter evidence.
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
  byte mappings under a 131,072-byte/128-fragment payload ceiling, including
  the full legal 80,869-byte/512-event snapshot. The base pilot carries a
  sampled command subset; private event mode carries descriptor and narrow
  final-emission event DATA. Private snapshot mode now carries the exact final
  server view through both real production hooks, deferred semantic admission,
  sole native cgame timeline publication, reverse ACK, and exact server
  retention release. The two-run corpus now continues through generation-
  checked snapshot/player copy-out, locally constructed canonical input
  resolution, and a pure prediction-authority selector for 100,000 positive
  observations with nonempty ranges and maximum replay 127. It remains an
  in-process, single-profile, eight-entity/four-area-byte fixture using dynamic
  harness vectors and within-burst scheduling with synthetic receive sequence.
  It does not execute `cg_predict`, PMove, engine V2 request construction, real
  sockets, multi-client load, soak, or cross-platform matrices. No capability
  is publicly advertised. Exact scope is recorded in
  `docs-dev/fr-10-t04-t06-t07-t08-t09-t14-serialized-production-snapshot-corpus-2026-07-17.md`.
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
  ordering, audit-off authority lifecycle plus audit-off legacy-inferred
  reference correctness, strict engine-owner epoch
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
- The final Windows stage contains 16 root runtime files, one dependency, 474
  assets, 31 botfiles, and 215 RmlUi assets. Its short runtime smoke accepts
  388/388 default and 386/386
  impaired cgame publications with zero mismatches/failures/rejections, but it
  is not a multi-client, long-session, or supported-platform release
  matrix.
- The networking suite and its repeat runs prove deterministic focused and
  integration behavior on the configured Windows Clang build. They do not
  replace live load, malformed-corpus, soak, or cross-platform acceptance.

## 2026-07-18 `FR-10-T04/T05/T08/T09/T14` live reconnect authority addition

The local-action authority slice now has repeated live process-boundary and
connection-lifecycle evidence. The schema-v32 gate launches a dedicated server
and two network-only headless clients, disconnects/reconnects the shooter,
observes a second `serverdata` handshake and command epoch 3, and then drives a
normal Blaster attack through production client command, sgame lease/callback,
private native event, cgame correlation, semantic ACK, release, collision, and
damage paths.

The three-repeat final report records exact commands `3:132`, `3:135`, and
`3:133`; Blaster catalog `1`, flags `127`, blocker mask `4367`, nonzero shadow
hashes, and exact scoped/leased/continuity/joined proof in every row. Aggregate
authority parity is 45 receipts and 45 matches with zero unmatched,
outstanding, mismatch, conflict, or resync. Each repeat applies exactly 15
normal Blaster damage. The fixture retains only immutable diagnostic value
copies after the exact join so its 32-row observation ledger may continue to
rotate without making RCON timing an acceptance dependency.

Evidence:
`docs-dev/fr-10-t04-t05-t08-t09-t14-live-reconnect-local-action-authority-2026-07-18.md`
and
`.tmp/networking/local-action-authority-reconnect-acceptance-final.json`.

Task status and accounting remain unchanged: overall **74/190 complete
(38.9%)** and **116/190 open (61.1%)**. This milestone closes the narrow live
cross-process receipt and reconnect-rebase gaps, but closes zero project tasks.
`FR-10-T04`, `FR-10-T05`, `FR-10-T08`, `FR-10-T09`, and `FR-10-T14` remain
Incomplete under their published parent acceptance floors.

## Completion rule

An `FR-10` task may be checked complete only when every item in its Definition
of Done has direct current evidence at the same scope. Parent completion is not
inferred from a foundation API, default-off shadow, short smoke test, or a
documented intention. The living and strategic roadmaps must be updated in the
same change that adds or invalidates completion evidence.
