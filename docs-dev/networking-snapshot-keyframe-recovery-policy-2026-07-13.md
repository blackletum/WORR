# Snapshot Keyframe Recovery Policy and Legacy Transport Adapter

Date: 2026-07-13  
Project task: `FR-10-T06`  
Related observability task: `FR-10-T14`

## Outcome

This slice adds an explicit, deterministic policy for recovering snapshot
lineage with a full, non-delta snapshot. It does not invent a second snapshot
model and does not modify `q2proto/`.

The policy is now available as a pointer-free common core, is observed by the
live client snapshot paths, and can use every currently supported legacy
protocol's existing delta acknowledgement field to ask the server for a full
snapshot. The behavior-changing part of that adapter is default-off through
`cl_snapshot_recovery 0`.

Normal Quake II recovery behavior is unchanged. When the legacy parser cannot
reconstruct a frame, it already invalidates `cl.frame`; the next move carries
`lastframe = -1`. The server's existing `get_last_frame()` interprets a
non-positive acknowledgement as no usable base and emits a frame with
`deltaframe = -1`. This safety path remains authoritative and is never
rate-limited by the new optional policy.

## Deterministic policy core

The public API is defined in
`inc/common/net/snapshot_recovery.h` and implemented in
`src/common/net/snapshot_recovery.c`.

All public records use fixed-width integers and contain no pointers:

- `worr_snapshot_recovery_config_v1` declares the thresholds and bounded
  retry cadence.
- `worr_snapshot_recovery_observation_v1` records one accepted frame,
  recoverable failure, canonical success, or forced request.
- `worr_snapshot_recovery_state_v1` owns the complete replayable state and
  saturating telemetry counters.
- `worr_snapshot_recovery_decision_v1` reports whether the current transport
  opportunity should request a full snapshot.

The default policy is:

- arm after one legacy reconstruction failure;
- arm after three consecutive canonical projector/parity failures;
- request on at most three consecutive transport opportunities;
- suppress two transport opportunities before another request burst; and
- clear an armed request only after an accepted full/non-delta snapshot or an
  explicit connection reset.

Accepted delta frames reset the legacy-failure streak but do not silently
clear an armed recovery. Canonical successes reset only the canonical-failure
streak. This separation prevents a successfully parsed legacy delta from
masking repeated canonical projection failures.

Generation exhaustion fails closed. Invalid configs, states, observations,
reserved fields, and impossible accepted-delta bases are rejected before the
state is modified. Telemetry counters saturate instead of wrapping.

## Live client adapter

The engine adapter is defined in `inc/client/snapshot_recovery.h` and
implemented in `src/client/snapshot_recovery.cpp`.

The adapter observes:

- valid and invalid legacy frame reconstruction in `src/client/parse.cpp`;
- canonical projection and parity results after the accepted-frame shadow in
  `src/client/entities.cpp`; and
- actual outgoing default and batched move opportunities in
  `src/client/input.cpp`.

`src/client/main.cpp` resets the policy with the rest of client connection
state. Serverdata begins a fresh policy epoch after negotiated protocol limits
are known.

The adapter deliberately distinguishes three cases:

1. A legacy invalid frame has already selected `lastframe = -1`. The adapter
   preserves it regardless of its cvar or retry state.
2. A canonical projector or parity streak arms the policy. With
   `cl_snapshot_recovery 1`, a request decision replaces the otherwise valid
   acknowledged base with `-1` for that outgoing move.
3. Promotion policy or a downstream cgame consumer rejects an otherwise
   transport-valid snapshot. The adapter records this separately and does not
   request a full snapshot because transport recovery cannot repair consumer
   policy.

This is a safe live legacy seam because both `Q2P_CLC_MOVE` and
`Q2P_CLC_BATCH_MOVE` already carry the signed `lastframe` value, and the
existing server snapshot builder already falls back to a full frame for a
non-positive or unavailable base. Vanilla, R1Q2, Q2PRO, and Q2REPRO therefore
need no new service opcode or `q2proto/` change for this recovery operation.

## Controls and telemetry

- `cl_snapshot_recovery 0|1` controls canonical-failure-driven full snapshot
  requests. It is archived and defaults to `0`.
- `cl_snapshot_recovery_debug 0|1` prints arm and applied-request decisions.
- `cl_snapshot_recovery_status` prints the current generation, reason mask,
  failure streaks, burst/cooldown state, recovery count, applied overrides,
  inherited legacy requests, disabled-policy blocks, ignored non-transport
  rejections, and the last core result.

`CL_SnapshotRecoveryGetStatus()` also copies this information into the
pointer-free `cl_snapshot_recovery_status_v1` record. That is the bounded
telemetry seam for the broader `FR-10-T14` observability work; no console text
needs to be scraped.

## Verification

Meson targets and networking-suite entries were added for:

- `snapshot_recovery_test` / `network-snapshot-recovery`;
- `snapshot_recovery_layout_c_test` /
  `network-snapshot-recovery-layout-c`; and
- `snapshot_recovery_layout_cpp_test` /
  `network-snapshot-recovery-layout-cpp`.

The behavioral test covers bounded request bursts and cooldown, accepted-full
recovery, legacy/canonical streak separation, reason accumulation, invalid
input transactionality, 20,000-step deterministic replay, counter saturation,
and generation exhaustion. C and C++ layout tests protect sizes, offsets,
standard layout, and trivial copyability.

The combined Windows Clang integration pass produced the following evidence:

- all three recovery policy behavioral/layout targets passed within the full
  67/67 networking suite;
- three consecutive networking-suite repetitions passed 201/201 test
  invocations;
- the runtime networking smoke target passed;
- the rewind acceptance matrix passed all 120 invocations;
- the cgame module, sgame module, dedicated engine, and client engine
  production targets all built and linked successfully, including the recovery
  adapter at the legacy parser, canonical snapshot, connection-reset, and
  move-send seams; and
- `.install/` was refreshed and validated for `windows-x86_64`: 16 root
  runtime files, one dependency, the `basew` runtime tree, a 308-asset
  `pak0.pkz`, botfile payload, and RmlUi payload all passed validation.

This evidence verifies the deterministic default-off policy and current live
adapter. It does not complete the open impairment, long-session,
cross-platform, or release-default gates, and the validated Windows staging
tree is not by itself packaged-release acceptance.

## Deliberate limits and remaining gates

This is an FR-10-T06 recovery-policy slice, not completion of `FR-10-T06`.
Its pointer-free status record is an input to FR-10-T14 observability, but that
does not transfer ownership of snapshot/keyframe policy to FR-10-T14 or
FR-10-T16.

- Canonical snapshot promotion remains gated by the existing parity and cgame
  audit policy.
- The default-off canonical override still needs impairment and long-session
  evidence before becoming a release default.
- Server-side per-peer byte/CPU budgets and 100,000-snapshot live parity
  evidence remain open.
- A future native WORR transport may encode an explicit keyframe request, but
  it must feed this same decision core and canonical snapshot constructors
  rather than creating parallel policy or state.
