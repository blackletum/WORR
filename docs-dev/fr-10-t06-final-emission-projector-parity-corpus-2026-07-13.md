# FR-10-T06 final-emission/projector parity corpus

Date: 2026-07-13
Project task: FR-10-T06
Classification: deterministic offline parity evidence; not live acceptance

## Outcome

WORR now has a deterministic 100,000-snapshot corpus that compares the
server's public final-emission shadow with an independent receiver projection
through public canonical/q2proto APIs. The corpus runs twice, requires the two
complete JSON results to be identical, qualifies explicit coverage floors,
and writes machine-readable evidence to
`.tmp/networking/snapshot-parity/evidence.json`.

The proof does not serialize a datagram or run the client parser, cgame,
packet scheduler, or renderer. It therefore strengthens FR-10-T06 offline
parity evidence but does not close the live impairment, long-session,
promotion, cross-platform, or release-default gates.

No file under `q2proto/` was modified.

## Compared invariants

For every successful emission the probe:

1. constructs and commits the exact public q2proto frame and entity-delta
   services through `SV_SnapshotShadow*V1`;
2. publishes the same services into a separately allocated
   `Worr_SnapshotQ2Proto*V2` receiver;
3. compares the legacy parity hash and all four semantic component hashes;
4. copies the receiver's public snapshot metadata, installs the exact
   authoritative server tick, tick delta, and time sideband, rebuilds its
   public hashes, and requires exact server endpoint-hash equality; and
5. independently verifies chronology and discontinuity flags.

This preserves the intentional distinction between legacy semantic parity and
server-only chronology. The normalization step models the exact metadata a
future negotiated carrier would deliver; it does not claim that the current
legacy packet transports the authoritative server tick.

## Deterministic coverage

The fixed manifest is
`tools/networking/scenarios/snapshot_final_projection_parity_corpus.json`.
Its 100,000 successful snapshots cover:

- acknowledged delta branches within a 64-slot retained window;
- periodic full/key frames, including the wire `deltaframe == 0` keyframe
  convention;
- overwritten-base rejection followed by a keyframe at the same unconsumed
  wire sequence;
- entity add, update, remove, identity reuse, and controlled visibility gaps;
- transport-truncation, fragment-stall, and rate-suppression discontinuities;
- exact monotonic server time across two simulated tick intervals and one
  authoritative `uint32_t` tick wrap;
- stale sent-ref rejection after slot reuse; and
- eight wire-sequence boundary cases ending at `serverframe == INT32_MAX` and
  canonical sequence `2147483648`.

The public q2proto wire frame is a nonnegative signed 32-bit value. Snapshot V2
maps it to `serverframe + 1`, and the projector rejects rollback or wrap.
Accordingly, the evidence reports `wrap_supported: false` and zero wire-wrap
cases instead of claiming a sequence-wrap behavior the public domain cannot
represent. The separate authoritative server tick is unsigned and its valid
wrap is exercised.

The final Windows Clang evidence recorded:

- 100,000 endpoint-hash matches;
- 100,000 legacy-hash matches;
- 100,000 component-hash matches;
- 100,000 exact chronology matches;
- 93,703 acknowledged branches;
- 127 keyframes, including 62 zero-base keyframes;
- 24 overwritten-base rejections and 24 immediate keyframe recoveries;
- 53,435 adds, 74,767 updates, 53,217 removals, and 30,058 reuse adds;
- 75,000 visibility-gap snapshots and 10,532 reentries;
- 250 truncations, 164 fragment stalls, and 7,466 rate suppressions;
- 1,979,245 independently checked unchanged-entity interpolation-origin
  transitions; and
- corpus digest `7b185107eeb0f6e7` across two identical executions.

## Legacy unchanged-entity correction

Live loopback diagnostics exposed a canonical entity-payload mismatch for an
entity omitted from a delta because it was unchanged. The legacy parser's
`CL_ParseDeltaEntity(..., delta = nullptr)` path advances `old_origin` to the
retained entity's current `origin` for non-beam entities. The projector had
copied the retained entity unchanged, leaving a stale interpolation origin.

`reconstruct_entities` now applies the legacy rule to a local scratch copy in
`append_unchanged`. It retains explicit beam endpoints and never mutates the
base slot, so a later publication failure remains transactional. The focused
`snapshot_q2proto_test` regression checks both the carried endpoint and the
unchanged retained base. The large corpus supplies nonzero origins and checks
the rule independently on every eligible carried entity; without the fix it
fails immediately.

## Public fragment-stall observation

The server final-emission shadow gained the dormant
`SV_SnapshotShadowMarkFragmentStallV1` observation call and
`SV_SNAPSHOT_SHADOW_SENT_FRAGMENT_STALL` sent-record flag. The call maps to
the existing public canonical fragment-stall discontinuity. No production
caller was added, packet output is unchanged, and the corpus uses the marker
to prove true server-final versus receiver parity for that cause rather than
labeling a projector-only case as final-emission evidence.

## Build and execution

Meson provides:

- executable `snapshot_final_projection_parity_corpus`;
- networking test `network-snapshot-final-projection-parity-corpus`; and
- run target `networking-snapshot-final-projection-parity`.

The runner records manifest and probe SHA-256 hashes, build identity,
repeatability, qualified coverage, explicit limitations, and
`live_acceptance: false`.

Focused Windows Clang verification passed:

- `network-snapshot-q2proto-projection`;
- `network-server-snapshot-shadow`; and
- `network-snapshot-final-projection-parity-corpus`.

All three passed in one Meson invocation. The corpus test completed two
100,000-snapshot executions in 14.71 seconds, and the run target generated the
repository-local evidence with the same digest.
