# Post-command weapon lease runtime acceptance

Date: 2026-07-17

Project tasks: `FR-10-T08`, `FR-10-T09`, `FR-10-T14`

Status: live observation-only server-callback evidence implemented; no weapon
simulation or presentation promotion.

## Outcome

The canonical weapon runtime gate now has a dedicated
`blaster-local-action-lease` mode. It launches one dedicated server and two
hidden, input-disabled clients. The harness sends ordinary `+attack` only to
the admitted shooter; production `ClientThink`, `ClientBeginServerFrame`,
`Think_Weapon`, and Blaster callback paths retain all authority.

The original lease-only status schema was
`worr.networking.canonical-weapon-damage-runtime.v29`. The descriptor-complete
continuation advances it to v30; its lease suffix reports
catalog/lease readiness; offer, supersede, duplicate, rebase, claim, expiry,
and rejection totals; the exact observed command ID; and scoped, leased,
continuity, and joined-record proofs.

## Continuity correction

The first live run found that a raw byte comparison was too strict: the same
weapon deadlines are projected as relative milliseconds, so both values
legitimately decay when authoritative time advances from command completion to
the next `ClientBeginServerFrame`.

`Worr_LocalActionObservationStatesContiguousV1` now admits only equal,
non-negative decay of both timers within a caller-supplied ceiling no greater
than 1,000 ms. Both states must validate first, and every other byte remains
exact. Unequal decay, time reversal, excess delay, or any input, inventory,
weapon, phase, frame, rate, or presentation change fails closed. The joined
oracle and runtime evidence use this shared rule.

## Three-repeat evidence

The lease-only final report is
`.tmp/networking/local-action-lease-runtime-final.json`. The later shared
shadow-model report is
`.tmp/networking/local-action-shadow-runtime-final.json`; see
`docs-dev/fr-10-t08-t09-descriptor-complete-local-action-shadow-2026-07-17.md`.

| Run | Offers | Supersedes | Claims | Expired | Rejected | Command | Joined |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 466 | 24 | 452 | 14 | 0 | `1:317` | 1 |
| 2 | 425 | 48 | 408 | 17 | 0 | `1:300` | 1 |
| 3 | 418 | 52 | 396 | 22 | 0 | `1:288` | 1 |

Every run also records `catalog_ready=1`, `lease_ready=1`, exact scoped and
leased records, `continuity_exact=1`, `joined_record=1`, zero duplicates, and
zero live rebases. The deterministic lease-core test separately proves the
epoch-rebase transition; this runtime milestone does not claim reconnect
coverage.

## Authority and safety

- The runtime fixture never constructs a command record, opens a command
  context, calls a weapon function, supplies a hit, or edits weapon state.
- The post-command status refresh occurs only after the leased scope destructor
  has appended its immutable record.
- The legacy Blaster callback, damage, projectile, and current-world collision
  path remain authoritative.
- No V2 local-action transaction is built and none of the 22 weapon entries is
  promoted from shadow-only status.
- `q2proto/` is unchanged.

## Verification

- `local_action_observation_test.exe` passes the valid-record, hostile-output,
  corruption, equal timer-decay, unequal timer-decay, reversal, mutation, and
  ceiling cases.
- `tools/networking/test_run_canonical_rail_damage_runtime_gate.py`: 42/42.
- `sgame_x86_64.dll` compiles and links with format checking clean.
- `networking-local-action-lease-acceptance`: three hidden-client repeats pass
  with the counters above and zero stderr.
- `.install/` was refreshed and validated before the final runtime evidence.

## Remaining boundary

The lease is an observational oracle, not a weapon simulator. A deterministic
live reconnect fixture is still required before epoch-rebase runtime coverage
can be claimed. The next dependency-safe step is a richer shared action model
derived from scoped-plus-leased observations, followed by live cgame/sgame
shadow parity; presenter suppression and correction budgets remain later
promotion gates.
