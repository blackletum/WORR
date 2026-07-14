# FR-10-T06/T14 Live 100,000-Snapshot Acceptance Gate

Date: 2026-07-13  
Project tasks: FR-10-T06, FR-10-T14  
Status: live target-count gate passed with 115,914 exact accepted snapshots;
broader FR-10-T06/T14 promotion gates remain open

## Purpose

The existing staged impairment smoke proves that a production Windows client,
local server, cgame, and sgame can reach active play while canonical snapshot
projection, legacy comparison, consumer acceptance, adaptive input, recovery,
and deterministic packet impairment are enabled. Its deliberately short run is
not enough to satisfy the roadmap's 100,000-live-snapshot durability gate.

`tools/networking/run_live_snapshot_acceptance_gate.py` adds a target-count
runtime gate. It schedules bounded `wait` chunks through the normal engine
command buffer and does not emit its terminal status block until the requested
physics-frame budget has elapsed. The harness then parses production status
commands and rejects the run unless every attempted canonical frame was:

- projected and published;
- eligible for promotion;
- compared exactly with the legacy final-emission state;
- offered to and accepted by the canonical consumer; and
- free of mismatch, overflow, frame failure, and consumer rejection.

The default profile also requires deterministic loss, reordering, duplication,
and upstream stalls to occur without overflowing the bounded impairment queue.
It explicitly rejects any wall-clock rate throttling in this accelerated gate.

## Acceleration model

The runtime is still a real loopback client/server session on `mm-rage` using
protocol 1038 and game API 2025, and it loads the staged `basew` cgame module.
The gate does not infer a cgame API number from the module filename. It sets
`cl_async 1`, `cl_maxfps 1000`, `r_maxfps 10`, and `fixedtime 25`. Physics, networking,
snapshot construction, projection, comparison, and consumer code therefore run
at high frequency while OpenGL presentation runs at low frequency. No snapshot
or network implementation is replaced by a test stub.

The target-count gate sets `net_impair_rate_kbps 0`. The rate shaper uses
wall-clock `com_eventTime`, while `fixedtime 25` intentionally advances
simulation much faster than wall time. Applying a fixed wall-clock bandwidth
cap here creates a duration-dependent artificial backlog rather than a bounded
simulated-link impairment. Latency, jitter, loss, burst loss, reordering,
duplication, and upstream stalls remain active. The short staged runtime still
configures the live 1,024-kbps path, and deterministic impairment model tests
prove actual throttle scheduling. This target-count run is therefore snapshot
durability evidence, not the still-open FR-10-T14/T16 bandwidth gate.

The engine's `wait` command accepts no more than 1,000 frames per invocation.
The gate schedules 150% of the requested target plus a 2,048-frame startup
allowance and partitions that budget into legal chunks
instead of relying on wall-clock sleeps. A terminal 1,000-frame wait keeps the
process alive until the harness has flushed, parsed, and retained its status
block.

The allowance was measured against the deterministic impaired profile: an
initial 1,000-target production probe produced 734 accepted frames from a
1,064-frame schedule, leaving a 330-frame startup/connection deficit. That
short probe was observed interactively and is not retained as acceptance
evidence, so the deficit is not attributed exclusively to handshake cost. The
larger allowance absorbs startup variability, while the parsed accepted count
remains the actual pass/fail condition.

A later 12,048-frame impaired run produced 8,292 attempted frames. This showed
that the impaired snapshot-production ratio is about 69%, not a fixed startup
offset. The default 150% schedule plus the startup allowance accounts for that
measured ratio, but still cannot pass unless the terminal production and
consumer counts independently meet the requested target.

## Evidence contract

The default command is:

```powershell
python tools/networking/run_live_snapshot_acceptance_gate.py `
  --client-exe .install/worr_x86_64.exe `
  --working-dir .install `
  --output .tmp/networking/live-snapshot-acceptance.json `
  --target 100000 `
  --profile impaired `
  --timeout 1800
```

The JSON schema is `worr.networking.live-snapshot-acceptance.v1`. It records a
unique run ID, UTC bounds, target, scheduled frame count, elapsed wall time,
acceleration settings, exact command, nonce-bearing completion marker, parsed
adapter state, canonical snapshot telemetry, impairment counters, and retained
stdout/stderr paths and hashes. Before launch it hashes the exact staged
launcher, engine, cgame, sgame, and OpenGL renderer, including size and mtime;
the same manifest is recomputed after each profile and any change invalidates
the run. Evidence is not considered passing merely because the process stayed
alive: the target and exact pipeline equalities are mandatory. Terminal status
must also retain `consumer=1` and the complete `accept_flags=0x3` contract.

Every invocation invalidates the requested pass report before launching. Logs
live under `<output-stem>.runs/<run-id>/`, so a failed rerun cannot leave an old
pass report pointing at overwritten failure logs. On failure the harness writes
both a run-local manifest and `<output-stem>.failure.json` with the exact
arguments, active command, prelaunch component manifest when available, error,
and artifact hashes. Missing or unreadable binaries therefore produce a
failure manifest without the exception handler trying to hash the missing
file. Report construction and both atomic pass writes are inside the same
failure boundary. The pass report is written only after all selected profiles
pass.

The engine emits a unique completion marker after all terminal status commands.
The harness reads only newly appended stdout while the process is alive and
requires spawn, impairment counters, and that marker. Disconnect/drop markers
are scanned first and fail even when a failure line and completion marker share
one appended chunk. This avoids both quadratic whole-log rescans and accidental
early completion if another status command is added later.

## Focused verification

```powershell
python -m py_compile `
  tools/networking/run_live_snapshot_acceptance_gate.py `
  tools/networking/test_run_live_snapshot_acceptance_gate.py
python tools/networking/test_run_live_snapshot_acceptance_gate.py
python tools/networking/test_run_staged_impairment_smoke.py
```

The 14 gate tests and 12 shared runtime-parser tests verify legal wait
partitioning, the full 100,000-target
schedule, acceptance of a lossless pipeline, and rejection of both a short
consumer count and an internal projection/publication loss. It also covers
stale pass/failure invalidation, all five staged runtime component hashes,
missing-component failure, terminal consumer/acceptance flags,
completion-marker command safety, successful marker-controlled termination,
failure-marker precedence (including a shared output chunk), and natural exit
before completion. The impaired counter contract directly rejects a missing
impairment class, bounded-queue overflow, or accidental wall-clock throttling;
the clean contract also treats throttling as a raw-routing violation.

## Long-session failure found by the gate

The first configured 100,000-target diagnostic attempt failed without producing
pass JSON. Before the terminal status block, the production server dropped the
loopback client with `invalid canonical command stream` after sustained
impaired operation. The retained diagnostic log is:

```text
.tmp/networking/live-snapshot-acceptance.impaired.stdout.log
```

The tail contains 10,808 out-of-date delta requests followed by the canonical
command-stream disconnect. The original diagnostic predates the failure-
manifest support above, so the log alone does not independently prove its
configured target or complete impairment counters and is not acceptance
evidence. A separate 12,048-frame run remained connected and preserved exact
canonical snapshot parity. Together these observations justify investigation
of a timing-sensitive sustained-session problem, but no stronger claim. The
Instrumented reproduction located the rejection at incoming range `1:8911/3`
after received/consumed cursor `1:8749`: 161 commands were missing, while the
server incorrectly used the 128-slot retention ring as a 127-command transport
validity ceiling. A stress profile reproduced the same fault with the
`1:1581` to `1:1983` distance (401 missing commands).

The fix separates storage retention from transport validity. Server-observed
packet history remains the tighter allowance under a named 4,096-command
security ceiling. Identity distance is computed in O(1), all predictable
cursor/time failures are preflighted before simulation, a bounded policy prefix
may be materialized, and the remaining already-lost commands advance through a
transactional O(capacity) stream operation. Focused tests reproduce both full
128-slot retained-ring states with `head=45`, sequence rollover, zero-duration
commands, the 4,096/4,097 boundary, sample-time overflow, terminal epoch
exhaustion, corrupt state, and pending commands. The complete nonce/build/log-
hash-backed 100,000 run below now closes the snapshot-count item. Rate-shaped
command-gap stress remains separate from general snapshot acceptance.

The command-gap investigation also produced a terminal-marker-complete 50,001-
frame stress run that exercised 24 bounded fast-forwards and skipped 10,229
already-lost commands without a command-policy rejection. Its outer snapshot
gate correctly failed because the 1,024-kbps wall-clock shaper overflowed
30,784 packets, so it is scoped command-recovery diagnostic evidence rather
than a passing snapshot report. That run simulated 1,250.025 seconds in
112.758 wall seconds (11.086x): 99,330 delayed copies were offered, only 67,522
were released, 1,024 remained queued, and 30,784 overflowed. Increasing queue
capacity would preserve an invalid multi-second backlog; it was not done.

## Current 100,000-target evidence

The final impaired target-count run is retained at:

```text
.tmp/networking/live-snapshot-acceptance.json
```

Run `20260713T205847.194133Z-11224` scheduled 152,048 physics frames and
completed in 683.187 seconds. Against the required 100,000 target it recorded
115,914 attempts, projections, publications, promotion-eligible frames,
legacy comparisons, consumer attempts, and consumer accepts. Mismatches,
entity mismatches, frame failures, capture overflows, promotion blocks,
consumer rejections, rate throttles, queue overflows, and queue resets were all
zero. The terminal consumer remained active with `consumer=1` and
`accept_flags=0x3`.

The deterministic impaired link recorded 304,131 packets seen, 3,697 dropped,
789 burst-dropped, 1,553 reordered, 1,482 duplicated, and 150,151 upstream
stalls. The report binds five staged runtime components. Its stdout SHA-256 is
`ccb13f88e7012952def4eee67918a944f1185854eb72ec320b65a04678d58836`;
stderr was empty with SHA-256
`e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`.

| Runtime role | SHA-256 |
|---|---|
| launcher | `68bab536d00d43eb3bcb7dee7e347bc52c52fa69380b62ac28c35d035963e2f9` |
| engine | `403c0ef81515b2973fb72c1199cc56687f5dcd0ca18131642f1d0be0193054ab` |
| cgame | `d717da8b9f05548d9d09e2ae9a9d5dd02c48e50d37a793db3753e76de0bb287d` |
| sgame | `ede285a2041e882038e9e901c452db8799a96be60a489eaedc66568d3796e722` |
| OpenGL renderer | `bf06374cbbdc550acd4014875a89c8141f01f5b02f021ed66964f75c173bf942` |

The harness recomputed this manifest after the run and rejected any change; a
manual post-run SHA-256 check also matched all five entries. This current run
supersedes the earlier 121,664-accept report: independent evidence review found
that its staged engine had been rebuilt after that report completed. The
production targets were rebuilt, `.install/` was refreshed and validated, and
the gate was rerun from a unique directory before this evidence was recorded.

## Remaining promotion boundary

This evidence closes only the live 100,000-frame durability item. It does not
by itself close FR-10-T06 or FR-10-T14: the
remaining roadmap requirements include native-wire serialized parity,
multi-client load, decoder fuzz/range matrices, performance budgets, longer
soaks, and platform coverage.
