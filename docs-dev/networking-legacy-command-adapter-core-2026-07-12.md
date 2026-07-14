# Legacy command sideband and transactional adapter core

Date: 2026-07-12

Project task: `FR-10-T09`

Scope: transport-neutral adapter core only

Subsequent integration status: the negotiated legacy carrier is now live. The
client assigns canonical IDs and stages the nine-pair header atomically beside
MOVE/BATCH_MOVE; the server packet parser validates the adjacent range,
projects it into the canonical stream, and advances `consumed_cursor` only
around authoritative simulation. The post-callback cursor is published with
snapshots, strictly parsed and session-bound by the client, attached to
snapshots/demos, and used by prediction and rewind context. Native exact render
watermarks, event correlation, full impairment/load acceptance, and the T04
native envelope remain open.

## Outcome

This slice adds the allocation-free bridge required to project decoded legacy
MOVE and BATCH_MOVE command ranges into the canonical command stream without
confusing packet acknowledgement with command identity. It includes:

- `inc/common/net/legacy_command_adapter.h`, which defines the signed-int16
  sideband pairs, strict parser, adapter result/report contracts, and fixed
  layouts;
- `src/common/net/legacy_command_adapter.c`, which implements fieldwise
  encoding, a fail-closed packet-scoped parser, canonical command projection,
  duplicate/stale handling, and checked sample-time derivation;
- `Worr_CommandStreamInsertBatchV1` in the existing command stream, which uses
  caller-owned scratch to make a record range transactional;
- `tools/networking/legacy_command_adapter_test.c`, which covers legacy carrier
  shapes, hostile headers, loss/retry/reordering, wrap, overflow, bootstrap,
  and no-partial-commit behavior; and
- C11 and C++20 layout checks in
  `legacy_command_adapter_layout_c.c` and
  `legacy_command_adapter_layout_cpp.cpp`.

At this slice revision this was deliberately **not** live protocol integration. It did not change
client command production, server parsing/execution, netchan, cgame, sgame,
demos, MVD, or any file under `q2proto/`. The integrating change added this
core and its three focused executables to the Meson networking suite and
updated both project roadmaps. Negotiation and live wiring subsequently landed
as summarized above; no file under `q2proto/` changed.

## Why a legacy sideband is necessary

Legacy transport identity cannot be reused as command identity. One ordinary
MOVE packet contains three decoded commands, while a Q2PRO/Q2REPRO batch can
contain up to four frames of up to 31 commands each. Packets may be lost,
duplicated, reordered, or retransmit old backup commands under a different
packet sequence and `lastframe`. A packet ACK therefore does not say which
individual command the server accepted or consumed.

The sideband names the flattened, oldest-to-newest decoded command range with
an explicit canonical `{epoch, first_sequence, count}`. There is no packet
sequence or packet ACK in the schema or adapter API. The first command has the
declared ID; every subsequent decoded command receives the exact canonical
successor, including natural `{E, UINT32_MAX} -> {E + 1, 1}` progression.

The sideband is CLC_SETTING-compatible: every carrier item is exactly one
`{int16_t index, int16_t value}` pair. The negative indices are outside the
current server setting table and are ignored by an unextended setting handler.
That fact is a compatibility property, not permission to send the extension
without negotiation. A live client must first negotiate support and must use
only a protocol that already represents `CLC_SETTING`; vanilla compatibility
must retain the explicitly legacy/inferred fallback.

## Sideband v1 wire projection

The v1 header is exactly nine consecutive signed-int16 pairs:

| Order | Index | Value |
|---:|---:|---|
| 0 | -32000 | exact schema version (`1`) |
| 1 | -31999 | epoch bits 0..15 |
| 2 | -31998 | epoch bits 16..31 |
| 3 | -31997 | first-sequence bits 0..15 |
| 4 | -31996 | first-sequence bits 16..31 |
| 5 | -31995 | flattened command count |
| 6 | -31994 | CRC-32C bits 0..15 |
| 7 | -31993 | CRC-32C bits 16..31 |
| 8 | -31992 | independent folded commit word |

The values preserve all 16 raw bits by an explicit portable conversion between
unsigned words and signed int16 values. No implementation-defined narrowing is
used.

The CRC-32C is fieldwise and domain-separated with `WC`/`S1`. It covers the
version and the exact ordered index/value words through count. The commit word
folds both checksum words, every range word, the count, version, and a fixed
domain constant. Strict parser order rejects missing, reordered, and duplicate
fields before commit. CRC plus commit rejects accidentally mixed headers. This
is corruption and assembly detection, not authentication; it is not a MAC and
does not replace normal netchan trust/security policy.

A pinned cross-platform vector for
`{epoch=0x89abcdef, sequence=0xfedcba98, count=3}` is:

```text
CRC-32C = 634f2487
commit  = 4741
```

## Exact packet-scoped state machine

The parser has an explicit packet boundary and advances only through:

```text
BEGIN -> epoch low/high -> sequence low/high -> count
      -> checksum low/high -> COMMIT -> MOVE/BATCH_MOVE
```

The final move must be the very next decoded service. An ordinary setting that
is not one of the reserved sideband fields, a string command, userinfo update,
nop, or any other intervening service clears the pending header. Packet begin
and packet end also clear pending state. A malformed reserved field poisons the
candidate until an intervening service or packet boundary; later fields cannot
repair it.

The parser distinguishes packet start/end, non-sideband settings, accepted
fields, committed headers, matched moves, packet/intervening resets,
unsupported versions, unexpected order, checksum/commit failure,
missing headers, count mismatch, invalid carriers, invalid arguments, and
invalid lifecycle state. Its 21 counters saturate at `UINT64_MAX` and cover
packet boundaries, settings, accepted/committed headers, matched moves,
malformation, all reset causes, carrier/count failures, and API misuse.

Carrier shape is validated twice: against actual legacy structural maxima and
against the committed header count.

- ordinary MOVE must decode exactly three slots;
- BATCH_MOVE must decode 1 through 124 commands; and
- 124 is derived from four frames times the actual per-frame accepted maximum
  of 31 (`num_cmds >= 32` is rejected by the legacy parser).

The core accepts the already flattened q2proto decode order. It does not define
a second frame or delta-command representation.

### Live placement rule

When this core is wired live, all nine setting services and the corresponding
MOVE/BATCH_MOVE must be serialized consecutively into the **same unreliable
`msg_write` payload**. They must not be put in the reliable netchan message
buffer: reliable retransmission could pair an old header with a later move,
which the sender must prevent even though the receiver will usually reject the
mixed range. Parser packet begin/end calls must bracket every client packet,
and every decoded non-move service must pass through the adjacency reset hook.

## Canonical adapter rules

`Worr_LegacyCommandAdapterApplyV1` accepts only the completed T02
`worr_prediction_command_v1` array. No parallel buttons/angles/movement
payload exists. Inputs are canonicalized with the same short-angle and integer
movement rules used by the command ABI.

The adapter validates the complete range before touching the authoritative
stream:

1. validate the sideband checksum/version/count and the whole ID progression;
2. validate exact command count, movement-model revision, every T02 command,
   and the packet-shared watermark;
3. classify every ID relative to the authoritative `received_cursor`;
4. collision-check retained duplicates with fieldwise semantic equality;
5. classify already-received IDs no longer retained in the ring as idempotent
   stale retries;
6. require the first new ID and all following new IDs to be exact successors;
7. derive each new cumulative sample time from
   `stream->last_received_sample_time_us + duration_ms * 1000`, with checked
   64-bit addition; and
8. stage only the new suffix and commit it through the transactional stream
   API.

Retained duplicates preserve the first accepted record. A changed command,
sample time, movement revision, or provenance class is a conflict. A legacy
retry may carry a different packet timing watermark: the command ABI keeps the
`LEGACY_PACKET_SHARED` provenance class semantic but treats its detailed
timing as diagnostic, so the retry is still idempotent. The adapter requires
that exact provenance and never upgrades it to exact-command timing.

An evicted older ID cannot be collision-checked because its record is no longer
resident. It is accepted only as stale and never reinserted, assigned a new
sample time, or simulated. This is safe because the stream's contiguous cursor
already proves that identity was accepted earlier.

The result report distinguishes applied versus fully idempotent ranges and
records inserted, retained-duplicate, and stale counts. Failures distinguish
invalid input/state/range/watermark, alias or scratch violations, future gaps,
wrong epochs, conflicts, capacity stalls, sample overflow, epoch exhaustion,
and unexpected stream rejection.

## Transaction and alias contract

The original single-record insertion API necessarily mutates after each
successful call, so it cannot by itself guarantee a multi-command carrier is
atomic. `Worr_CommandStreamInsertBatchV1` provides that boundary without heap
allocation:

1. validate the live stream and all array sizes;
2. require the stream envelope, live slots, source records, and caller scratch
   slots to be distinct non-overlapping half-open ranges;
3. clone the complete envelope, ring, cursors, sample time, and telemetry into
   scratch;
4. apply the ordered range to the clone, accepting inserted, retained
   duplicate, and stale members; and
5. copy the clone back only after every member succeeds.

Any member failure leaves the original stream envelope, slots, cursors,
sample time, and telemetry byte-identical. Scratch contents are intentionally
unspecified. The adapter imposes the stronger pairwise non-overlap contract on
the live stream, immutable inputs, record scratch, stream scratch, and report
output. Checked `size_t` multiplication and `uintptr_t` range-end arithmetic
fail before any authoritative write.

The batch API does not alter `Worr_CommandStreamInsertV1` behavior. It only adds
an all-or-nothing composition boundary.

## Ordinary MOVE bootstrap and phantom backups

An ordinary MOVE always decodes three slots, including on the first client
packets where one or two backup slots can still be zero-initialized phantoms.
The extension must **not** mint real canonical IDs for those phantoms.

Live activation must therefore wait until three real command identities exist,
or establish an explicit baseline from commands the legacy server truly
accepted/executed. The baseline must follow actual legacy `net_drop` and
executed-prefix behavior; array position alone is not proof of execution. A
backup prefix already represented by the baseline is duplicate/stale and must
not be simulated again.

The bootstrap test fixes this case directly:

```text
sideband MOVE range: {epoch 12, first sequence 100, count 3}
stream baseline:     consumed/received {epoch 12, sequence 101}
baseline sample:     9000 us
result:              IDs 100 and 101 stale; ID 102 inserted only
ID 102 sample:       9000 us + 4 ms = 13000 us
```

This proves that a three-slot carrier can preserve exact shape while inserting
only its genuinely new third command. It does not authorize seeding sequence
100/101 from phantom zeros, nor does it authorize executing the prefix twice.

## Deterministic verification

`legacy_command_adapter_test.c` proves:

- exact nine-pair encoding, golden CRC/commit, C11/C++20 layouts, and signed
  high-word round trips;
- strict begin/field/commit/move adjacency;
- missing, reordered, duplicate, mixed, checksum-corrupt, commit-corrupt,
  unsupported-version, ordinary-setting, other-service, and packet-boundary
  behavior;
- ordinary three-command MOVE mapping and the stale-prefix bootstrap case;
- variable four-frame flattening including a zero-command frame, plus the full
  124-command batch bound;
- retained backup collision checks, loss recovery with a duplicate prefix and
  new suffix, harmless retry under changed packet timing, opaque evicted stale
  retries, and future-range reorder/retry;
- natural sequence/epoch wrap and terminal exhaustion;
- checked cumulative-time overflow;
- capacity failure after a staged prefix with byte-identical live state;
- direct transactional batch failure after a valid first member with no
  partial mutation; and
- insufficient scratch, alias, count, watermark, and malformed-command
  failures.

Verification completed on 2026-07-12:

- Windows LLVM 20 C11 with `-Wall -Wextra -Werror -Wpedantic -Wconversion
  -Wsign-conversion`: adapter suite passed three consecutive runs;
- Windows LLVM 20 C++20 layout compilation: passed;
- Windows LLVM ASan + UBSan: passed (`detect_leaks=0`, unsupported by this
  runtime);
- Linux GCC 13.3 C11 with the same strict warning policy: passed;
- Linux GCC 13.3 C++20 layout compilation: passed;
- Linux GCC ASan + UBSan with leak detection: passed;
- LLVM static analyzer on both the adapter and transactional stream core:
  passed with no diagnostics; and
- existing canonical command-stream regression suite on Windows and Linux:
  passed.

Repository integration additionally passed the Meson command and adapter
targets three times on Windows: 18/18 across the command stream, command C/C++
layouts, legacy adapter, and adapter C/C++ layouts.

All generated executables, objects, sanitizer binaries, and analyzer output
were kept under `.tmp/legacy-command-adapter/`.

## Remaining `FR-10-T09` work

Items 1 through 7 from the original integration plan have landed: capability
negotiation with legacy fallback, live ID assignment and phantom-bootstrap
handling, adjacent unreliable serialization, strict packet parsing, stream
projection, simulation-only consume advancement, and atomic snapshot cursor
publication. The client also binds the cursor epoch to the negotiated session,
rejects regression, and makes packet ACK unavailable to prediction once cursor
authority is established.

Current remaining work is:

1. complete staged deterministic impairment/runtime parity, malformed-stream,
   reconnect, wrap, demo, and long-session acceptance matrices;
2. add native exact render watermarks and authoritative event correlation;
3. measure command stream/parser CPU, memory, capacity, and flood behavior under
   the mandatory T14 load gates; and
4. replace the compatibility sideband with the T04 native envelope while
   retaining the same canonical validators and explicit legacy fallback.

Live networking still carries commands in the legacy payload shape, but the
canonical cursor—not packet ACK—is the established prediction/rewind authority
for capable sessions.
