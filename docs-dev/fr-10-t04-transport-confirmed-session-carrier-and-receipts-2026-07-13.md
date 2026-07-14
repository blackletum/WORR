# FR-10-T04 Transport-Confirmed Session Carrier and Receipts

Date: 2026-07-13  
Primary project task: `FR-10-T04`  
Supporting tasks: `FR-10-T14`, `FR-10-T16`  
Status: session-to-WTC1 dispatch and retained-ACK foundations are complete and
now serve the default-off one-command production observation linked below;
mixed DATA/ACK packing, capability advertisement, and dual-adapter acceptance
remain open

## Outcome

WORR now has the transport-neutral state machines required to connect the
native WNE1 session/retention core to the bounded WTC1 packet carrier without
claiming that bytes were sent before a transport accepted them.

The implementation adds three related pieces:

- a reusable non-mutating native TX selection ticket in
  `inc/common/net/native_session.h` and `src/common/net/native_session.c`;
- a per-connection, transport-confirmed WTC1 DATA dispatch gate in
  `inc/common/net/native_carrier_session.h` and
  `src/common/net/native_carrier_session.c`; and
- a bounded, owner/provenance-checked receipt ledger and serialized ACK-only
  handoff scheduler in
  `inc/common/net/native_carrier_ack.h` and
  `src/common/net/native_carrier_ack.c`; and
- a dormant transactional NEW-channel transmit seam in `inc/common/net/chan.h`
  and `src/common/net/chan.c` that sees the complete final application slice and
  reports exact synchronous packet-copy acceptance.

This remains an isolated foundation. `WORR_NET_CAP_NATIVE_ENVELOPE_V1` is not
advertised, no live client/server packet path invokes these APIs, legacy
q2proto traffic remains authoritative, and `q2proto/` is unchanged.

## Why the first bridge shape was rejected

The first local adapter prototype initialized a fragment burst only after
`Worr_NativeTxSessionSelectDueV1`. That existing selector deliberately records
a send attempt, timestamp, and dispatch age immediately. It is correct for an
already-submitted transport, but not for a multi-packet WTC1 burst:

- a later PMTU/reserve failure could leave an attempt recorded without any
  packet;
- a partial transport failure could make an incomplete message ACK-eligible;
- a detached caller-owned burst could be copied and replayed; and
- receipts had no retained ACK-only path when traffic was one-way.

Independent review classified the missing two-phase lifecycle as a correctness
blocker for live promotion. The post-selection prototype was replaced rather
than documented as a live-ready helper.

## Non-mutating native send tickets

`Worr_NativeTxSessionPrepareDueV1` chooses the same due slot, priority-aging
order, and sequence tie-break as `Worr_NativeTxSessionSelectDueV1`, but leaves
the TX session and every slot byte-identical. Its fixed 104-byte ticket binds:

- the confirmed transport epoch;
- a non-zero process-local connection-owner identity;
- slot index;
- selection tick and resend interval; and
- the exact pre-send retained-slot image.

`Worr_NativeTxSessionPreparedValidateV1` rechecks that proof between fragments.
Enqueueing unrelated work may continue, but ACKing or superseding the selected
slot makes the ticket stale. `Worr_NativeTxSessionConfirmPreparedV1` is called
only after the whole burst has been accepted for transport. It transactionally
applies the same attempt count, send timestamp, priority aging, dispatch-clock
rebase, and telemetry mutation as the selected path of the original scheduler.

The first confirmation changes the retained slot, so the same ticket or any
copy is no longer valid. Saturated attempt counters still require a strictly
newer completion tick, ensuring at least one retained-slot field changes. The
shared scheduler refactor also corrected the `UINT64_MAX` rebase boundary when
occupied slots legitimately all had `enqueue_dispatch == UINT64_MAX`.

The connection-owner identity is deliberately not serialized. A future live
adapter must allocate a fresh incarnation value that is not reused while any
derived object or copied token can survive, and keep it stable while advancing
that connection's transport epoch. A monotonically allocated process-local ID
is recommended. It prevents same-epoch objects from different clients or stale
incarnations from being cross-paired by local integration errors.

## Per-connection DATA dispatch gate

One 80-byte `worr_native_carrier_tx_gate_v1` is owned by one connection owner
and transport epoch. It permits exactly one active native message, consumes a
monotonically increasing dispatch token, and keeps the authoritative
confirmed-fragment cursor outside caller-copyable dispatch state. Its
committed, aborted, and accepted-but-stale retired counters are saturating.

The lifecycle is:

1. `DispatchBeginV1` reserves the gate and obtains a non-mutating native send
   ticket. It checks the direction's real application budget and the retained
   slot's already-frozen WNE1 fragmentation plan. Native state is unchanged.
2. `DispatchBindPayloadV1` resolves the process-local payload handle and binds
   immutable canonical bytes plus their WNE1 payload CRC.
3. `DispatchPreparePacketV1` builds one DATA-only WTC1 packet. It advances only
   a pending fragmenter and binds the complete application packet's length and
   full CRC, including the legacy prefix. A second build is rejected
   while that outcome is pending.
4. If synchronous netchan handoff rejects the packet,
   `DispatchRejectPacketV1` clears only the pending image and permits an exact
   rebuild of the same fragment.
5. If handoff accepts the prepared packet, `DispatchConfirmPacketV1` validates
   its length, full CRC, nested WNE1 identity, gate token, retained-slot ticket,
   and monotonic tick before advancing the confirmed cursor. These CRC checks
   are accidental-corruption guards, not adversarial byte-identity proof.
6. Final-fragment confirmation atomically records the native send attempt,
   consumes the gate token, and returns `DISPATCH_COMMITTED`. If the selected
   identity was concurrently acknowledged or superseded after the accepted
   packet was prepared, confirmation instead consumes the known handoff,
   returns `DISPATCH_RETIRED`, clears the gate, and leaves native send
   accounting untouched.
7. `DispatchAbortV1` can consume an active token only when no packet has an
   unknown outcome. Confirmed partial receiver state remains unacknowledged and
   expires normally; native send accounting remains unchanged.

The gate's pending flag and confirmed cursor make copied dispatch objects
stale after any build outcome. A newer snapshot may supersede an active old
snapshot; the next validation stops further fragments, and abort releases the
gate without clobbering the newer retained slot. ACK processing and unrelated
enqueues may continue while a burst spans packets.

The DATA dispatch deliberately does not yet accept arbitrary ACK arguments.
Receipts are emitted through the authoritative ledger below, preventing a
caller-created range from acquiring receipt authority. DATA piggyback can be
added later by passing a ledger-generated prepare token and committing the DATA
and ACK handoff mutations together. ACK-only emission already provides the
required liveness path.

## Exact receipt ledger and ACK-only liveness

`worr_native_carrier_ack_ledger_v1` reserves storage for up to 80 exact
committed message sequences: the native RX core's 64 receipt-history identities
plus the bounded snapshot-tombstone storage. Current snapshot authority admits
only committed tombstones; the larger allocation is a conservative fixed ABI
bound. There is intentionally no public general-purpose `enqueue_ack(range)`
or caller-authored repeat function.

The pointer-free ACK ledger is 2,152 bytes and embeds one 120-byte active emit
token. The token carries owner, epoch, non-wrapping token ID, ledger generation,
prepare tick, optional full-packet binding, and at most eight exact ranges. The
DATA gate is 80 bytes and its caller-owned dispatch image is 248 bytes; all are
covered by C/C++ and i686 fixed-layout assertions.

Receipt ingress is limited to:

- `Worr_NativeCarrierSessionCommitRetainedV1`, which stages the RX session,
  slots, and ledger, invokes the canonical RX commit, imports only its exact
  singleton receipt, and commits all three objects atomically; and
- `Worr_NativeCarrierSessionAcceptDataRetainedV1`, which performs admitted
  carrier DATA intake and captures the native RX core's one-shot repeat output
  internally only when that same call returns `ALREADY_COMMITTED`. The owner,
  epoch, singleton sequence, and retained RX authority must all agree before
  the receipt is rearmed.

Reconciliation retires ledger identities no longer represented by those
bounded authoritative RX sets and imports any missing authoritative identity.
The TX session's 64-sequence reliable receipt-window gate prevents an
unacknowledged reliable record from legitimately aging beyond the RX proof
window. Snapshot tombstones preserve the separate supersedable identity cases.

Range preparation chooses forced, never-handed-off, and least-recently-handed-
off due receipts fairly. Adjacent due committed sequences may be coalesced,
but an absent sequence is never bridged. Success reserves the ledger's sole
owner-bound emit token; another prepare or receipt mutation is rejected until
that short synchronous handoff transaction reaches a terminal state. A WTC1
packet may carry at most eight ACK-only ranges or seven ranges beside one DATA
entry.

ACK-only packet preparation fits:

```text
complete_legacy_prefix + 32-byte WTC1 footer + 16 bytes per ACK range
```

against `min(1200, netchan.maxpacketlen)`. One ACK therefore needs at least 48
bytes after the complete legacy prefix. `PreparePacketV1` returns an already
bound token; a ranges-only future piggyback caller must call `BindPacketV1`
after final packet assembly and before transport. Binding records the full
application packet's length and CRC, including legacy and DATA bytes, as well
as its exact ordered ACK subsequence. After synchronous handoff, commit
rechecks owner, epoch, monotonic tick, token identity, length, full CRC, and ACK
identity before spending retry credits. A definite rejection uses
`RejectHandoffV1`; `AbortV1` is legal only for an unbound token known never to
have reached transport. Every copied token becomes stale at bind or terminal
mutation. These CRCs are accidental-corruption/replay guards, not
authentication.

Receipt commit and repeat intake are serialized against an active emit token.
This intentionally small critical section prevents a reentrant mutation after
physical handoff from making commit stale and allowing a sent ACK to escape
the configured one-to-eight proactive accounting bound. Prepare/bind,
synchronous transport handoff, and commit/definite-reject must execute as one
serialized section. An unknown bound-packet outcome deliberately remains
active and blocks receipt ingress until the exact outcome is resolved or the
connection epoch is torn down; it must never be guessed into an abort.

Each receipt receives a configured one-to-eight proactive handoff budget. The
recommended value is three. After those successful handoffs it remains
dormant, avoiding endless ACK-only chatter. If every proactive copy was lost,
the sender retransmits DATA; RX returns `ALREADY_COMMITTED`, and authoritative
repeat capture immediately rearms that exact receipt. This supplies bounded
one-way liveness under eventual delivery without ACK-of-ACK state. The live
loop must therefore poll ACK-only emission independently of DATA and legacy
traffic; merely piggybacking when other traffic exists would not realize this
one-way recovery guarantee.

## Packet budget and whole-burst invariant

`Worr_NativeCarrierSessionDataBudgetV1` computes:

```text
application budget
- complete legacy reserve
- 32-byte WTC1 footer
- 8-byte DATA entry header
- 16 bytes per reserved ACK entry
```

The application budget may not exceed 1,200 bytes, and a DATA packet can
reserve no more than seven ACK ranges. The result must fit the 56-byte WNE1
header plus at least one payload byte.

At the current 1,024-byte client application ceiling, zero legacy/ACK reserve
permits a 984-byte WNE1 datagram and a 928-byte payload stride. WNE1's 64
fragments therefore carry at most 59,392 bytes, below the canonical codec's
65,536-byte ceiling. A future live enqueue adapter must prove the complete
legacy/ACK reserve and whole-message capacity before allocating a transport
sequence. It may not silently replan an already-retained sequence after PMTU or
prefix growth.

## Security and live integration boundary

WTC1 CRC and the dispatch packet CRC are accidental-corruption/replay guards,
not authentication. Every live call must require:

- a native session binding created from confirmed capability negotiation;
- the current non-zero connection epoch;
- a non-zero process-local incarnation identity that is not reused while any
  derived object or copied token can survive;
- normal sequenced `Netchan_Process` admission before ACK/DATA consumption;
- canonical codec identity and store/journal validation before RX commit; and
- ACK provenance only from retained commit/repeat state.

For a live native-carrier session, canonical consumers must use
`Worr_NativeCarrierSessionCommitRetainedV1` and admitted input must use
`Worr_NativeCarrierSessionAcceptDataRetainedV1`. Calling the raw native RX
commit/accept APIs around those wrappers would bypass ledger serialization and
can forfeit ACK-loss liveness; those raw APIs remain valid only for isolated
native-session use without this retained carrier ledger.

The legacy prefix passed to WTC1 must be the complete final application stream,
including reliable bytes prepended by netchan. `NetchanNew_Transmit` now has a
dormant V1 prepare/completion seam after reliable and unreliable assembly and
before physical packet duplication. It stages replacements transactionally,
leaves headers and legacy fragmentation outside the hook, reports the exact
final application bytes, and completes every token-bearing prepare exactly
once with all-copy acceptance counts. At this slice's original validation
point, no production connection registered it.
Wrapping only the caller's unreliable buffer would still produce an invalid
`legacy_bytes` boundary. WTC1 must never be attached before the new seam, and
Vulkan or renderer paths are unrelated. Detailed contract:
`docs-dev/fr-10-t04-post-assembly-netchan-hook-2026-07-13.md`.

## Validation evidence

Meson now registers the existing three native-session tests plus:

- `network-native-carrier-session`;
- `network-native-carrier-session-layout`;
- `network-native-carrier-ack`; and
- `network-native-carrier-ack-layout`.

Current Windows x86_64 Clang evidence:

- the combined envelope/session/carrier/dispatch/ACK focused matrix passes
  12/12;
- the post-assembly netchan behavior/layout slice passes 2/2 and ten complete
  repetitions pass 20/20;
- the complete networking suite passes 92/92 and three complete repetitions
  pass 276/276;
- strict C11 and C++20 builds pass with
  `-Wall -Wextra -Wpedantic -Werror`;
- the behavioral paths pass AddressSanitizer plus UndefinedBehaviorSanitizer;
- Clang static analysis is clean;
- C11/C++20 `i686-pc-windows-msvc` syntax and fixed-layout checks pass; and
- the client and dedicated engines rebuild/link, the refreshed Windows
  `.install/` validates, the staged clean/impaired runtime smoke passes, and
  `q2proto/` remains untouched.

Behavioral coverage includes non-mutating selection, rebuild after definite
transport rejection, packet checksum/identity mismatch, partial-burst
accounting, final-only send mutation, copied cursor/token rejection, payload
mutation, accepted-stale snapshot retirement, same-epoch cross-owner rejection,
unsent ACK rejection, full ACK-batch rollback, token exhaustion and epoch
reset, transactional commit capture, combined repeat provenance, serialized
prepare/bind/commit/reject/abort outcomes, full-packet mismatch with unchanged
ACKs, no-gap coalescing, eight-range ACK-only and seven-range piggyback limits,
the exact 47/48-byte ACK-only boundary, bounded three-send dormancy, repeat
rearming, a cross-component one-way-loss property that stalls at the TX
64-sequence receipt window and resumes after committed-DATA retry/reverse-path
recovery, `UINT32_MAX`, legacy prefixes, history retirement, C/C++ ABI, and
32-bit layout.

## Original remaining promotion work

1. Add default-off client and server command-shadow adapters through the
   dormant post-assembly seam. Preserve legacy authority, compare canonical
   identities/bytes, and use ACK-only receipts.
2. Add the corresponding post-`Netchan_Process` receive seam and an explicit
   endpoint-readiness barrier before either sender may emit native bytes.
3. Add atomic ledger-token DATA piggyback after ACK-only correctness is proven.
4. Add snapshot and authoritative-event adapters only after command shadow
   passes loss, duplication, reorder, ACK-loss, reconnect, downgrade, PMTU,
   malformed-input, and queue-pressure matrices.
5. Prove native versus legacy canonical parity, input age, recovery, correction,
   bandwidth, CPU, and memory budgets under 1/8/16/32-client load.
6. Complete the per-decoder 100,000-malformed-case, soak, cross-platform,
   rollback, demo/spectator, and release gates required by
   `FR-10-T13/T14/T15/T16`.
7. Advertise `WORR_NET_CAP_NATIVE_ENVELOPE_V1` only after those staged gates
   are accepted.

## Production integration update (2026-07-14)

Items 1 and 2 of the promotion sequence are now satisfied for a single
observational command: production hooks, readiness-gated retained DATA, and an
independent ACK-only reverse path are wired default-off. Exact completion
tokens bind client DATA accounting and server ACK handoff accounting; current
and retired ledgers are selected fairly, and an exact duplicate rearms receipt
delivery. Atomic mixed DATA-plus-ACK packing and all broader acceptance work
remain open. See
`docs-dev/fr-10-t04-one-shot-native-command-shadow-production-pilot-2026-07-14.md`.
