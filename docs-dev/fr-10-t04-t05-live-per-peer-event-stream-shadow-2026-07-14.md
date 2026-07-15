# Live Per-Peer Native Event-Stream Shadow

Date: 2026-07-14

Project tasks: `FR-10-T04`, `FR-10-T05`, `FR-10-T06`, `FR-10-T07`,
`FR-10-T14`, `FR-10-T16`

## Outcome

WORR now has a default-off, per-peer native event-stream shadow that carries
canonical events from the server's exact final legacy snapshot emission into
the engine-owned cgame event runtime. The lane is reliable, sequenced, bounded,
and full duplex: server-originated descriptor/event DATA can share a WTC1
carrier with client-command ACKs, while client-command DATA can share a carrier
with descriptor/event ACKs. Current and immediately retired transport banks
preserve late receipt liveness across a map transition without allowing old
semantic DATA to reactivate authority.

This is an observation and authority-proof milestone, not a gameplay or
presentation cutover. Legacy snapshots, commands, demos, and effect/audio
presenters remain authoritative. The public capability tuple remains exactly
`0x03`, the two new event controls default to `0`, and event mode is reachable
only through the existing default-off native shadow. No file under `q2proto/`
was changed.

The work advances the listed tasks as follows:

- `FR-10-T04`: adds the first live server-to-client canonical DATA lane and
  production mixed-carrier use in both directions;
- `FR-10-T05`: assigns per-peer event IDs only after descriptor authority is
  acknowledged and admits them through the typed event runtime;
- `FR-10-T06`: derives event candidates from the exact retained final-emission
  snapshot rather than a pre-visibility or global event source;
- `FR-10-T07`: feeds the engine-owned cgame authority shadow with immutable
  canonical records while leaving presentation unchanged;
- `FR-10-T14`: adds bounded storage, failure counters/status, exact packet
  limits, lifecycle tests, and hostile-ACK/sequence coverage; and
- `FR-10-T16`: connects DATA/ACK multiplexing and idle-send wakeups to the live
  client and server schedulers. Adaptive policy remains future work.

All six tasks remain In Progress because the native snapshot adapter,
presentation/prediction cutover, full event-family producers, broad impairment
matrix, load/soak evidence, and release promotion are not complete.

## Opt-in and compatibility contract

The existing command-only pilot is preserved byte-for-byte at its protocol
boundary:

| Mode | Required controls | Private readiness mask | Readiness exchange |
|---|---|---:|---|
| Command shadow | `cl_worr_native_shadow=1`, `sv_worr_native_shadow=1` | `0x13` | `CHALLENGE -> CLIENT_READY -> SERVER_ACTIVE` |
| Event shadow | Base shadow plus `cl_worr_native_event_shadow=1` and `sv_worr_native_event_shadow=1` | `0x33` | `CHALLENGE -> CLIENT_READY -> SERVER_ACTIVE -> CLIENT_ACTIVE_CONFIRM` |

Both modes continue to publish and confirm only
`WORR_NET_CAP_LEGACY_STAGE_MASK == 0x03`. The private `0x13` mode adds the
native-envelope bit only; the separate `0x33` mode also adds the reserved
native-event-stream bit. An exact private-mask mismatch fails the pilot closed
instead of reinterpreting traffic. Disabling either base shadow avoids native
event initialization entirely, and enabling the event control without the base
control has no effect.

The event-mode confirmation is not cosmetic. After the server queues
`SERVER_ACTIVE`, the client can establish its event RX state and enqueue the
fourth record. During the server's `WAIT_CLIENT_ACTIVE_CONFIRM` phase:

- native RX is open through a receive-only session binding;
- native TX remains closed;
- the server can retain descriptor/event work but cannot emit it; and
- a client command DATA carrier may be processed before the legacy setting
  parser observes the confirmation in the same packet.

That ordering matches the actual netchan boundary, where admitted application
RX precedes legacy service parsing. Only the exact active-confirm record opens
server native TX. The original active-only binding API remains unchanged; the
new receive binding grants no transmit authority.

## End-to-end architecture

### 1. Final per-peer snapshot projection

Candidate extraction runs only after the real per-peer frame header and entity
deltas have been successfully encoded. The snapshot shadow projects those
exact final deltas, including visibility, entity-generation, protocol-limit,
and transport-truncation effects. It therefore never starts from the global
sgame event shadow and cannot expose an event that was filtered from that
client's emitted snapshot.

For each retained snapshot slot, the shadow stores one compact 16-byte source
row per emitted legacy entity event:

- source ordinal in the final canonical entity range;
- source entity index and generation; and
- raw legacy event value.

The retained canonical snapshot already owns source tick/time and semantic
event references, so keeping a second 168-byte event record per source is
unnecessary. `SV_SnapshotShadowCopyEventCandidatesV1` resolves the exact
slot/generation reference, rebuilds each typed ID-less candidate, and compares
its semantic hash with the corresponding final projection reference before it
publishes any output. A stale slot, capacity shortfall, invalid projection, or
semantic mismatch is transactional.

One intentional compatibility boundary remains: candidate `source_tick`
continues to use the peer's wire-frame number because the current q2proto
projection and client parity hashes use that value. The retained snapshot
separately carries the authoritative server tick. Changing event source ticks
requires a shared canonical projection contract and parity migration; this
slice does not silently substitute the authoritative tick and invalidate the
existing semantic hashes.

### 2. Snapshot-to-sender queue

After `SV_SnapshotShadowCommitFrameV1` succeeds, the server passes the exact
snapshot shadow reference to the native peer. Event mode copies at most 512
candidates into peer-owned scratch, preflights the sender backlog, and appends
the whole batch transactionally. The legacy frame has already been finalized
at this point. Candidate copy or queue failure can drain the native event lane,
but it cannot cancel, rewrite, or delay the authoritative snapshot.

The queue accepts only candidate-form records: they have canonical typed
payloads and semantic identity but no authority ID. This is essential because
visibility filtering is per peer; allocating global event sequence numbers
before filtering would either leak hidden events or create unexplained gaps.

### 3. Descriptor-first sender

Each new event transport bank receives a separately allocated semantic stream
epoch and a descriptor with first sequence `1`. Initialization retains the
descriptor immediately. The sender may buffer ID-less candidates while the
fourth readiness confirmation and descriptor receipt are outstanding, but it
cannot promote those candidates into authoritative events until the exact
descriptor transport ACK is applied.

After that ACK, `Worr_NativeEventSenderPumpV1` assigns IDs in FIFO order from
the descriptor's `{stream_epoch, first_sequence}` domain, encodes each event,
and moves as many records as the 64-slot reliable TX/payload bank permits.
Descriptor and event payload bytes remain immutable until exact acknowledged
release. Duplicate ACKs are idempotent; partial or multiple exact ranges
release only the named retained payloads. Candidates never consume a sequence
while blocked in the ID-less backlog, and `UINT32_MAX` exhaustion is a terminal
native-stream condition rather than wraparound.

Event stream epoch, transport epoch, official connection epoch, command epoch,
snapshot epoch, and wire frame are distinct domains. The server uses a
process-lifetime monotonic allocator dedicated to semantic event epochs; it
does not derive one from any of the other identities. Map rotation requires a
new descriptor and semantic epoch. Reconnect provenance is still supplied by
the independent connection owner.

### 4. Full-duplex current and retired banks

Each direction now uses the same WTC1 packet as an application carrier rather
than assigning one direction to DATA and the other to ACK-only traffic:

- server TX: one current descriptor/event DATA fragment plus zero to seven
  current command ACK ranges;
- server RX: client command DATA plus zero to seven descriptor/event ACK
  ranges;
- client TX: one current command DATA fragment plus zero to seven current
  descriptor/event ACK ranges; and
- client RX: descriptor/event DATA plus zero to seven command ACK ranges.

ACK-only packets remain available when no DATA record is due or the legacy
prefix leaves insufficient DATA capacity. The mixed transaction is prepared on
staged sender/ledger state and is completed exactly once from the netchan's
synchronous handoff result. Definite rejection aborts the dispatch for exact
retry; uncertain or invalid completion fails the native stream closed.

A map transition retains at most one prior bank. The current server event
sender is marked retired, which permanently forbids queueing or DATA emission
but still permits exact late ACK release. The client likewise never submits
retired DATA or a retired repeat to cgame. Pending ACKs that were already
authorized before retirement may still be emitted, and ACKs received for old
outbound records may still release their exact payloads. Fair current/retired
selection prevents a continuously active bank from starving the immediately
preceding epoch.

### 5. Client semantic admission and cgame shadow

Current descriptor/event DATA passes through the semantic admission core, not
the generic retained-message commit API. Fresh messages are decoded from an
exact completed WNE1 record, then the RX commit and ACK-ledger update are
prepared on private copies. Only after those mutations are known to be
possible does the adapter invoke the engine-owned cgame consumer.

Descriptor admission resets cgame authority to the exact semantic epoch and
baseline. Event admission submits the exact canonical record. In both cases,
the adapter immediately requests a fresh cgame status and requires proof of
the descriptor state or exact selective event receipt before publishing the
staged transport state and ACK authority. It never relies on a cached status.

An exact committed repeat must also have been newly observed on the wire and
must still be present in a fresh cgame status. If semantic authority is stale,
missing, or incoherent, the adapter publishes neither the repeat refresh nor
unrelated semantic receipts; it burns the attempted authority lineage,
quiesces the cgame endpoint, and retires pending semantic ACK authority.
Retired-bank DATA cannot invoke either path.

This cgame path remains a shadow. It proves typed ordering, receipt, duplicate
handling, and future present-once ownership, but the legacy parser and legacy
effect/audio presenters still produce the user's visible and audible result.
Native event arrival does not suppress the legacy event or grant native
snapshot authority.

### 6. Scheduler and receipt liveness

Application DATA retries and semantic ACKs are not netchan reliable bytes, so
ordinary reliable cadence alone cannot keep them live.

On the client, a nonmutating output-due query covers command DATA/retries and
current/retired event receipts. Event processing raises `cl.sendPacketNow`,
including sleep/max-fps modes. `CL_SendCmd` issues an empty keepalive when
native work is due but there is no new command, and it continues to do so while
paused or before active command generation. The TX hook remains the only place
that reserves and confirms a carrier transaction.

On the server, the generalized output-due query covers current event DATA,
fragment continuation/retry, and current/retired command receipts. It runs
after a normal snapshot build so current events can join that packet, and the
async send loop creates an empty native wake when an active client has no
synchronous snapshot pass. Existing rate and netchan fragment gates remain
authoritative; their deferrals are counted and retried rather than bypassed.

Both sides use a 100 ms DATA/ACK retry interval and bounded proactive receipt
handoffs. Output-due checks are nonmutating, so repeated scheduler probes cannot
consume an ACK token, age a retained record, or fabricate a send attempt.

## Exact packet limits

The live netchan application ceiling in this path is 1,024 bytes. WTC1 permits
at most eight entries, so a mixed DATA packet reserves space for one DATA entry
and up to seven 16-byte ACK entries, plus the 32-byte footer. WNE1 adds its
56-byte header to the canonical WNC1 payload.

For the current records:

| DATA record | WNC1 bytes | WNE1 bytes | Largest legacy prefix at a 1,024-byte application budget |
|---|---:|---:|---:|
| Event-stream descriptor | 56 | 112 | 760 fits; 761 fails |
| Final-emission legacy entity event | 120 | 176 | 696 fits; 697 fails |

These are legacy-prefix boundaries, not output-buffer capacities. With a
760-byte descriptor prefix or a 696-byte event prefix, the complete mixed
output is 1,024 bytes. An output buffer of only 761 or 697 bytes respectively
must therefore fail transactionally even though the legacy prefix itself fits
in that smaller buffer. The tests cover both distinctions. Per-record frozen
fragment sizing uses the smaller of the configured WNE1 ceiling and the exact
payload plus WNE1 header, avoiding a pessimistic maximum-size reservation for
the short descriptor and legacy-entity record.

## Bounded storage and allocation

The sender is pointer-free and has hard compile-time limits:

- 64 retained TX records and 64 generation-tagged encoded payload slots;
- 512 ID-less backlog candidates;
- 192 encoded bytes per event payload;
- one prepared mixed transaction at a time; and
- no allocation in queue, promotion, send, retry, or ACK release.

`worr_native_event_sender_v1` is 104,832 bytes on the validated Windows x64
ABI. Its 512 full candidate records account for 86,016 bytes. The server's
event-only heap state owns current and retired senders plus one 512-record copy
scratch, for 295,712 bytes per opted-in peer. Command-only peers retain a null
event-state pointer and do not pay that heap cost. Event state is allocated
once at peer initialization and freed through the idempotent detach/destroy
lifecycle.

The snapshot shadow stores compact candidate sources independently of native
event opt-in so an exact retained projection can be queried later. With the
production 16 snapshot slots, the extended 512-entity profile adds exactly
139,328 bytes per peer: 131,072 bytes of retained 16-byte source rows, 8,192
bytes of source scratch, and 64 bytes of per-slot counts. The classic
128-entity profile adds 34,880 bytes. These buffers are created with the
snapshot peer and never grow.

The client owns fixed current/retired 16-slot RX sessions, fixed 3,072-byte
payload arenas, ACK ledgers, and a process-local event owner. It performs no
steady-state allocation per received fragment or event. Every capacity failure
has an explicit result and leaves caller-visible output/state unchanged unless
the native lane deliberately transitions to drain.

## Failure isolation

The live event shadow observes the following failure policy:

- before any native wire commitment, initialization/readiness failure removes
  hooks and returns to pure legacy behavior;
- after native traffic may legitimately exist, failure enters drain so the
  adapter can continue stripping recognized trailers and releasing already
  authorized receipts without admitting new authority;
- descriptor uncertainty, cgame callback/status failure, semantic repeat
  uncertainty, epoch conflict, and sequence exhaustion retire semantic ACK
  authority rather than acknowledging data the consumer cannot prove;
- snapshot candidate mismatch, 512-record backlog overflow, TX/payload
  saturation, or event queue failure affects only the opted-in native event
  stream because the legacy snapshot was already committed;
- output-capacity and definite handoff rejection retain exact immutable DATA
  for retry and do not consume ACKs; and
- malformed native candidates fail closed at the application seam instead of
  being reinterpreted as legacy bytes.

All counters are bounded/saturating. Server event status reports mode, current
and retired initialization, TX-open state, stream epoch, descriptor receipt,
backlog/retention, output due, queue/promote/ACK totals, and packet first-send,
retry, confirm, and reject totals. Existing command/readiness status remains
available in both modes.

## Implementation inventory

- `inc/common/net/native_readiness.h`,
  `src/common/net/native_readiness.c`, and the readiness sideband add the
  optional client-active confirmation while retaining the existing 13-pair
  carrier layout.
- `inc/common/net/native_session.h` and
  `src/common/net/native_session.c` add the receive-only binding for the
  server's confirmation wait.
- `inc/common/net/native_event_sender.h` and
  `src/common/net/native_event_sender.c` own descriptor-first ID assignment,
  reliable event retention, mixed carrier transactions, and retired release.
- `inc/common/net/legacy_entity_event_candidate.h` and its common-net
  implementation provide the one canonical legacy-entity candidate mapper
  shared by projection and copy-out.
- `inc/server/snapshot_event_candidates.h`, its private final-emission adapter,
  and `src/server/snapshot_shadow.c` retain and reconstruct exact per-peer
  candidates.
- `inc/server/native_shadow.h` and `src/server/native_shadow.c` own the server
  readiness, current/retired transport/event banks, queue, scheduler, hooks,
  and scalar diagnostics.
- `src/server/entities.c`, `src/server/send.c`, `src/server/main.c`, and
  `src/server/user.c` connect successful snapshot commits, synchronous/async
  output service, per-connection mode selection, the default-off server
  control, and response-free active-confirm admission.
- `inc/client/native_readiness_pilot.h` and
  `src/client/native_readiness_pilot.cpp` own the client mode selection,
  current/retired command/event banks, semantic admission, and mixed hooks.
- `src/client/input.cpp` and `src/client/main.cpp` connect idle keepalives and
  sleep-mode wakeups to the nonmutating native-output query.
- Existing `src/client/cgame_event_runtime.cpp` and the external cgame runtime
  remain the only native semantic consumer boundary; no direct DLL callback
  table is exposed to transport code.

## Validation

The converging implementation has the following focused evidence before the
final integrated pass:

- readiness state/layout/sideband selection, including the optional fourth
  confirmation and unchanged `0x13` path: 5/5;
- native receive-binding session coverage: 1/1;
- final-emission candidate builder/copy/parity coverage: 3/3;
- native event sender coverage: 1/1 test target, including descriptor-before-
  event gating, ACK transactionality, duplicate/partial/multi-range release,
  `UINT32_MAX` exhaustion, retired behavior, and exact 760/761 and 696/697
  boundaries;
- the final-emission parity corpus remains deterministic at 100,000 cases with
  digest `7b185107eeb0f6e7`; and
- the complete networking suite passed 119/119 after the candidate extractor
  landed, before the final sender and live-adapter integration pass.

Final integrated evidence:

- focused readiness/session/candidate/sender/server/client/admission selection:
  10/10;
- complete networking suite: 120/120;
- three complete networking repetitions: 360/360;
- current Windows Clang client engine, dedicated engine, cgame, and sgame
  targets: built successfully;
- the all-target build reached the final renderer compilation but is blocked by
  an unrelated pre-existing Vulkan edit in `src/rend_vk/vk_world.c:1399`
  (`bytes` is undeclared); this round did not modify or mask that separate
  dirty renderer work;
- no two-process runtime/impairment launch was run in this round; the focused
  client/server hook tests execute headlessly and the previous parser/runtime
  evidence remains valid but narrower than a live event-stream process gate;
- refreshed `windows-x86_64` `.install`: 16 root runtime files, one dependency,
  330 packaged assets, 31 botfiles, and 215 RmlUi assets;
- build/stage SHA-256 equality: client engine
  `80B35D1DD5A9BC178F280FE25C779CD97FE2033517CFD8B581FBEF58845C3A94`,
  dedicated engine
  `8A68871788F4BF22D49E3738AE332B333D5288F2F1EE983FA04C073A0914859C`,
  cgame
  `04776955C1C7F43DE2ADC8572C18EC36B8AFCF19D658B9B141274048DE79874F`,
  and sgame
  `8EB3788E129B360DE040FA5DF02E1731F91BD733475BC10AF6BA5D1BFE2A3673`;
  and
- `git diff --check` is clean apart from line-ending notices, and the
  `q2proto/` working tree remains untouched.

Automated launches for this and subsequent gates must remain headless, using
the dedicated binary or an explicit no-window mode with isolated runtime data.

## Remaining Definition-of-Done work

This milestone does not complete any of its six project tasks. The critical
remaining work is:

- extend final per-peer production beyond legacy entity events to temporary
  entities, muzzle flashes, spatial audio, direct sgame multi-events, and
  predicted cgame local actions without visibility leaks or unexplained gaps;
- define and migrate the shared authoritative event-tick contract instead of
  retaining the current wire-frame tick compatibility boundary;
- implement the native acknowledged-baseline snapshot DATA adapter and prove
  that legacy and native transports feed the same canonical snapshot/event
  validators and cgame consumers;
- connect full local prediction, predicted-event reconciliation, and the real
  cgame effect/audio presenters, then prove present-once parity before changing
  legacy presentation authority;
- add explicit gameplay-critical versus cosmetic pressure policy rather than
  treating a full 512-record backlog as a native-stream failure;
- complete loss/reorder/duplication and one-way ACK-loss tests across map
  rotation, disconnect/reconnect, descriptor retry, multi-fragment DATA, sleep,
  pause, rate limiting, and netchan fragment pressure;
- measure bandwidth, CPU, and the 295,712-byte (approximately 289 KiB)
  event-state plus snapshot-source memory cost at the required 1/8/16/32-
  client load and soak gates;
- add demo, spectator/MVD, cross-platform, malformed-input, fuzz, security,
  long-session epoch/sequence exhaustion, and release rollback evidence;
- replace fixed retry/wake behavior with the `FR-10-T16` adaptive native
  pacing, batching, selective redundancy, and server-feedback policy; and
- keep the event capability unadvertised and both event controls default-off
  until the dual-adapter acceptance matrix and every applicable `FR-10-T14`
  floor pass without waiver.
