# Canonical Snapshot ABI and Immutable Store, Stage A (2026-07-12)

Task: `FR-10-T06`.

Strategic project:
`docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`.

Living plan:
`docs-dev/plans/progressive-networking-events-snapshots-roadmap.md`.

Status: Stage A substrate and tests implemented. Subsequent Stage B and live
client/cgame shadow integration are active; server peer shadow, promotion, and
the full `FR-10-T06` acceptance gates remain open.

## Subsequent Integration Status

This document preserves the Stage A design and evidence while reporting the
current V2 ABI below. Stage B added public-q2proto projection and semantic
parity tests. The live client now captures accepted legacy baselines, frames,
entity deltas, the authoritative consumed-command cursor, and a stateful
canonical server clock across FPS changes; it retains projection lineage before
precache and through demo seeks and feeds only parity-qualified immutable views
to external cgame. Legacy parsing/rendering remains authoritative. No
`q2proto/` source was changed. Server-side peer shadow, exact sent-reference
retention, 100,000-frame live parity, impairment/load evidence, and promotion
remain open.

## Outcome

WORR now has the first transport-neutral canonical snapshot ABI and a bounded
immutable store on which the legacy shadow and future WORR transport adapters
can converge. Persistent records are valid C11 and C++20, contain no pointers,
have fixed field-defined layouts, and use the existing T02 prediction state and
T05 event/entity identities instead of parallel movement or event payloads.

The new production files are:

- `inc/shared/snapshot_abi.h`;
- `src/common/net/snapshot_abi.c`;
- `inc/common/net/snapshot_store.h`;
- `src/common/net/snapshot_store.c`.

This implementation slice intentionally did not edit `meson.build` or an
existing runtime source. Root integration has since registered the canonical
snapshot core library and the three focused tests in Meson. Later T06 stages
added the public-q2proto projector and live client/cgame audit path described
above. No file under `q2proto/` changed.

## Current V2 Records

The ABI freezes the following layouts with C and C++ static assertions:

| Record | Bytes | Purpose |
| --- | ---: | --- |
| `worr_snapshot_id_v2` | 8 | Non-zero snapshot epoch and sequence |
| `worr_snapshot_ref_v2` | 8 | Process-local slot/generation handle; never hashed or serialized |
| `worr_snapshot_serial_range_v2` | 16 | Store-local arena lifetime guard |
| `worr_snapshot_discontinuity_v2` | 24 | Explicit previous snapshot, tick delta, gap count, flags, and primary reason |
| `worr_snapshot_entity_generation_v2` | 16 | T05 entity identity plus authoritative/inferred provenance |
| `worr_snapshot_consumed_command_v2` | 16 | Exact server-consumed T09 cursor and provenance; never packet ACK |
| `worr_snapshot_event_ref_v2` | 32 | Authority or legacy-carrier identity plus semantic event hash |
| `worr_snapshot_event_range_v2` | 40 | Ordered authority/carrier reference range and bounds |
| `worr_snapshot_entity_v2` | 144 | Canonical entity components without a duplicate one-byte event |
| `worr_snapshot_player_v2` | 328 | Player/view/weapon/stat state embedding `worr_prediction_state_v1` |
| `worr_snapshot_v2` | 216 | Immutable metadata, consumed cursor, arena ranges, component hashes, and final hash |

Every persistent record uses exact-width integers, IEEE-754 binary32 floats,
explicit reserved fields, fixed 64-entry stats, and size/version/revision
headers where the record is independently constructed. Runtime envelopes in
the store header contain native pointers and are explicitly excluded from
persistence and hashing.

Entity V2 makes `TRANSFORM` mandatory, including for a stationary entity.
Other component bits describe meaningful field groups; absent groups must use
their canonical zero representation. `OWNER` is set only when the owner is a
non-absent T05 entity reference. Entity impulse events are not stored in the
entity record: a snapshot refers to the canonical T05 journal through its event
range.

Player V2 makes the T02 movement component mandatory. It embeds the frozen
56-byte `worr_prediction_state_v1`, and the snapshot validator enforces the T02
v1 structural, finite-value, movement-type, and movement-flag contract. The
snapshot metadata now embeds `worr_snapshot_consumed_command_v2`, validated as
an exact server-consumed T09 cursor when present. Packet acknowledgement is not
valid provenance for this field.

## Identity and Metadata Invariants

The ABI rejects contradictory identities before they can acquire a valid
snapshot hash:

- present entities have a non-zero generation and exactly one authoritative or
  legacy-inferred provenance bit;
- the snapshot controlled identity and player controlled identity must match;
- `AUTHORITATIVE_GENERATIONS` agrees with the player and every stored entity;
- if the controlled entity also appears in the entity list, its full identity,
  generation, and provenance tuple must match (omission remains legal because
  player state is stored separately);
- entity indices and T05 event IDs are strictly increasing and unique;
- generation zero and `{ epoch = 0, sequence = 0 }` are reserved absence
  values, so slot reuse cannot silently alias an old identity.

Keyframe and discontinuity state is also canonical:

- `KEYFRAME` and `FULL_SNAPSHOT` are equivalent, and a keyframe has no base;
- a delta base is from the same epoch, precedes the current snapshot, and is no
  newer than the explicitly recorded previous snapshot;
- initial, map-reset, and demo-rewind boundaries start at sequence one and have
  no previous snapshot;
- every ordinary snapshot has a meaningful same-epoch previous ID;
- a non-gap previous ID is exactly one sequence behind; a sequence gap's exact
  distance is `skipped_sequences + 1`;
- hard reset/boundary flags require a keyframe;
- transport truncation agrees in snapshot and discontinuity flags and can
  never be promotion eligible.

`PROMOTION_ELIGIBLE` is deliberately not restricted to native WORR transport
or authoritative generations. A complete, parity-proven legacy projection
with inferred client generations may be promoted to the same canonical
consumer under rollout R1. Stage A validates only structural eligibility
(complete and non-truncated); the live shadow/parity policy owns whether to set
the bit. `LEGACY_PROJECTION` remains provenance, not disqualification.

For a contiguous T05 event range, `one_past_authority_id` must be exactly
`count` ID advances after `first_authority_id`, including sequence-to-epoch
rollover. Validation computes this in overflow-safe constant time over the
non-zero sequence domain and rejects epoch exhaustion even when an untrusted
record supplies `UINT32_MAX` count. Non-contiguous ranges retain strict ordered
authority bounds without pretending missing IDs were delivered.

## Hash Contract

Player, entity, ordered entity-list, area-byte, ordered event-reference, and
snapshot hashes use domain-separated FNV-1a-64 over explicit little-endian
fields. They are deterministic diagnostics, not authentication primitives.

The implementation:

- never hashes a C object representation or padding;
- normalizes positive and negative zero to the same float bits;
- rejects non-finite canonical inputs;
- hashes only component groups declared present and requires absent groups to
  be canonical zero;
- binds component masks, entity generations/provenance, ordered T05 IDs and
  semantic hashes, discontinuity state, counts, and component hashes;
- excludes addresses, allocation state, wall time, slot refs, and arena
  `first_serial` values from the semantic snapshot hash.

Excluding store-local serials is intentional: two stores publishing identical
semantic snapshots at different physical/serial positions produce the same
snapshot hash while still retaining independent stale-view guards.

The checked test goldens for model revision 1 are:

```text
player=2814beca80cdff37
entity=d926fd5e55821c6a
entity_list=35e878a0545c77f0
area=d73f4710d7b043cf
events=8c8585c13e594f41
snapshot=c3087b2bee168952
soak_chain=a264120af5011a77
```

## Fixed Transactional Store

`worr_snapshot_store_v2` owns no allocation. Initialization receives a caller
owned slot array and fixed per-slot entity, area-byte, and event-reference
arenas. Required bytes are fully predictable:

```text
576 * slot_count
+ 144 * slot_count * entities_per_slot
+   1 * slot_count * area_bytes_per_slot
+  32 * slot_count * event_refs_per_slot
```

All count/byte products are checked before initialization, including the
32-bit `size_t` slot-array bound. The store object, slots, and arenas must be
distinct non-overlapping regions. The store is externally synchronized;
publication sources may alias their corresponding arena because committed
copies use overlap-safe moves.

Publication is a two-phase transaction:

1. validate every header, component, finite value, generation, entity order,
   event order, capacity, serial increment, destination generation, and
   publication counter;
2. derive ranges and event bounds, calculate all component/final hashes, and
   validate the complete prospective snapshot in local variables;
3. only after no failure remains, copy payloads into the selected fixed slot,
   publish the immutable slot record, advance serial/capacity telemetry, and
   return the new slot generation.

No failing publication mutates the store, any arena, or the caller's output
ref. Slot reuse increments a non-zero generation. Reset first proves every slot
generation can advance, then invalidates all refs while preserving monotonic
arena serials; generation exhaustion fails without partial reset.

Consumers receive copy-out APIs only. Each copy validates the slot generation,
the mirrored arena serial, the live serial bound, the frozen snapshot hash, and
the requested component hash before modifying the destination. Buffer
shortage, stale refs, corruption, and invalid arguments leave outputs and
counts unchanged. No raw arena pointer is exposed across a future module
boundary.

The store tracks successful publication count and high-water occupancy/entity/
area/event counts. It has no failure counter because the atomic failure
contract requires the entire store to remain byte-identical on rejection.

## Isolated Test and Fault Evidence

New test/tool files are:

- `tools/networking/snapshot_schema_layout_c.c`;
- `tools/networking/snapshot_schema_layout_cpp.cpp`;
- `tools/networking/snapshot_store_test.c`;
- `tools/networking/run_snapshot_stage_a.py`.

The suite covers:

- C11/C++20 size/offset parity and standard-layout/trivial-copy properties;
- fieldwise padding independence, negative-zero normalization, fixed hash
  goldens, component sensitivity, and non-finite rejection;
- mandatory transform/movement, absent-component zeroing, OWNER presence, and
  unknown flag/version/reserved-field faults;
- authoritative and inferred generations, controlled entity tuple conflicts,
  sorted/duplicate entities, and sorted/duplicate T05 event refs;
- exact contiguous event bounds, gaps, `UINT32_MAX` count, sequence-to-epoch
  wrap, and terminal epoch-exhaustion rejection;
- initial/keyframe/full/base/previous/gap contradictions and legacy promotion
  eligibility versus truncation rejection;
- fixed-capacity and zero-payload stores, copy buffer shortage, slot overwrite,
  stale refs, reset, range mirror corruption, and payload/hash corruption;
- byte-for-byte no-mutation on capacity, event-order, player, entity,
  controlled-identity, late metadata/hash, serial-exhaustion, generation-
  exhaustion, and reset-exhaustion failures;
- 100,000 deterministic successful publications in one fixed store with no
  address changes or allocator path, plus stable high-water telemetry.

The standalone runner builds with `-Wall -Wextra -Wpedantic -Werror`, strict
floating-point flags, C11, and C++20. Local evidence under `.tmp/networking/`
records:

- Windows Clang/Clang++ 20.1.7: three repeated layout/behavior runs and three
  repeated golden runs, all identical;
- WSL Ubuntu GCC/G++ 13.3.0: the same three-plus-three repetitions with the
  identical golden-output SHA-256
  `0f30b71f6a07f1b78ee2061f7f59e5f66d1760b0dbac3345d26cd1c8d5e83641`;
- one WSL Clang AddressSanitizer/UndefinedBehaviorSanitizer run of the complete
  100,000-publication behavioral/fault suite;
- a clean Clang static-analyzer pass over both production C sources;
- the root-integrated Windows Clang Meson targets and all three named
  `networking` tests passing 3/3.

Meson now exposes the same core through `canonical_snapshot_core` and registers
`network-snapshot-store`, `network-snapshot-schema-layout-c`, and
`network-snapshot-schema-layout-cpp` in the `networking` test suite. This was
build/test registration at the Stage A revision; subsequent client projection
and cgame audit integration are summarized above.

The evidence reports are local scratch artifacts:

```text
.tmp/networking/snapshot-stage-a/snapshot-stage-a-report.json
.tmp/networking/snapshot-stage-a-gcc/snapshot-stage-a-report.json
```

The 32-bit `size_t` overflow guard and conditional hostile initialization test
are present, but this slice does not claim execution on a 32-bit target.

## Remaining `FR-10-T06` Work

Stage A no longer stands alone on the client, but the task still requires:

1. unified authoritative sgame/T05 entity-generation propagation;
2. server shadow construction at the final customized/packed/truncated frame
   seams, exact sent-reference retention, and acknowledgement-base validation;
3. field-path mismatch diagnostics, keyframe recovery, deterministic
   impairment scenarios, and the full rollout R0/R1 promotion evidence;
4. measured capacity/load defaults, long live parity runs, platform coverage,
   and staged-runtime compatibility matrices under `FR-10-T14`.

The existing legacy snapshot path remains authoritative. The current V2
records/store, public-q2proto projector, live client shadow, and cgame timeline
are progressive audit infrastructure until promotion gates pass.
