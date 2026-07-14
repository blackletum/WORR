# Canonical command ABI and bounded stream core (Phase 1)

Date: 2026-07-12

Project task: `FR-10-T09`

Scope: Phase 1 domain ABI and storage core only

Subsequent integration status: this document preserves the Phase 1 design and
tests. The core is now used by the negotiated legacy command carrier, live
client command-ID assignment, server receive/consume stream, post-callback
consumed-cursor publication, client snapshot/demo attachment, authoritative
cgame input replay, and the tri-state rewind command context. Packet ACK remains
excluded. Native exact render watermarks, event correlation, full runtime/
impairment gates, and the T04 native envelope remain open.

## Outcome

This phase establishes a transport-independent identity, record, and bounded
timeline for client commands. It deliberately does **not** change live client,
server, cgame, sgame, snapshot, demo, or `q2proto/` behavior. The next adapter
phase can therefore shadow this core against the legacy path before any
authority moves.

The implementation adds:

- `inc/shared/command_abi.h`: pointer-free C ABI records and fieldwise hash
  contract;
- `src/common/net/command_abi.c`: validation, canonicalization, identity
  progression, provenance-aware equality, and deterministic hashes;
- `inc/common/net/command_stream.h`: caller-owned bounded stream declarations;
- `src/common/net/command_stream.c`: contiguous receive/consume/reset core and
  saturating telemetry;
- `tools/networking/command_stream_test.c`: hostile deterministic behavior
  tests; and
- `tools/networking/command_schema_layout_c.c` and
  `command_schema_layout_cpp.cpp`: strict C11/C++20 boundary checks.

Meson and the strategic roadmaps were intentionally left for the integration
owner in this phase; both have since been integrated.

## The transport acknowledgement is not the consumed-command acknowledgement

This distinction is the central architectural proof behind `FR-10-T09`.

The current legacy move carries one `lastframe` value followed by multiple
backup/current commands. In `src/client/input.cpp`, the ordinary path writes one
`move.lastframe` and three deltas; the batch path writes one
`batch_move.lastframe` and potentially several duplicated frames. On the server,
`src/server/user.c` calls `SV_SetLastFrame` before it decides which backups to
run and before each eventual `SV_ClientThink`. Consequently, that packet field
can prove only that the client reported receipt of a snapshot used for packet
delta/timing context. It cannot prove that authoritative simulation consumed a
particular command. Packet loss, backup repetition, command-budget rejection,
or a later simulation discontinuity can separate all three facts:

1. the packet was acknowledged by the transport;
2. a command record was accepted into the receive timeline; and
3. the command was actually consumed by authoritative simulation.

Substituting item 1 for item 3 makes prediction replay skip unprocessed input
and makes lag compensation associate the wrong observation time with gameplay.
Phase 1 therefore stores `received_cursor` and `consumed_cursor` separately.
Only an explicit, successful consume operation advances the latter. The live
server now publishes the post-callback consumed cursor immediately before the
corresponding snapshot path, and the client attaches it to the accepted
canonical view; a packet ACK must never populate it.

### Quake III lesson retained, not copied blindly

Quake III already keeps the useful conceptual separation:

- `server/sv_client.c` reads transport `messageAcknowledge` independently;
- old duplicate user commands are filtered by `usercmd.serverTime` relative to
  `lastUsercmd.serverTime`;
- `game/bg_pmove.c` assigns `playerState.commandTime` only as movement executes;
  and
- `cgame/cg_predict.c` replays commands newer than the authoritative
  `playerState.commandTime`.

WORR adopts the separation but replaces overloaded signed time with an explicit
`{epoch, sequence}` command ID plus a cumulative 64-bit sample time. This makes
reset, natural wrap, zero-duration commands, and exhaustion unambiguous.

## ABI contract

### Existing command payload is canonical

`worr_command_record_v1.command` is exactly
`worr_prediction_command_v1` from the completed T02 prediction ABI. No second
buttons/angles/movement payload was introduced. Canonicalization uses the same
signed short-angle and integer movement rules as the existing network command
adapter. This prevents prediction, wire adapters, and replay from drifting into
parallel input definitions.

The pointer-free layouts are fixed as follows:

| Record | Size | Important offsets |
|---|---:|---|
| `worr_command_id_v1` | 8 | epoch 0, sequence 4 |
| `worr_command_cursor_v1` | 8 | epoch 0, contiguous sequence 4 |
| `worr_command_render_watermark_v1` | 40 | source time 24, rendered time 32 |
| `worr_command_record_v1` | 104 | ID 8, sample time 16, T02 command 32, watermark 64 |
| `worr_command_stream_slot_v1` | 128 | semantic hash 104, state 120 |

All reserved fields must be zero. Every record header/version is exact. C11 and
C++20 compile-time assertions pin the same layouts, and the C++ check requires
the domain records to remain standard-layout and trivially copyable.

### Identity, cursors, wrap, and reset

A real command ID requires nonzero epoch and sequence. `{0, 0}` is the absent
ID only. A cursor has a nonzero epoch but permits sequence zero to mean “nothing
contiguous in this epoch.”

Natural progression is:

```text
{E, N} -> {E, N + 1}
{E, UINT32_MAX} -> {E + 1, 1}
```

`{UINT32_MAX, UINT32_MAX}` has no successor and fails closed as epoch
exhaustion. An explicit reset is different from natural wrap: it requires a
strictly greater nonzero epoch, clears resident records, resets cumulative
sample time to zero, and resets both cursors to `{new_epoch, 0}`. Reusing or
decreasing an epoch is rejected transactionally.

Initialization accepts a fully consumed baseline cursor and baseline sample
time. This supports state restoration and makes boundary tests possible without
fabricating billions of records. A sequence-zero baseline must have sample time
zero.

### Duration and sample time

The negotiated maximum is represented as `uint16_t` so an invalid value such as
251 can be rejected rather than truncated into a byte. Every negotiated limit
from 0 through 250 ms is structurally valid; a zero ceiling intentionally
permits only zero-duration records. A command duration must not exceed the
negotiated limit.

`sample_time_us` is cumulative command-end time since explicit reset. A newly
accepted record must equal:

```text
previous_sample_time_us + duration_ms * 1000
```

Zero duration is valid, advances identity, and leaves sample time unchanged.
Unsigned addition overflow is detected before mutation. This avoids using time
as identity while retaining the Quake III benefit of an execution-time axis.

## Render watermark and provenance

The watermark intentionally contains no snapshot ID. Adding a second snapshot
identity here would either duplicate the T06 schema or create a command/snapshot
include cycle when snapshots later embed the consumed cursor. Phase 1 records
only validated timing facts:

- authoritative source server tick;
- authoritative source server time;
- client-rendered server time;
- validated tick interval; and
- provenance plus interpolation/extrapolation mode.

`NONE` requires every other watermark field to be zero. A non-`NONE` watermark
requires a tick interval in 1 through 1,000,000 microseconds. Interpolated and
extrapolated are mutually exclusive:

- exact mode has rendered time equal to source time, including the valid
  initial `{tick 0, time 0}` case;
- interpolation is behind the newest source time by at most the structural
  250 ms public-policy ceiling; and
- extrapolation is ahead by at most
  `min(4 * tick_interval_us, 250000 us)`, using 64-bit checked arithmetic.

These are hostile-input structural bounds, not a promise that every accepted
claim will receive that much lag compensation. T07/T10/T11 server policy may
apply tighter adaptive interpolation, clock trust, weapon, history, and fairness
limits.

### Immutable exact provenance versus legacy retry context

Render timing can affect rewind fairness, so an
`EXACT_COMMAND` watermark is part of immutable semantic identity. Reusing a
command ID with changed exact timing is a conflict. Trusted
`SERVER_SYNTHESIZED` timing follows the same rule.

Legacy packets are different: one packet-shared `lastframe` context covers
backup and new commands, and the same backup may later appear under a different
packet header. `LEGACY_PACKET_SHARED` therefore keeps its provenance class in
the semantic projection but excludes that retry-varying timing context. A
legacy/legacy retry with the same ID, sample time, and T02 command is an
idempotent duplicate. The first accepted record is never overwritten. Changing
the provenance class—especially trying to upgrade legacy context to exact—is a
conflict.

This policy avoids both failure modes: exact rewind claims cannot mutate after
acceptance, while legitimate legacy backup repetition does not create false
conflicts.

## Fieldwise hashing and packet exclusion

Hashes use domain-separated FNV-1a over explicit little-endian fields. They
never hash C padding or a raw struct. IEEE-754 fields use canonical bits after
the existing command canonicalizer. A golden exact-command vector pins:

```text
semantic hash = dbbfb822917044b4
content hash  = ca0389fe242397cc
```

The semantic hash follows the provenance policy above. The content hash always
includes the complete accepted watermark and is useful for diagnostics and
replay audits. Duplicate decisions compare the semantic hash **and** perform
collision-safe fieldwise equality.

Packet sequence, packet ACK, netchan generation, retry count, and message
position do not exist in the record and cannot affect either identity or hash.

## Caller-owned bounded stream

The runtime envelope owns no memory. Its slot pointer refers to a distinct,
non-overlapping caller-owned array whose lifetime covers every operation.
Initialization checks `capacity <= SIZE_MAX / sizeof(slot)` before clearing the
array. Checked `uintptr_t` half-open ranges reject exact or partial
envelope/storage overlap, including range-end overflow, before any write.
Ongoing stream validation enforces the same separation. Invalid initialization
leaves both envelope and storage byte-identical.

Accepted new records must be the exact successor of `received_cursor`.
Retained records form one contiguous ring:

- an exact retained retry is duplicate or conflict according to semantic
  equality;
- a missing older ID is stale;
- a same-epoch gap is future;
- an unexpected later epoch is wrong-epoch; and
- an invalid record, sample mismatch/overflow, or exhausted epoch fails closed.

Consumption also requires the exact successor of `consumed_cursor`. Receiving a
record never consumes it. Re-consuming an older/equal ID is idempotent
`ALREADY_CONSUMED`; skipping ahead is `NOT_READY` (or wrong-epoch). The next
unconsumed record is always retained.

When full, insertion may reclaim only the ring head and only after that head was
contiguously consumed. It never evicts an unconsumed command and cannot create
an interior hole. If the head is still unconsumed, insertion returns
`CAPACITY`; the caller can then apply later hard-resync/flood policy without
silent simulation loss.

### Transaction boundary and telemetry

All rejected operations leave records, slots, head/count, cursors, sample time,
and output records unchanged. The sole deliberate mutation is saturating
telemetry for the attempted result. Corrupt stream state fails before even
telemetry is touched. Successful reset preserves lifetime telemetry while
clearing the data plane.

Twenty-one counters cover receive/consume/reset attempts, insertion,
duplicates, conflicts, stale/future/wrong-epoch input, invalid records,
capacity stalls, sample-time rejection, not-ready/already-consumed results,
wrap/exhaustion, and invalid usage. Every increment saturates at `UINT64_MAX`.

## Phased legacy and WORR compatibility plan

### Phase 2: legacy shadow adapter

Without changing `q2proto/`, client command production will assign canonical
IDs/sample times and retain them alongside the existing command history. The
legacy move decoder will project decoded commands into the same canonical
records. Packet-level `lastframe` timing is tagged
`LEGACY_PACKET_SHARED`, never exact. Server shadow mode will compare receive and
consume cursors with current `SV_ClientThink` behavior before authority changes.

### Ephemeral settings sideband rule

Any temporary CLC setting used to carry command identity, sample time, or render
provenance must be serialized into the **same unreliable datagram payload as
the move it describes**. It must not be flushed into the reliable
`cls.netchan.message` buffer used by persistent CLC settings.

Reliable netchan data has an independent loss/retransmission lifetime. If an
ephemeral command header is placed there, a dropped move or retransmitted
reliable fragment can pair the header for command/packet A with the move from
command/packet B. That silently corrupts identity and rewind provenance. The
safe legacy transition writes the negotiated ephemeral sideband immediately
adjacent to its move in `msg_write`, repeats the pairing together when needed,
and treats an unpaired sideband/move as invalid. Existing persistent preference
settings may remain on their reliable path; that path must not be repurposed
for per-move metadata.

### Current phase status

- T06/T07 now embed the shared `worr_command_cursor_v1` consumed watermark in
  live projected snapshots and deliver parity-qualified immutable views to the
  cgame-owned timeline.
- T08 replays verified retained successors after the exact consumed cursor and
  hard-resets locally when canonical history is ambiguous or invalid.
- T10/T11 consume the same identity through an API-v2 tri-state command context
  and a sealed common rewind scene; rejected/synthesized-gap contexts cannot
  fall back to packet-ack rewind.
- T04 still must give negotiated WORR transport a native canonical command
  representation rather than the compatibility sideband.
- T05 prediction/event correlation and native exact render watermarks remain
  open integration work.

## Verification

The Phase-1 tests cover:

- C11/C++20 ABI layout and C++ trivial-copy guarantees;
- normal identity progression, natural wrap, explicit reset, and terminal
  epoch exhaustion;
- zero-duration commands, 250 ms acceptance, 251 ms rejection, cumulative
  time mismatch, and 64-bit overflow;
- every watermark structural boundary, including initial zero time, 250 ms
  interpolation lag, and cadence-relative/capped extrapolation;
- exact timing conflicts, legacy context-varying duplicates, provenance-class
  conflicts, and preservation of the first legacy record;
- future/stale/wrong-epoch classification;
- independent received and consumed cursors, contiguous consumption, duplicate
  consumption, bounded capacity, and consumed-head-only reclamation;
- byte-preserving data-plane failure and untouched output records;
- exact/partial envelope-storage alias rejection before initialization writes;
  and
- saturation at `UINT64_MAX`.

The standalone strict build used Clang 20.1.7 with `-std=c11`/`-std=c++20`,
pedantic errors, conversion/sign/shadow warnings, strict prototypes for C, and
warnings-as-errors. The behavior executable and both layout executables passed.

The behavior suite also passed a combined AddressSanitizer and
UndefinedBehaviorSanitizer build (`-fsanitize=address,undefined`, no recovery,
frame pointers enabled). On this Windows LLVM installation the runtime DLL
directory was added to `PATH` and the UBSan link used `shell32`/`dbghelp`.
`clang --analyze` then completed with no findings. All generated executables and
objects remain under `.tmp/command-core/`.
