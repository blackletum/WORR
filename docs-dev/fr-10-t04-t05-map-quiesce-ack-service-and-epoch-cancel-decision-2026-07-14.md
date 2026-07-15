# FR-10 Map-Quiesce ACK Service and Epoch-Cancellation Decision

Date: 2026-07-14

Project tasks: `FR-10-T04`, `FR-10-T05`, `FR-10-T14`, `FR-10-T16`

## Outcome

The client production pilot can now finish already-authorized event ACK work
during the narrow map-quiesced `DRAIN` interval. This removes an avoidable
reverse-path blackout without reopening native DATA, readiness, or semantic
event admission.

A deterministic production-wrapper lifecycle diagnostic now also proves the
remaining P1 exactly: after all three proactive event-ACK handoffs are accepted
locally but lost, one map rotation strands the retained descriptor and one more
rotation overwrites the sole current/retired lineage on both peers. The
diagnostic is intentionally evidence of an open defect, not a passing claim of
liveness.

The resulting architecture decision is to make a negotiated, monotonic native
epoch-cancellation barrier the correctness boundary. Retired-DATA transmission
will not be used to recover obsolete map semantics. The next implementation
slice will add explicit, counted cancellation to the common transport/event
cores and bind it transactionally to the readiness challenge/ready exchange.

No project task is completed by this slice. Legacy commands, snapshots, demos,
and presentation remain authoritative, the public capability offer remains
`0x03`, and `q2proto/` is unchanged.

## Task alignment

- `FR-10-T04`: narrows a live client adapter lifecycle gap and ratifies the
  generational cancellation contract needed before repeated map rotations can
  be safe.
- `FR-10-T05`: preserves the event-stream semantic fence: old-map DATA cannot
  obtain fresh cgame authority merely to release transport retention.
- `FR-10-T14`: adds a deterministic executable diagnostic for bounded ACK
  exhaustion, retention, bank overwrite, and delayed-old-ACK rejection.
- `FR-10-T16`: keeps mixed ACK scheduling live through map quiesce while
  explicitly freezing command DATA.

All four tasks remain In Progress because the cancellation barrier, impairment
matrix, load/soak, native snapshot authority, broader event families, and
dual-adapter release evidence remain open.

## Narrow quiesce ACK service

`src/client/native_readiness_pilot.cpp` now recognizes one exact exception to
the normal readiness-output gate:

- native event mode is enabled and initialized;
- readiness mode is `DRAIN`; and
- the map is explicitly quiesced.

In that state, `CL_NativeReadinessPilotOutputDue()` checks the current event ACK
ledger followed by the immediate-retired ledger. Packet preparation may enter
only the existing ledger-backed ACK-only path. It cannot select command DATA,
event DATA, a mixed DATA transaction, or a readiness resurrection.

The exception does not apply to a generic failure-driven `DRAIN`. The retained
client command remains frozen and the packet result remains a legacy-prefix
bypass when no event ACK is due. Retired event DATA admission remains forbidden.

The focused regression proves:

- a current event receipt remains due and can be emitted after map quiesce;
- current/retired ACK selection remains fair after one rotation;
- retained client command DATA is not scheduled or mutated; and
- failure-driven drain remains closed.

This is an opportunistic pre-barrier drain. It improves ordinary transition
behavior but cannot repair a receipt whose three proactive handoffs were
already exhausted.

## Exact exhaustion and overwrite diagnostic

`tools/networking/native_event_virtual_link_test.cpp` now runs an additional
diagnostic after the existing golden scenario. It is excluded from the original
digest so the previously established directional-fault evidence remains
comparable.

The diagnostic reports:

```text
ack_handoffs=3 due_after_exhaustion=0 old_retained=1 retired_receipts=1 retired_retained=1 release_due=0 release_attempted=0 retained_after_window=1 client_bank_overwrite=1 server_bank_overwrite=1 delayed_old_ack_reject=1
```

This sequence proves:

1. all three authorized event ACK copies can be accepted by the local netchan
   and lost on the link;
2. no fourth ACK becomes due without a matching current-epoch DATA repeat;
3. one rotation leaves both the old server DATA and old client receipt retained
   in their immediate-retired banks;
4. retired DATA remains correctly ineligible for semantic admission and cannot
   rearm the receipt;
5. the second rotation replaces both sole retired banks; and
6. a delayed exact old ACK can no longer release the old sender record.

The compile-time assertion
`WORR_NATIVE_EVENT_REQUIRE_EXHAUSTED_ACK_RELEASE` is an opt-in future gate. The
normal suite records the current failure mode without pretending the intended
cancellation behavior exists yet.

## Architecture decision: negotiated epoch cancellation

### Decision

Add a private capability for native epoch cancellation and require it in both
native command and event bindings. When negotiated, a fresh reliable readiness
`CHALLENGE` for transport epoch `Enew` declares every earlier private transport
epoch for the same connection owner obsolete. The client accepts the barrier
only as part of the transaction that queues the matching `CLIENT_READY`; the
ready record is the reliable echo that lets normal activation start from fresh
transport state.

Each endpoint will retain one monotonic
`cancelled_through_transport_epoch`, not another bank ring. Native transport
epochs are strictly increasing and never reused for the connection owner, so a
single high-water value identifies every deliberately canceled predecessor
across unlimited rotations.

### Required semantics

- Cancellation is an explicit successful terminal disposition. It is not an
  ACK, timeout, failure, retry exhaustion, or silent overwrite.
- No state may be canceled while a packet completion outcome is unknown.
- Common TX retention, event-sender payload retention, RX/reassembly state, ACK
  receipts, dispatch gates, and payload registries are cleared through explicit
  APIs that return counted cancellation results.
- Event-owner semantic epoch high-water survives map cancellation; only a full
  disconnect resets connection lineage.
- A structurally valid carrier at or below the cancellation floor is stripped
  and exposes only its unchanged legacy prefix. It cannot mutate current TX,
  RX, ACK, readiness, event-owner, or cgame state.
- A malformed or corrupt old carrier still fails closed.
- Current-epoch semantic repeats continue to require exact committed transport
  history plus a fresh cgame status/receipt. Cancellation creates no retired
  semantic-admission bypass.
- Persistent gameplay state must be represented again by the next canonical
  snapshot/event stream; obsolete one-shot map events and sampled command
  observations are not replayed into a new map.

### Rejected correctness mechanisms

Retired-DATA release repeats would need an additional client dispatcher and a
retired server sender, still could not acknowledge DATA that never arrived,
would weaken the fresh-cgame-status boundary, and would still need a terminal
rule at the following rotation. Extra retained banks only postpone the same
unbounded-lifecycle problem. Either technique may later be useful as a bounded
pre-barrier optimization, but neither is the correctness mechanism.

## idTech3 inspiration

The design follows the useful generational property of Quake 3 without copying
its protocol. In the local Quake3e reference,
`E:\_SOURCE_CODE\Quake3e-master\code\client\client.h` retains a server/map
identity and `E:\_SOURCE_CODE\Quake3e-master\code\server\sv_client.c` rejects
old user commands or resends the current game state across map-restart identity
changes. WORR generalizes that idea into a negotiated private-transport epoch
barrier while retaining its stricter exact-receipt, legacy-prefix, and cgame
semantic-authority rules.

## Verification

Focused and repeated validation:

- client readiness pilot plus virtual link: 2/2 passed;
- combined focused repeat: 20/20 passed;
- full networking suite repeated three times: 363/363 passed;
- original virtual-link digest remains `f71ed4ca89c5a1b0`.

Production builds:

- `worr_engine_x86_64`: passed, SHA-256
  `f7f775f9e2167bbf168f1e54dbcd2d80a80d23a93657348a423ef11238f78e20`;
- `worr_ded_engine_x86_64`: passed, SHA-256
  `8a68871788f4bf22d49e3738ae332b333d5288f2f1ee983fa04c073a0914859c`;
- `cgame_x86_64`: passed, SHA-256
  `04776955c1c7f43de2adc8572c18ec36b8afcf19d658b9b141274048de79874f`;
- `sgame_x86_64`: passed, SHA-256
  `8eb3788e129b360de040fa5df02e1731f91bd733475bc10af6ba5d1bfe2a3673`.

The mandatory `.install/` refresh and validation passed with 16 root runtime
files, one root dependency, 331 packaged assets, 31 botfiles, and 215 RmlUi
assets. Each staged production module matches its build artifact. The staged
`basew/pak0.pkz` SHA-256 is
`d9b21c0e36c8326f21b8e4cba07b1eeb378bccd636857326955850d51db95ce4`.

Scoped diff checks pass and `q2proto/` remains untouched.

## Completion impact

Roadmap checklist state is unchanged:

- overall strategic roadmap: 68/180 complete (37.8%), 112 open;
- FR-10: 3/16 complete (18.75%), 13 open;
- tasks completed by this slice: 0.

The next critical implementation is the negotiated cancellation capability,
common counted-cancel APIs, transactional client/server barrier integration,
and a replacement virtual-link lifecycle gate proving ACK exhaustion plus two
consecutive rotations without retained-bank overwrite or stale cgame mutation.
