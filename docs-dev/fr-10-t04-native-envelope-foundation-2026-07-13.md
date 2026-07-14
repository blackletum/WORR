# FR-10-T04 Native WORR Envelope Foundation

Date: 2026-07-13  
Project task: `FR-10-T04`  
Status: core foundation implemented and one complete command envelope now
serves the default-off production observation linked below; capability
advertisement, general adapters, and rollout acceptance remain open

## Outcome

WORR now has a transport-only, versioned native envelope core under
`common/net`. It can frame opaque encodings of the existing canonical command,
snapshot, and event record classes, split them into datagrams no larger than
1,200 bytes, reconstruct them into caller-owned storage after reordered or
duplicated delivery, and schedule caller-owned payload handles through a
bounded priority queue.

This is intentionally not a live protocol cutover. No client or server stream
calls the core, `WORR_NET_CAP_NATIVE_ENVELOPE_V1` remains unadvertised, legacy
traffic remains authoritative, and `q2proto/` is unchanged. The implementation
therefore advances the framing/fragmentation/priority portion of `FR-10-T04`
without implying that its negotiation, adapter, compatibility, or rollout
Definition of Done is complete.

## Architecture

The public API is in `inc/shared/native_envelope.h`; the strict C11 core is in
`src/common/net/native_envelope.c`. Every retained state structure is
pointer-free and versioned. Payload memory and reassembly memory remain owned
by the caller, so the core does not allocate and cannot retain a pointer across
calls.

`worr_native_record_ref_v1` is a transport reference, not a new domain ID. It
contains:

- one of the canonical command, snapshot, or event record classes;
- that canonical record's schema version; and
- the two integer values copied from its canonical epoch/sequence identity.

The envelope treats payload bytes as opaque. Serialization of command,
snapshot, or event content belongs to the corresponding canonical adapter and
validator. This separation prevents the transport from accumulating a second
gameplay schema.

## Wire frame v1

All integer fields are encoded little-endian. The fixed header is 56 bytes.
The encoded payload stride is `selected_datagram_size - 56`, and the largest
allowed datagram is 1,200 bytes. At that limit, each non-final fragment carries
1,144 bytes.

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 4 | `WNE1` magic |
| 4 | 2 | wire version |
| 6 | 2 | header byte count |
| 8 | 1 | canonical record class |
| 9 | 1 | priority, 0 (highest) through 7 |
| 10 | 2 | exact first/last-fragment flags |
| 12 | 2 | canonical record schema version |
| 14 | 2 | reserved, required to be zero |
| 16 | 4 | transport/session epoch |
| 20 | 4 | envelope message sequence |
| 24 | 4 | canonical object epoch |
| 28 | 4 | canonical object sequence |
| 32 | 4 | complete payload byte count |
| 36 | 4 | complete payload CRC-32 |
| 40 | 4 | exact fragment offset |
| 44 | 2 | fragment payload byte count |
| 46 | 2 | zero-based fragment index |
| 48 | 2 | fragment count |
| 50 | 2 | fragment stride |
| 52 | 4 | datagram CRC-32, calculated with this field zero |
| 56 | variable | opaque canonical payload bytes |

The decoder derives the only legal count, offset, fragment size, and boundary
flags from total size, stride, and index. It rejects alternate or overlapping
ranges even if their CRC is valid. Version, class, reserved bits, IDs,
priority, payload size, fragment count, stride, and exact datagram length are
also checked before a fragment can mutate reassembly state.

The hard limits are:

- 1,200 bytes per datagram;
- 65,536 bytes per complete payload;
- 64 fragments per payload; and
- 64 entries in the transmit priority queue.

Small selected datagram sizes remain legal only when the resulting message
fits the 64-fragment cap. Count and offset calculations use quotient/remainder
or bounded operands rather than unchecked additive ceiling expressions.

## Fragmentation and integrity

`Worr_NativeEnvelopeFragmenterInitV1` freezes all message metadata and the
complete-payload CRC. `Worr_NativeEnvelopeFragmentNextV1` emits fragments in
canonical index order and changes its iterator only after the destination
capacity check and frame construction succeed. It uses a bounded temporary
frame so ordinary output failure cannot expose a partial datagram. The complete
caller-owned payload must remain immutable for the iterator lifetime; an output
range overlapping any part of that payload is rejected because an early frame
could otherwise overwrite bytes required by a later fragment.

Each datagram has its own CRC-32. Reassembly publishes completion only when all
derived ranges are present exactly once and the CRC over the assembled payload
matches the message CRC. That final all-fragments-plus-CRC transition is the v1
commit condition. CRC is an accidental-corruption check, not cryptographic
authentication; a later live adapter must still bind frames to the negotiated
session and existing net-channel trust boundary.

## Bounded reassembly

`worr_native_envelope_reassembly_v1` represents one in-flight message and uses
a 64-bit receipt bitmap. A caller can maintain multiple slots and route the
`{transport_epoch, message_sequence}` tuple to the appropriate slot. The core
itself retains no global state.

The first valid fragment may have any index, so reverse-order delivery does not
need a special path. Subsequent fragments must match the complete message key,
record reference, priority, total size, payload CRC, stride, and fragment
count. Identical duplicates are idempotent. A duplicate whose payload differs
from the already accepted bytes is a distinct conflict. A frame for another
message cannot evict or partially overwrite the active slot.

Capacity, decode, datagram-integrity, message-conflict, and duplicate-conflict
failures do not mutate the slot. If the final whole-payload checksum fails, the
slot resets and the incomplete bytes remain unpublished. The caller must keep
using the same storage allocation for a slot until completion or reset.

## Progressive priority scheduling

The transmit queue stores only value metadata and a non-zero, process-local
payload handle supplied by its caller. It never owns or dereferences the
payload. Lower numeric priority is selected first and equal-priority work is
FIFO by a monotonic enqueue serial.

Strict priority alone can permanently starve a snapshot or cosmetic event, so
v1 applies deterministic aging: after every eight dispatches, a waiting entry
gains one effective priority level. A priority-7 entry therefore reaches the
top class after 56 intervening dispatches and wins its FIFO tie against newer
work. Queue-full, duplicate-handle, duplicate-canonical-reference, invalid
metadata, and counter-exhaustion outcomes are explicit and non-mutating.

The eventual live adapter is responsible for mapping canonical semantics to a
priority. For example, command freshness and reliable event progress can be
placed ahead of supersedable snapshot work without encoding that policy into
the canonical record models.

## Deterministic proof coverage

Meson declares these networking-suite targets:

- `network-native-envelope` / `native_envelope_test.exe`;
- `network-native-envelope-layout-c` /
  `native_envelope_layout_c_test.exe`; and
- `network-native-envelope-layout-cpp` /
  `native_envelope_layout_cpp_test.exe`.

The behavioral test covers:

- one-byte, exact-stride, stride-plus-one, multi-fragment, and maximum
  65,536-byte payloads;
- exact 1,200-byte MTU enforcement and fragment-count rejection for an
  undersized selected datagram;
- complete reverse-order delivery, identical duplicates, and completion-time
  payload comparison;
- insufficient destination capacity without state mutation;
- conflicting message metadata and conflicting duplicate content;
- corrupted datagram CRC and a separately corrupted whole-message CRC;
- malformed magic, version, class, priority, flags, reserved bytes, schema,
  IDs, size, offset, fragment length, index, count, and stride;
- fragmenter failure transactionality and exhausted iteration;
- payload/output and transmit-queue output alias rejection;
- priority/FIFO selection, duplicate admission, full capacity, invalid input,
  and the 56-dispatch starvation bound; and
- C11/C++20 size, critical queue-item offsets, total queue size,
  standard-layout, and trivial-copy compatibility.

The native-envelope behavioral and layout targets passed as part of the
combined Windows Clang integration run. The accumulated evidence was:

- the complete networking suite passed 67/67 tests;
- three consecutive networking-suite repetitions passed 201/201 test
  invocations;
- the runtime networking smoke target passed;
- the rewind acceptance matrix passed all 120 invocations;
- the cgame module, sgame module, dedicated engine, and client engine
  production targets all built and linked successfully; and
- `.install/` was refreshed and validated for `windows-x86_64`: 16 root
  runtime files, one dependency, the `basew` runtime tree, a 308-asset
  `pak0.pkz`, botfile payload, and RmlUi payload all passed validation.

This verifies the isolated envelope core and its Meson registration. It does
not advertise the capability, activate a live adapter, satisfy the open
malformed/flood and long-session matrices, prove cross-platform behavior, or
constitute packaged-release acceptance.

## Remaining FR-10-T04 work at foundation validation

1. Define canonical byte codecs/adapters that feed the existing command,
   snapshot, and event validators and stores; the envelope must remain opaque.
2. Bind message epoch/sequence allocation and receive-slot lifecycle to the
   already implemented negotiated session/reconnect state.
3. Add selective acknowledgement, retention, supersession, and fragmentation
   timeout policy appropriate to each canonical delivery class.
4. Add client/server shadow adapters and prove native-versus-legacy semantic
   parity before advertising `WORR_NET_CAP_NATIVE_ENVELOPE_V1`.
5. Exercise downgrade, reconnect, unknown-version, malformed/flood, MTU,
   packet-loss/reorder/duplication, legacy server/demo, and modern dual-stack
   matrices with machine-readable evidence.
6. Enable live negotiation only behind the staged rollback gates in
   `FR-10-T14` and `FR-10-T15`.

## Production integration update (2026-07-14)

The envelope now carries one complete unfragmented command in the default-off
production pilot. Its exact shape is a 110-byte WNC1 payload inside a 166-byte
first-and-last WNE1 datagram, nested in one WTC1 DATA entry. The envelope remains
opaque to command semantics and the server still validates the canonical codec
separately. This does not wire event/snapshot envelopes or advertise native
capability. See
`docs-dev/fr-10-t04-one-shot-native-command-shadow-production-pilot-2026-07-14.md`.
