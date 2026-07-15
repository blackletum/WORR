# Directional Event-Shadow Production-Wrapper Virtual Link

Date: 2026-07-14

Project tasks: `FR-10-T04`, `FR-10-T05`, `FR-10-T07`, `FR-10-T14`,
`FR-10-T16`

## Outcome

The default-off native event shadow now has a deterministic, bidirectional
virtual-link gate over its real client and server application-carrier hooks.
The gate proves that the production wrappers converge through selected DATA
loss, ACK loss, reordering, and duplicate delivery while preserving one
semantic presentation. It also proves that recognized corruption in either
direction and DATA from a retired epoch fail closed.

This is an executable integration-evidence milestone, not an authority
cutover. Legacy snapshots, commands, demos, and effect/audio presentation
remain authoritative. The test does not change protocol negotiation or
production defaults, and no file under `q2proto/` was changed.

The work advances the listed tasks as follows:

- `FR-10-T04`: exercises the negotiated WTC1/WNE1 carrier through the live
  client and server wrapper boundary under directional faults;
- `FR-10-T05`: proves descriptor-before-event delivery, reliable event retry,
  exact receipt release, and duplicate-idempotent semantic admission;
- `FR-10-T07`: verifies that repeated native event delivery does not submit or
  present the same canonical event twice in the cgame authority shadow;
- `FR-10-T14`: adds deterministic malformed, lifecycle, and impairment
  evidence to the headless networking suite; and
- `FR-10-T16`: exercises mixed full-duplex DATA/ACK packets, retry wakeups,
  and recovery after directional loss.

No task is marked complete by this slice. Broader event production, native
snapshot authority, full prediction/presenter integration, modeled impairment,
load/soak, multi-process, and cross-platform gates remain open.

## Architecture and test boundary

`tools/networking/native_event_virtual_link_test.cpp` links the production
client readiness pilot, cgame event-runtime bridge, server native shadow,
snapshot shadow, native carrier/event sender, and their supporting common-net
implementations. It allows those components to register the same application
TX prepare, TX completion, and RX callbacks used by `netchan` in the engine.
Packets are prepared and completed through those registered callbacks rather
than by invoking a private sender or receiver test seam.

The virtual link deliberately reports local TX completion as
`NETCHAN_APP_TX_COMPLETION_ACCEPTED` before applying a scheduled link fault.
This reproduces the important ownership boundary: an accepted local handoff
does not prove remote delivery. Retries must therefore be driven by the real
retained DATA and receipt state, not by pretending that the local send was
rejected.

The link scheduler then explicitly chooses whether, when, and how often to
pass each accepted application payload to the opposite production RX hook.
Client and server readiness records are exchanged through the real readiness
sideband APIs. The server queues a real canonical event candidate, and the
client queues a real canonical command record. A small fake cgame export is
used only at the engine-owned semantic consumer boundary so reset, submit,
selective receipt, and presentation counts can be asserted deterministically.

This remains an in-process headless integration test. It does not open a
socket, start a client window, or launch a second process. It validates the
production application-hook/wrapper state machines and their packet bytes; it
does not claim operating-system socket, netchan datagram framing, process
scheduling, or live renderer coverage.

## Deterministic adversarial schedule

The converging scenario activates the private event-shadow epoch, queues one
server event candidate and one client command, and applies the following exact
schedule:

1. The initial server-to-client descriptor DATA is accepted locally and then
   lost. The retained descriptor becomes due at the production retry interval
   and its retry reaches the client.
2. The client's mixed command DATA plus descriptor ACK packet is accepted and
   lost. Both directions remain retained. The next client retry reaches the
   server, releases the descriptor gate, admits the command, and allows the
   server to promote the queued event.
3. The previously delivered descriptor retry is delivered again after the
   newer reverse-direction carrier. This deliberately reordered duplicate is
   treated as a semantic repeat: it refreshes only the same receipt and does
   not reset cgame authority.
4. The server emits a genuinely full-duplex packet containing event DATA and
   the exact command ACK. The client admits the event once and releases its
   retained command.
5. The first client event ACK is accepted locally and lost. The server retries
   the event DATA. That retry is delivered twice, proving transport duplicate
   handling and fresh semantic ACK revalidation without another cgame submit
   or presentation.
6. The final refreshed ACK reaches the server and releases the last retained
   event record.

Separate directional corruption cases flip a CRC-covered byte while leaving
the WTC1 terminal discriminator recognizable. Corrupted server-to-client
descriptor DATA is rejected before cgame authority is created. Corrupted
client-to-server descriptor ACK is rejected and drains the opted-in native
server peer instead of being reinterpreted as legacy traffic.

The lifecycle scenario admits an old descriptor receipt, rotates to a new
transport epoch, and verifies that one already-authorized ACK can be emitted
from the immediately retired client bank to release the matching old server
record. A severely delayed copy of the old descriptor DATA is then rejected at
the one-retired-bank fence and cannot republish old cgame authority.

The stable summary emitted by the harness is:

```text
converged=1 s2c_loss=1 c2s_loss=2 ack_loss=1 reordered=2 duplicates=3 corrupt_s2c=1 corrupt_c2s=1 retired_ack=1 retired_data_reject=1 presented=1 digest=f71ed4ca89c5a1b0
```

The digest is computed over the complete scalar metric record. It makes an
accidental change to this deterministic schedule or its observed outcomes
visible in normal test output.

## Correctness contracts exercised

### Convergence after accepted-but-undelivered packets

Local handoff acceptance consumes the prepared transaction exactly once, but
does not release reliable application DATA. A descriptor, command, or event is
released only by its exact remote receipt. Consequently, the selected losses
do not fabricate success and the real resend/output-due paths eventually
converge when the link resumes delivery.

### Exactly-once semantic admission

The descriptor repeat cannot reset the active semantic epoch, and repeated
event DATA cannot produce another cgame submit. After three duplicate
deliveries across the scenario, the fake cgame consumer still reports one
event authority/presentation and one submit. The test therefore proves the
native shadow's present-once admission contract at the cgame consumer seam. It
does not yet prove parity through the real effect and audio presenters, which
remain legacy-authoritative.

### Full-duplex receipt isolation

The test observes both mixed carrier shapes used in production: client command
DATA with a server-event descriptor ACK, and server event DATA with the client
command ACK. Loss of either mixed packet retains each component under its own
identity. A repeat refreshes only the exact semantic receipt that was observed
again; it does not restore unrelated ACK authority.

### Fail-closed corruption and lifecycle boundaries

A carrier whose marker remains recognizable but whose CRC-covered contents
are damaged returns an RX rejection in both directions. No damaged descriptor
reaches cgame, and no damaged ACK releases server state. Likewise, an old DATA
packet cannot cross the retired-epoch semantic admission fence even though a
previously authorized retired ACK can still release its exact old sender
record.

## Open P1: finite ACK credit can strand retired data

The new gate validates one delayed retired ACK while that receipt still has a
proactive handoff available. During review of the surrounding lifecycle, a
distinct P1 liveness gap was found. This round does **not** fix or close it.

Each receipt currently has three finite proactive ACK handoffs. Current-bank
DATA repeats can rearm the matching ACK authority, but the following sequence
can exhaust that recovery path:

1. all three ACK handoffs are accepted locally and lost before reaching the
   DATA sender;
2. a map transition rotates the still-retained DATA and exhausted receipt into
   the one-retired-bank state; and
3. retired semantic DATA is correctly barred from retry/admission, so it
   cannot trigger a fresh receipt, while the exhausted retired ACK has nothing
   left to send.

The old retained DATA can then remain stranded indefinitely. Two adjacent
lifecycle properties raise the severity:

- `CL_NativeReadinessPilotQuiesceMap` enters map quiesce/drain, but the current
  client output-due and output preparation gates require an active,
  non-quiesced pilot. Already-authorized ACK work therefore cannot drain
  during this part of the map handshake; and
- only one retired bank is retained. A second rotation can overwrite an
  unresolved retired bank, losing the remaining release lineage rather than
  resolving it explicitly.

This is a liveness/retention defect, not evidence that retired DATA should be
readmitted semantically. Any correction must preserve the proven rule that old
DATA cannot reset or submit into the new cgame authority. The next lifecycle
slice must add an exhaustion regression and define a bounded release-only
retry, explicit cancellation, or equivalent protocol-safe resolution before
the current/retired design can be considered complete.

## Validation

All automated test launches were headless. Final evidence for this slice:

- focused server shadow, client readiness pilot, and production-wrapper
  virtual-link selection: 3/3;
- virtual-link deterministic repeat: 10/10, with stable digest
  `f71ed4ca89c5a1b0`;
- complete networking suite: 121/121;
- three complete networking repetitions: 363/363;
- targeted Windows Clang builds of `worr_engine_x86_64`,
  `worr_ded_engine_x86_64`, `cgame_x86_64`, and `sgame_x86_64`: passed;
- built/staged SHA-256 pairs were exact: client engine
  `80b35d1dd5a9bc178f280fe25c779cd97fe2033517cfd8b581fbef58845c3a94`,
  dedicated engine
  `8a68871788f4bf22d49e3738ae332b333d5288f2f1ee983fa04c073a0914859c`,
  cgame `04776955c1c7f43de2adc8572c18ec36b8afcf19d658b9b141274048de79874f`,
  and sgame
  `8eb3788e129b360de040fa5df02e1731f91bd733475bc10af6ba5d1bfe2a3673`;
- refreshed and validated `windows-x86_64` `.install`: 16 root runtime files,
  one dependency, 331 packaged assets, 31 botfiles, and 215 RmlUi assets;
  staged `basew/pak0.pkz` SHA-256
  `d9b21c0e36c8326f21b8e4cba07b1eeb378bccd636857326955850d51db95ce4`;
  and
- `q2proto/` remained untouched.

## Deliberate limitations and next evidence

The virtual link uses an explicit deterministic packet schedule. It does not
yet drive the reusable `NetImpair` directional model/queue, publish a seeded
golden impairment matrix, or statistically sample loss, latency, jitter,
duplication, reordering, and corruption combinations.

The semantic fixture carries one event and its fake cgame receipt model is
sequential. It therefore does not cover multiple distinct events arriving out
of order, selective receipt gaps, larger retained windows, multi-fragment
event messages, event pressure, or long-stream sequence behavior.

There is also no live two-process socket run, client-count load profile,
bandwidth/CPU/memory soak, or non-Windows build/runtime evidence in this
slice. Those gates, plus the P1 retired-release lifecycle correction above,
remain required under `FR-10-T04`, `FR-10-T05`, `FR-10-T07`, `FR-10-T14`, and
`FR-10-T16`. All five tasks remain In Progress.
