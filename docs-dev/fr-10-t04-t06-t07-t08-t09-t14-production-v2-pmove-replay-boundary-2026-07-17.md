# FR-10 Production V2 Prediction and PMove Replay Boundary

Date: 2026-07-17

Tasks: `FR-10-T04`, `FR-10-T06`, `FR-10-T07`, `FR-10-T08`,
`FR-10-T09`, and `FR-10-T14`

Status: implemented and focused-validation complete; parent tasks remain
Incomplete

## Purpose

The serialized 100,000-snapshot corpus proved native transport, semantic
admission, immutable timeline publication, cursor-bound range resolution, pure
prediction-authority selection, acknowledgement, and release. It deliberately
stopped before the shipped engine V2 request constructor and before cgame's
real `CL_PredictMovement`/PMove/reconciliation path.

This milestone closes that boundary gap with one production-linked,
headless, deterministic vertical slice. It does not claim weapon/action
prediction, presenter cutover, load, soak, or release qualification.

## Production changes

### One engine-owned prediction-input resolver

The V1 and V2 cgame prediction imports now live in
`src/client/cgame_prediction_input.cpp`. The shipped `CG_GetExtension` path and
the production-linked test request the same immutable import objects.

The extracted implementation retains the existing behavior and ownership:

- it copies the live engine command ring by value;
- it resolves retained command identities through the canonical identity
  owner;
- it carries the pending command separately;
- V2 requires negotiated canonical-cursor capability, server-consumed
  provenance, a valid cursor, and exact returned-cursor equality;
- V2 never substitutes packet acknowledgement; and
- every result is exposed through a small saturating diagnostic record.

This removes the earlier testability split where production resolution was a
file-local implementation embedded in `src/client/cgame.cpp`.

### One cgame prediction configuration provider

`CG_GetPredictionConfigV1` now lives in
`src/game/cgame/cg_prediction_config.cpp`. The shipped cgame replay path and
the deterministic oracle both consume the same movement-model revision,
air-acceleration, N64-physics, and Q3-overbounce configuration.

Prediction-history clearing now also resets `cl.predicted_step_time`; a hard
resync therefore cannot retain a timestamp from a discarded replay chain.

### Slot-bound semantic-admission receipts

Each canonical timeline slot now owns an immutable prediction receipt created
only after both of these conditions are true for the exact published
generation:

1. canonical timeline publication succeeded; and
2. the snapshot event fence accepted the same snapshot.

The receipt binds the timeline slot/generation, admission generation,
snapshot ID/hash, consumed cursor, server tick/time, and controlled-entity
identity/generation/provenance. Prediction copy-out and the pure authority
selector both validate the complete receipt. A timeline-only publication
remains available to diagnostic/render queries but returns `NOT_FOUND` from
prediction copy-out.

This prevents mutable "latest status" from being mistaken for movement
authority and prevents a failed event fence from seeding promoted replay.

## Production-linked acceptance path

`native_snapshot_production_virtual_link_test` now executes the following
chain in one hidden, input-free process:

1. server final snapshot projection;
2. WNC1/WNE1/WTC1 serialization and native virtual-link delivery;
3. client expectation matching and semantic admission;
4. sole canonical cgame timeline publication and event-fence receipt;
5. exact engine V2 input request construction;
6. canonical prediction-authority selection;
7. real cgame `CL_PredictMovement` and shared `Worr_PredictionStepV1` PMove;
8. prediction-ring state/collision/replay hashes compared with an independent
   replay oracle using the same production movement configuration; and
9. bounded correction and correction-threshold hard-reset reconciliation.

The fixture covers:

- a pending command followed by byte-identical finalized replay;
- a legal 127-command canonical range;
- a deliberate 128-command exhaustion that clears every prediction ring and
  reports `RANGE_EXHAUSTED`;
- one bounded state-divergence correction;
- one correction-threshold hard reset; and
- one timeline publication with an intentionally wrong event-fence epoch,
  proving diagnostic copy availability while prediction authority is withheld.

The focused deterministic result is:

```text
prediction_ready=4 receipt_fence_blocks=1 v2_replays=4
pending_finalized=1 oracle_matches=3 bounded_corrections=1
threshold_corrections=1 range127=1 range128_reset=1
collision_traces=1419 point_contents=387
state_hash=f49cc087878cc954 collision_hash=0f918c82efaaa180
replay_hash=c5830959d33890a6 digest=abf2723d5f03cbe9
```

The 100,000-snapshot corpus includes the virtual-link fixture source. Its
Meson target therefore links the same engine resolver, command identity,
cgame replay/configuration, and prediction core even though the corpus does
not invoke the focused replay-only case. A 1,000-frame corpus smoke retained
exact acceptance/acknowledgement/release/authority totals and produced corpus
digest `d55f9d80d1f58f15` for that reduced count.

## Validation

All executable launches used `tools/networking/headless_process.py`, Windows
`CREATE_NO_WINDOW`, `stdin=DEVNULL`, captured output, process-tree cleanup, and
no client/input/mouse initialization.

Final integration validation passed:

- full production build, including client/cgame/sgame, dedicated engine,
  launchers, OpenGL, Vulkan, Vulkan RTX, and updater targets;
- production virtual-link replay gate;
- canonical prediction-authority selector gate;
- 1,000-frame serialized corpus smoke;
- nine fail-closed prediction source-contract tests; and
- complete headless networking suite: `149/149` in `224.8 s`, with the
  two-pass 100,000-frame corpus row passing in `209.08 s`;
- refreshed corpus evidence: golden `c6aee48df85341ab`, normalized JSON
  SHA-256 `a35973b39947387d7454a45650d5f9489e0ed136158e55177d9770c904444d38`,
  exact repeatability, zero accepted abandonment, and explicit
  `mouse_capture=false`;
- packaged-asset tests: `16/16`;
- release bootstrap headless contract: `1/1`; and
- `windows-x86_64` `.install/` refresh and validation: 16 root runtime files,
  one dependency, a 507-file `basew/pak0.pkz`, 31 botfile payloads, 215 RmlUi
  assets, and one q2aas reference map.

The 507-file staged archive includes unrelated renderer-parity assets already
present in the shared working tree; this milestone claims only that the final
combined payload validates. `q2proto/` remained untouched.

## Explicit non-claims

This milestone does not complete any parent task. It does not yet prove:

- the complete Rerelease movement/weapon/action prediction catalog;
- predicted audiovisual suppression or authoritative present-once behavior;
- real-socket, cross-snapshot netchan reorder, adaptive delivery, or both
  legacy/native recovery matrices;
- correction budgets under sustained impairment or multi-client load;
- native demo/MVD/GTV/spectator reproduction;
- 1/8/16/32-client performance, malformed corpora, long soak, rollback, or
  cross-platform release qualification; or
- public/default-on native snapshot or prediction authority.

Task accounting therefore remains overall 74/190 complete (38.9%) with 116
open, and `FR-10` 3/16 complete (18.75%) with 13 open.
