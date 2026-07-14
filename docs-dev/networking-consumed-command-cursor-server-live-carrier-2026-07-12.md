# Live server consumed-command cursor carrier

Date: 2026-07-12  
Project tasks: `FR-10-T09`, `FR-10-T06`

## Outcome

The enhanced server snapshot path now publishes the authoritative command
stream's consumed cursor immediately before each `Q2P_SVC_FRAME` when
`WORR_NET_CAP_CONSUMED_COMMAND_CURSOR_V1` was negotiated. Before the canonical
command stream is initialized it publishes the defined absent cursor `{0, 0}`;
after initialization it copies `worr_command_stream_v1.consumed_cursor`.

The legacy-shadow capability stage now advertises and supports bits 0 and 1:

- `WORR_NET_CAP_LEGACY_COMMAND_SIDEBAND_V1`; and
- `WORR_NET_CAP_CONSUMED_COMMAND_CURSOR_V1`.

The cursor is never emitted merely because the peer offered the bit. The live
writer checks the connection's confirmed negotiated mask for every frame.

## Atomic wire transaction

`SV_WriteFrameToClient_Enhanced()` builds the five-pair sideband with
`Worr_ConsumedCursorSidebandInitV1()` and
`Worr_ConsumedCursorSidebandEncodeV1()`. It then stages the complete 45-byte
setting tuple and the following q2proto frame header in a bounded temporary
`sizebuf_t`. Only a successful complete encode is appended to the outgoing
datagram. This gives the following properties:

- all five settings are adjacent and in canonical begin/epoch/sequence/
  checksum/commit order;
- the next byte after the commit value begins `Q2P_SVC_FRAME`;
- a capacity failure cannot publish a committed tuple without its frame;
- an invalid cursor, unsupported carrier protocol, q2proto write error, or
  buffer overflow fails the complete frame operation; and
- suppression and frame flags are cleared only after the complete frame and
  entity terminator have been written successfully.

Entity-delta and entity-terminator q2proto results are now checked as part of
the same frame transaction. Existing datagram callers clear the output when
`SV_WriteFrameToClient_Enhanced()` reports failure, so no partial snapshot is
transmitted.

## Protocol compatibility

The Rerelease carrier uses `svc_rr_setting`; Q2PRO and R1Q2 use
`svc_q2pro_setting`. Each service record contains its signed 32-bit index and
signed 32-bit value, preserving the exact bit-container representation emitted
by the common sideband encoder.

No file under `q2proto/` was changed. q2proto models and decodes server
settings, but its legacy server writers intentionally leave private/game
setting emission to the engine. The live carrier therefore uses the same
engine-owned raw SVC_SETTING representation already used for capability
confirmation, while q2proto writes the immediately adjacent frame header.

## Verification

Focused verification completed on 2026-07-12:

- dedicated-server C11 syntax checks for `src/server/entities.c` and
  `src/server/user.c` with the production server defines;
- `capability_test`, including decimal stage-mask value `3`; and
- `consumed_cursor_sideband_test`, covering exact encoding, corruption,
  ordering, packet boundaries, and hostile 32-bit values.

Subsequent client integration now requires and consumes the negotiated tuple at
packet and service boundaries, binds its established epoch to the negotiated
session, rejects regression, and attaches the cursor to canonical snapshots and
client demos. Cgame prediction and the rewind context consume the same exact
identity. The original server-focused verification above is not relabelled as
the final combined acceptance pass: full client/server impairment, reconnect,
demo, long-session, load, and release gates remain `FR-10-T06`/`FR-10-T09`
work.
