# Post-command weapon observation lease

Date: 2026-07-17

Project tasks: `FR-10-T08`, `FR-10-T09`, `FR-10-T14`

Status: implemented observation-only ownership and joined-oracle prerequisite;
no simulation, gameplay, or presentation promotion.

## Outcome

The authoritative local-action oracle no longer treats every
`ClientBeginServerFrame` weapon advance as anonymous. After a validated
canonical `ClientThink` observation record succeeds, it may offer that exact
command to a bounded lease. The next `ClientBeginServerFrame` activates the
latest offer for that client, and the normal post-command `Think_Weapon` path
may claim it once.

The lease does not reopen or extend the engine command context. Lag
compensation and every other consumer continue to see the ordinary command
scope as inactive. The lease is private to local-action observation and cannot
authorize a trace, projectile, damage result, event, packet, snapshot, demo,
audio/visual effect, or timing change.

## Shared lifecycle contract

`worr_local_action_command_lease_v1` is a 144-byte pointer-free record with a
validated canonical command and its semantic hash. Its states are:

1. `EMPTY`: canonical zero command/hash.
2. `PENDING`: an authenticated observation offered one command.
3. `FRAME_ACTIVE`: the next server-frame callback owns the opportunity.
4. `CLAIMED`: one post-command advance consumed it.

Only a zeroed output can be initialized or claimed. Offers are transactional:
an exact duplicate is idempotent, the exact next ID supersedes the previous
pending offer, and a new command epoch explicitly rebases a reused client slot.
A same-epoch gap or conflict is rejected byte-atomically. Claim succeeds once.
Frame end always clears the command and hash, reporting either claimed or
expired; no lease can silently survive to another frame.

## Production integration

- `SG_LocalActionObservationScope` offers a lease only after it has captured a
  valid authenticated pre/post record. A rejected scope or invalid state
  cannot create an offer.
- A frame RAII scope is constructed before the first early return in
  `ClientBeginServerFrame`, so intermission, freeze, respawn, menu, death, and
  ordinary completion paths all expire the lease deterministically.
- The existing normal post-command `Think_Weapon` call has a narrow leased-
  advance RAII scope. It claims before the call, captures the real weapon state
  immediately before and after, and appends a separate bounded value-only
  record. The weapon call and `weapon.thunk` decision are unchanged.
- Command-scoped, leased, and truly unscoped weapon-think counters are separate
  per stable weapon catalog ID. Offer, supersede, duplicate, epoch-rebase,
  claim, expiry, and rejection totals are read-only diagnostics.
- Read-only lookups expose the scoped and leased records by exact client and
  command identity. A joined lookup succeeds only if both command records are
  semantically equal, every non-timer field is byte-identical, and both
  relative weapon timers decay by the same non-negative amount within the
  1,000 ms continuity ceiling. This admits authoritative clock passage between
  command end and the next frame without hiding input, inventory, weapon,
  presentation, or unequal-timer changes. The lookup then rebuilds one
  validated record from the original scoped before-state to the leased
  after-state. Missing halves or any state gap fail closed.

Grapple remains counted separately and continues to use the independent Hook
request/receipt contract.

## Safety properties

- At most one post-command advance can claim an offer.
- Multiple commands before one server frame retain only the exact latest
  adjacent command; supersession is explicit telemetry.
- A reused client slot cannot inherit an old-epoch observation.
- Early returns expire an unused offer.
- A leased record cannot be joined across a command conflict or intervening
  local-action state change.
- The existing authoritative command-context import and API version are
  untouched.
- `Worr_LocalActionBuildTransactionV2` is not called, all 22 weapon semantics
  remain V2-blocked, and `q2proto/` is unchanged.

## Verification

All executable checks use the repository's no-window, input-free policy.

- `network-local-action-command-lease` covers accepted, duplicate, adjacent-
  superseded, epoch-rebased, same-epoch-gap-rejected, one-shot claimed,
  unclaimed-expired, corrupt/nonzero-output, and canonical-empty lifecycles.
- `network-local-action-command-lease-source` proves the frame lease precedes
  all production early returns, the leased scope wraps only the existing
  post-command weapon call, an offer follows a validated scoped record, all
  Begin/Claim/End operations are present, and no V2 transaction builder enters
  the observation module.
- The existing hostile observation ABI test passes.
- The shipped `sgame_x86_64.dll` compiles and links with the production scopes,
  bounded ledgers, telemetry, and fail-closed joined lookup.
- `networking-local-action-lease-acceptance` uses the ordinary hidden-client
  Blaster path. Three repeats prove real offers, supersedes, claims, expiries,
  exact scoped/leased records, bounded timer-decay continuity, and joined
  records with zero rejection. Detailed evidence is recorded in
  `docs-dev/fr-10-t08-t09-post-command-weapon-lease-runtime-acceptance-2026-07-17.md`.

## Remaining boundary

This establishes exact observational ownership, not deterministic weapon
replay. The live fixture proves claimed, expired, superseded, and joined-
continuity rows; reused-slot epoch rebase remains deterministic core evidence,
not a live reconnect claim. The joined oracle must now drive the richer shared
action model required by the 22-weapon semantics audit. Live cgame/sgame shadow
parity, event/presenter de-duplication, correction budgets, impairment/load
evidence, and explicit promotion gates remain open.
