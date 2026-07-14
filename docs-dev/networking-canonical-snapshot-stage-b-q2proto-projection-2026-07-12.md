# Canonical Snapshot Stage B: Public-q2proto Projection Core (2026-07-12)

Task: `FR-10-T06`.

Strategic project:
`docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`.

Living plan:
`docs-dev/plans/progressive-networking-events-snapshots-roadmap.md`.

Status: Stage B projection core and parity tests implemented. Subsequent live
client shadow and cgame audit delivery are active; consumer authority, server
peer shadow, and completion of `FR-10-T06` remain open.

## Subsequent Integration Status

This document preserves the original Stage B design and isolated evidence. The
live client parser now feeds accepted q2proto baselines, frame headers, and
entity deltas into this projector, attaches the negotiated authoritative
consumed-command cursor, maintains a stateful canonical server clock across FPS
changes, and compares accepted legacy state independently. Projection lineage
is retained before precache and through demo seek/reset behavior. Only
parity-qualified, promotion-eligible immutable views are delivered to the
external cgame timeline; legacy parsing, rendering, and event presentation
remain authoritative. Server peer shadow, exact sent-ref retention, the
required 100,000-frame live parity corpus, impairment/load gates, and consumer
promotion remain open.

## Outcome

WORR now has a bounded adapter core that reconstructs complete canonical V2
snapshots from the public q2proto frame and entity-delta records already
produced by the legacy decoder. The adapter sits above q2proto: it neither reads
private codec state nor changes any file under `q2proto/`.

The production surface is:

- `inc/common/net/snapshot_projection.h` and
  `src/common/net/snapshot_projection.c` for exact endpoint and narrow legacy
  parity hashes;
- `inc/common/net/snapshot_q2proto.h` and
  `src/common/net/snapshot_q2proto.cpp` for public-q2proto reconstruction,
  baseline handling, lineage history, bounded publication, and immutable
  generation-safe views.

Meson linked this core into focused networking tests at the Stage B revision.
The subsequent live client and cgame audit integrations are summarized above;
the server frame builder still has no peer canonical shadow and the active
legacy snapshot/presentation path remains authoritative.

## Adapter Boundary

`Worr_SnapshotQ2ProtoPublishV2` accepts one already-decoded
`q2proto_svc_frame_t` and the complete, strictly ordered public
`q2proto_svc_frame_entity_delta_t` carrier range ending in q2proto's zero-entity
terminator. The engine adapter must also supply information q2proto cannot own
or infer reliably:

- the controlled entity index;
- canonical movement type and movement flags when those playerstate fields are
  present;
- optional team identity;
- normalized monotonic server time;
- observer-attach, transport-truncation, fragment-stall, and optional
  generation-only full-frame lineage-parent facts.

This preserves the ownership boundary. Codec records describe legacy wire
semantics; engine facts remain explicit inputs and canonical records remain
transport-neutral outputs.

The projector validates structure sizes, schema versions, reserved fields,
known q2proto delta bits, finite/canonical component values, entity ordering,
model/sound/entity bounds, area/event/entity capacity, and all snapshot/base
identities before committing a slot. It uses only public q2proto functions for
coordinate, angle, sound, and packed-field conversion.

## Complete Reconstruction and Acknowledged Bases

A published delta is reconstructed against the exact `deltaframe` slot, not
against the chronologically previous received frame. This preserves Quake II's
acknowledged-base behavior and supports a frame branching from an older retained
base after loss. Missing, stale, overwritten, future, or cross-epoch bases are
rejected; the projector never substitutes a different base or publishes a
partial view.

Entity reconstruction performs a sorted merge of the selected complete base
with the ordered carrier range:

- unchanged base entities are retained;
- matching carriers update their base entity;
- removals require an entity present in the selected base;
- new entities start from a configured public-q2proto baseline when present,
  otherwise from the canonical empty entity;
- a full frame has no entity-state delta base, but may name a retained snapshot
  as a generation-only lineage parent.

Playerstate deltas use the selected delta snapshot's complete player record.
Full frames construct a complete player record from canonical defaults. Area
bytes are copied into fixed storage. Supported one-byte legacy entity events
are normalized into ordered T05 semantic event references with legacy-inferred
provenance and carrier ordinals; this slice does not yet merge the broader T05
typed temp/muzzle/sound audit range into the snapshot.

## Entity Lifecycle Lineage

Each retained snapshot slot stores a complete `{generation, present}` lineage
table for every configured entity index. A branch therefore derives lifecycle
identity from its selected base rather than from the most recently observed
frame. Remove followed by add increments the inferred generation, while an
update on a branch that predates the removal retains the earlier generation.

Full snapshots can optionally keep lifecycle continuity through a
generation-only parent without treating that parent as an entity delta base.
A full snapshot without such a parent starts a new inferred lineage and marks
new identities with epoch-reset provenance.

Playerstate proves the controlled entity lifecycle exists even when the
first-person entity is omitted from that observer's packet-entity range. The
projector marks that lineage present. If the controlled entity later first
appears in the entity carrier range, it retains generation 1 rather than being
misclassified as a reuse. A focused regression test covers this transition.

These generations are explicitly `LEGACY_INFERRED`. Authoritative sgame-owned
generation IDs and their unification with T05 remain later `FR-10-T05`/T06
work.

## Discontinuities

Canonical snapshots distinguish current snapshot identity, wire delta base,
and previous observed snapshot. The projector records:

- observer attachment when the first accepted full frame begins mid-epoch;
- initial and full snapshot boundaries;
- exact visible sequence gaps and skipped-sequence counts;
- a `BASE_JUMP` when a valid delta intentionally branches from a retained base
  other than the previous observed snapshot;
- rate suppression reported in the public q2proto frame;
- explicit server-only fragment-stall cause;
- explicit transport truncation.

When several facts apply, the canonical primary reason follows deterministic
priority while every applicable flag remains present. For example, a visible
gap caused by a server fragment stall keeps `SEQUENCE_GAP` as its primary reason
and also carries `FRAGMENT_STALL`.

Transport truncation clears promotion eligibility and sets matching snapshot
and discontinuity flags. It is never silently treated as a complete promotable
projection.

## Exact Endpoint Hash and Legacy Parity Hash

Every immutable view receives two domain-separated hashes:

- `endpoint_hash` binds the complete accepted canonical snapshot hash and exact
  player/entity/area/event hashes. It therefore detects receiver chronology,
  discontinuities, generation/event provenance, server time, transport-only
  facts, and the attached consumed-command cursor when present.
- `legacy_parity_hash` binds only legacy-reconstructible semantic content:
  canonical model revision, key/full/legacy status, snapshot and delta-base
  identity, controlled entity identity, payload counts, semantic player and
  entity fields, area bytes, and event carrier semantics.

The parity domain deliberately excludes store serials, receiver-only previous
chronology, server-only provenance, authority IDs, the canonical
consumed-command cursor that legacy payload semantics cannot reconstruct, and
transport-only truncation/fragment diagnostics.
The test suite proves that removing only transport truncation changes the
endpoint hash while preserving the legacy parity hash. It also proves that
authoritative provenance/cursor metadata can change endpoint identity without
creating a false legacy payload mismatch.

The hashes are deterministic diagnostics, not authentication primitives.

## Fixed Storage and Failure Rules

The context allocates nothing. The caller supplies mutually disjoint fixed
arenas for slots, complete entities, area bytes, event references, per-slot
lineage, baselines, and one set of scratch reconstruction buffers. Slot
publication increments a non-zero generation, so a view ref cannot silently
alias an overwritten slot. View construction revalidates the committed
projection hashes before returning pointers whose lifetime is bounded by that
slot generation.

All persistent slot/cursor/serial/ref outputs are committed only after complete
reconstruction and validation. Scratch arenas are working storage and are not
part of the byte-identical failure contract after a late reconstruction error;
callers must not treat their contents as published state.

Hostile overlap is rejected before writes at each public boundary:

- initialization requires the context, profile, storage envelope, and all
  arenas to be correctly aligned and mutually disjoint;
- baseline input cannot alias context or storage;
- publication input/frame/delta/area/ref regions cannot overlap each other or
  context/storage;
- view outputs cannot overlap each other or context/storage.

Focused tests place the initialization context in the slot arena and place
publish/view outputs inside the live context. Each call returns
`INVALID_ARGUMENT`, leaves all fixture bytes unchanged, and leaves independent
outputs unchanged.

The fixed storage cost, excluding the caller's source q2proto records, is:

```text
248                                      context
+ 648 * slots                            slot metadata/player/snapshot/hashes
+ 144 * slots * entities_per_slot        retained canonical entities
+   1 * slots * area_bytes_per_slot      retained area bytes
+  16 * slots * events_per_slot          retained event references
+   8 * slots * max_entities             per-base lifecycle lineage
+ 144 * max_entities                     q2proto baselines
+   1 * max_entities                     baseline-present flags
+ 144 * entities_per_slot                entity scratch
+   1 * area_bytes_per_slot              area scratch
+  16 * events_per_slot                  event scratch
+   8 * max_entities                     lineage scratch
```

The per-slot lineage table is intentionally the main Stage B memory tradeoff:
it makes acknowledged-base branches deterministic in constant lookup time but
costs `8 * slots * max_entities` bytes. At 64 slots and 4096 entity indices,
lineage alone is 2 MiB. Production capacities, age limits, and load budgets are
not selected by this slice and must be measured under `FR-10-T14` before live
enablement.

## Verification Evidence

New Stage B tests are:

- `tools/networking/snapshot_q2proto_test.cpp`;
- `tools/networking/snapshot_q2proto_wire_test.cpp`;
- `tools/networking/snapshot_q2proto_schema_layout_c.c`;
- `tools/networking/snapshot_q2proto_schema_layout_cpp.cpp`.

The projection test covers exact branch-base reconstruction, baselines,
add/remove/reuse generation behavior on competing branches, generation-only
full-frame parents, epoch-reset full frames, controlled-entity first-person
omission, inferred entity event references, `BASE_JUMP`, sequence gap plus
server fragment stall, explicit truncation, endpoint/parity domain separation,
malformed ordering, invalid/missing bases, reset/stale refs, consumed-command
metadata validation, early hostile alias rejection, and committed-state
atomicity on rejected frames.

The wire test performs public q2proto server encode, byte transport, public
client decode, and canonical projection for Vanilla, R1Q2, Q2PRO extended, and
Q2REPRO/Rerelease profiles. All four produce the same legacy parity hash for
equivalent player, entity, area, and one-byte event semantics. This validates a
representative cross-protocol path; it is not yet the required live shadow
corpus.

On Windows Clang, the following integrated Meson command passed all three
repetitions on 2026-07-12:

```powershell
meson test -C builddir-win --print-errorlogs --repeat 3 `
  network-snapshot-store `
  network-snapshot-schema-layout-c `
  network-snapshot-schema-layout-cpp `
  network-snapshot-q2proto-projection `
  network-snapshot-q2proto-wire `
  network-snapshot-q2proto-schema-layout-c `
  network-snapshot-q2proto-schema-layout-cpp
```

Result: `21/21` passed, `0` failed. The C11 and C++20 schema guards agree on
the 648-byte slot, 248-byte context, and all projection/runtime envelope
layouts.

No file under `q2proto/` changed.

## Current Limitations and Next T06 Stages

Stage B has since entered the live client audit path, but these limits remain:

1. The first observer-attach snapshot has no pre-attachment lifecycle history.
   It must be an explicit full attach frame and seeds visible/controlled
   inferred generations from the first observation; an entity reused before
   attachment cannot be distinguished retroactively.
2. The server frame builder does not yet construct a peer canonical shadow at
   the final customized/packed/truncated seam. Exact sent-ref retention and
   acknowledged-base comparison remain absent.
3. Stage B directly projects supported legacy entity impulse events. The live
   client can associate the broader typed T05 temp/muzzle/sound audit range,
   but canonical event presentation and authoritative source ordering remain
   open.
4. Client demo recording/playback and seek-lineage preservation now cover the
   capability/cursor sideband, but MVD/GTV, spectator switching, native demo
   schema/versioning, and full record/play/seek/relay matrices remain.
5. Visibility churn, malformed corpora, deterministic impairment matrices,
   100,000 live shadow snapshots, memory/CPU, and multi-platform gates remain.
6. The future WORR-native adapter must call the same canonical validation and
   projection model rather than introduce a second snapshot schema.

Accordingly `FR-10-T06` remains **In Progress**. Stage B is now the tested
public-q2proto oracle behind a live client audit feed, but this document does
not claim the required zero-mismatch corpus or authorize consumer cutover.
