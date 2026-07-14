# FR-10-T09 Bounded Command-Gap Fast-Forward

Date: 2026-07-13

Project tasks: `FR-10-T09`, `FR-10-T14`, `FR-10-T16`

Status: common-core implementation and focused deterministic tests pass. A
post-change 50,001-frame rate-shaped diagnostic exercised live recovery, and
the independent 100,000-target snapshot gate passed; a dedicated retained
command-gap acceptance gate remains open.

## Purpose

The negotiated legacy command sideband must survive a legitimate loss burst
that is larger than the canonical command stream's retained audit ring. The
server still needs to reject an unsubstantiated or unbounded identity jump,
must not simulate an attacker-selected number of missing commands, and must
not let receipt advance past commands that are waiting for authoritative
simulation.

This slice separates three concepts that were previously coupled:

1. transport-observed history bounds how far an incoming command range may be
   ahead;
2. the fixed 128-slot command ring retains recent command records for replay,
   duplicate detection, and audit; and
3. the server's simulation budget determines how much missed input may still
   be applied to gameplay.

The new path retains full materialized history for ordinary gaps. For a larger
but transport-authorized gap, it applies only the bounded simulation policy and
advances the already-consumed canonical baseline by count in bounded work.

## Deterministic failure that motivated the change

The original long-run impaired profile failed at
`.tmp/networking/command-reject-50k-profile.impaired.stdout.log:9082` with:

```text
stage=gap range=1:8911/3 ... gap=127 simulate=0 ...
stream_count=128 head=45 received=1:8749 consumed=1:8749
```

The number of missing command identities was not 127. Command 8911 follows a
cursor at 8749 with:

```text
8911 - 8749 - 1 = 161 missing commands
```

A more aggressive stress reproduction failed at
`.tmp/networking/command-reject-stress.stdout.log:5011` with incoming command
1983 after cursor 1581:

```text
1983 - 1581 - 1 = 401 missing commands
```

Both failures had equal received and consumed cursors, a full production-sized
ring (`count=128`, `head=45`), and zero future-gap, conflict, capacity,
sample-time, invalid-record, and invalid-state telemetry. The command adapter
and stream were internally consistent. The rejection happened because the
accepted gap had been capped by `CMD_BACKUP - 1`, or 127, even though packet
history justified a larger discontinuity.

That made retention capacity an accidental protocol-validity limit. Enlarging
the ring would only move the failure point and increase per-client memory.
Materializing or simulating every missing command would make work proportional
to a loss burst. The appropriate recovery is therefore a bounded,
transport-authorized baseline advance.

The two retained logs above are pre-fix diagnostic evidence. They are not
post-fix live acceptance evidence.

## Constant-time identity distance

`Worr_CommandCursorGapBeforeV1` in `src/common/net/command_abi.c` computes the
number of identities strictly between a cursor and a later command. Its public
contract is declared in `inc/shared/command_abi.h`.

For IDs in the same epoch, the result is:

```text
later.sequence - cursor.contiguous_sequence - 1
```

Across epoch rollover, the helper adds the remaining identities in the current
epoch, any complete intervening epochs, and the identities before the later
command in its epoch. It uses 64-bit intermediates; the maximum representable
epoch/sequence distance fits the calculation. Runtime is O(1), regardless of
the requested distance.

The helper is also the policy check. It succeeds only when the command is
strictly later and the computed gap is at most the caller's `maximum_gap`.
Equal, older, invalid, or over-limit identities fail without writing
`gap_out`. The focused tests cover:

- the exact 161-command and 401-command failures;
- an accepted 4,096-command boundary and rejected 4,097-command boundary;
- a natural `{epoch, sequence}` rollover;
- an adjacent command across rollover, which correctly has a zero gap; and
- equal and backwards identities.

This avoids walking one identity at a time merely to determine whether a range
is admissible.

## Advance-by-count command-stream core

`Worr_CommandStreamFastForwardV1` in `src/common/net/command_stream.c` is the
transport-neutral advance-by-count primitive. The caller supplies only a
nonzero number of skipped commands and their canonical duration. The function
derives the destination cursor and cumulative sample time from the validated
current stream; it does not accept an arbitrary destination ID or timestamp.
Its distinct success result is `WORR_COMMAND_STREAM_FAST_FORWARDED`.

### Preconditions

The operation rejects unless all of the following hold:

- the command stream, envelope, slots, hashes, cursors, and timing validate;
- `skipped_commands` is nonzero;
- `duration_ms` is no greater than the stream's negotiated duration limit;
- received and consumed cursors are identical, so no pending authoritative
  command can be discarded;
- sequence advancement does not exhaust `{UINT32_MAX, UINT32_MAX}`; and
- duration multiplication and cumulative sample-time addition fit in
  `uint64_t`.

Every rejection is transactional. The complete stream envelope, retained slot
storage, and telemetry remain byte-identical. In particular, this operation is
not reported as an epoch reset, so reset attempts, resets, and reset
rejections retain their existing meaning.

### Commit

On success, the primitive:

1. advances both received and consumed cursors by exactly
   `skipped_commands`;
2. adds `duration_ms * 1000 * skipped_commands` to the cumulative sample time;
3. clears the old retained slots because they cannot form a contiguous ring
   with the new baseline;
4. resets `head` and `count` to zero; and
5. records a natural epoch wrap in existing stream telemetry when one occurs.

The work is O(capacity), dominated by validating and clearing the fixed slot
array, rather than O(skipped commands). The next ordinary command must be the
exact successor of the derived baseline and still passes the normal insert and
consume contracts.

Zero-duration commands are legal: identity advances while sample time remains
unchanged. One natural sequence rollover advances the epoch. Landing exactly
on `{UINT32_MAX, UINT32_MAX}` is legal; any further advance fails with
`EPOCH_EXHAUSTED`. Landing exactly on `UINT64_MAX` sample time is also legal;
the next positive-duration advance fails with `SAMPLE_TIME_OVERFLOW`.

## Server recovery policy

`SV_WorrFillCommandGap` in `src/server/user.c` owns transport policy. The
common command stream deliberately does not decide whether a network gap is
credible.

### Packet-history-derived allowance

The incoming canonical range cannot authorize its own jump. The legacy server
derives an allowance from server-observed packet loss and the backup shape of
the decoded carrier:

- a three-command MOVE subtracts its two backup commands from `net_drop`;
- a BATCH_MOVE subtracts its duplicate packet frames and multiplies remaining
  missing packets by at most `MAX_PACKET_USERCMDS - 1` commands; and
- both paths clamp the result to
  `WORR_CANONICAL_COMMAND_MAX_TRANSPORT_GAP`, currently 4,096.

The 4,096-command ceiling is a security and resource-policy boundary, not the
size of the retained ring. A claimed range beyond the packet-history allowance
or beyond the hard ceiling is rejected. The constant-time identity helper
computes the actual gap and enforces that allowance.

### Preflight before gameplay mutation

Before any synthetic command can reach `SV_ClientThink`, the server validates
the whole stream, requires received and consumed cursors to match, validates
the retained `lastcmd` duration, and proves both total duration multiplication
and cumulative sample-time addition cannot overflow. A predictable core
failure therefore cannot be discovered only after a partially simulated
prefix.

Policy and preflight failures increment the separate saturating
`worr_command_gap_policy_rejections` counter. They do not increment the core
fast-forward rejection count because the core was never called.

### Materialize, simulate, then skip

For a gap no larger than `CMD_BACKUP - 1`, the server preserves the existing
complete synthetic record trail. Every missing identity is inserted and
consumed; only commands inside the simulation budget call gameplay.

For a larger allowed gap, the server materializes at most the simulation
budget, consumes that prefix, and invokes `Worr_CommandStreamFastForwardV1`
for the remainder. This keeps simulation work policy-bounded and core work
ring-bounded. After the advance, the incoming range's first ID is again the
exact next command, so it proceeds through the unchanged transactional legacy
adapter and authoritative consume path.

The policy does not fabricate client-specific movement for the skipped tail.
Those inputs were already lost and are represented only by a
transport-authorized baseline advance using the last validated command
duration. It also never advances over received-but-unconsumed canonical work.

## Telemetry and diagnostics

Five per-client, saturating 64-bit counters are reset when the negotiated
legacy command-sideband connection epoch is initialized:

- `worr_command_fast_forward_attempts`;
- `worr_command_fast_forwards`;
- `worr_command_fast_forwarded_commands`;
- `worr_command_fast_forward_rejections`; and
- `worr_command_gap_policy_rejections`.

The first four describe calls at the server policy/core boundary. The final
counter includes identity-distance, transport-cap, readiness, duration, and
time-overflow rejections that fail preflight before a core call.

A successful debug record includes client, before/after cursor, actual gap,
materialized count, skipped count, packet-history allowance, all four
fast-forward totals, and policy rejections. A canonical command rejection now
reports `allowance` rather than mislabeling that value as the actual gap, and
includes stream cursors/ring state, relevant core failure counters, the four
fast-forward totals, and policy rejections.

These counters improve `FR-10-T14` diagnosis, but they are not yet a complete
machine-readable load, malformed-input, or cross-platform acceptance report.

## Deterministic retained-ring and boundary coverage

`tools/networking/command_stream_test.c` contains production-shaped regression
cases rather than testing only empty streams:

- A 128-slot ring is initialized behind command 8,749, then 173 commands are
  inserted and consumed. This produces the observed full ring at `count=128`,
  `head=45`. Advancing 161 identities clears it, lands both cursors on 8,910,
  lands sample time on 8,910,000 microseconds, and accepts/consumes command
  8,911 normally.
- The same retained-ring shape is built through command 1,581. Advancing 401
  identities lands on 1,982 and accepts/consumes command 1,983 normally.
- Zero count, over-limit duration, pending unconsumed work, corrupt stream
  state, epoch exhaustion, and sample-time overflow all prove byte-identical
  rejection for envelope and storage.
- Zero duration, natural rollover, exact terminal identity, exact terminal
  sample time, and the first operations beyond each terminal are covered.
- Successful fast-forward proves that reset telemetry is untouched and the
  resulting stream validates.

Focused Windows validation performed after inspecting the final code:

```text
meson test -C builddir-win --print-errorlogs network-command-stream
1/1 passed

meson test -C builddir-win --print-errorlogs --repeat 3 network-command-stream
3/3 passed
```

This focused test establishes deterministic common-core behavior. It does not
replace the impaired live acceptance runs.

## Post-change live evidence

The retained 50,001-frame rate-shaped run at
`.tmp/networking/command-fast-forward-50k-profile-v3.failure.json` reached its
nonce terminal marker after 24 successful fast-forwards and 10,229 skipped
commands, with zero core or gap-policy rejection. It included the exact
401-command stress discontinuity. The outer snapshot gate rejected the run
because its wall-clock 1,024-kbps shaper intentionally saturated the bounded
packet queue (`overflow=30784`); therefore this is scoped live command-recovery
diagnostic evidence, not a passing general networking report.

The separate unshaped accelerated target-count gate retained at
`.tmp/networking/live-snapshot-acceptance.json` passed 115,914 exact canonical
snapshot consumer accepts against a 100,000 target, with zero disconnect,
command-stream rejection, parity mismatch, consumer rejection, or queue
overflow. Its impairment profile did not produce an over-retention command
gap, so it validates sustained compatibility of the new recovery code but does
not replace a purpose-built live command-gap acceptance contract.

## Task impact and remaining work

- `FR-10-T09`: the live legacy carrier can now distinguish canonical retention
  capacity from a larger, validated transport discontinuity while preserving
  exact received/consumed identity. Native carrier integration and the full
  impairment/runtime matrix remain open.
- `FR-10-T14`: bounded policy counters, detailed rejection context, hostile
  boundary checks, and production-ring regressions are present. Mandatory
  malformed, performance, soak, and cross-platform gates remain open.
- `FR-10-T16`: legacy MOVE and BATCH_MOVE loss recovery no longer disconnects
  solely because a credible gap exceeds 127 commands. Native-adapter,
  fresh-input-age, bandwidth, command-flood, and cadence evidence remain open.

The full snapshot durability gate is now retained and the original live gap is
recovered under scoped stress. A dedicated command-gap gate must still expose
machine-readable fast-forward counters, require at least one recovery, and
separate its deliberate transport-loss stimulus from unrelated snapshot
acceptance assertions. Until that report and the broader native/load/platform
matrices pass, this remains recovery evidence rather than closure for any of
the three parent tasks.

## Implementation surface

- `inc/shared/command_abi.h`
- `src/common/net/command_abi.c`
- `inc/common/net/command_stream.h`
- `src/common/net/command_stream.c`
- `src/server/server.h`
- `src/server/user.c`
- `tools/networking/command_stream_test.c`
