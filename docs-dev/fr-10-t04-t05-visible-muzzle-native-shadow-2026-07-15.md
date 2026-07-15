# FR-10-T04/T05 Visible Muzzle Native-Shadow Producer

**Date:** 2026-07-15
**Tasks:** `FR-10-T04`, `FR-10-T05`
**Status:** Implemented as a narrow, default-off observational producer; the
broader event-family and authority work remains open.

## Outcome

The default-off native event shadow now has a second additional event producer:
eligible player and monster muzzle flashes. It follows the visible spatial-audio
safety boundary: the legacy packet remains authoritative; the producer observes
only a message that has already fitted the exact per-client post-snapshot
datagram; it reconstructs a canonical candidate; it binds the source identity
and generation in that exact final snapshot; and it queues only a native shadow
copy through the negotiated, default-off sender.

The initial one-carrier boundary has since been extended to a bounded
homogeneous muzzle sequence; see
`docs-dev/fr-10-t04-t05-bounded-muzzle-sequence-native-shadow-2026-07-15.md`.

No `q2proto/` source changed. Legacy servers, demos, packet presentation, and
rendered muzzle effects retain their existing behavior.

## Shared Canonicalization

`common/net/legacy_muzzle_event_candidate.*` owns narrow raw decoding and
canonical candidate construction. It accepts only a complete, standalone legacy
player `svc_muzzleflash`, monster `svc_muzzleflash2`, or extended monster
`svc_muzzleflash3` carrier. It rejects combined, truncated, malformed, or other
non-exact raw messages before a server candidate is considered.

The shared sequence decoder now accepts up to 16 consecutive carriers while
preserving their player/monster family and silence state. It publishes no
partial decode result; mixed-opcode, malformed, truncated, or over-capacity
sequences remain legacy-only.

The builder validates player/monster muzzle payloads against the shared event
ABI, strips the player silencer flag into its canonical field, rejects a
silenced monster record, and emits an unresolved-source, replay-safe event
template. It temporarily supplies a valid source reference solely for ABI
validation and restores the unresolved reference, so raw indices cannot be
mistaken for authoritative entity lineage. Rejected inputs leave outputs
unchanged.

Cgame now calls the same builder while capturing its V2 legacy event range,
removing duplicate player/monster muzzle mapping. Cgame still resolves the raw
source index to an observed entity generation only at range delivery.

## Exact Final-Emission Binding

`SV_SnapshotShadowBuildMuzzleCandidateV1` builds an observation only from a
client snapshot wire record that has already been emitted. It uses the exact
final-emission view and visible-source binding shared with spatial audio,
including emitted tick/time and entity generation. A source absent from that
client's final snapshot, an invalid candidate, or missing final-emission record
is rejected without touching the authoritative legacy packet.

The server writer invokes this only after `MSG_WriteData` has accepted the raw
unreliable message. It is deliberately excluded for old netchan mode, disabled
shadow/capability state, missing snapshot metadata, reliable traffic, malformed
or combined carriers, and source-invisible records. Those paths stay
legacy-only. This is an observation adapter, not native event authority,
snapshot serialization, or a presenter cutover.

`SV_SnapshotShadowBuildMuzzleCandidatesV1` applies the same binding rule to a
full sequence and publishes its native candidate array only if every source is
visible in the exact sent view.

## Verification

- `network-legacy-muzzle-event-candidate` covers all three raw families,
  silencer handling, bounds, invalid records, atomic failure, and raw-carrier
  rejection.
- `network-server-snapshot-event-candidates` covers player and monster binding
  to the exact wire frame/tick/time/generation, plus invisible and invalid
  source rejection, ordered batch binding, and batch atomicity.
- `network-native-event-virtual-link` exercises the shared native sender with
  ordered event batches through directional loss, reorder, duplication, one-way
  ACK loss, corruption, and cancellation. Its digest remains
  `be9724b38fb5f682`.
- The production `engine`, dedicated engine, `cgame`, and `sgame` targets build.
  The headless networking suite passes **124/124** tests, and three complete
  repetitions pass **372/372**.
- The Windows x86-64 `.install/` stage was refreshed from the production build:
  16 root runtime files, one runtime dependency, the `basew` tree, and its
  q2aas reference-map asset are present before packaging.

## Remaining Work

Other direct sgame multi-event families, reliable and off-frame
audio, native snapshot adapters/serialization, native authority, prediction
submission and reconciliation cutover, presenter parity, wider impairment
goldens, load/soak, and cross-platform evidence remain open. This does not
change the roadmap task completion state.

## Roadmap Accounting

- Overall: **68/180 complete (37.8%)**; **112 open**.
- `FR-10`: **3/16 complete (18.75%)**; **13 open**.
- This round: **+0 completed roadmap tasks**; it expands bounded implementation
  evidence for the existing in-progress `FR-10-T04` and `FR-10-T05` tasks.
