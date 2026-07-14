# FR-10-T06 Stage C: Server final-emission snapshot shadow

Date: 2026-07-13  
Project task: FR-10-T06  
Scope: server-side canonical snapshot observation and exact sent-reference retention

## Outcome

The server now maintains an optional per-peer canonical shadow at the actual
q2proto frame-emission boundary. The legacy packet writer remains the sole
authority for network output. The shadow observes only frame headers and
entity-delta services that the writer successfully accepted, commits only
after the terminal entity service has been emitted, and retains a
generation-validated reference to the exact endpoint sent to that peer.

This closes the Stage C gap between the authoritative simulation frame stored
in `client_frame_t` and the client-side canonical projector introduced in
FR-10-T06 Stage B. In particular, a per-client wire frame number is no longer
used as an authoritative simulation tick in the server record.

## Design

### Transaction boundary

Each outgoing enhanced frame uses a three-part shadow transaction:

1. `BeginFrame` runs only after the complete q2proto frame header and any
   negotiated consumed-command sideband have been accepted by the real packet
   buffer.
2. Every successfully written entity add, update, remove, and the mandatory
   zero terminator is copied into transaction-owned storage. Deltas suppressed
   by q2proto because they carry no change are correctly absent.
3. `CommitFrame` runs only after packet-entity emission succeeds. Any packet
   failure calls `AbortFrame`. Any shadow allocation, capture, projection, or
   retention failure is ignored by the packet path and cannot reject or change
   a valid legacy datagram.

Packet-size truncation is recorded at the point where
`SV_TruncPacketEntities` rewrites the retained legacy endpoint. The canonical
record therefore describes that rewritten endpoint and carries
`WORR_SNAPSHOT_FLAG_TRANSPORT_TRUNCATED`, which prevents promotion while
preserving the narrow legacy semantic parity domain.

The public shadow also accepts a dormant fragment-stall cause marker. It maps
to the existing canonical fragment-stall discontinuity without changing
packet output. The production packet path does not yet call it; the offline
final-emission parity corpus uses it to qualify that cause without
misclassifying a projector-only case.

### Identity and time domains

The retained record intentionally contains both domains:

- `wire_snapshot_number` and `wire_base_snapshot_number` are the peer-local
  q2proto frame identifiers used for delta transport.
- `snapshot.snapshot_id` remains the canonical transport identity projected
  from the wire frame.
- `authoritative_server_tick` and `snapshot.server_tick` come from
  `client_frame_t.server_frame`, the engine simulation frame captured while
  building the client endpoint.
- `authoritative_tick_delta` comes from
  `client_frame_t.server_frame_delta`. A value greater than one adds the
  rate-suppressed discontinuity flag even when peer-local wire frames are
  contiguous.
- `snapshot.server_time_us` is copied from
  `client_frame_t.server_time_us`. The map-owned
  `sv.worr_server_time_us` clock advances exactly once for every executed game
  frame by that frame's active interval, including spawn-settle and save-load
  catch-up frames. In the regular server loop the completed frame's time is
  published immediately after `RunFrame` and before any client frame is built;
  `sv.framenum` remains stable through emission and advances only afterward.
  The snapshot therefore cannot pair post-simulation state with the preceding
  tick's time. The clock remains exact if simulation rate changes are made
  dynamic later and never needs to infer time from a wire identifier or the
  latest rate.

Validated client acknowledgements retain that exact `client_frame_t` time
beside their authoritative frame and frame-delta watermark. Canonical command
contexts use the retained value directly; they no longer reconstruct source
time with `server_tick * current_interval`, and between frames they identify
the last completed tick rather than the next tick that has yet to run.

The endpoint hash is rebuilt after installing authoritative tick/time
metadata. The narrower `legacy_parity_hash` remains directly comparable with
the client projector because it excludes server-only chronology and
provenance.

### Exact sent references

`sv_snapshot_shadow_ref_v1` is a process-local `(slot, generation)` handle.
Each committed record retains:

- its canonical q2proto projection reference;
- the exact base sent-reference resolved by wire base number;
- authoritative tick metadata;
- transport/rate flags;
- endpoint and legacy parity hashes; and
- an immutable canonical snapshot metadata copy.

Delta publication fails only inside the shadow if the exact base record is no
longer retained. This is deliberately stricter than guessing the latest frame.
When a slot is reused, its generation advances, so stale readers cannot alias a
new endpoint. `SV_SnapshotShadowViewV1` combines the retained metadata with the
projector-owned immutable player/entity/area/event payloads after validating
both references.

### Baselines and lifecycle

The shadow is created for each peer after final protocol limits and per-client
packed baselines are known. The same public q2proto baseline delta sent in the
gamestate is installed in the projector. Storage uses the negotiated maximum
entity/model/sound ranges, `UPDATE_BACKUP` retention slots, the protocol's
packet-entity limit, and fixed arenas; no q2proto implementation-private state
is read.

`sv_snapshot_shadow` controls creation and defaults to `1`. Turning it off
removes observation only. Peer cleanup destroys all shadow storage. Observer or
cinematic frames without a legal controlled player entity are currently
skipped, matching the Snapshot V2 controlled-generation contract. Dead and
recording clients retain the valid `clientNum + 1` controlled identity; this is
intentionally independent from the zero-valued first-person renderer sentinel.

## Public surface

`inc/server/snapshot_shadow.h` defines:

- versioned configuration, frame-input, sent-record, ref, and status types;
- create/destroy and baseline installation;
- begin/capture/truncation/fragment-stall/abort/commit transaction operations;
- exact wire lookup and generation-checked record/view access; and
- counters for allocation, attempts, aborts, truncations, projection failures,
  base misses, commits, and the most recent parity hashes.

The implementation is isolated in `src/server/snapshot_shadow.c`. It depends
only on public q2proto service structures and the canonical snapshot projection
APIs, and is shared by client-hosted and dedicated server engine builds.

## Verification

`tools/networking/server_snapshot_shadow_test.cpp` provides focused Stage C
coverage:

- a wire snapshot number distinct from a large authoritative simulation tick;
- wire frame zero as an initial boundary and delta frame zero as a keyframe;
- exact keyframe and branch-base reference retention;
- server tick delta and rate-suppressed discontinuity projection;
- packet truncation marking and promotion blocking;
- receiver-compatible legacy semantic hashes;
- baseline reconstruction and immutable view access;
- explicit abort and missing-terminator atomicity; and
- slot overwrite/stale-generation rejection plus status telemetry.

The combined Windows Clang integration pass validated the Stage C shadow and
the accumulated-clock follow-up:

- the complete networking suite passed 67/67 tests, including
  `network-server-snapshot-shadow`;
- three consecutive networking-suite repetitions passed 201/201 test
  invocations;
- the runtime networking smoke target passed;
- the rewind acceptance matrix passed all 120 invocations;
- the cgame module, sgame module, dedicated engine, and client engine
  production targets all built and linked successfully, including the
  client-hosted server objects in `entities.c`, `user.c`, `main.c`, and
  `snapshot_shadow.c`; and
- `.install/` was refreshed and validated for `windows-x86_64`: 16 root
  runtime files, one dependency, the `basew` runtime tree, a 308-asset
  `pak0.pkz`, botfile payload, and RmlUi payload all passed validation.

This verifies the observational shadow and chronology correction on Windows.
It does not satisfy the open long-session parity, impairment, cross-platform,
promotion, or release-default gates, and the validated Windows staging tree is
not by itself packaged-release acceptance.

## Follow-up boundaries

Stage C is observational and does not yet place sent refs into a new wire
acknowledgement protocol. Later FR-10-T06 work can use this exact record as the
server side of parity diagnostics, authoritative event attachment, snapshot
acknowledgement policy, rewind auditing, and progressive protocol promotion.
Those changes must retain legacy server/demo compatibility and must not make
shadow availability a requirement for packet delivery.
