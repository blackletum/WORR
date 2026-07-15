# FR-10-T04 / FR-10-T05: Visible Temporary-Entity Native Shadow

Date: 2026-07-15

Tasks: `FR-10-T04`, `FR-10-T05` (both remain In Progress)

## Outcome

The default-off native event shadow can now observe a third final-emission
family: legacy temporary entities. It initially admitted one standalone
carrier and now also admits a bounded homogeneous sequence, as recorded in
`docs-dev/fr-10-t04-t05-bounded-temp-sequence-native-shadow-2026-07-15.md`.
This extends the existing
visible spatial-audio and exact standalone muzzle adapters without changing
the legacy packet, demo, renderer, cgame presentation, sgame ABI, or any file
under `q2proto/`.

The legacy stream remains authoritative. The native record is observational
until the roadmap's presentation and authority-cutover work is complete.

## Shared raw canonicalization

`Worr_LegacyTempEventDecodeRawV1` in
`src/common/net/legacy_temp_event_candidate.c` consumes exactly one complete
engine-game `svc_temp_entity` carrier. It validates the opcode and effect type,
reads the legacy little-endian IEEE-754 position fields, signed scalar/entity
fields, and byte-normal direction fields, then delegates to the shared
temporary-event candidate builder.

The decoder covers the currently ABI-supported temporary-entity field shapes:
position/direction effects, count effects, paired positions, position-only
effects, source/subject beams, grapple and lightning, flashlight, forcewall,
steam, widow beam-out, power splash, and damage-dealt. It requires the packet
to end precisely at the expected shape boundary and does not modify the output
on a malformed or unsupported carrier. This keeps cgame and final server
emission on the same payload-template mapping while preserving raw source and
subject indices until identity can be established safely.

The canonical-event static library now depends transitively on a small
common-math static library for `bytedirs`. Production engine and module targets
retain their own common-math ownership, avoiding duplicate global math symbols
while allowing the standalone event tests to link the decoder.

## Final-emission and identity boundary

The `write_msg` path in `src/server/send.c` attempts the adapter only after
the temporary-entity bytes have successfully been appended to an unreliable
`NETCHAN_NEW` client datagram. It requires native event-shadow mode, an exact
snapshot reference, and a decodable homogeneous temporary-carrier sequence.

`SV_SnapshotShadowBuildTempCandidateV1` obtains that peer's exact sent
snapshot view, builds the shared payload template with the emitted wire
snapshot number and source time, and binds identity only from that view:

- Entityless effects receive the explicit world source identity `{ index = 0,
  generation = 1 }`.
- Source and subject entity indices must both be present in the exact sent
  view; their observed generations are copied into the candidate.
- Missing, stale, malformed, unsupported, mixed-opcode, reliable, old-netchan,
  or otherwise ineligible carriers queue no native record and continue only
  through their original legacy path.

The candidate output is failure-atomic. A rejected source or subject therefore
cannot leave a partially rebound native event to be queued.

## Regression coverage

Headless tests now cover:

- exact raw gunshot, splash, lightning, and sustained-steam decoding;
- trailing-byte and unsupported-type rejection with unchanged output;
- final snapshot binding for an entityless gunshot and source/subject lightning,
  plus atomic rejection of non-visible source or subject entities; and
- a temporary-entity payload in the real production-wrapper loss, reorder,
  duplication, corruption, selective-receipt, and epoch-cancellation virtual
  link.

Validation completed:

```text
meson test -C builddir-win --suite networking --print-errorlogs
# 124/124 passed

meson test -C builddir-win --suite networking --repeat 3 --print-errorlogs
# 372/372 passed

builddir-win\\native_event_virtual_link_test.exe
# converged=1, digest=be9724b38fb5f682
```

The production link gate also rebuilt `worr_engine_x86_64.dll`,
`worr_ded_engine_x86_64.dll`, `cgame_x86_64.dll`, and `sgame_x86_64.dll`.
The required refreshed `windows-x86_64` `.install/` stage contains 16 root
runtime files, one root dependency, one q2aas reference map, 342 packaged
assets, 31 botfile payloads, and 215 RmlUi asset payloads.

## Deliberate scope and follow-up

This is an exact, visible-snapshot adapter rather than a new transport or a
presentation replacement. Direct sgame multi-event production, reliable and
off-frame effect/audio coverage, predicted local side-effect submission,
native snapshot fences, native presentation, and authority cutover remain
roadmap work.
