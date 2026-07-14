# Deterministic Networking Impairment Harness (2026-07-12)

Task: `FR-10-T03`.

Strategic project:
`docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`.

Living plan:
`docs-dev/plans/progressive-networking-events-snapshots-roadmap.md`.

## Outcome

WORR now has one deterministic integer-only packet-impairment model shared by
the runtime engines and the noninteractive test runner. It replaces the old
`net_dropsim`-only testing limitation with bounded injection of:

- latency and signed jitter;
- independent and burst loss;
- explicit packet reordering and duplication;
- bit corruption at a deterministic bounded byte/bit location;
- extra upstream delay for sequenced client datagrams;
- byte-rate throttling;
- bounded-queue saturation;
- stale/future acknowledgement rejection and legacy sequence-exhaustion cases.

The feature is default-off, non-archived, and does not alter `q2proto/` or any
wire schema. The existing `net_dropsim` remains as a compatibility-only legacy
loopback loss control; all new evidence uses `net_impair_*`.

## Production Model

`inc/common/net/impair.h` and `src/common/net/impair.c` contain the portable
model. Decisions use PCG-XSH-RR with a fixed stream, integer basis-point
probabilities, integer microsecond rate scheduling, and saturating arithmetic.
The same seed, profile, packet sizes, flags, and virtual times therefore produce
the same decisions on all supported hosts.

The model returns a value-only decision describing drop, release time, optional
second copy, reordering, and bounded corruption. It never reads wall clock,
network sockets, renderer state, or mutable engine globals. Sequenced client
datagrams can be tagged for additional upstream delay distinct from symmetric
latency. Because one legacy datagram carries both commands and an
acknowledgement, this control deliberately delays the whole datagram; it does
not claim to rewrite or independently stall an acknowledgement field.

`inc/common/net/sequence.h` and `src/common/net/sequence.c` are the production
legacy netchan validation seam. Both old and new netchan processors now ignore
regressing acknowledgements, reject acknowledgements for packets not yet sent,
and use the same explicit non-wrapping sequence rule exercised by the test
runner. Legacy connections renew before the 30/31-bit sequence space is
exhausted; a masked zero after the maximum is stale, never a guessed new epoch.

## Engine Integration

`src/common/net/net.c` integrates the model immediately above raw UDP/loopback
send. Both client and dedicated engines compile the same code.

Delayed packets live in a binary min-heap of indices into 1,024 fixed packet
slots. The scheduler and 32-to-64-bit clock extender are isolated in
`inc/common/net/impair_queue.h` and `src/common/net/impair_queue.c`, allowing
the exact production primitives to run in the ordinary unit suite. Packet slot
storage is allocated once in `NET_Init`; send, heap insertion, release,
duplicate, loss, and corruption paths do not allocate. The active limit is
independently clampable to `1..1024`. Equal release times retain monotonic
insertion order.

`NET_GetPackets` and `NET_Sleep` pump due packets. Sleep duration is shortened
to the next release deadline, avoiding an artificial polling-granularity delay.
An extended 64-bit runtime clock preserves ordering across the 32-bit system
millisecond wrap.

Queue overflow behaves like a successful local send followed by virtual-link
loss. This preserves network-failure semantics rather than reporting a local
socket error. Disabling or changing the profile clears queued packets and
restarts both directional deterministic streams.

## Controls

All controls are developer/test controls with `CVAR_NOARCHIVE`:

| Control | Range / meaning |
|---|---|
| `net_impair_enable` | `0..1`; default `0` |
| `net_impair_seed` | 32-bit deterministic seed |
| `net_impair_latency_ms` | `0..2000` one-way base delay |
| `net_impair_jitter_ms` | `0..2000` signed delay range |
| `net_impair_loss_pct` | `0..100` independent loss |
| `net_impair_burst_loss_pct` | `0..100` burst-start probability |
| `net_impair_burst_length` | `1..64` packets per burst |
| `net_impair_reorder_pct` | `0..100` explicit extra-hold probability |
| `net_impair_duplicate_pct` | `0..100` duplicate probability |
| `net_impair_corrupt_pct` | `0..100` deterministic one-bit corruption |
| `net_impair_upstream_stall_ms` | `0..2000` extra delay for sequenced client-to-server datagrams |
| `net_impair_rate_kbps` | `0..1000000`; zero disables throttling |
| `net_impair_queue_limit` | `1..1024` active fixed-slot limit |

`net_impair_status` reports the complete active profile, queue occupancy/high
water, resets, overflow, and per-feature counters. `net_impair_reset` restarts
the profile and clears counters. `net_impair_queue_selftest` fills the real
runtime heap to its configured limit, proves exactly one overflow, pops every
entry in release order, returns and reuses a slot, validates the free-list
invariant, then clears the synthetic entries.

## Deterministic Matrix and Evidence

`tools/networking/scenarios/impairment_matrix.json` is the versioned profile
source. Its 15 profiles cover:

- exact `0`, `50`, `100`, and `200` ms latency;
- jitter;
- `1`, `5`, `10`, and `20` percent independent loss;
- burst loss;
- reorder, duplicate, corruption, throttling, and upstream stall.

`network_impair_model_test` executes unit assertions for exact schedules,
bounds, repeatability, boundary corruption, throttling, upstream stalls,
production queue ordering/overflow/pop/reuse, clock wrap, stale/future
acknowledgements, and the legacy fail-closed sequence-exhaustion policy. Its
JSON mode exposes a stable digest, exact feature counters, scheduled-copy
accounting, and observed release-order inversions.

`tools/networking/run_networking_baseline.py` executes every profile at least
twice, requires byte-equivalent normalized JSON, enforces per-feature
observability, exact delay bounds, duplicate-copy accounting, and actual
release-order inversion for the reorder profile, then writes the versioned
aggregate report.
An ordinary run also projects each report onto its stable regression contract
(profile name, normalized model configuration, counters, delay statistics,
digest, and sequence/acknowledgement invariants) and compares it with:

```text
tools/networking/baselines/impairment-model-golden-v1.json
```

Paths, the evidence output location, and the requested repeat count are not
part of this comparison. A deterministic but behaviorally changed model now
fails with a unified diff instead of silently replacing the expected result.

### Intentional rebaseline workflow

Updating the golden is an explicit maintainer action. First review the model or
scenario change, build `network_impair_model_test`, then run:

```powershell
python tools/networking/run_networking_baseline.py `
  --model-exe builddir-win/network_impair_model_test.exe `
  --output .tmp/networking/impairment-baseline.json `
  --repeat 3 `
  --rebaseline
```

`--rebaseline` writes the golden only after every matrix profile passes its
semantic validation and produces identical reports in all repeated runs.
Review the golden diff, especially configuration, counters, delay statistics,
and digest changes, then rerun the same command without `--rebaseline` to prove
the checked-in expectation. Use `--golden <path>` only when deliberately
creating or verifying a separately versioned baseline.

The top-level Meson project registers these two T03-owned ordinary, safe tests
regardless of the dangerous legacy `-Dtests=true` option:

```text
network-impair-model
network-impair-baseline-repeatability
```

The `networking-baseline` run target writes:

```text
.tmp/networking/impairment-baseline.json
```

On Windows, `tools/networking/run_staged_impairment_smoke.py` and the
`networking-runtime-smoke` run target exercise the staged dedicated queue and a
real staged client/server loopback sessions. A default-off control must load
game API 2025, connect on protocol 1038, reach `cs_spawned`, and transmit while
all impairment counters and queue overflow remain zero. A second profile must
observe actual loss, reorder, duplicate, and upstream-stall counters without
queue overflow. Evidence is accepted only while each client process is still
alive; the report records that the harness, rather than a crash or spontaneous
exit, terminated it.

Runtime evidence:

```text
.tmp/networking/impairment-runtime.json
.tmp/networking/impairment-runtime.control.stdout.log
.tmp/networking/impairment-runtime.control.stderr.log
.tmp/networking/impairment-runtime.stdout.log
.tmp/networking/impairment-runtime.stderr.log
.tmp/networking/impairment-runtime.queue.stdout.log
.tmp/networking/impairment-runtime.queue.stderr.log
```

## Validation

Completed:

```powershell
meson test -C builddir-win --suite networking --print-errorlogs
meson compile -C builddir-win networking-baseline
meson compile -C builddir-win networking-runtime-smoke
```

The two T03 tests pass, including the extracted production queue, clock, and
sequence validators. After the T02/T05 tests joined the same suite, the full
2026-07-12 integration run passed 33/33 across three repetitions. The matrix
report matches the checked-in golden across three executions of every profile.
Both staged control and impaired runtimes loaded API 2025, reached `cs_spawned`,
and were terminated by the harness while alive. The control transmitted with
`seen=0`; the impaired run recorded `seen=975`, `dropped=7`, `reordered=6`,
`duplicated=7`, `upstream_stalled=572`, zero queue overflow, and empty stderr.
The dedicated queue self-test passed at capacity 7 with high-water 7, exactly
one overflow, ordered release, and slot reuse.

## Scope Boundary

This completes the deterministic virtual-link and repeatable baseline
requirements of `FR-10-T03`; it does not by itself prove snapshot, prediction,
event, rewind, weapon, demo, or release acceptance. Those subsystems must
consume this harness and add their own state/event/hit hashes and pass/fail
budgets under the declared matrix.
