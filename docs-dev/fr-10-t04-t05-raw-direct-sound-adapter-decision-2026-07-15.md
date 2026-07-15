# FR-10-T04/T05 Raw Direct `svc_sound` Adapter Decision

Task IDs: `FR-10-T04`, `FR-10-T05`
Date: 2026-07-15
Status: decision recorded; no raw-byte adapter is introduced.

## Decision

Do not add a hand-written raw `svc_sound` decoder to the native event shadow
for direct sgame `SV_Multicast` byte messages. Those messages remain
legacy-only until they reach a structured final-emission carrier with the
recipient's negotiated q2proto semantics available at the boundary.

## Evidence

There are two distinct final server paths:

- `SOUND_PACKET` carries a `q2proto_svc_sound_t` and passes through
  `q2proto_server_write` in `emit_snd`. That is the structured path used by
  the visible and explicit-positional off-frame native audio shadow.
- Direct game-DLL multicast data reaches `write_msg` as a raw byte span. Its
  positioned `svc_sound` messages are sorted for legacy delivery but do not
  carry a decoded `q2proto_svc_sound_t` or an independently owned decoder
  context.

q2proto's authoritative service parser is `q2proto_client_read`, which needs a
client context configured with the recipient protocol and parser state. The
server holds a server write context; it cannot use that context to decode an
arbitrary post-snapshot raw byte span. A local parser based only on the base
`SND_*` layout would silently diverge for negotiated extensions, coordinate
formats, framing, or any mixed service message. Treating an unknown trailing
opcode as safe is especially incorrect because direct multicast spans may
contain multiple variable-length records.

`q2proto/` remains read-only. No fallback parser, raw reserialization, or
pre-visibility event record is permitted by this decision.

## Required successor boundary

When this family is promoted, the server needs one of the following explicit
boundaries before final byte serialization:

1. a structured game-import sound carrier that survives per-client filtering,
   forcing, and protocol encoding; or
2. a q2proto-owned, server-side exact service decode API that accepts the same
   negotiated context and reports one consumed record atomically.

The successor must bind a visible source to the exact snapshot generation or
an explicit finite position to world with `POSITION_FORCED`; it must reject
reliable, malformed, mixed/unknown, and positionless off-frame cases without
altering native output. It needs raw-wire parity coverage for every supported
negotiated protocol before shadow admission.

## Validation and scope

No runtime behavior changes in this decision round. The preceding positional
audio implementation remains validated by its focused 3/3 gate, production
module links, full 125/125 networking suite, three 375/375 repetitions, and
the refreshed `windows-x86_64` staged runtime. This decision does not close
`FR-10-T04` or `FR-10-T05`.
