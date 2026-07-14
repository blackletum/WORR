# Live Snapshot Parity and Adapter Runtime Gate

Date: 2026-07-13

Project tasks: `FR-10-T06`, `FR-10-T14`, `FR-10-T16`

## Scope

This change strengthens the staged loopback networking smoke test from a
process-liveness and impairment-counter check into a direct integration gate
for three live progressive adapters:

- canonical snapshot projection and legacy parity;
- canonical-failure-driven full-snapshot recovery; and
- adaptive input pacing and redundancy.

It remains a short, local, protocol-1038 loopback test. It is not a substitute
for the mandatory malformed-input corpus, 1/8/16/32-client load profiles,
weapon fairness scenarios, soak duration, dual-adapter matrix, or supported-
platform release evidence required by `FR-10-T14` and `FR-10-T15`.

## Faults discovered by the stronger gate

The first clean-profile run exposed persistent canonical snapshot parity flags
`0x108`, meaning `ENTITY_PAYLOAD | HASH_BUILD`. The accepted legacy parser
updates `old_origin` to the current origin when an unchanged non-beam entity is
carried into a new frame. The canonical q2proto projector originally copied the
entire previous entity unchanged, retaining a stale `old_origin`.

This was not a transport fault and should not have consumed recovery bandwidth.
It was a canonical reconstruction parity defect. The projector now mirrors the
legacy rule for unchanged entities while preserving the special beam endpoint
semantics, and the deterministic snapshot corpus contains a targeted invariant
for both cases.

After legacy parity became exact, the new cgame-consumer requirement exposed a
separate first-frame lifecycle defect. On an initial keyframe that carried the
controlled entity, entity reconstruction assigned generation one before the
projector decided whether the controlled playerstate generation belonged to an
epoch reset. The entity therefore carried
`LEGACY_INFERRED | EPOCH_RESET`, while the snapshot and player carried only
`LEGACY_INFERRED`. The immutable cgame timeline correctly rejected that view as
an invalid projection.

The projector now captures the controlled lifecycle's reset state before
reconstructing entities. A targeted public-q2proto test carries controlled
entity one on the first keyframe, requires byte-exact generation equality among
the entity, snapshot, and player records, requires the epoch-reset provenance,
and publishes the resulting view through `Worr_SnapshotTimelinePublishV1`.

## Direct snapshot-shadow status

The client command `cl_snapshot_shadow_status` reports two stable,
machine-readable lines from `cl_snapshot_shadow_status_v1`:

- lifecycle, epoch, pending state, projector result, capture failure, latest
  parity flags, accept flags, consumer attachment, and the consumer's last
  rejection result; and
- frame attempts, projections, publications, lineage-only frames, promotion
  eligibility, comparisons, mismatches, capture overflow, promotion blocks,
  and consumer counters.

The command is observational. It does not mutate the shadow, attach a consumer,
enable canonical rendering, request a full snapshot, or alter the wire path.

## Runtime evidence schema v3

`tools/networking/run_staged_impairment_smoke.py` now emits
`worr.networking.impairment-runtime.v3`. Each profile records parsed adaptive-
input, snapshot-recovery, and snapshot-shadow status alongside impairment
counters.

The default-off control must prove:

1. protocol 1038 and game API 2025 reach `cs_spawned`;
2. impairment routing remains inactive with zero processed/dropped/reordered/
   duplicated/stalled/overflow packets;
3. adaptive input is disabled and has no live decision or fallback;
4. snapshot recovery is disabled and all recovery state/counters remain clean;
5. the canonical snapshot shadow is active and healthy;
6. at least one frame is attempted, projected, published, and compared; and
7. the latest and cumulative parity mismatch counts are zero, with no frame or
   capture failure; and
8. the attached cgame consumer attempts and accepts live canonical frames with
   zero rejection.

The impaired profile must additionally prove live adaptive evaluation windows
and nonzero deterministic loss, reorder, duplication, and upstream-stall
events without queue overflow. Canonical snapshot parity must remain exact for
every accepted frame even when the legacy transport is impaired. Snapshot
recovery may arm under genuine invalid-base/projector failures, but it must not
enter an invalid or exhausted policy state.

## Verification

The intended local verification sequence is:

```text
python -m py_compile tools/networking/run_staged_impairment_smoke.py
meson compile -C builddir-win worr_engine_x86_64
meson test -C builddir-win --suite networking --print-errorlogs
python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64
meson compile -C builddir-win networking-runtime-smoke
```

The generated child evidence is
`.tmp/networking/impairment-runtime.json`. Its short loopback scope must be
identified when it is referenced from a wider
`worr.networking.acceptance-evidence.v1` report.

The verified Windows x86-64 run emitted
`worr.networking.impairment-runtime.v3`. The clean profile projected, published,
legacy-compared, promotion-qualified, and cgame-accepted 390/390 frames. The
impaired profile accepted 384/384 through the same path while recording loss,
reorder, duplication, and upstream stalls. Both profiles reported zero parity
mismatches, entity mismatches, frame/capture failures, promotion blocks,
consumer rejections, and ignored non-transport recovery rejections. The clean
profile kept recovery wholly inactive; the impaired profile produced live
adaptive evaluation windows without integration fallback or queue overflow.

This evidence closes the short staged live-consumer gate only. It does not turn
the canonical path into broad rendering authority or satisfy the 100,000-frame
live, serialized-datagram, memory/CPU, soak, load, or cross-platform gates.
