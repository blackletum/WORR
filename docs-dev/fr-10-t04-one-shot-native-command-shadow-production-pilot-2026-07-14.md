# FR-10-T04 One-Shot Native Command-Shadow Production Pilot

Date: 2026-07-14

Tasks: `FR-10-T04`, `FR-10-T09`, `FR-10-T14`, `FR-10-T16`

Status: implemented and validated as a default-off, unadvertised production
observation slice. It carries at most one canonical client command per private
transport epoch and returns an exact ACK-only receipt. Legacy
`MOVE`/`BATCH_MOVE` remains the sole simulation authority. This slice does not
complete any parent FR-10 task.

## Outcome and authority boundary

The earlier readiness-only pilot now has the smallest useful native DATA/ACK
transaction connected to the production NEW-netchan boundaries:

1. The client continues to write its authoritative legacy `MOVE` or
   `BATCH_MOVE` command stream.
2. After that exact legacy range is committed, the client retains the newest
   command in the written range as one immutable 110-byte `WNC1` command
   record.
3. The post-assembly TX hook may append one `WTC1(DATA(WNE1(WNC1)))` trailer.
4. The server's post-admission RX hook validates, retains, commits, and joins
   the native record with the authoritative legacy command, then exposes only
   the unchanged legacy prefix to the ordinary parser.
5. The server returns one exact-range `WTC1` ACK entry. The client applies that
   receipt transactionally and releases the retained payload.

The native record is diagnostic evidence only. It cannot insert a command into
the authoritative stream, advance the consumed-command cursor, drive
simulation, replace legacy failure behavior, or claim full-record parity. The
join reports only the semantics honestly shared by the legacy and canonical
records, including command fields and sample-time offset.

Both controls remain default-off:

- `cl_worr_native_shadow 0`
- `sv_worr_native_shadow 0`

The public offer, support, and negotiated mask remains exactly `0x03`. The
native-envelope bit is added only to the private readiness proof, whose exact
mask remains `0x13`. No capability advertisement or `q2proto/` change is part
of this work.

## Production ownership and data flow

### Client command selection

The client initializes its command-shadow builder when the existing public
capability tuple is confirmed. The builder uses the official connection epoch
and observes every subsequently finalized command in canonical sequence order.
`NetUsercmd_ToPredictionCommandV1()` is the shared, alias-safe conversion from
the production `usercmd_t` representation to the deterministic prediction
command; client and server no longer maintain separate field-copy logic for
this path.

`CL_NativeReadinessPilotObserveEncodedCommandRange()` is called only after the
legacy identity sideband and `MOVE`/`BATCH_MOVE` bytes have been encoded
successfully. It selects the newest command in that exact written range, not a
newer command merely present in the client ring. One TX slot, one immutable
payload handle, and priority zero are used. The only admitted native message
sequence is `1`, and `proof_enqueued_once` prevents a second DATA selection in
the same private transport epoch.

The post-assembly hook sees the final reliable-plus-unreliable application
prefix and the real direction-specific ceiling. A budget miss is a `BYPASS`:
the authoritative legacy packet is transmitted byte-for-byte and the retained
native record remains eligible for a later packet. Transport accounting is
committed only from the exact synchronous completion token and application
bytes reported by netchan.

### Server admission and observational join

Immediately before `Netchan_ProcessEx()` can invoke the receive hook, the
server stamps the pilot's checked extended clock. Native retention, receipt
creation, and the legacy-stream reconciliation that follows therefore use the
same admitted packet time, including after a long idle interval.

The server accepts only one carrier entry with this exact shape:

- direction: client to server;
- entry: one `DATA_V1`;
- envelope: one complete first-and-last `WNE1` fragment;
- record class/schema: command V1;
- payload: exactly one 110-byte `WNC1` command;
- transport epoch: exactly the selected current or immediately retired bank;
- command epoch: exactly the official connection epoch stored with that
  transport bank.

First arrival is retained in one RX slot and committed to the exact-receipt
ledger before the legacy prefix is exposed. The bounded join handles both
arrival orders. A native retry can reconcile against an authoritative legacy
record already present in the command stream before the packet parser runs;
the ordinary command-consume path supplies the legacy half when it arrives
after native DATA.

Native/legacy command or sample-offset disagreement is counted and enters
`DRAIN`; it never changes the legacy command selected for simulation. A join
that expires after 500 ms also enters `DRAIN` without revoking already-created
receipt authority.

### Reverse ACK liveness

The server's ACK ledger uses a 100 ms retry interval and three proactive
handoffs. An exact committed duplicate DATA refreshes receipt delivery after
all proactive copies were lost. `Worr_NativeCarrierAckPeekDueV1()` provides a
non-mutating due check so normal frame sends and the asynchronous send loop can
decide whether a packet is useful without spending a handoff.

`SV_SendAsyncPackets()` evaluates the ACK wake only after the client rate gate
and pending-fragment handling. It runs before the normal active-client skip,
so an otherwise idle spawned client can send an ACK-only application packet
without violating fragmentation order or configured rate. When both current
and retired banks have due receipts, the adapter alternates them rather than
starving either bank. A completion token is bound to the selected bank and the
exact packet bytes before handoff accounting advances.

The client accepts exactly one ACK entry, routes it to exactly one current or
retired epoch, applies it to staged TX/slot/registry copies, and publishes the
new state only after all three structures validate and the immutable payload
handle is released correctly.

## Exact wire and packet budgets

| Direction and record | Native bytes | 1,024-byte application boundary |
|---|---:|---:|
| Client `WNC1` command payload | 110 | n/a |
| Client complete `WNE1` datagram | 166 | n/a |
| Client WTC entry plus datagram plus footer | 206 | Legacy prefix 818 fits exactly; 819 returns `BYPASS` and retries later |
| Server one-range ACK entry plus footer | 48 | Legacy prefix 976 fits exactly; 977 returns `BYPASS` and retries later |

The carrier's absolute ceiling remains 1,200 bytes, but neither production
adapter assumes that ceiling is available. Each clamps to the actual netchan
application limit. A carrier is appended atomically after the unchanged legacy
prefix; partial DATA or partial ACK emission is impossible.

## Epoch, map, and failure lifecycle

Each endpoint keeps at most two transport banks:

- `current`: the newly active private transport epoch;
- `retired`: the immediately preceding epoch, retained only for exact
  in-flight receipt completion while the connection is in or can service
  `DRAIN`.

A later map activation replaces the older retired bank. Each bank retains its
own official command epoch, native transport binding, session state, payload or
RX storage, receipt state, and join state. This prevents a delayed old-epoch
ACK or DATA from being interpreted against the current map's command epoch.

There are two irreversible wire commitments:

- On the client, atomically queuing the 65-byte `CLIENT_READY` record means the
  server may later activate the private epoch. Subsequent local pilot failure
  retains the hooks in `DRAIN` rather than detaching them. If an invalid
  same-epoch `SERVER_ACTIVE` arrives after that commitment, a later valid ACTIVE
  cannot resurrect the epoch or allocate DATA state. Only a successfully
  validated fresh map challenge after explicit quiesce may return the client to
  arming.
- On the server, transferring the 117-byte `SERVER_ACTIVE` record to the NEW
  reliable queue sets a monotonic wire-committed latch before any later
  fallible transport initialization. If local initialization then fails, an
  exact structurally valid carrier for that committed epoch has its WTC trailer
  stripped and its authoritative legacy prefix exposed, but native DATA is not
  admitted or acknowledged.

Before either point of no return, a private initialization failure may detach
the pilot and leave the connection entirely legacy. After commitment or any
observed WTC marker, hooks remain installed until connection teardown. This
prevents native trailer bytes that a peer may legitimately send from reaching
the legacy parser after a unilateral local fallback.

The receive policy is deliberately narrow:

| Input | Result |
|---|---|
| No WTC marker | `BYPASS`; ordinary legacy parsing is byte-identical |
| First exact current-epoch DATA | Commit observational record and expose legacy prefix |
| Exact committed duplicate in current or retired bank | Report already committed, rearm ACK, expose legacy prefix |
| Second distinct current-epoch DATA | Count one-shot limit, strip trailer, expose legacy prefix, enter `DRAIN` |
| First-time DATA for a retired/draining bank | Strip trailer and expose legacy prefix; do not grant new native authority |
| Malformed carrier/envelope/codec, wrong direction or shape, unknown/ambiguous epoch, or mismatched record reference | Reject the admitted application packet and enter fail-closed `DRAIN` |

`DRAIN` stops selection or admission of new DATA but preserves the minimum
liveness necessary for exact already-committed duplicates and authorized ACKs.
Final disconnect, replacement, or teardown explicitly removes both hooks and
destroys the private owner.

## Diagnostics and bounded state

The server peer publishes saturating counters for readiness records and
duplicates, TX/RX bypass, carriers, commits, repeat refreshes, rejections,
drained records, one-shot limits, legacy joins, matches, command/sample
mismatches, join expiry, and ACK prepare/handoff/rejection. It also records the
last typed failure, lifecycle, current/retired transport state, and exact
wire-commit epoch.

The client and server hot paths use fixed caller-owned storage. Per active bank
the pilot uses one command payload, one TX or RX slot, one retained reference,
and bounded receipt/join state. It performs no steady-state heap allocation in
command selection, carrier build/decode, receipt processing, or comparison.

## Validation evidence

Current Windows Clang validation for this integration slice includes:

- 14/14 focused production-pilot, readiness, session, carrier, ACK,
  command-shadow, usercmd-wire-parity, and netchan-hook tests;
- all 104 registered networking tests passing in one complete run;
- three consecutive complete networking runs, 312/312 total;
- production-profile strict-warning x86-64 and i686 client adapter/test syntax
  checks, including a fresh post-fix client rerun;
- ASan/UBSan endpoint pilot runs, including a fresh post-fix client binary with
  `/STACK:8388608`;
- clean Clang static-analysis passes for both production adapters, with fresh
  post-fix client adapter/test plists containing zero diagnostics; and
- successful post-lifecycle-fix production builds of the client and dedicated
  engines, cgame, sgame, both launchers, and updater;
- a final refreshed and validated Windows x86-64 `.install/` containing 16 root
  runtime files, one dependency, the 308-asset package, 31 botfiles, and 214
  RmlUi assets;
  and
- final staged runtime smoke acceptance of 388/388 default and 386/386 impaired
  comparisons, publications, and consumer accepts with zero snapshot mismatch,
  entity mismatch, frame failure, or consumer rejection. The impaired run
  exercises 7 drops, 7 duplicates, 6 reorders, and 1 throttle event with
  ordered/reuse queue checks passing.

The focused harness covers default-off behavior, reliable-queue commitment,
exact command-range selection, the 818/819 and 976/977 packet boundaries,
current/retired ACK routing and fairness, map replacement of the older retired
bank, both native/legacy arrival orders, duplicate/retry/rearm behavior,
one-shot drain, direction/shape/epoch/reference rejection, post-queue local
initialization failure, clock admission ordering, and hook ownership through
the wire-commit boundary. The final regression additionally proves that an
invalid post-`CLIENT_READY` same-epoch ACTIVE stays drained and cannot allocate
a session, retained payload, or DATA. Focused, full, repeated networking,
production build, strict/sanitizer/analyzer, stage, and runtime-smoke validation
all pass after that fix. The final repository check confirms `q2proto/` remains
unchanged.

Final built/staged SHA-256 identities are:

| Artifact | SHA-256 |
|---|---|
| `worr_engine` | `1BC5AB98CF9F2011F31D0FDBAB1B621B2FBD015AE0BB111D708A2732E4D1BC14` |
| `worr_ded_engine` | `41782A64E603509357B40DDE05DAFAB7F173AD7F244F373AAEFEAFF7CF7D5645` |
| `worr.exe` | `F96F194E305FB8260754ADCD5837F1909A8A4AC962F0E906ADA6EDE44374228B` |
| `worr_ded.exe` | `8F382092ECF9E038C3594E65FE506AC158FB1F85AF21D7808CFF4983A9FED9D9` |
| `cgame` | `7769847E3546F947EF9E20848A27C5D85D473768C9FE81F72661904B90D14190` |
| `sgame` | `88E4B7F67A47325EEAD86BD6993070DB5B21CCD46BB7FE54EB32C46A86393E50` |

## Remaining project work

This is one observational command, not a complete native command adapter and
not completion of `FR-10-T16`. The following remain open:

- repeated/batched native command delivery and accepted native command
  authority;
- mixed DATA-plus-ACK carrier piggyback and its packet-budget policy;
- native event and snapshot adapters over the same canonical models;
- complete real-netchan reliable-transfer and asynchronous-ACK-wake end-to-end
  tests under impairment;
- dual-adapter input-age, loss-recovery, bandwidth, correction, flood, load,
  soak, demo/spectator, rollback, and supported-platform evidence;
- capability advertisement and any rollout/default promotion.

The next safe transport step is to prove the real reliable and asynchronous
receipt paths under deterministic impairment, then add transactional mixed
DATA/ACK packing without relaxing the one-authority rule. Parent tasks
`FR-10-T04`, `FR-10-T09`, `FR-10-T14`, and `FR-10-T16` all remain Incomplete.
