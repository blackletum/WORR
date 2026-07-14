# Authoritative consumed-command cursor SVC_SETTING sideband

Date: 2026-07-12
Project tasks: FR-10-T09, FR-10-T06

## Outcome

FR-10-T09 has a bounded server-to-client carrier, now used by the live
negotiated compatibility path, for attaching
the authoritative consumed command cursor to the immediately following
snapshot frame without modifying `q2proto/`. The carrier is implemented by
`inc/common/net/consumed_cursor_sideband.h` and
`src/common/net/consumed_cursor_sideband.c`.

This is a compatibility-stage transport. It uses five adjacent signed 32-bit
SVC_SETTING index/value pairs:

1. begin and schema version;
2. cursor epoch;
3. contiguous consumed sequence;
4. checksum; and
5. commit token.

The epoch, sequence, checksum, and commit values are treated as exact 32-bit
bit containers. `memcpy` bit conversion avoids implementation-defined
unsigned-to-signed conversion and preserves values with bit 31 set.

The committed tuple belongs only to the next decoded snapshot-frame service.
An ordinary setting, any other intervening service, another reserved setting,
a duplicate field, a frame before commit, or a packet/reset boundary poisons
or resets the pending tuple and returns a failure result. Callers must drop or
disable the negotiated modern path on every result other than the explicitly
expected success result. A tuple cannot be continued in a later packet.

`{epoch = 0, sequence = 0}` is the sole absent cursor. It permits snapshots
before the server command stream is initialized. `{nonzero epoch, sequence =
0}` represents an initialized stream that has consumed no command. No other
zero-epoch value is valid.

## Safety properties

- Wire records and parser state contain no heap ownership or pointers.
- Encoding stages all five fields and commits output only after full
  validation and alias checks.
- Frame output is untouched on failure; aliasing parser storage is rejected.
- Parser storage and work are fixed-size and independent of hostile values.
- Checksum and commit bind schema version, epoch, and sequence.
- Packet begin/end, exact service adjacency, and frame consumption are
  explicit API operations.
- Malformed, out-of-order, duplicate, checksum-corrupt, commit-corrupt, and
  intervened tuples fail closed for the packet.
- All telemetry counters saturate at `UINT64_MAX`.
- C and C++ compile-time layout assertions pin the carrier and parser ABI.

The checksum is corruption/framing protection, not authentication. The cursor
is authoritative because it is emitted by the server inside the established
netchan, never because the checksum is secret.

## Verification

`tools/networking/consumed_cursor_sideband_test.c` covers absent and active
cursors, high-bit signed carrier values, exact multi-frame adjacency,
malformed order, duplicate begin, unsupported versions, invalid zero epochs,
checksum and commit corruption, settings and service interleaving, missing
headers, tuple truncation at packet boundaries, hostile terminal values,
alias rejection, transactional output, corrupted parser state, and saturation
telemetry.

The ABI probes are:

- `tools/networking/consumed_cursor_sideband_layout_c.c`; and
- `tools/networking/consumed_cursor_sideband_layout_cpp.cpp`.

## Live integration status

The end-to-end compatibility carrier is now live. The server publication half
is documented in
`networking-consumed-command-cursor-server-live-carrier-2026-07-12.md`; the
client brackets every packet, routes settings and intervening services through
the strict parser, requires an adjacent matched tuple for negotiated frames,
binds an established nonzero cursor epoch to the negotiated session, and
rejects regression. The matched cursor is attached to canonical snapshots and
new client demos; playback uses the same strict packet parser and seek resets
cursor/projection lineage. Legacy peers and recordings without confirmed
metadata retain the explicit legacy fallback.

Implemented integration steps:

1. **Implemented:** add a parser and initialization flag to each client connection. Reset them
   on serverdata, reconnect, map change, demo seek, and disconnect.
2. **Implemented:** on the server, immediately before writing each snapshot frame, copy
   `worr_command_stream_v1.consumed_cursor` when the stream is initialized, or
   use `{0, 0}` otherwise. Initialize and encode the sideband into staging
   storage. Write all five pairs with `svc_rr_setting` for Rerelease or
   `svc_q2pro_setting` for Q2PRO/R1Q2, using `MSG_WriteLong` for both index and
   value. Atomically append the tuple and frame to the same outgoing message.
3. **Implemented:** emit the tuple only when
   `WORR_NET_CAP_CONSUMED_COMMAND_CURSOR_V1` is confirmed for that connection.
   A negotiated frame must never silently omit it.
4. **Implemented:** on the client, call `PacketBegin`/`PacketEnd` for each decoded server
   packet. Route every SVC_SETTING through `ObserveSetting`; swallow only
   accepted reserved fields, and pass through only `NOT_SIDEBAND` settings.
5. **Implemented:** route every non-setting, non-frame service through
   `ObserveInterveningService`. Immediately before accepting a decoded frame,
   call `ConsumeFrame` and require `FRAME_MATCHED`. Treat every other result as
   a protocol failure.
6. **Implemented:** copy the matched cursor into the canonical snapshot publication metadata
   used by FR-10-T06. Do not infer it from packet acknowledgements, netchan
   sequence numbers, or client command history.
7. **Implemented:** keep `q2proto/` unchanged: this carrier is an engine-owned projection around
   already supported SVC_SETTING and frame services.

## Meson integration

`src/common/net/consumed_cursor_sideband.c` is part of
`canonical_command_core_lib`. The existing networking test section includes:

- `consumed_cursor_sideband_test`, linked with `canonical_command_core_lib`;
- `consumed_cursor_sideband_layout_c_test`, C11; and
- `consumed_cursor_sideband_layout_cpp_test`, C++20.

They are registered as `network-consumed-cursor-sideband`,
`network-consumed-cursor-sideband-layout-c`, and
`network-consumed-cursor-sideband-layout-cpp`, respectively, in the
`networking` suite. The client and server engines already link the canonical
command core; no second copy of the implementation should be added to
`common_src`.
