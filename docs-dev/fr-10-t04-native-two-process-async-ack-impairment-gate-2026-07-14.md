# FR-10-T04 Native Two-Process Reliable and Asynchronous ACK Impairment Gate

Date: 2026-07-14

Tasks: `FR-10-T04`, `FR-10-T09`, `FR-10-T14`, `FR-10-T16`

Status: accepted as a stable 3/3 staged Windows gate for the default-off,
unadvertised, one-command observation pilot. This is not soak, multi-client,
cross-platform, repeated-native-stream, or native-authority evidence. All four
parent tasks remain Incomplete.

## Accepted evidence

The primary artifact is:

```text
.tmp/networking/native-shadow-runtime.json
```

It reports schema `worr.networking.native-shadow-runtime.v1`, `passed: true`,
run ID `20260714T043333.212083Z-16912`, and 86.016 seconds total elapsed time.
The run started at `2026-07-14T04:33:33.212083+00:00` and completed at
`2026-07-14T04:34:59.228295+00:00`.

Three consecutive revised-profile reports pass:

| Evidence | Run ID | Elapsed | SHA-256 |
|---|---|---:|---|
| Primary | `20260714T043333.212083Z-16912` | 86.016 s | `2305019bf20272b2b567349e8dfafdba6ea3f1bd9488f16140e5e439694a8719` |
| Repeat 2 | `20260714T043520.056196Z-20372` | 85.828 s | `d90c3fca114bcf7fa619b825f2142ad79f8647b114939cea77ffb07e0a9062ad` |
| Repeat 3 | `20260714T043655.512382Z-16852` | 86.062 s | `9b75dc0b5db284c707b8b872d695884a7a0890594d550d4109333e967a8a574b` |

The revised gate establishes these narrow production facts:

- a staged client and staged dedicated server complete private readiness over
  separate IPv4 UDP processes while the public capability mask remains `0x03`
  and the private proof remains `0x13`;
- one canonical command is retained, transported observationally beside the
  authoritative legacy command, matched on the server, acknowledged, and
  released exactly once on the client in each fresh connection;
- bounded ordinary reliable traffic is delivered completely and exactly once
  through the same real netchan;
- the low-rate trial observes real server rate and fragment-owner deferral;
- the high-rate trial deterministically admits the async owner and records
  exactly three asynchronous wake attempts and three ACK handoffs; and
- no accepted trial records a native drain, mismatch, rejection, or failure.

Legacy `MOVE`/`BATCH_MOVE` remains the sole simulation authority throughout.
No capability is advertised or promoted.

The complete registered networking suite also passes 105/105, and three
complete repetitions pass 315/315 with zero failures.

## Transport-only harness

`tools/networking/run_native_shadow_runtime_smoke.py` starts two fresh staged
process pairs sequentially. It does not use an in-process `map` client. The
dedicated server listens on `127.0.0.1`, the client connects over IPv4 UDP, and
both negotiate Rerelease protocol `1038` with `net_maxmsglen 512`. The server
runs at 40 Hz.

The harness uses stock `base1` with `deathmatch 0`, `coop 0`, `maxclients 1`,
and the owner/match auto-join controls enabled. This auto-joins the one client
without entering the multiplayer welcome/menu service, whose larger UI
`stufftext` batches are outside this transport-only proof. A single bounded
`stuffall` command releases the client's held native probe at a deterministic
point; reliable pressure itself uses SVC print records so it never overflows
the client's 4 KiB stuffed-command buffer.

Both endpoints enable deterministic impairment with 25 ms latency, queue limit
1,024, and fixed seeds `424242` (client) and `817263` (server). Jitter, packet
loss, burst loss, reordering, duplication, corruption, upstream stall, and the
impairment layer's separate rate throttle are all zero. The low-rate trial uses
the dedicated server's real 1,500-byte-per-second client rate and a 512-byte
netchan ceiling. The async trial uses 1,000,000 bytes per second; every staged
datagram is below 1,000 bytes, so the integer send-delay calculation is zero and
the async owner is admitted by construction instead of racing a synchronous
snapshot tick.

Before each burst, the server waits 1,000 frames so join/bootstrap reliables can
drain, sends the one control command, waits another 80 frames, then queues the
trial's reliable SVC print records. Completion markers must follow valid V1
client/server status rows. The runner rejects stderr output, incomplete or
duplicate reliable prints, status disagreement, failure counters, changed
runtime components, missing logs, or a report that cannot validate itself.

## Scheduler-stability correction

The first two-trial design used the 1,500-byte-per-second rate in both trials.
One isolated async trial passed, but a repeat exposed that the async owner and
the synchronous snapshot tick could legitimately race for the same scheduling
window. That low-rate async result is superseded and is not accepted evidence.

The correction separates mutually exclusive scheduler properties rather than
loosening an assertion:

- `fragment_pressure` retains the 1,500-byte-per-second rate and requires both
  rate and fragment deferral counters to advance; and
- `post_burst_async_ack` uses the distinct 1,000,000-byte-per-second zero-delay
  profile and requires async wake and ACK-handoff counters to advance.

The revised profile passes three consecutive complete runs. This resolves the
test's ownership nondeterminism; it does not claim packet-loss recovery or
broaden production authority.

## Final post-bootstrap readiness scheduler

The production readiness lifecycle deliberately separates bootstrap queue
time from protocol negotiation time:

1. `SV_New` sends `SERVERDATA` and the public legacy-only capability
   confirmation through the normal bootstrap. It does not create a private
   readiness epoch.
2. Accepted `SV_Begin` records only a pending challenge flag and its request
   time. It never calls `SV_NativeShadowBeginEpochV1()` directly, so no
   readiness nonce, epoch, or 10-second protocol deadline exists yet.
3. `SV_MaintainNativeShadowChallengePending()` validates the binding and the
   separate wrap-safe pre-start bound before rate or fragment early exits.
   Queued, in-flight, and fragmented reliable generations are expected
   deferrals rather than failures.
4. The synchronous `SV_SendClientMessages()` path services the request only
   after rate admission and fragment completion. Because a paused active client
   has no synchronous snapshot pass, `SV_SendAsyncPackets()` performs the same
   clean-boundary service while `SV_PAUSED`, again only after its rate and
   fragment gates.
5. At the first clean boundary, the server preflights the exact 117-byte SVC
   record, creates the private epoch/nonce and 10-second deadline, atomically
   copies `CHALLENGE` into the empty reliable queue, and immediately calls
   `Netchan_Transmit` with zero frame payload. This creates a dedicated
   117-byte reliable generation. The synchronous path suppresses that one
   snapshot frame; the paused asynchronous path has no snapshot to suppress.

The pending pre-start wait is independently bounded to 60 seconds. If a clean
boundary never appears, the optional pilot fails closed with typed `QUEUE`
failure while legacy traffic and simulation continue. The separate 10-second
readiness deadline begins only in the clean-boundary handoff transaction, so
bootstrap, game-start output, rate deferral, and older reliable fragments do
not consume it. Invalid official binding, capacity, clock, or readiness-core
state still fails closed for the pilot.

## Trial profiles and primary results

| Trial | Rate | Reliable pressure | Probe release | Primary elapsed | Required result |
|---|---:|---:|---:|---:|---|
| `fragment_pressure` | 1,500 B/s | 16 x 800 bytes = 12,800 bytes | 124 client frames | 42.891 s | Rate and fragment deferral |
| `post_burst_async_ack` | 1,000,000 B/s | 8 x 800 bytes = 6,400 bytes | 186 client frames | 42.953 s | Zero-delay async wake and ACK handoff |

Every numbered reliable print was present in full exactly once: 16/16 in the
fragment trial and 8/8 in the async trial. The fragment trial used UDP port
54916 and the async trial used port 54255; ports are reserved afresh per run and
are evidence metadata, not part of the contract.

### Native command and receipt counters

| Counter | `fragment_pressure` | `post_burst_async_ack` |
|---|---:|---:|
| Client challenges / CLIENT_READY / SERVER_ACTIVE | 1 / 1 / 1 | 1 / 1 / 1 |
| Client first sends / retries / handoffs | 1 / 20 / 21 | 1 / 0 / 1 |
| Client ACK carriers / acknowledged reliable | 3 / 1 | 3 / 1 |
| Client retained high-water / releases / remaining | 1 / 1 / 0 | 1 / 1 / 0 |
| Server RX carriers / commits / repeat refreshes | 21 / 1 / 20 | 1 / 1 / 0 |
| Server legacy joins / command matches | 1 / 1 | 1 / 1 |
| Server ACK prepares / handoffs | 3 / 3 | 3 / 3 |
| Server async rate / fragment deferrals | 202 / 26 | 0 / 0 |
| Server async wake attempts / ACK handoffs / no-handoff | 0 / 0 / 0 | 3 / 3 / 0 |

Both clients finish in active mode with hooks installed, readiness complete,
public/private masks `3`/`19`, one wire proof, zero drains, zero failures, and
`last_failure=0`. Both servers finish active and wire-committed with one
challenge, one CLIENT_READY, one SERVER_ACTIVE, one command match, and zero
command mismatch, sample mismatch, RX rejection, TX ACK rejection, drained RX,
drain, or failure counters. `ack_eligible=0` confirms no receipt remains due at
the terminal status point.

Across all three accepted reports, each fragment trial delivers all 12,800
reliable bytes exactly once, records positive rate and fragment deferrals,
matches and releases one native command, and finishes without pilot failure.
Each async trial delivers all 6,400 reliable bytes exactly once, records
`async_wake_attempts=3` and `async_ack_handoffs=3`, matches and releases one
native command, and finishes without pilot failure.

### Primary impairment observations

| Trial/endpoint | Packets seen | Queue high-water | Drops/reorders/duplicates/corruption/overflow |
|---|---:|---:|---:|
| Fragment client | 1,376 | 2 | 0 / 0 / 0 / 0 / 0 |
| Fragment server | 1,042 | 2 | 0 / 0 / 0 / 0 / 0 |
| Async client | 2,187 | 2 | 0 / 0 / 0 / 0 / 0 |
| Async server | 1,616 | 5 | 0 / 0 / 0 / 0 / 0 |

All four endpoints also report zero burst drops, throttle events, upstream
stalls, and impairment resets. This is expected for the declared latency-only
impairment profile.

## Artifact identity

The runner hashes every staged runtime component before the trials, verifies
that the manifest is byte-identical afterward, and records each retained log.

| Runtime role | Bytes | SHA-256 |
|---|---:|---|
| Client executable | 13,570,560 | `4ff606bf60893b5daec17e65ad12b0d867fb22d78b8463d620f510a0ff6c3396` |
| Dedicated executable | 13,570,048 | `dbacd7c4fb442cc019dc492f8e7a37ab7930d84a5cb2aa3f14fa52d9ec77e84a` |
| Client engine | 15,254,016 | `2585884dabc7538f267b26fd0eb45046f8dac407adec9428b8a049828592c2c2` |
| Dedicated engine | 1,690,624 | `20bb159394650a8b59da3a4943cfabb08c36563eca55c7eeccfe5958cc97de93` |
| Cgame | 2,181,120 | `41630ff95acefe557a79b568ec3775fb305597cbf4f34db88c8d0e6c7f481759` |
| Sgame | 9,004,032 | `0717444516f8c2bed5b260bfbffba62a83927f9897d484ee37d49a0f112863ce` |
| OpenGL renderer | 1,750,016 | `8719123d77e785123b5df4a2852b2c8fd24284544bbfd1a75e7064cd4a481b5d` |

The primary reconstructed command lines are bound by argument count and hash:

| Trial | Client argc / argv SHA-256 | Server argc / argv SHA-256 |
|---|---|---|
| `fragment_pressure` | 105 / `d985326035f03cdeeac8a51d322db31c1a4ea48a455f1e6c33ce92a054e21911` | 142 / `2311a95121104e88d188e38e8a9dccd319884ef076f14ba2cb2a46bb43ed2f5d` |
| `post_burst_async_ack` | 105 / `f6d512a453481d3d2a0bd09e9f5cc3d57722a738cb27d445db5712ed8871248b` | 126 / `ec07c608d8acddf4bcdd1d8ef2e6106c89dd0ea9d5887b305b82cbd8ca71daeb` |

Primary retained stdout hashes are:

| Log | Bytes | SHA-256 |
|---|---:|---|
| Fragment client | 19,390 | `76d6c788cee0199c0237fb9087d17b26d63cc50bae6f6828383e8e3fe22fce41` |
| Fragment server | 16,924 | `451baa48643995977b275b5ff852df6a0798c49279f052bef45a82b3c37b0bd1` |
| Async client | 12,987 | `449041db83b17fc292a927aabae67abc94205fe68a38e2103de3d3d2442c3777` |
| Async server | 10,582 | `8982c2de13b1f30a0e45479b89e8702e15ecc9257759704809c73d6adb137325` |

All four primary stderr logs are empty and hash to
`e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`.
The harness terminates both processes after their validated terminal markers;
that bounded cleanup is recorded as harness termination rather than a crash.

## Scope and remaining gates

This result does not complete any parent task. Its explicit limits are:

- exactly one observational native command per fresh connection;
- no mixed DATA-plus-ACK packet and no repeated native command stream;
- separate fresh connections and distinct rate profiles for fragment-owner and
  asynchronous-wake evidence;
- deterministic latency but no targeted readiness-packet loss, reordering, or
  duplication;
- localhost UDP rather than WAN congestion, NAT traversal, or multi-client
  fairness;
- no native command authority, capability advertisement, or default promotion;
  and
- no soak or supported-platform stability claim.

The next transport work is transactional mixed DATA/ACK packing and repeated
stop-and-wait command shadowing, followed by directional loss/reorder/
duplication, adaptive input-age and bandwidth evidence, multi-client load,
soak, demo/spectator, rollback, and supported-platform matrices. Tasks
`FR-10-T04`, `FR-10-T09`, `FR-10-T14`, and `FR-10-T16` remain Incomplete.
