# FR-10-T04 Repeated Command Shadow and Mixed Carrier Core

Date: 2026-07-14

Tasks: `FR-10-T04`, `FR-10-T09`, `FR-10-T14`, `FR-10-T16`

Status: implemented and validated as two deliberately separate advances:

- the default-off, unadvertised production command shadow now carries a
  bounded repeated stop-and-wait stream from client to server; and
- an additive common-core transaction can compose one DATA fragment with up to
  seven exact ACK ranges, but no production adapter calls it yet.

Legacy `MOVE`/`BATCH_MOVE` remains the sole simulation authority. No native
capability is advertised, no live packet in this milestone contains mixed
DATA-plus-ACK entries, and all four parent tasks remain Incomplete.

## Outcome and authority boundary

The one-command production observation has advanced to a repeated stream
without changing its authority boundary:

1. The client writes and sends the authoritative legacy command range exactly
   as before.
2. When no native command is retained, it selects the newest command from that
   exact successfully encoded range and retains one immutable canonical
   `WNC1` record.
3. The production TX hook sends that record as one
   `WTC1(DATA(WNE1(WNC1)))` transaction and waits for an exact receipt.
4. The server validates and commits the native record, joins it against the
   matching authoritative legacy command in either arrival order, and exposes
   only the unchanged legacy prefix to simulation.
5. A successful comparison retires the bounded join slot immediately. The
   server continues exact-receipt delivery, and the client transactionally
   releases its retained record when a covering ACK arrives.
6. Only after release may the client retain the next sampled command.

This is intentionally stop-and-wait. It bounds the production pilot to one
retained command per active client bank, preserves exact identity at every
transition, and demonstrates repeated lifecycle reuse without introducing an
unbounded queue or changing command consumption.

The private readiness proof still uses mask `0x13`; public offer, support, and
confirmation masks remain `0x03`. Both controls remain default-off:

- `cl_worr_native_shadow 0`
- `sv_worr_native_shadow 0`

No file under `q2proto/` changed.

## Repeated command lifecycle

### Client retention and receipt processing

The client no longer has a one-shot DATA latch. It tracks the last command ID
enqueued in the active bank and may select a strictly newer command after the
previous retained payload has been released. A failed or rejected prepare does
not consume the candidate; a synchronous rejected handoff keeps the same
fragment retryable; and exact ACK application remains an all-or-none update of
the staged transport session, slot, and payload registry.

The live gate exposed an old ACK-only parser assumption that a carrier had to
contain exactly one entry. The client now accepts one or more entries only when
every entry is an exact ACK range. It rejects a carrier containing DATA in that
direction, applies the complete ACK set transactionally, and releases at most
the one record permitted by the stop-and-wait pilot. Focused coverage includes
multiple ranges, redundant coverage, old and same command notifications, lost
receipt retries, and rejected prepares.

A 256-command client test repeatedly enqueues, hands off, acknowledges, and
releases the same bounded slot. Its final counters require 256 proofs, first
sends, acknowledgements, and releases, with retained high-water one and zero
records remaining.

### Server compare, retirement, and replay guard

`Worr_NativeCommandShadowJoinRetireComparedV1()` is an exact, pointer-free
join operation. It accepts only a valid command ID naming a slot that has
already completed comparison, clears that slot, decrements bounded occupancy,
and returns `WORR_NATIVE_COMMAND_SHADOW_JOIN_RETIRED`. A missing,
uncompared, or invalid identity leaves the join unchanged.

After an exact match, the production server:

- retires the compared slot before publishing the match;
- clears the pending native identity;
- records the matched canonical command as a per-peer high-water mark; and
- keeps the committed receipt eligible until normal ACK accounting completes.

An exact transport duplicate is handled before fresh-DATA admission and
refreshes the already committed receipt without recreating join state. A fresh
native message identity may not reuse the same or an older canonical command
ID. Such a replay leaves the staged RX/receipt mutation uncommitted, exposes the
authoritative legacy prefix, and enters bounded `DRAIN`. Delayed DATA for the
retired transport bank is drained before the current-bank high-water guard, so
an old map epoch cannot poison the active stream.

Command or sample-offset disagreement remains fail-closed for the optional
pilot and never changes the command simulated from the legacy path.

## Mixed DATA-plus-ACK common core

`native_carrier_mixed` is an additive, transport-neutral coordinator over the
existing carrier dispatch and ACK-ledger APIs. It does not advertise a
capability, install a netchan hook, or send a packet.

Its pointer-free V1 token is exactly 168 bytes and binds:

- connection owner and transport epoch;
- dispatch token, message sequence, and fragment index;
- complete packet length and CRC; and
- the exact ordered ACK ranges prepared for that packet.

The coordinator reserves space for seven ACK entries before beginning one DATA
dispatch. A successful packet may therefore contain one DATA entry plus zero
to seven exact ACK ranges within the existing eight-entry WTC1 ceiling. If no
ACK is due, preparation succeeds as DATA-only; any other ACK preparation error
rejects the whole operation without mutating caller state.

Confirm, reject, and abort are staged all-or-none transactions:

- confirm spends DATA send accounting and ACK handoff credits only after the
  exact bound packet is synchronously accepted;
- reject preserves the DATA fragment and ACK ranges as retryable without
  spending handoff credit;
- packet abort proves the packet never reached transport, restores the ACK
  ledger, and aborts the DATA dispatch; and
- pre-packet abort closes an active DATA dispatch without touching unrelated
  ACK state.

The additive
`Worr_NativeCarrierSessionDispatchConfirmMixedPacketV1()` validates the mixed
shape and exact dispatch fragment while retaining the original strict
exactly-one-DATA confirmation API unchanged. Core and C++ layout tests cover
maximum seven-range composition, DATA-only fallback, packet/owner/epoch/
dispatch/fragment/range mismatch, copied or stale tokens, confirm/reject/abort
repeat-call rejection, and transactional state preservation.

The mixed core is compiled and linked into both production client and dedicated
engine targets. That proves availability at the next adapter seam; it is not
evidence that a production packet has used it.

## Repeated two-process runtime gate

`tools/networking/run_native_shadow_repeated_runtime_smoke.py` launches a
staged client and dedicated server as separate IPv4 UDP processes. The profile
uses Rerelease protocol `1038`, `base1`, a 40 Hz server, `net_maxmsglen 512`,
fixed impairment seeds `424242` and `817263`, and 25 ms deterministic latency
in each direction. It holds the first sample until readiness is active, permits
600 client sampling frames, re-holds selection, drains receipts, and requires
at least 32 completed commands.

The V1 report reconstructs and hashes the exact process argument vectors,
hashes every runtime component and retained log, reparses the final status rows
from those logs, requires empty stderr, and binds the report to the same
counter contract. Three consecutive reports pass:

| Evidence | Run ID | Commands | Elapsed | ACK carriers | Report SHA-256 |
|---|---|---:|---:|---:|---|
| `.tmp/networking/native-shadow-repeated-runtime.json` | `20260714T053456.390921Z-41232` | 152 | 33.000 s | 426 | `dd21b26de0fc462ab02c6c8c214704ec62a9d7d7665cddb6f3f2e5d8ab2d1622` |
| `.tmp/networking/native-shadow-repeated-runtime-repeat2.json` | `20260714T053540.250797Z-42976` | 150 | 32.187 s | 419 | `720fb072536062c5260c88973e7f0e4ed0363fb3976f1998aa8b68ef7a89973f` |
| `.tmp/networking/native-shadow-repeated-runtime-repeat3.json` | `20260714T153504.103689Z-54580` | 151 | 32.984 s | 445 | `b9f478a103c6e879e5b9a2dc1439d062d4f6070cc06d7e7d5ea80bbfdc2c52ab` |

Across 453 completed commands, every run has exact equality among:

- client proof enqueues, first sends, acknowledged reliable messages, and
  retained releases;
- server RX commits, legacy joins, and command matches; and
- the report's accepted command count.

Every client finishes with retained high-water one, zero retained messages,
zero TX retries, zero drains, and zero failures. Every server finishes with
zero ACKs still eligible, zero command/sample mismatch, zero RX/TX rejection,
zero drained records, zero drains, and zero failures. ACK prepare, handoff,
async-wake, and client ACK-carrier totals agree within each run: 426, 419, and
445 respectively.

## One-command compatibility regression

The original two-profile gate was rerun after repeated streaming and the
multi-range ACK change. It remains exact:

```text
.tmp/networking/native-shadow-runtime-post-repeated.json
```

The report has run ID `20260714T153628.255979Z-24364`, elapsed time 85.578
seconds, SHA-256
`9382b6d74b8bd53e5ac5620d8610e4cb36252b902fab3eb1295be8563b4b3ff7`,
and `passed: true`.

| Trial | Native result | ACK/scheduler evidence | Reliable payload |
|---|---|---|---|
| `fragment_pressure` | proof 1, match 1, release 1 | 4 ACK handoffs, 206 rate deferrals, 25 fragment deferrals | 12,800 bytes, complete exactly once |
| `post_burst_async_ack` | proof 1, match 1, release 1 | 3 ACK handoffs, 3 wakes, 3 async ACK handoffs | 6,400 bytes, complete exactly once |

Both trials end with zero retained commands, mismatch, rejection, drain, or
failure. This preserves the original narrow lifecycle and scheduler contract
while the same production binary also supports repeated sampling.

## Build, staging, and test evidence

The complete Windows Clang production build succeeds. The refreshed canonical
`.install/` was verified, then copied to an immutable test-only runtime at:

```text
.tmp/networking/native-shadow-isolated-20260714T063249514/.install
```

The clone prevents an unrelated concurrent staging refresh from changing a
binary between process launch and report validation. At clone creation, all 12
copied runtime/package artifacts matched the canonical `.install/` by SHA-256.
The six principal built/staged identities are:

| Artifact | SHA-256 |
|---|---|
| `worr_x86_64.exe` | `7d333012bc1197d20ebb753f14a9b22b2479ee7c0dccfec4a065285ffe61cccb` |
| `worr_ded_x86_64.exe` | `caa652d8670e359372f6f48c94aeab8b8e21ac5af8fefdb857b075ff6f32113c` |
| `worr_engine_x86_64.dll` | `c2e06b485a9afce5149a2ea14500fb2b77a2ca7984ec801c7036e58971aea5e7` |
| `worr_ded_engine_x86_64.dll` | `6089bf9219c59e5647103492bdea61703c938fe0b884939c96a6ad34c1010eb8` |
| `basew/cgame_x86_64.dll` | `b58c0152c2a715b48b9621c422a4eb2a8901d1c0347b5ab740343e656b8afad3` |
| `basew/sgame_x86_64.dll` | `0717444516f8c2bed5b260bfbffba62a83927f9897d484ee37d49a0f112863ce` |

Current validation includes:

- focused command-shadow, server-pilot, client-pilot, carrier-session, mixed-
  carrier, C/C++ layout, and report-parser tests passing;
- the original runtime-validator Python unit suite passing 33/33;
- the repeated-runtime-validator Python unit suite passing 8/8;
- all 108 registered networking tests passing once; and
- three complete networking-suite repetitions passing 324/324.

Those counts are the exact validation baseline for this transport milestone.
After the later `FR-10-T05/T07/T08` cgame event-runtime/export follow-up, the
current integrated baseline is 113/113 once and 339/339 across three complete
repetitions; the production build and refreshed Windows `.install/` also pass.
That later evidence is recorded separately in
`docs-dev/fr-10-t05-cgame-event-runtime-and-direct-authority-export-2026-07-14.md`.

## Scope limits and next gates

This milestone does not complete `FR-10-T04`, `FR-10-T09`, `FR-10-T14`, or
`FR-10-T16`. Its explicit limits are:

- legacy `MOVE`/`BATCH_MOVE` remains the sole simulation authority;
- the repeated gate samples a bounded stop-and-wait subset, not every legacy
  command generated while a native record is retained;
- the live path remains client-originated DATA with server ACK-only traffic;
- mixed DATA-plus-ACK is production-linked common core, not a live adapter;
- the repeated profile adds 25 ms latency but no loss, jitter, reordering,
  duplication, corruption, bandwidth pressure, or upstream stall;
- localhost UDP does not establish WAN, NAT, multi-client fairness, load, soak,
  or supported-platform behavior; and
- native event/snapshot delivery, adaptive native batching/redundancy, demos,
  spectators, advertisement, and default promotion remain open.

The next transport slice is to connect the mixed coordinator at the existing
default-off production seams, add server-originated DATA, and exercise repeated
delivery under directional loss, reordering, duplication, corruption, and
bandwidth pressure. Native authority can be considered only after dual-adapter
identity, input-age, correction, demo, load, soak, and platform evidence is
exact.
