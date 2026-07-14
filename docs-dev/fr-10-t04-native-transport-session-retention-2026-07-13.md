# FR-10-T04 native transport session and retention foundation

Date: 2026-07-13

Project task: `FR-10-T04`

Status: core and transport-confirmed tickets implemented and tested, then used
by the default-off one-command production observation linked below; no
capability advertisement, demo carrier, or general production adapter

## Purpose and boundary

This change adds the session and lifetime policy that sits around the
previously implemented native WORR envelope. It deliberately does not turn
that envelope on. `WORR_NET_CAP_NATIVE_ENVELOPE_V1` remains outside
`WORR_NET_CAP_LEGACY_STAGE_MASK`, no client or server advertises it, and no
legacy q2proto path was changed.

The implementation consists of:

- `inc/common/net/native_session.h`, the versioned C ABI;
- `src/common/net/native_session.c`, the strict C11 policy core;
- `tools/networking/native_session_test.c`, behavioral and hostile-order
  coverage;
- C and C++ layout tests; and
- three networking-suite Meson targets.

Canonical command, snapshot, and event bytes remain opaque. The session core
stores only canonical record references, transport identity, caller-owned
payload handles, sizes, scheduling state, and receipt state. It does not cast,
hash, decode, or duplicate a gameplay schema.

## Negotiated epoch binding

`worr_native_session_binding_v1` can be constructed only from a valid,
confirmed `worr_net_capability_state_v1` whose negotiated mask includes
`WORR_NET_CAP_NATIVE_ENVELOPE_V1`. Both TX and RX initialization require this
binding and copy its nonzero connection epoch as the transport epoch. Binding
initialization also requires a nonzero process-local `connection_owner_id`
that identifies one connection incarnation. It must not be reused while any
binding-derived object or copied token from an earlier incarnation can survive;
monotonic process-local allocation is recommended. The owner is copied into
TX/RX sessions, prepared-send tickets, ACK ranges, and completed RX messages
but is never serialized. This prevents same-epoch state belonging to different
clients or stale incarnations from being cross-paired by an integration error.

This gives the isolated core the same fail-closed session identity expected of
a future live adapter without changing current capability selection. An
in-connection advance requires the same owner plus a strictly newer negotiated
epoch and clears
all retained messages, reassembly slots, replay state, snapshot high-water,
sequence allocation, scheduling age, and telemetry. A genuinely fresh
reconnect object may call initialization directly; this permits a restarted
server to begin from a numerically lower epoch without reusing the old object.

## Pointer-free TX retention

`worr_native_tx_session_v1` and `worr_native_tx_slot_v1` are pointer-free.
Callers own the bounded slot array and pass it to every operation. A nonzero
`payload_handle` names process-local caller storage; the core never
dereferences it. The caller must preserve byte identity and lifetime until the
corresponding slot is acknowledged or superseded.

Each retained message also freezes its native-envelope fragmentation plan.
`Worr_NativeTxSessionEnqueueV1` accepts `max_datagram_bytes` and stores the
derived `fragment_stride` and `fragment_count` in the TX slot. Every first send
and retry must initialize the fragmenter with
`fragment_stride + WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES` from the selected
slot. A PMTU change may affect newly enqueued messages, but cannot silently
change an existing transport sequence's immutable identity.

### Message sequences and exhaustion

Every retained message receives one nonzero, monotonically increasing
transport sequence. Sequence zero is invalid. `UINT32_MAX` may be allocated
once; the session then enters an explicit exhausted state and refuses further
allocation. It never wraps within one transport epoch.

Capacity, duplicate, conflict, stale-snapshot, receipt-window, and sequence-
exhaustion failures do not consume a sequence. This makes allocation
deterministic and prevents failed enqueue attempts from manufacturing holes.

### Reliable records versus supersedable snapshots

Commands and events are reliable retained records. They are never evicted to
make room for another message. A full reliable set therefore returns an
explicit capacity result.

Snapshots are the only supersedable class. At most one snapshot occupies the
TX set. A snapshot with a strictly newer canonical
`{object_epoch, object_sequence}` replaces that resident snapshot atomically,
including when the set is otherwise full. Reliable slots are untouched. The
snapshot canonical high-water survives acknowledgement so an already-acked
or superseded snapshot cannot be re-enqueued as new work in the same epoch.

The same canonical reference with different payload handle, payload size,
fragmentation plan, or priority is a conflict rather than a duplicate.
Payload handles are unique among retained slots, preventing ambiguous
caller-owned lifetime.

### Exact acknowledgements and the 64-sequence safety bound

`worr_native_ack_range_v1` is an inclusive exact range. Applying it removes
only resident transport sequences inside that range. It never implies receipt
below a cumulative prefix and accepts a single-message receipt with
`first == last`, which is what RX commit emits.

Commit acknowledgements are reproducible rather than one-shot. The required
`repeat_acknowledgement_out` argument to
`Worr_NativeRxSessionAcceptV1` is written only when an exact committed
duplicate returns `WORR_NATIVE_RX_ALREADY_COMMITTED`. The caller can send that
one-message acknowledgement again, so loss of the first commit ACK cannot
retain a command, event, or snapshot forever. A conflicting reuse never
receives an ACK and leaves the repeat-ACK output untouched.

RX replay state represents the latest 64 transport sequences. To prove that a
legitimate reliable record cannot age out of this bounded window, TX refuses
to allocate a candidate whose distance from its oldest unacknowledged command
or event reaches 64:

```text
candidate_sequence - oldest_unacked_reliable < 64
```

Distance 63 is accepted; distance 64 returns
`WORR_NATIVE_TX_RECEIPT_WINDOW`. Superseded snapshots still consume transport
sequence identity, so rapid snapshot replacement cannot silently push an old
reliable command/event outside the receiver's anti-replay window. Exact
acknowledgement of the old reliable record releases the gate.

### Deterministic priority aging

Selection retains the native envelope queue's proven aging quantum rather
than introducing a weaker retry scheduler. Lower numeric priority remains
more urgent, but every eight intervening successful dispatches promotes a
waiting record by one priority level. A priority-7 record therefore reaches
effective priority zero after at most 56 intervening dispatches.

Ties choose the oldest dispatch age and then the lowest transport sequence.
The selected retained slot is re-aged from the current dispatch count, so a
priority-zero retry cannot permanently monopolize the queue. The 64-bit
dispatch clock has a deterministic rebase path at exhaustion; it does not
wrap.

### Transport-confirmed prepared sends

The later WTC1 integration review found that the immediate mutation performed
by `Worr_NativeTxSessionSelectDueV1` cannot safely begin a multi-packet burst:
transport rejection or a partial burst would otherwise look like a complete
send attempt. The session core now also provides a 104-byte, pointer-free
`worr_native_tx_send_ticket_v1` and three reusable operations:

- `Worr_NativeTxSessionPrepareDueV1` chooses the same due record without
  changing the session or slot array;
- `Worr_NativeTxSessionPreparedValidateV1` rechecks the exact pre-send slot
  between fragments; and
- `Worr_NativeTxSessionConfirmPreparedV1` applies send-attempt, timestamp,
  dispatch-aging/rebase, and telemetry mutation transactionally only after a
  whole transport burst has been accepted.

Confirmation changes the retained slot, so a replayed or copied ticket fails
its exact baseline check. The shared selector refactor also corrected the
dispatch-rebase sentinel when every occupied slot legitimately has
`enqueue_dispatch == UINT64_MAX`. The carrier-side gate and receipt ledger are
documented in
`docs-dev/fr-10-t04-transport-confirmed-session-carrier-and-receipts-2026-07-13.md`.

## Caller-owned RX routing and lifecycle

`worr_native_rx_session_v1` and `worr_native_rx_slot_v1` are pointer-free.
The caller supplies:

- up to 16 routing/reassembly slots;
- a contiguous payload arena divided into one fixed-stride region per slot;
- a fragment timeout in caller ticks; and
- a completed-message timeout in caller ticks.

The router decodes the envelope into a local header, checks the negotiated
transport epoch, routes `{transport_epoch, message_sequence}` to a slot, and
delegates fragment validation/reassembly to the existing native envelope
core. Payload bytes remain in the caller arena.

Caller ticks must be monotonic. Expiry is evaluated only when the caller
invokes accept or expire; there is no wall-clock dependency or hidden timer.
Novel fragments refresh activity. Duplicate fragments do not, so repeated
duplicates cannot keep an incomplete slot alive indefinitely.

Completion does not immediately acknowledge or free a slot. It returns a
versioned descriptor naming the caller-owned arena region. The canonical
consumer must then choose one of two explicit outcomes:

- commit after accepting the canonical payload, which records replay
  identity, emits an exact one-message acknowledgement, and frees the slot;
- discard after rejecting or abandoning it, which frees without
  acknowledging and permits a reliable resend.

Complete and incomplete slots use separate timeout policies. Timeout frees
without acknowledging.

## Anti-replay receipt window

RX keeps a highest committed transport sequence, a backward 64-bit selective
mask, and an exact message identity for every set bit. Bit zero names the
highest committed sequence; bit `n` names `highest - n`. Out-of-order commits
inside the represented window are valid and fill their exact bit.

When the high-water advances, identities outside the 64-sequence window are
removed. A datagram older than the represented window returns
`WORR_NATIVE_RX_STALE_REPLAY`; it cannot allocate a slot or reach the
canonical consumer. A replay inside the window must match the full immutable
message identity: canonical record reference, payload size and CRC, fragment
stride and count, and priority. A different identity reusing the same
transport sequence is a message conflict. An exact committed duplicate
returns a reproducible exact ACK without a second canonical delivery.

Advancing the commit high-water also evicts any already resident slot that
has just become older than the window. This closes the hostile order where a
sequence-1 slot is retained, sequence 100 is committed, and sequence 1 is
then committed: the old slot is released before a shift can occur, no shift
count can exceed 63, and its later datagrams are stale replay.

The TX distance invariant above proves that a conforming peer cannot cause a
legitimate unacknowledged reliable record to be evicted this way. The RX
behavior is nevertheless fail-closed for hostile or nonconforming peers.

## RX snapshot freshness and retry identity

RX tracks the latest *committed* canonical snapshot
`{object_epoch, object_sequence}` for its transport epoch. Merely seeing a
header, accepting a fragment, completing reassembly, timing out, discarding,
or detecting a whole-message checksum failure cannot advance this high-water.
Only canonical-consumer commit can make an older canonical snapshot stale.

This separates integrity from freshness and preserves a usable prior object:

- complete snapshots may coexist while the canonical consumer decides which
  one to commit;
- a higher incomplete or corrupt object cannot evict a prior complete object;
- committing a snapshot atomically advances canonical freshness and removes
  resident snapshots at or below the committed identity;
- a snapshot below the committed high-water returns
  `WORR_NATIVE_RX_STALE_SNAPSHOT`; and
- reuse of either a canonical snapshot identity or a transport message
  sequence with different immutable identity is a message conflict.

When all slots are occupied, a higher snapshot may supersede an older
*incomplete* snapshot, but never a complete snapshot or reliable record. This
retains progressive snapshot supersession without allowing an unverified
object to destroy a deliverable prior state.

RX retains up to 16 pointer-free `worr_native_snapshot_identity_v1` entries.
A retry identity records the full immutable header identity when a snapshot
slot times out, is explicitly discarded, loses a whole-message checksum, or
is superseded while incomplete. The exact object can reassemble again even if
its transport sequence has since fallen behind the ordinary replay window;
same-sequence or same-canonical conflicts remain fail-closed. The bounded set
uses touch-to-tail refresh and evicts retry identities deterministically while
preserving the one current committed snapshot identity. Once an exact retry
has allocated a slot, `WORR_NATIVE_RX_SLOT_SNAPSHOT_RETRY` carries that
authorization with the active reassembly, so later cache pressure cannot
poison or stale-evict the live retry.

Commit converts that canonical entry to a committed identity. It reproduces
an exact ACK for a matching duplicate even after the corresponding ordinary
64-entry receipt has aged out. This provides snapshot ACK-loss liveness
without permitting duplicate canonical delivery.

## Transactionality and telemetry

Every public entry validates version, capacity, state invariants, and memory
range separation. State envelopes, slot arrays, payload arenas, input
datagrams, and outputs may not overlap in combinations that could invalidate
an operation. Alias rejection occurs before mutation, including output
mutation.

Malformed, unsupported, corrupt, and wrong-epoch datagrams do not change slot
state or payload bytes. Their saturating telemetry counters are the sole
permitted session-state change. Outputs are untouched unless their documented
result writes them. `message_out` is written only for `MESSAGE_COMPLETE`, and
`repeat_acknowledgement_out` only for `ALREADY_COMMITTED`; both are required,
pairwise distinct, and range-checked against all state, storage, datagram, and
arena regions. Whole-message checksum failure releases that attempt and may
leave its caller-owned scratch region unspecified, but preserves an exact
snapshot retry identity and emits no completed descriptor.

Telemetry is pointer-free, deterministic, and saturating. It distinguishes
first sends/retries, scheduler rebases, snapshot supersession, reliable and
snapshot acknowledgements, receipt-window stalls, fragment duplicates,
capacity/storage stalls, malformed/corrupt input, conflicts, timeout class,
stale replay, stale snapshot, repeat acknowledgements, retry-identity
evictions, commit, and discard.

## Versioned ABI layout

The public state remains pointer-free and C/C++ layout-checked. The revised RX
and TX types are:

- `worr_native_session_binding_v1`: 24 bytes, with the owner at offset 16;
- `worr_native_ack_range_v1`: 32 bytes, with the owner at offset 24;
- `worr_native_tx_slot_v1`: 64 bytes, including immutable
  `fragment_stride` and `fragment_count`, with `enqueue_tick` at offset 32 and
  `enqueue_dispatch` at offset 48;
- `worr_native_tx_send_ticket_v1`: 104 bytes, with its exact pre-send slot at
  offset 32 and owner at offset 96;
- `worr_native_tx_session_v1`: 216 bytes, preserving telemetry at offset 48
  and placing the owner at offset 208;
- `worr_native_receipt_history_entry_v1`: 32 bytes, including the full
  immutable committed message identity;
- `worr_native_snapshot_identity_v1`: 32 bytes;
- `worr_native_rx_telemetry_v1`: 192 bytes; and
- `worr_native_rx_session_v1`: 2824 bytes, with `history` at offset 64,
  `snapshot_tombstones` at offset 2112, `telemetry` at offset 2624, and owner at
  offset 2816; and
- `worr_native_rx_message_v1`: 56 bytes, with owner at offset 48.

`Worr_NativeRxSessionAcceptV1` now takes the required final
`worr_native_ack_range_v1 *repeat_acknowledgement_out` parameter. This is an
isolated, unadvertised ABI; no live adapter currently consumes it.
`Worr_NativeTxSessionEnqueueV1` now takes `max_datagram_bytes` before
`now_tick` and freezes its derived plan in the retained slot.

## Verification

The Meson networking suite exposes:

- `network-native-session`;
- `network-native-session-layout-c`; and
- `network-native-session-layout-cpp`.

The behavioral test covers:

- confirmed-capability/owner binding, same-epoch cross-owner rejection, and
  epoch reset;
- sequence allocation through `UINT32_MAX` and exhaustion without wrap;
- reliable retention, exact acknowledgement, capacity, duplicate, and
  conflict behavior;
- transactional reliable-capacity rejection when a full set contains one
  snapshot and one reliable record;
- TX snapshot supersession and RX commit-only canonical snapshot high-water;
- priority aging at the exact 56-intervening-dispatch bound and dispatch-clock
  rebase;
- non-mutating prepared selection, between-fragment validation, final-only
  send confirmation, stale/replayed ticket rejection, saturated-attempt
  single-use behavior, and the all-`UINT64_MAX` occupied-slot rebase boundary;
- the exact reliable distance-63 acceptance/distance-64 stall boundary with
  63 interleaved snapshot versions;
- reordered fragments, duplicate fragments, completion, commit, discard, and
  caller-tick expiry;
- out-of-order receipt commits and selective-mask shifts;
- eviction at the 64-entry receipt boundary and a severely delayed replay;
- the hostile resident-sequence-1/commit-sequence-100 order;
- exact reliable and snapshot re-ACK after deliberate first-ACK loss, followed
  by TX release and sequence-window progress;
- frozen TX fragmentation layout and fail-closed changed-stride retry;
- exact repeat ACK at transport sequence `UINT32_MAX`;
- committed snapshot re-ACK after its ordinary receipt ages out of the
  64-entry window;
- stale and conflicting snapshot identity, including hostile canonical and
  transport-sequence reuse;
- full-reliable-capacity snapshot rejection followed by exact retry;
- exact snapshot retry after timeout, discard, and corrected whole-message
  checksum failure;
- preservation of a complete prior snapshot while a much higher incomplete
  or corrupt snapshot times out and retries;
- full 16-entry retry-identity pressure, touch-to-tail refresh, committed
  identity preservation, active retry survival, and stale-window exact retry;
- malformed, corrupt, wrong-epoch, and alias transactionality; and
- saturating-policy state/layout validity in both C11 and C++20.

Verification completed on Windows x86-64 with Clang 20.1.7:

- focused Meson build: passed;
- focused Meson run: 3/3 passed;
- focused five-repeat run: 15/15 passed;
- complete networking suite after the later carrier/dispatch/ACK/netchan-hook
  additions: 92/92 passed, with three complete repetitions passing 276/276;
  and
- independent `-std=c11 -Wall -Wextra -Wpedantic -Werror` compile/run:
  passed.

The later carrier/session integration test exercises prepared selection and
confirmation under AddressSanitizer plus UndefinedBehaviorSanitizer. Clang
static analysis and i686 MSVC-target C/C++ layout checks also pass.

## Work deliberately left open at original validation

This foundation does not complete `FR-10-T04`. Still required before live
promotion are:

- the reviewed dormant post-netchan-assembly TX seam still needs an admitted RX
  seam, endpoint-readiness barrier, and production client/server command-shadow
  adapters with lifecycle integration;
- capability advertisement only after both peers have production consumers;
- atomic DATA/ACK piggyback after the ACK-only path is proven live;
- congestion control, pacing, and bandwidth-budget integration;
- demo, MVD/GTV, and spectator carrier policy;
- impairment, malformed-corpus, soak, and cross-platform acceptance evidence;
  and
- kill-switch, rollback, and release-gate proof.

No `q2proto/` file was modified.

## Production integration update (2026-07-14)

The session core now backs a default-off production one-command transaction.
The client uses one retained TX slot and immutable payload; the server uses one
retained RX slot and exact receipt. One current plus one immediately retired
bank preserves ACK completion across a map transition, and committed duplicate
DATA rearms the 100 ms/three-handoff ACK policy. The earlier statement that RX,
readiness, and production adapters are still absent is superseded only for this
narrow observation. Details and remaining full-adapter work are in
`docs-dev/fr-10-t04-one-shot-native-command-shadow-production-pilot-2026-07-14.md`.
