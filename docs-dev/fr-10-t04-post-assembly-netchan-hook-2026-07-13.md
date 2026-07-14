# FR-10-T04 Dormant Post-Assembly Netchan Transmit Hook

Date: 2026-07-13  
Primary project task: `FR-10-T04`  
Supporting tasks: `FR-10-T14`, `FR-10-T16`  
Status: transmit integration seam implemented and now registered by the
default-off one-command production observation linked below; no native
capability is advertised

## Outcome

WORR now has a bounded, transactional hook at the only safe transmit boundary
for a future WTC1/native carrier adapter: after `NetchanNew_Transmit` has
assembled its complete reliable and unreliable application stream, but before
the first physical `NET_SendPacket` handoff.

The hook is deliberately dormant. No client or server registers it,
`WORR_NET_CAP_NATIVE_ENVELOPE_V1` remains excluded from the live capability
mask, legacy packets remain byte-identical when the hook is absent or bypasses,
and `q2proto/` is unchanged.

The implementation is in:

- `inc/common/net/chan.h`, which defines the local V1 callback ABI and
  registration API; and
- `src/common/net/chan.c`, which stages a candidate application slice,
  preserves the legacy fallback, observes every physical packet-copy outcome,
  and issues one terminal callback for every token-bearing prepare attempt.

## Why this boundary is required

The `data` argument passed to `Netchan_Transmit` is not the whole application
packet. Netchan can prepend retained reliable bytes before that caller-owned
unreliable stream. A carrier wrapped around only `data` would therefore publish
the wrong WTC1 `legacy_bytes` boundary and could place a native trailer inside,
rather than after, the actual legacy application stream.

For a non-fragmented NEW channel packet, assembly is now treated as:

```text
netchan sequence/ack header
+ optional client qport
+ complete reliable application bytes
+ complete unreliable application bytes
```

The hook sees only the final two application components. It never sees or
rewrites the sequence, acknowledgement, reliable flag, or qport header. The
candidate it returns replaces the complete application slice while the header
stays owned by netchan.

This is a hybrid architecture rather than an idTech3 clone. It retains WORR's
existing connection/channel sequencing and compatibility path while adopting
the useful idTech3 principle that transport packet assembly has one explicit,
validated boundary. Canonical commands, snapshots, and events remain above the
wire adapter instead of becoming netchan-owned gameplay schemas.

## Local V1 callback contract

`Netchan_SetApplicationTxHook` accepts a prepare/completion pair plus one opaque
connection context. Both callbacks must be present to register; both must be
null to clear. Mixed registrations fail without changing the current pair, and
OLD channels reject registration. The opaque object remains caller-owned and
must outlive all synchronous callbacks that use it. Clear and close only forget
the pointer; they do not call a finalizer or release adapter state.

The callback records are fixed-width and pointer-free:

- 32-byte prepare information: ABI/version, outgoing sequence, real direction
  application ceiling, exact reliable/unreliable/combined byte counts, and the
  requested physical copy count;
- 24-byte prepare output: ABI/version, replacement application length,
  reserved-zero validation, and caller-owned 64-bit token; and
- 32-byte completion information: terminal result, requested and accepted copy
  counts, exact final application length, and the same token.

Transient byte pointers are passed separately. Prepare receives a read-only
view of the exact legacy application plus a distinct candidate buffer whose
capacity is `max_application_bytes`. Completion receives the exact final
header-excluded application bytes while they are still valid. This lets a
future adapter call the carrier's packet-bound confirm/commit functions without
retaining a second packet copy.

The callbacks are synchronous and connection-local. They may not retain byte
pointers, recursively transmit on the same channel, or destroy that channel.
The callback pair and opaque context are copied before prepare and remain the
pair used for that transmit attempt even if registration changes later.

## Transaction and outcome model

Prepare returns one of two intentional states:

- `BYPASS`: send the untouched legacy application, create no token, and issue
  no completion; or
- `PREPARED`: validate the candidate metadata, replace the complete application
  slice transactionally, and establish a completion obligation.

An unknown return value or invalid ABI, size, reserved field, or candidate
length never partially edits the live packet. Netchan sends the byte-identical
legacy fallback and completes the supplied token once with `PREPARE_INVALID`.
The separately staged candidate is initialized before prepare, so an internal
adapter bug cannot disclose uninitialized stack bytes.

For a valid prepared candidate, netchan attempts every requested packet copy.
It does not short-circuit after a success or failure. Completion reports:

- `ACCEPTED` when at least one `NET_SendPacket` call accepted its copy;
- `NOT_ACCEPTED` when no copy was accepted, including a zero-copy call; and
- the exact `accepted_copies` count so partial packet-duplication success is
  observable without inventing an ambiguous aggregate result.

This interpretation matches the existing transport: deliberate impairment
loss or delayed-queue admission returns accepted because netchan has handed off
ownership locally, while a synchronous local rejection returns not accepted.
Every non-bypass prepare receives exactly one completion. An invalid candidate
still receives a terminal rejection even if the legacy fallback is physically
accepted, so a future native DATA dispatch or ACK emit token cannot be left
active accidentally.

Netchan's existing sequence advancement, reliable bookkeeping, `last_sent`,
packet-duplication loop, return-byte accounting, and legacy fallback remain in
their original ownership domain.

## Fragment policy

The hook is intentionally absent from both NEW-channel fragmentation paths:

- an already pending fragment calls `Netchan_TransmitNextFragment` before hook
  preparation; and
- an oversized newly assembled legacy stream is copied to the existing
  fragment buffer and begins legacy fragmentation before the hook boundary.

That restriction avoids stretching one token-bearing application transaction
across later frames, where a synchronous prepare/transport/terminal guarantee
would no longer hold. A future WTC1 adapter must instead ensure that each
complete WTC1 application packet fits both `min(1200,
netchan.maxpacketlen)` and the direction's current reserve. WNE1 owns native
message fragmentation; netchan must not fragment an individual WTC1 carrier
packet.

## Default behavior and teardown

No production call site currently invokes `Netchan_SetApplicationTxHook`.
Unregistered NEW channels follow the original branch and OLD channels never
enter the seam. `Netchan_Close` already frees the reliable allocation and
zeroes the complete `netchan_t`, which clears callbacks and opaque context as
part of connection teardown.

This change does not add a cvar or a partial rollout mode. Registration belongs
to a later per-connection native adapter after confirmed capability and
connection-incarnation setup. Keeping the seam dormant avoids presenting an
unproved half-protocol as an operator option.

## Validation evidence

Meson registers:

- `network-netchan-application-tx-hook`; and
- `network-netchan-application-tx-hook-layout`.

The focused slice passes 2/2, and ten complete repetitions pass 20/20.
Behavioral coverage includes:

- C and C++ fixed-layout checks for all three V1 records;
- default passthrough and explicit bypass;
- exact reliable-plus-unreliable input with sequence/ack/qport excluded;
- valid full-slice replacement and zero-length legacy input;
- invalid candidate metadata with byte-identical legacy fallback and exactly
  one `PREPARE_INVALID` completion;
- all-attempt packet duplication with any-success aggregation and zero-copy
  rejection;
- existing and newly selected legacy-fragment bypass;
- registration, mixed-pair rejection, OLD-channel rejection, and close-time
  clearing; and
- callback-pair/opaque freezing when prepare changes registration.

Additional current Windows x86_64 Clang evidence:

- strict C11 and C++20 builds pass with
  `-Wall -Wextra -Wpedantic -Werror` after the project's existing anonymous-
  struct compatibility suppression;
- C11 `i686-pc-windows-msvc` syntax and C++ fixed-layout checks pass;
- AddressSanitizer plus UndefinedBehaviorSanitizer is clean;
- Clang static analysis is clean;
- the complete networking suite passes 92/92 and three complete repetitions
  pass 276/276;
- the production client and dedicated engine targets rebuild and link;
- `tools/refresh_install.py` refreshes and validates 16 Windows root runtime
  files plus the `basew` runtime and 308-asset `pak0.pkz`;
- the staged schema-v3 clean/impaired runtime smoke passes with 387/387 and
  384/384 canonical cgame publications respectively, zero parity mismatch, and
  zero consumer rejection;
- staged client and dedicated engine DLL SHA-256 values match their final build
  outputs; and
- `q2proto/` remains untouched and repository search finds no production hook
  registration.

## Historical live-adapter promotion checklist

This seam is necessary but does not make the native carrier live-ready. The
next `FR-10-T04` promotion slice must still:

1. Allocate one non-reused process-local owner identity per connection
   incarnation and create native TX/RX, WTC1 gate, dispatch, and receipt-ledger
   state only after capability confirmation.
2. Register client and server command-shadow adapters without advertising the
   native capability, using the exact final application pointer to perform
   carrier confirm/reject and ACK commit/reject synchronously.
3. Add the receive seam only after ordinary `Netchan_Process` admission. It
   must parse from the message's current `readcount`, validate the WTC1 trailer,
   and expose only the declared legacy prefix to existing q2proto parsing.
4. Add an explicit server-to-client readiness barrier. Server transmission must
   not begin merely because negotiation bits exist; both endpoint adapters and
   epochs must be installed first.
5. Schedule ACK-only sends independently of DATA and legacy traffic, while
   preserving the serialized prepare/handoff/terminal section required by the
   retained ledger's one-way-loss guarantee.
6. Define demo, MVD/GTV, spectator, reconnect, downgrade, unknown-outcome,
   queue-pressure, and teardown behavior before any capability advertisement.
7. Run command-shadow parity, malformed-input, loss/duplication/reorder,
   ACK-loss, PMTU, 1/8/16/32-client load, soak, cross-platform, and rollback
   gates before promoting snapshots, events, or a native default.

The receive seam, live registration, readiness protocol, and advertisement are
intentionally not inferred from this transmit hook.

## Production integration update (2026-07-14)

Eligible NEW client/server connections now register this hook behind the
default-off native-shadow controls. The client uses it for one retained command
DATA observation; the server uses it for ACK-only receipts and can wake an idle
active client only after rate and pending-fragment gates. Exact completion
tokens retain the prior transactional contract. The earlier no-production-
registration statement is superseded only for this pilot; capability
advertisement remains absent. See
`docs-dev/fr-10-t04-one-shot-native-command-shadow-production-pilot-2026-07-14.md`.
