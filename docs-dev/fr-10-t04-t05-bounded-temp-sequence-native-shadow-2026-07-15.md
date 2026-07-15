# FR-10-T04 / FR-10-T05: Bounded sgame Temporary-Entity Sequences

Date: 2026-07-15

Tasks: `FR-10-T04`, `FR-10-T05` (both remain In Progress)

## Outcome

The default-off native event shadow first observed this direct sgame
multi-event family safely as a bounded homogeneous sequence of legacy
`svc_temp_entity` carriers in one game-DLL multicast message. It is now
subsumed by the ordered mixed-carrier adapter documented in
`fr-10-t04-t05-mixed-game-event-native-shadow-2026-07-15.md`; a single carrier
continues to work as the sequence-of-one case.

This resolves the gap between the server's game-DLL `SV_Multicast` ownership
and the earlier exact-single-carrier adapter. The game may append several
temporary effects before multicast fan-out; the per-client final writer now
preserves that source order in its native candidate batch after the exact
legacy message has fitted the client datagram.

Legacy packets, demos, effect presentation, and authority remain unchanged.
No `q2proto/` file changed.

## Canonical raw-sequence boundary

`Worr_LegacyTempEventDecodeRawSequenceV1` accepts only a complete sequence of
consecutive `svc_temp_entity` carriers using the established engine-game
layout. It uses a fixed maximum of 16 effects and stages every decoded value
before publishing the output array and count.

- A malformed, truncated, unsupported, mixed-opcode, or over-capacity sequence
  leaves both output array and count unchanged.
- The existing single-carrier API remains compatible: it delegates through the
  sequence decoder and still rejects additional carriers as a non-standalone
  shape.
- The initial homogeneous sequence added no wire framing or q2proto parser
  change; the mixed extension retains that property and is solely a final
  server-emission observation of legacy game-import bytes.

## Exact snapshot batch binding

`SV_SnapshotShadowBuildTempCandidatesV1` makes final-emission identity binding
explicitly transactional. It rebuilds every decoded effect against the same
exact sent snapshot and publishes the candidate array only when all entries
are valid.

The sender receives the complete ordered batch only after all of these gates:

1. The legacy game message was written to an unreliable `NETCHAN_NEW`
   post-snapshot datagram.
2. Every byte is a supported temporary-entity carrier and the sequence has at
   most 16 entries.
3. Every effect has a resolvable identity in that exact per-peer snapshot:
   entityless effects bind to world `{0, 1}`, while source and subject entities
   require visible generations in the sent view.

If one carrier fails any gate, no member of that native sequence is queued.
The full legacy multicast remains intact. Reliable, old-netchan, and mixed
game messages remain legacy-only.

## Regression and validation

The new focused coverage proves:

- ordered raw gunshot-plus-lightning decoding;
- sequence-capacity and malformed-tail rejection without output mutation;
- batch source order, world binding, source/subject generation binding, and
  batch-level failure atomicity; and
- three temporary-event candidates across the native sender's selective-receipt
  reordering schedule.

Validation completed headlessly:

```text
ninja -C builddir-win worr_engine_x86_64.dll worr_ded_engine_x86_64.dll \
  cgame_x86_64.dll sgame_x86_64.dll
# passed

meson test -C builddir-win --suite networking --print-errorlogs
# 125/125 passed

meson test -C builddir-win --suite networking --repeat 3 --print-errorlogs
# 375/375 passed
```

The required `windows-x86_64` `.install/` refresh completed after validation:
16 root runtime files, one root dependency, one q2aas reference map, 342
packaged assets, 31 botfile payloads, and 215 RmlUi asset payloads were
validated.

## Remaining scope

This is one tightly bounded direct sgame multi-event family, not general game
message canonicalization. Mixed `svc_*` payloads, multi-muzzle batches,
reliable/off-frame event classes, direct predicted action submission, native
snapshot fences, native presentation, and authority cutover remain open
roadmap work.
