# FR-10-T04 Post-admission Netchan Receive Seam

Date: 2026-07-14

Task: `FR-10-T04`

Status: Transport boundary implemented and now registered by the default-off
one-command production observation linked below. Native capability remains
unadvertised.

## Purpose

The WTC1 carrier can be interpreted safely only after netchan has authenticated
the packet's sequencing context and accepted its acknowledgement state. The
legacy parser, however, must see only the carrier's declared legacy prefix and
must never receive the native trailer. The former boolean `Netchan_Process`
boundary could not distinguish an intermediate/stale packet from a packet whose
transport header was valid but whose application trailer was rejected.

This change adds the missing admitted receive boundary without changing legacy
wire bytes or enabling WORR-native traffic.

## Boundary and ordering

`NetchanNew_Process` invokes the optional callback only after all of the
following have succeeded:

1. sequence, acknowledgement, qport, and optional fragment-header parsing;
2. stale/future/conflicting acknowledgement checks;
3. final fragment assembly, when applicable;
4. incoming sequence and reliable-bit admission;
5. liveness, dropped-packet, and received-packet accounting.

Transport admission is deliberately not rolled back when the application
callback rejects a packet. Replaying the same reliable bit or transport
acknowledgement would be incorrect. The rejected application is instead hidden
from q2proto and reported as a distinct result.

The callback receives the exact unread application range:

```text
pointer = msg_read.data + msg_read.readcount
length  = msg_read.cursize - msg_read.readcount
```

For ordinary packets, the offset is eight bytes, or nine bytes on a
client-to-server NEW channel with a qport. Final netchan reassembly produces an
application-only buffer at offset zero. The V1 callback info records the full
message size, application offset/length, admitted sequence, reliable flag, and
reassembled flag so those cases cannot be confused.

## API contract

`inc/common/net/chan.h` now defines:

- `netchan_app_rx_info_v1_t`, a fixed 32-byte pointer-free input record;
- `netchan_app_rx_output_v1_t`, a fixed 16-byte prefix-selection record;
- `Netchan_SetApplicationRxHook`, accepted only for NEW channels;
- `Netchan_ProcessEx`, with `NO_APPLICATION`, `APPLICATION_READY`, and
  `APPLICATION_REJECTED` outcomes;
- the existing boolean `Netchan_Process` compatibility wrapper, which returns
  true only for `APPLICATION_READY`.

Callback outcomes are transactional:

- `BYPASS` restores the complete shared message descriptor byte-for-byte and
  exposes the unchanged application.
- `EXPOSE_LEGACY` validates the output ABI, size, reserved field, and bounded
  prefix length, then changes only `msg_read.cursize`. It never moves bytes or
  resets the read offset. A zero-byte legacy prefix is valid.
- `REJECT`, unknown results, invalid output, or any callback mutation of the
  shared message descriptor restores the descriptor, clamps `cursize` to the
  original `readcount`, and returns `APPLICATION_REJECTED`.

The application pointer is read-only and callback-scoped. The callback may not
retain it, drive the global message reader, recursively process or destroy the
same channel, or retain the output pointer. Hook storage remains caller-owned
and `Netchan_Close` clears the registered pointers through its existing full
channel wipe.

## Fragment and parser policy

Intermediate netchan fragments never call the hook. Final assembly calls it
exactly once with `REASSEMBLED` and offset zero. Ordinary fragmented legacy
traffic may therefore bypass unchanged. A future native adapter must reject a
matching WTC1 candidate carrying `REASSEMBLED`, because WNE1 already owns
native message fragmentation and WTC1 must remain within its exact application
packet budget.

The two live sequenced-packet call sites now consume `Netchan_ProcessEx`:

- the client drops the server connection on explicit application rejection;
- the server drops the affected client on explicit application rejection;
- ordinary no-application outcomes retain the prior silent return behavior.

Because no hook is registered, these call-site changes are behaviorally inert
for current legacy traffic. They establish the fail-closed behavior needed
before a native adapter can be armed. Demo playback, MVD playback, and other
paths that initialize `msg_read` directly do not traverse netchan and remain
outside this seam.

## Validation

The focused C integration test links the real `chan.c`, sequence core, and
size-buffer implementation. It covers:

- no-hook legacy identity, NEW registration/clear/close, OLD rejection;
- client offset eight and server offsets eight/nine with qport exclusion;
- reliable metadata and exact callback/output initialization;
- byte-identical bypass and zero/partial/full prefix exposure;
- explicit rejection plus ABI, size, reserved, oversize, and unknown-result
  failures;
- hostile descriptor mutation restoration and suffix suppression;
- retained sequence/reliable/liveness accounting after rejection;
- duplicate suppression before callback entry;
- boolean-wrapper compatibility;
- intermediate and final fragment behavior, exact reassembled bytes, and a
  policy rejection for reassembled carrier candidates.

The C++20 consumer locks the V1 sizes, offsets, flags, result values, and
three-way process enum. Both focused RX tests and the existing TX seam tests
pass in the configured Windows Clang build.

## Remaining work at seam validation

This seam does not advertise or negotiate native traffic. `FR-10-T04` still
requires the endpoint-readiness proof, WTC1 client/server adapters, canonical
command/event/snapshot routing, native-versus-legacy parity, malformed and MTU
fault matrices, reconnect lifecycle, and release evidence. A native callback
must treat no-carrier input as `BYPASS`, matching malformed candidates as
`REJECT`, and valid carriers as `EXPOSE_LEGACY` only after all admitted native
state changes have passed their own transactional validation.

## Production integration update (2026-07-14)

The seam is now registered default-off for the one-command pilot. The server
stamps its checked admission clock before `Netchan_ProcessEx`, accepts only one
strict DATA/WNE1/WNC1 shape, and publishes the legacy prefix after staged
retention/join/receipt validation. The client accepts exactly one ACK entry.
Malformed, wrong-direction, unknown-epoch, and mismatched-reference candidates
reject. The previous statement that WTC1 adapters/readiness were absent is
superseded for this narrow scope by
`docs-dev/fr-10-t04-one-shot-native-command-shadow-production-pilot-2026-07-14.md`.
