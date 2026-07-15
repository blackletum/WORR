# FR-10 Parser-Shared Stale-Readiness Capacity Gate

Date: 2026-07-15

Project task: `FR-10-T04`

## Outcome

The production server’s native-readiness parser now uses one shared decision
boundary for complete sideband settings:
`SV_NativeShadowObserveSettingWithResponseCapacityV1`.

The boundary first invokes the existing complete-record decoder and validator.
Only if that result is a current `CLIENT_READY` requiring `SERVER_ACTIVE` does
it test the fixed 117-byte reliable append capacity. A valid old
`CLIENT_READY` or `CLIENT_ACTIVE_CONFIRM` at or below the negotiated
cancellation floor is therefore consumed before reliable-capacity availability
is relevant. A malformed, wrong-direction, capability-mismatched, or current
record retains the existing fail-closed behavior.

This closes a review finding in the former wrapper ordering. The parser had
previously inspected its current readiness phase and could disable the shadow
when the reliable queue was full before the native shadow had an opportunity to
classify a delayed canceled record as response-free.

## Production boundary

`SV_ExecuteClientMessage` in `src/server/user.c` delegates every native
readiness setting to the new parser-shared helper with its live `msg_write` and
per-client reliable queue. The helper returns `SERVER_ACTIVE_READY` only when
the current record is valid and the fixed append can fit. The wrapper retains
the checked atomic append and its defensive failure fallback; no native wire
state is committed until that append and `SV_NativeShadowServerActiveQueuedV1`
both succeed.

If a valid old carrier was already observed, a later current queue failure
enters the existing drain lifecycle to preserve legacy-prefix stripping for
that carrier. This is distinct from wire commitment: no new transport is
initialized and `native_wire_committed` remains clear.

## Regression coverage

`native_server_shadow_pilot_test` now supplies a physically full reliable
buffer to the exact shared helper in both cases:

- A delayed valid `CLIENT_READY` for an explicitly canceled epoch is consumed,
  does not request `SERVER_ACTIVE`, leaves the replacement peer enabled, and
  increments only the stale-readiness disposition counter.
- A valid `CLIENT_READY` for the replacement epoch receives the same full
  buffer and fails closed with `SV_NATIVE_SHADOW_FAILURE_QUEUE`, without
  transport initialization or native-wire commitment.

The test also carries a valid delayed native carrier before the second case,
so it confirms the deliberate drain behavior and retained legacy-only parser
provenance when a current response later cannot fit.

## Scope and compatibility

No `q2proto/` source changed. The native command/event shadows remain default
off and diagnostic-only; legacy settings, commands, snapshots, demos, and
event presentation retain authority. This is coverage and lifecycle hardening
within `FR-10-T04`, not completion of the roadmap task.

## Verification

- Focused readiness/server-shadow/event virtual-link gate: 5/5 pass.
- Production client engine, dedicated-server engine, cgame, and sgame build:
  pass.
- Full headless networking suite: 121/121 pass.
- Three consecutive headless suite repetitions: 363/363 pass.
- `python tools/refresh_install.py --build-dir builddir-win --install-dir
  .install --base-game basew --platform-id windows-x86_64`: pass; staged
  16 root runtime files, one root dependency, 342 packaged source assets,
  31 botfiles, and 215 RmlUi assets.

Roadmap completion remains unchanged: 68 of 180 tasks complete (37.8%, 112
open); `FR-10` remains 3 of 16 complete (18.75%, 13 open).
