# Local-Action Prediction/Authority Audit Ring

Date: 2026-07-13

Project tasks: `FR-10-T08`, `FR-10-T09`, `FR-10-T14`, `FR-10-T16`

## Purpose

This slice adds an isolated retention and comparison core for the canonical
local-action transaction model. It gives future cgame and sgame shadow paths a
single fail-closed place to correlate a predicted transaction with its
authoritative counterpart, retain the resulting correction evidence, and
measure divergence without making the experimental action model live gameplay
authority.

The core does not alter cgame, sgame, snapshots, legacy Quake II transport,
`q2proto/`, or protocol negotiation. It is an instrumentation and convergence
primitive for the later `FR-10-T08` live integration and rollout gates.

## Public surface

The implementation is split into:

- `inc/common/net/local_action_audit.h`: C/C++ public declarations and fixed
  record layouts;
- `src/common/net/local_action_audit.c`: validation, pairing, correction,
  retention, pruning, reset, and telemetry logic;
- `tools/networking/local_action_audit_test.c`: deterministic behavior,
  corruption, lifecycle, saturation, and stress coverage;
- `tools/networking/local_action_audit_benchmark.c`: maximum-capacity hot-path,
  destructive-lifecycle, terminal-ID, and CPU-budget evidence;
- `tools/networking/local_action_audit_layout_c.c` and
  `tools/networking/local_action_audit_layout_cpp.cpp`: strict C11 and C++20
  consumer/layout proofs.

The caller owns both the runtime envelope and fixed slot storage. Capacity is
explicit and is bounded by `WORR_LOCAL_ACTION_AUDIT_MAX_CAPACITY` at 512 slots.
One slot is 2,528 bytes, so the maximum retained transaction evidence is about
1.3 MiB. Both validation modes now use a fixed 1,024-entry open-addressed
identity set, keeping duplicate-ID proof O(n) with no allocation at the 512-slot
bound.

Storage ABI v1 and its original whole-ring-deep operations remain unchanged:

```text
Worr_LocalActionAuditInitV1
Worr_LocalActionAuditValidateV1
Worr_LocalActionAuditSubmitV1
Worr_LocalActionAuditCopyV1
Worr_LocalActionAuditPruneThroughV1
Worr_LocalActionAuditResetV1
Worr_LocalActionAuditGetTelemetryV1
```

Operational API v2 deliberately uses the same pointer-free v1 envelope and
slot layout, so no retained-record migration or second allocation is needed:

```text
Worr_LocalActionAuditInitV2
Worr_LocalActionAuditValidateOperationalV2
Worr_LocalActionAuditValidateDeepV2
Worr_LocalActionAuditSubmitV2
Worr_LocalActionAuditCopyV2
Worr_LocalActionAuditPruneThroughV2
Worr_LocalActionAuditResetV2
Worr_LocalActionAuditGetTelemetryV2
```

`WORR_LOCAL_ACTION_AUDIT_OPERATIONAL_API_VERSION` is 2; the retained storage
schema remains `WORR_LOCAL_ACTION_AUDIT_VERSION` 1. The separation is explicit:
V1 callers retain their prior semantics, while live integrations can opt into
the bounded operational contract without silently changing an existing API.

## Identity and pairing model

`worr_command_id_v1` is the only pairing key. The first validated role creates
an unmatched slot, and the opposite role may arrive later in either order.
Prediction and authority packet numbers, retry ordinals, snapshot numbers, and
arrival timing never become identity.

Connection provenance is a separate input to submission. Every call supplies a
nonzero `submission_connection_epoch`, which must equal the audit's active
connection epoch. This prevents a delayed transaction from an old connection
from aliasing a current command, while deliberately allowing
`command_id.epoch` to advance inside one connection when command sequence
`UINT32_MAX` rolls to the next command epoch at sequence one.

For an already retained producer role:

- a byte-identical validated transaction is an idempotent duplicate;
- any other validated transaction with that command ID is a conflict;
- neither outcome replaces the first accepted transaction.

When the second role arrives, the implementation constructs a candidate slot,
calls `Worr_LocalActionCorrectionClassifyV2`, validates the complete candidate,
and commits it once. A paired slot is immutable until explicit prune or reset.
V1 validation and `Worr_LocalActionAuditValidateDeepV2` later reproduce the
correction into scratch and compare all bytes; that is an integrity proof, not
a second classification/accounting event.

The retained slot is pointer-free and contains:

| Field | Purpose |
|---|---|
| Slot header | Schema, flags, command ID, and first-arrival serial |
| Predicted transaction | Exact first accepted predicted proof, or all zero while absent |
| Authoritative transaction | Exact first accepted authority proof, or all zero while absent |
| Correction | Exact, presentation-only, immediate-gameplay, or hard-resync result; all zero until paired |

## Retention, pruning, and reset

The audit never silently evicts. A full ring returns
`WORR_LOCAL_ACTION_AUDIT_CAPACITY` and increments the saturated capacity-stall
counter without changing any retained slot.

`Worr_LocalActionAuditPruneThroughV1/V2` uses a full `worr_command_id_v1`
watermark, compared lexicographically across command-epoch advancement. It
removes only classified pairs at or below the requested ID. If any unmatched
predicted or authoritative slot exists at or below that ID, the complete prune
is blocked: slot bytes, head, count, watermark, and generation remain
byte-identical. Only the saturated `prune_attempts` and `prune_blocks`
telemetry counters change. This makes incomplete evidence visible and prevents
retention pressure from masquerading as successful parity.

A successful prune compacts surviving slots without changing their contents or
first-arrival ordering, advances the full stale watermark, and makes every
later submission at or below it explicitly stale. The operation may advance
over gaps because the caller is explicitly declaring that range closed.
V2 prune deliberately retains whole-ring deep admission because it destroys
evidence; it is measured against the separate lifecycle budget rather than the
per-command hot-path budget.

`Worr_LocalActionAuditResetV1/V2` is the only connection discontinuity. Its new
connection epoch must be nonzero and numerically greater than the current one.
A successful reset clears storage, clears the command watermark, increments the
runtime generation, restarts arrival serials at one, and resets retained
unmatched gauges. Historical counters remain saturated lifetime telemetry.
Generation exhaustion rejects the reset rather than aliasing old evidence.

## Fail-closed validation and atomicity

V1 keeps its original contract: every public operation performs a whole-ring
deep validation before changing or returning state. V2 splits that work into
two explicit levels without returning or classifying unvalidated evidence.

The O(n) operational pass checks:

- runtime schema, capacity bound, object alignment, disjoint envelope/storage
  ranges, head/count, connection epoch, generation, reserved fields, and full
  command watermark;
- every active slot header, legal role/classification combination, retained
  command identity, and strictly increasing nonzero arrival serial;
- unique command IDs through the fixed half-full identity table rather than a
  pairwise scan;
- present/absent transaction headers, producer roles, command IDs, correction
  schema, correction-to-transaction hashes/cursors, and legal correction class;
- exact retained unmatched gauges and consistent saturated counter
  inequalities.

Operational submit fully reconstructs the supplied transaction and, for an
existing ID, deeply validates and reproduces that exact retained slot before
duplicate comparison, conflict detection, or pairing. Operational copy deeply
validates and reproduces its selected slot before returning bytes. A new slot
is committed only from a fully validated input/candidate. Destructive V2 prune
runs the whole-ring deep validator. V2 reset may clear semantically poisoned
payload because it consumes none of that payload, but still rejects structural
poison before changing the connection epoch.

`Worr_LocalActionAuditValidateDeepV2` and V1 validation additionally verify
every byte of unused storage, fully reconstruct every retained transaction,
validate all-zero absent records, and deterministically reproduce every paired
correction. This explicit trust-boundary/diagnostic audit detects corruption
outside the slot selected by a hot operation. It should run when state crosses
a trust boundary and on a caller-selected diagnostic cadence, not once per
command.

Initialization rejects zero and over-limit capacities before touching either
object. Submit, copy, and telemetry output objects may not overlap the runtime
or any slot storage. Corrupt structural state or corrupt selected evidence
returns `INVALID_STATE` without updating telemetry or retained state. Other
rejected operations may update their explicitly
documented saturated outcome counters, but leave retained slots and lifecycle
state unchanged. Copy output is untouched on every failure.

All historical counters increment or add with saturation at `UINT64_MAX`.
`unmatched_predicted` and `unmatched_authoritative` are exact retained-state
gauges, bounded by the 512-slot capacity rather than historical counters.

## Test evidence

The standalone behavior suite was compiled with Clang 20.1.7 in strict C11
mode, with warnings promoted to errors:

```powershell
clang -std=c11 -Wall -Wextra -Wpedantic -Werror -Iinc `
  tools/networking/local_action_audit_test.c `
  src/common/net/local_action_audit.c `
  src/common/net/local_action.c `
  src/common/net/command_abi.c `
  src/common/net/command_canonical.c `
  src/common/net/event_abi.c `
  -o .tmp/local_action_audit_test.exe

.tmp/local_action_audit_test.exe
```

Result:

```text
local action prediction/authority audit tests passed
```

The strict layout consumers were also compiled and run successfully:

```powershell
clang -std=c11 -Wall -Wextra -Wpedantic -Werror -Iinc `
  tools/networking/local_action_audit_layout_c.c `
  -o .tmp/local_action_audit_layout_c.exe
.tmp/local_action_audit_layout_c.exe

clang++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -Iinc `
  tools/networking/local_action_audit_layout_cpp.cpp `
  -o .tmp/local_action_audit_layout_cpp.exe
.tmp/local_action_audit_layout_cpp.exe
```

The maximum-capacity benchmark was compiled with the same strict C11 flags and
linked against the same implementation sources:

```powershell
clang -std=c11 -Wall -Wextra -Wpedantic -Werror -Iinc `
  tools/networking/local_action_audit_benchmark.c `
  src/common/net/local_action_audit.c `
  src/common/net/local_action.c `
  src/common/net/command_abi.c `
  src/common/net/command_canonical.c `
  src/common/net/event_abi.c `
  -o .tmp/local_action_audit_benchmark.exe

.tmp/local_action_audit_benchmark.exe
```

Coverage includes:

- prediction-first and authority-first pairing;
- exact, presentation-only, gameplay-immediate, and hard-resync corrections;
- exact duplicate idempotence, same-role conflict, and paired-slot
  immutability;
- full-capacity stalls with no eviction;
- out-of-order first arrivals, unmatched prune blocking, successful classified
  pruning, stale rejection, and non-regressing full command watermarks;
- command sequence `UINT32_MAX` advancing to the next command epoch while the
  same connection remains active;
- exact terminal command ID `{UINT32_MAX, UINT32_MAX}` insertion, pairing,
  duplicate lookup, copy, and full-ring prune;
- stale connection submissions, strict newer-epoch reset, and generation
  exhaustion;
- pointer alias rejection, corrupt transaction/slot/unused-storage detection,
  output atomicity, and invalid-role separation;
- V2 structural poisoning, selected-transaction semantic poisoning,
  schema-valid but non-reproducible correction poisoning, duplicate retained
  IDs, unused-payload deep detection, and transactional copy/submit/prune/reset
  rejection;
- arrival-serial exhaustion and saturated counters/gauge transitions;
- 256 deterministic exact pairs with alternating arrival order and progressive
  prune/validation cycles.

The behavior, maximum-capacity budget, and C/C++ layout executables are
registered in the normal Meson `networking` suite. Focused execution is:

```powershell
meson test -C builddir-win `
  network-local-action-audit `
  network-local-action-audit-performance `
  network-local-action-audit-layout-c `
  network-local-action-audit-layout-cpp `
  --print-errorlogs
```

Result: 4/4 passed; the focused repeat run passed 12/12.

## Performance gate result

An earlier independent correctness review of the V1 core found no sequence,
connection-epoch, pairing, prune, reset, alias, or undefined-behavior defect.
Its identified promotion blocker was the complete deep validator on every
operation. Operational API v2 removes that blocker while V1 and the explicit
deep audit preserve the stronger original proof.

The benchmark fills all 512 slots with classified pairs, places terminal
command ID `{UINT32_MAX, UINT32_MAX}` at the worst lookup position, and runs
2,000 iterations of operational validation, duplicate submit, copy, and
telemetry. The declared conservative budget is 500,000 ns average per hot
operation. Destructive prune and reset have a separate 10,000,000 ns one-shot
lifecycle budget. Deep validation is measured for visibility but is explicitly
outside the live hot-path budget.

Five consecutive `builddir-win` runs produced these observed ranges on the
Windows x86_64 Clang 20.1.7 `debugoptimized` configuration:

| Operation at 512 slots | Observed range | Budget |
|---|---:|---:|
| Operational validation | 8.0-9.5 us | 500 us average |
| Duplicate submit, terminal ID | 11.0-13.0 us | 500 us average |
| Copy, terminal ID | 10.0-11.5 us | 500 us average |
| Telemetry snapshot | 8.0-9.0 us | 500 us average |
| Explicit deep audit | 0.75-1.00 ms | diagnostic, not hot path |
| V2 destructive prune | 0.806-1.473 ms | 10 ms one-shot |
| V2 reset | 0.018-0.051 ms | 10 ms one-shot |

After restoring deep admission to destructive V2 prune, the strict unoptimized
standalone run measured 3.07 ms for prune and remained below its 10 ms budget.
The registered executable enforces both budgets and fails the networking suite
when either is exceeded. Linux/macOS measurements remain part of the broader
cross-platform promotion evidence, rather than an unresolved algorithmic
blocker in this core.

## Remaining integration gates

This core does not complete `FR-10-T08`, `FR-10-T09`, `FR-10-T14`, or
`FR-10-T16`.
The per-operation validation blocker is closed. Remaining work includes adding
the bgame-owned complete Rerelease action catalog, retaining live predicted cgame
and authoritative sgame transactions, exporting operator telemetry, and
demonstrating correction budgets under impairment/load before any authority
cutover.
