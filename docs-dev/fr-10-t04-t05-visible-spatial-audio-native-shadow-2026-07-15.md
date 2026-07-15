# FR-10-T04/T05 Visible Spatial-Audio Native Shadow

Date: 2026-07-15

Project tasks: `FR-10-T04`, `FR-10-T05`

## Outcome

The default-off `0x73` event shadow now has its first non-entity production
producer. An unreliable spatial sound becomes a native event only after the
real per-client server writer successfully emits that sound. A visible entity
source binds to the exact final snapshot generation; the later positional
extension documented in
`fr-10-t04-t05-positional-offframe-spatial-audio-native-shadow-2026-07-15.md`
also admits an off-frame source when that final sound carries an explicit
position.

The server resolves the audio source against that snapshot's immutable entity
generation, constructs an ID-less canonical audio template with the same
mapping cgame uses, binds the exact reference, and queues it through the
existing descriptor-first retained sender. Legacy sound output remains the
presenter and transport authority.

## Shared mapping and emission boundary

`Worr_LegacySpatialAudioEventCandidateBuildV1` centralizes the decoded
`q2proto_sound_t` to `worr_event_payload_spatial_audio_v1` mapping. Like the
temporary-entity constructor, it returns an unresolved template plus the raw
source index; the caller is responsible for binding lineage. It rejects invalid
asset, entity, channel, floating-point, or record shapes atomically.

`SV_SnapshotShadowBuildSpatialAudioCandidateV1` binds such a template to the
source identity retained by an exact final-emission snapshot. It uses the
per-client wire snapshot number as the action tick and the authoritative
snapshot time, matching cgame's legacy decode ordering contract. A missing
source remains a benign observation miss unless the final sound carries an
explicit position, in which case the record is world-anchored and marked
`POSITION_FORCED` without fabricating entity lineage.

The integration point is `emit_snd` in `src/server/send.c`, after
`q2proto_server_write` has accepted the final per-client sound. It deliberately
does not capture:

- reliable sounds, which follow a different message queue path;
- local/unicast sounds outside this final unreliable writer path.

This prevents a global pre-visibility sound record or a reconstructed legacy
byte stream from becoming accidental native authority. It also preserves the
current legacy sound presentation exactly.

## Verification

- Shared temporary/audio constructor, cgame range, final-emission binding, and
  production-hook virtual-link gate: 5/5 pass.
- The virtual link now sends an authoritative spatial-audio candidate through
  descriptor, DATA loss/retry, duplicate, reordering, ACK-loss, corruption,
  and cancellation coverage; its stable diagnostic digest remains
  `be9724b38fb5f682`.
- Production client engine, dedicated-server engine, cgame, and sgame modules:
  pass.
- Latest full headless networking suite: 125/125 pass.
- Latest three consecutive headless suite repetitions: 375/375 pass.

## Remaining scope

Temporary entities, muzzle flashes, reliable, positionless off-frame, and
local/unicast audio, direct sgame multi-event producers, native snapshot authority, and
presenter cutover remain open. `q2proto/`, legacy packet layouts, demos, and
legacy presenters are unchanged.

Roadmap task completion is unchanged: `FR-10` remains 3 of 16 complete. This
is default-off in-progress evidence for `FR-10-T04/T05`, not task closure.
