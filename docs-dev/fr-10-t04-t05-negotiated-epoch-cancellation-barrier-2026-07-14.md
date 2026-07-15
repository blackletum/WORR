# FR-10 Negotiated Native Epoch-Cancellation Barrier

Date: 2026-07-14

Project tasks: `FR-10-T04`, `FR-10-T05`, `FR-10-T14`, `FR-10-T16`

## Outcome

The default-off native command and event shadows now negotiate a monotonic
transport-epoch cancellation barrier. A fresh accepted readiness challenge is
an explicit, counted terminal disposition for every lower private transport
epoch owned by the connection. It replaces the previous current-plus-one-
retired-bank rollover model, so finite ACK-credit exhaustion and consecutive
map rotations cannot silently overwrite unresolved native state.

This is still shadow transport. Legacy commands, snapshots, demos, event
presentation, and gameplay authority remain unchanged. The milestone closes
the identified ACK-exhaustion/second-rotation lifecycle defect; it does not
complete any of the four referenced tasks.

## Negotiated capability contract

`WORR_NET_CAP_NATIVE_EPOCH_CANCEL_V1` occupies private capability bit 6,
`0x40`. Every private native readiness binding now requires both the native
envelope and the cancellation capability. The exact masks are:

| Readiness mode | Exact mask | Meaning |
|---|---:|---|
| Public legacy-stage offer/confirmation | `0x03` | Unchanged legacy command sideband plus consumed-command cursor |
| Private command shadow | `0x53` | Public `0x03`, native envelope `0x10`, cancellation `0x40` |
| Private command/event shadow | `0x73` | Command `0x53` plus native event stream `0x20` |

Bit `0x40` is carried only by private `CHALLENGE`/`CLIENT_READY` readiness
records. It is not advertised in the public `0x03` capability tuple. Missing,
downgraded, extra, or incorrectly echoed private bits fail the readiness proof;
neither endpoint guesses that cancellation is available.

For one connection owner, a fresh valid `CHALLENGE` for transport epoch `N`
declares every prior private transport epoch below `N` canceled. The client
echoes the exact capability/epoch/nonce tuple in `CLIENT_READY`, and the normal
`SERVER_ACTIVE` fence still controls new-epoch DATA authority. The cancellation
floor also covers an earlier advertised readiness epoch that never reached
transport activation. This pending-handshake coverage prevents a gap between
the last activated bank and the new epoch after consecutive challenge/map
advances.

## Common terminal-disposition APIs

Four allocation-free common APIs give each retained class an explicit local
terminal state:

- `Worr_NativeTxSessionCancelRetainedV1` releases every retained TX slot and
  reports the exact count;
- `Worr_NativeRxSessionCancelPendingV1` reports and releases complete and
  incomplete RX messages;
- `Worr_NativeCarrierAckCancelAllV1` clears and counts every exact retained
  receipt; and
- `Worr_NativeEventSenderCancelV1` transactionally releases retained messages,
  ID-less backlog candidates, and sender payload ownership.

Successful cancellation is idempotent. The TX, RX, receipt-ledger, and event-
sender objects become terminal and reject their former work until an explicit
epoch advance or fresh adapter initialization. Owner/epoch provenance, clocks,
identity high-water marks, and anti-alias state remain available for validation;
the disposition is not represented as a successful ACK or delivery.

An active packet whose transport outcome is still unknown blocks cancellation
without mutation. A known locally active dispatch can be aborted before the
terminal call. The event sender likewise refuses a prepared packet with an
unknown outcome. These preconditions prevent the barrier from retroactively
changing the meaning of a handed-off datagram.

## Transactional client and server integration

### Client

On a fresh challenge, the client validates the new monotonic epoch and builds
the complete cancellation on private copies of its current and any historical
bank state. That staged operation includes command TX retention and payload
ownership, event RX reassembly, and semantic ACK receipts. It also incorporates
the current readiness epoch in the prospective floor even when no transport
bank was activated.

The staged state becomes visible only after the exact `CLIENT_READY` record has
been appended successfully to the reliable queue. Failure before that point
leaves the live banks and floor unchanged. After the append succeeds, the
client publishes the counted dispositions, clears transport storage, and waits
for the new `SERVER_ACTIVE` proof before initializing fresh current state.

### Server

`SV_NativeShadowBeginEpochV1` prevalidates all current state, requires no
unterminated carrier/ACK emission, cancels event senders, RX sessions, and
receipt ledgers, publishes the monotonic floor and counters, and then issues the
fresh challenge. Server connection processing is single-threaded, so this
preflight makes the explicit terminal-call sequence the publish transaction.

The production server wrapper also lets `SV_NativeShadowObserveSettingV1`
finish sideband COMMIT decoding, record validation, direction/capability
classification, and cancellation-floor classification before it considers a
`SERVER_ACTIVE` reliable-queue reservation. A valid stale `CLIENT_READY` or
`CLIENT_ACTIVE_CONFIRM` needs no response, so the ordering permits it to be
consumed even when that queue is full. A current `CLIENT_READY` still requires
an exact `SERVER_ACTIVE`; failure to append that response disables the private
pilot and fails closed. Queue pressure therefore cannot turn a response-free
stale record into a current-readiness failure or let a current proof activate
without its response. This production-wrapper ordering is established by code
review and the successful production build; the focused test operates at the
adapter boundary.

The server floor takes the maximum of the previous floor, any activated current
or historical transport epoch, and `private_transport_epoch`, including a
challenge that was advertised but never activated. A successful new
`SERVER_ACTIVE` transition initializes fresh transport and event-sender state;
it does not rotate a current bank into a retired slot.

## Old-carrier admission policy

Both production receive hooks fully decode and structurally validate a native
candidate before consulting the cancellation floor. This preserves the strict
parser boundary:

- a structurally valid carrier at or below the cancellation floor has its
  native suffix stripped and exposes its byte-identical legacy prefix;
- that carrier performs no command, event, receipt, sender, cgame, or readiness
  mutation and increments the stale-canceled-carrier counter; and
- malformed, truncated, directionally invalid, or corrupt old traffic is still
  rejected. Cancellation is never a permissive parser bypass.

There are no retired transport banks, release rings, or multi-epoch recovery
queues after the barrier. A second rotation simply advances the monotonic floor
and starts another fresh current epoch. Delayed old DATA cannot reach cgame,
and delayed old ACK cannot release or rearm any current sender.

The same floor applies to delayed reliable readiness control records after
their full record/CRC, direction, capability, and shape validation. The server
consumes valid old `CLIENT_READY` and `CLIENT_ACTIVE_CONFIRM` records without
changing lifecycle state. The client similarly consumes valid old `CHALLENGE`
and `SERVER_ACTIVE` records without requeuing readiness or reopening DATA. Each
endpoint increments `stale_cancelled_readiness_records`. Malformed, wrong-
direction, or capability-downgraded control records still fail closed; the
cancellation floor cannot turn an invalid handshake into an ignorable one.

## Event-owner and cgame invariants

Transport cancellation does not reset the connection-owned semantic event
high-water. Map quiesce and map rotation preserve the event owner's monotonic
epoch/sequence history; only full disconnect creates a new connection owner and
resets that lifetime. This prevents reuse of a canceled transport epoch from
aliasing an older semantic event lineage.

Current-epoch descriptor/event admission and repeats still require the exact
binding plus a fresh cgame status/receipt proof before semantic ACK authority is
published. Cancellation neither manufactures a cgame receipt nor treats
transport disposal as event presentation. The lifecycle gate records no extra
cgame submit or reset from delayed canceled DATA, while the ordinary impairment
schedule still presents the one accepted event exactly once.

## Telemetry

The client status surface now exposes:

- `cancelled_through_epoch`, `cancellation_barriers`, and
  `cancelled_transports`;
- `cancelled_command_tx`, `cancelled_event_rx`, and
  `cancelled_event_receipts`; and
- `stale_cancelled_carriers` and `stale_cancelled_readiness_records`.

The server peer status exposes the same floor/barrier/transport information plus
`cancelled_rx_messages`, `cancelled_receipts`,
`cancelled_event_records`, `stale_cancelled_carriers`, and
`stale_cancelled_readiness_records`. Counters saturate and separate an explicit
terminal disposition from ordinary ACK, timeout, drain, or parser rejection
outcomes.

Server status also reports `wire_committed_transport_epoch` separately from
`transport_epoch`. The former identifies the epoch whose native wire point of
no return remains committed; the latter identifies the current readiness/
challenge epoch. During a replacement challenge they can intentionally differ,
making prior committed-wire fallback unambiguous instead of attributing it to
the not-yet-active replacement epoch.

## Integration evidence

The final cancellation-focused Windows Clang gate passes 8/8. The complete
registered networking suite passes 121/121, and three consecutive complete
repetitions pass 363/363. These tests use standalone/headless targets; no
interactive client was launched.

The adapter regressions additionally delay each reliable control direction
past a floor advance: old `CLIENT_READY`/`CLIENT_ACTIVE_CONFIRM` are consumed by
the server and old `CHALLENGE`/`SERVER_ACTIVE` by the client without readiness,
cgame, or transport mutation. The dedicated stale-readiness counters advance,
while malformed, wrong-direction, and downgraded variants still reject.

The parser-shared server decision helper now classifies a stale `CLIENT_READY`
as consumed before it considers `SERVER_ACTIVE` capacity. Its regression uses a
physically full reliable buffer for both that response-free stale record and a
current record, which fails closed without transport initialization or native
wire commitment. `SV_ExecuteClientMessage` uses this exact helper. Status
coverage also holds a prior committed wire epoch across a replacement challenge
and proves
`wire_committed_transport_epoch` remains distinct from the new
`transport_epoch` until the new wire epoch commits.

The production-wrapper golden row is:

```text
native_event_virtual_link_test: ok converged=1 s2c_loss=1 c2s_loss=2 ack_loss=1 reordered=2 duplicates=3 corrupt_s2c=1 corrupt_c2s=1 cancelled_ack_strip=1 cancelled_data_strip=1 cancelled_corrupt_reject=2 presented=1 digest=be9724b38fb5f682
```

The hard lifecycle row is:

```text
native_event_virtual_link_test: lifecycle_diagnostic ack_handoffs=3 due_after_exhaustion=0 old_retained=1 client_cancelled_receipts=1 server_cancelled_events=2 client_floor=6 server_floor=6 retired_receipts=0 retired_retained=0 release_due=0 second_rotation_no_retired=1 delayed_old_ack_strip=1 delayed_old_data_strip=1 corrupt_old_rejections=2
```

This proves the reproduced three-credit exhaustion begins with unresolved old
state, then terminates it as counted cancellation, leaves no historical bank or
release work, survives a second rotation without overwrite, strips valid old
traffic to legacy-only behavior, and rejects corrupt old traffic.

The production client engine, dedicated-server engine, cgame, and sgame modules
all build successfully. The refreshed Windows x86-64 `.install/` stage validates
16 root runtime files, one dependency, a `pak0.pkz` packed from 342 source asset
files, 31 botfiles, and 215 RmlUi assets.

The parser-shared capacity-gate follow-up is detailed in
`docs-dev/fr-10-t04-parser-shared-stale-readiness-capacity-gate-2026-07-15.md`.

## Compatibility and remaining work

No file under `q2proto/` changed. Legacy Q2 packet parsing, demos, commands,
snapshots, and event presentation remain authoritative, and the native shadows
remain unadvertised and default-off.

Still open are native acknowledged-baseline snapshots, broader event families,
multiple distinct out-of-order events/selective receipts, full prediction and
presenter cutover, production `NetImpair` golden coverage, live multi-process
fault breadth, malformed corpora, 1/8/16/32-client budgets, load/soak, and the
cross-platform/release matrices. Accordingly, `FR-10-T04`, `FR-10-T05`,
`FR-10-T14`, and `FR-10-T16` all remain In Progress.

Roadmap completion remains unchanged: 68 of 180 tasks complete (37.8%, 112
open); FR-10 remains 3 of 16 complete (18.75%, 13 open).
