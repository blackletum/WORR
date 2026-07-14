# Authoritative Prediction Input Range

Date: 2026-07-12  
Project tasks: `FR-10-T08`, `FR-10-T09`

## Outcome

Client prediction no longer treats the transport packet acknowledgement as the
server's consumed-input watermark after the canonical cursor is established.
The client engine now resolves one immutable, value-copied prediction range and
passes it to cgame through the versioned
`WORR_CGAME_PREDICTION_INPUT_IMPORT_V1` extension. The range contains:

- the exact authoritative consumed command and its mapped legacy history slot;
- every finalized local successor, in execution order;
- each successor's canonical command ID when canonical authority is active;
- a copied, not-yet-finalized local command, if one exists; and
- explicit source/result/bootstrapping flags.

The ABI is pointer-free across the engine/cgame boundary. The only pointer is
the import function itself; all range content is copied into cgame-owned
storage for that invocation. This removes prediction decisions from direct
reads of `netchan.incoming_acknowledged`, `cl.history`, and `cl.cmds` inside the
module.

## Authority State Machine

The resolver has three explicit authority modes:

1. On a server without the consumed-cursor capability, the legacy packet ACK
   remains the fallback watermark.
2. After capability negotiation but while the server publishes the sole
   bootstrap cursor `{0,0}`, packet-ACK replay remains available and is tagged
   `CANONICAL_BOOTSTRAP`. This avoids freezing prediction before the server
   initializes its canonical command stream.
3. The first non-absent cursor binds to the negotiated session epoch and
   permanently establishes canonical authority for that lifecycle. From then
   on, packet ACK cannot substitute for missing, malformed, regressing, or
   ambiguous canonical history.

The consumed-cursor runtime retains the last established cursor. It permits an
unchanged cursor, rejects same-epoch sequence regression, rejects `{0,0}` after
establishment, and permits only the next epoch on command-sequence rollover.
Explicit connection/map/demo resets clear this temporal state.

## Exact History Mapping

`CL_CommandIdentityReset` freezes the legacy sequence immediately preceding
canonical command ID 1. Finalization must then be contiguous in both legacy
sequence and canonical identity. The resolver scans the bounded identity ring
for the server-consumed ID, rejects zero or multiple matches, and verifies that
every copied legacy successor has exactly the next canonical ID. The packet ACK
is deliberately ignored in this mode.

Cursor sequence zero in the bound session maps to the frozen pre-command
legacy baseline, allowing the client to replay command ID 1 and later without
inventing a command-zero identity. Unsigned legacy sequence arithmetic remains
wrap-safe. A range of `CMD_BACKUP` commands or more is rejected because the
authoritative baseline has aged out.

## Fail-Closed Reconciliation

The following conditions request a hard prediction resync:

- canonical metadata disappears after establishment;
- the consumed ID is absent or duplicated in retained history;
- a successor ID is discontinuous;
- the initial cursor epoch does not match the negotiated session;
- retained history capacity is exceeded;
- copied command data or ABI headers are invalid; or
- the cgame import/result contract is inconsistent.

The current hard-resync action is deliberately local: cgame discards predicted
state/hash caches, snaps movement/view presentation to the current
authoritative player state, clears smoothing error and ground-step state, and
does not replay an uncertain range. It does not yet request an out-of-band full
snapshot from the server. A later valid snapshot/cursor pair can recover normal
replay without reconnecting.

`CL_CheckPredictionError` uses the same mapped consumed legacy sequence as
movement replay. A canonical sequence-zero cursor is the pre-command state and
therefore has no predicted command result to compare. Movement replays only the
copied successors, then the copied pending command. Step smoothing is skipped
when its predecessor prediction is not retained; collision state itself is
never smoothed.

## Files and Contracts

- `inc/shared/cgame_prediction.h`: stable input-range/import ABI.
- `inc/common/net/prediction_input.h` and
  `src/common/net/prediction_input.c`: pure bounded resolver.
- `src/client/cgame.cpp`: engine-side immutable range producer.
- `src/client/command_identity.cpp`: frozen baseline and contiguous identity
  production.
- `src/client/consumed_cursor.cpp`: session binding and monotonic cursor state.
- `src/game/cgame/cg_main.cpp`: import discovery and validation.
- `src/game/cgame/cg_predict.cpp`: canonical correction/replay consumer and
  local hard resync.

No file under `q2proto/` changed. The legacy carrier and demo parsing paths are
unchanged by this slice.

## Verification

`prediction_input_range_test` covers canonical precedence over a deliberately
newer packet ACK, sequence-zero baseline mapping, negotiated bootstrap, true
legacy fallback, missing/duplicate/discontinuous identities, epoch mismatch,
legacy sequence wrap, retained-range exhaustion, invalid commands, pending
command copying, and source immutability. C and C++ layout tests pin the 6,248
byte fixed range and its command-array offsets.

Focused verification performed:

```text
prediction input range tests passed
C ABI layout test passed
C++ ABI layout test passed
clang++ syntax checks passed:
  src/client/cgame.cpp
  src/client/command_identity.cpp
  src/client/consumed_cursor.cpp
  src/game/cgame/cg_main.cpp
  src/game/cgame/cg_predict.cpp
```

## Remaining `FR-10-T08` Work

This closes the authoritative movement replay-watermark seam, not all of
`FR-10-T08`. Predictable weapon/gameplay state, audiovisual side-effect
suppression/correlation, correction-class budgets, and impairment-matrix
promotion remain open. Those features should consume this same immutable range
and command identity rather than introducing another acknowledgement model.
