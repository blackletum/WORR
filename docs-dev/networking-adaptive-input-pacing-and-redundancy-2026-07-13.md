# Adaptive input pacing and selective redundancy

Date: 2026-07-13  
Project task: FR-10-T16  
Scope: bounded client input-pacing and command-redundancy production slice

## Outcome

WORR now has a deterministic, allocation-free adaptive input controller and a
default-off production integration for the existing batched user-command
transport. The controller does not define a protocol extension and does not
modify `q2proto/`. When `cl_adaptive_input` is `0` (the default), the client
continues to use the existing `cl_maxpackets` and `cl_packetdup` paths without
an adaptive decision affecting packet timing or batch contents.

This is a substantive FR-10-T16 input-delivery slice. Snapshot lineage and
keyframe-recovery policy are owned by FR-10-T06, not FR-10-T16. This slice does
not complete FR-10-T16's impairment tuning and promotion work or the FR-10-T14
telemetry backend.

## Core contract

`inc/common/net/adaptive_input.h` defines versioned V1 records for:

- controller configuration;
- one observation of connection and command pressure;
- one decision with a machine-readable reason mask;
- persistent deterministic controller state; and
- copyable telemetry suitable for a later FR-10-T14 sink.

Every public record is pointer-free, fixed-width, standard-layout data. The C
core in `src/common/net/adaptive_input.c` uses integer and fixed-point math,
fixed policy bounds, saturating telemetry counters, no allocation, no wall
clock reads, and no engine globals. Replaying the same observation sequence
therefore produces byte-identical state and output.

The production adapter supplies observations already maintained by the
client:

- the one-second smoothed RTT measurement;
- netchan cumulative packet/drop counters, normalized so inferred drops are not
  counted a second time in the loss denominator;
- commands queued since the most recent real transmission;
- outgoing packets not yet acknowledged;
- the negotiated/user-selected `rate` budget;
- `cl_maxpackets` as the nominal pacing choice; and
- `cl_packetdup` plus the existing batch-frame capacity as the redundancy
  boundary.

The controller samples at a fixed 100 ms policy interval. Loss is calculated
over monotonic counter windows and smoothed in Q8 fixed point. Sub-threshold
windows retain their counter baseline and accumulate until they contain enough
packets, so low-rate links do not remain permanently sample-starved. Counter
rollback and the 32-bit client clock wrapping/restarting are explicit rebaseline
results, not implicit unsigned underflow.

Client-state teardown resets the adapter and core before another server or map
can supply observations, so a numerically larger new netchan counter cannot
inherit loss, RTT, recovery-hold, or pacing state from the previous session.

## Policy and bounds

The V1 defaults intentionally prefer responsiveness while bounding added
traffic:

- nominal pacing remains the configured `cl_maxpackets` value;
- command queue, acknowledgement backlog, elevated loss, high RTT, or high
  jitter may raise pacing to a 75 packet/s pressure tier;
- critical command/ack backlog may raise pacing to a 90 packet/s tier;
- all adaptive pacing is capped at 125 packet/s;
- a configured `cl_maxpackets 0` remains unpaced and is never silently turned
  into a finite limiter;
- selective redundancy preserves the configured `cl_packetdup` baseline,
  raises it to at least one previous batch under ordinary protection and at
  least two under severe loss or critical backlog, and never exceeds the
  transport's existing maximum; this avoids treating downstream stability as
  proof that an asymmetric upstream needs no recovery;
- a low `rate` caps pacing and redundancy, while a critical rate budget caps
  pacing to 30 packet/s and disables duplicate frames; and
- protective increases are immediate, while recovery uses a one-second hold
  to avoid oscillation. Transport and rate caps still take effect immediately.

Cold start retains the configured `cl_packetdup` value until a sufficient loss
sample exists. A transport without batch redundancy reports that limitation in
the decision reasons and requests no duplicate frames.

## Client integration

`src/client/input.cpp` registers:

- `cl_adaptive_input 0`: archived opt-in switch; and
- `cl_adaptive_input_status`: prints the current pacing interval, redundancy,
  loss/RTT/jitter, queue and ack pressure, rate observation, reason names, and
  lifetime controller counters.

The adapter evaluates immediately before the existing batched-send readiness
check. Its selected interval replaces the static interval only while the
feature is enabled and a valid decision exists. The selected duplicate count
is assigned to the existing `Q2P_CLC_BATCH_MOVE` frame window; command encoding,
canonical command identity, packet acknowledgement, and all q2proto behavior
remain unchanged. Invalid core input fails locally to the legacy policy and
increments a visible integration-fallback counter.

The non-batched three-command legacy send path is not repurposed as a pacing
mechanism. Its historical fake-drop behavior would make skipped local send
opportunities appear as network loss, so the controller is suspended and its
decision is not applied on that path.

## Verification

Ordinary Meson networking targets were added:

- `adaptive_input_test` / `network-adaptive-input` covers cold start, held
  evaluations, stable-path baseline retention, loss tiers, queue pressure,
  rate caps, recovery hysteresis, unlimited pacing, no-batch behavior,
  counter/clock resets, invalid records, telemetry, and repeat determinism;
- `adaptive_input_layout_cpp_test` /
  `network-adaptive-input-layout-cpp` locks the V1 record sizes, important
  offsets, standard-layout/trivially-copyable properties, and representative
  pointer-free fields.

The client engine target exercises the live adapter and links the core through
`canonical_command_core`.

The combined Windows Clang integration pass produced the following evidence:

- the complete networking suite passed 67/67 tests;
- three consecutive networking-suite repetitions passed 201/201 test
  invocations;
- the runtime networking smoke target passed;
- the rewind acceptance matrix passed all 120 invocations;
- the cgame module, sgame module, dedicated engine, and client engine
  production targets all built and linked successfully, including the live
  adaptive-input adapter and core; and
- `.install/` was refreshed and validated for `windows-x86_64`: 16 root
  runtime files, one dependency, the `basew` runtime tree, a 308-asset
  `pak0.pkz`, botfile payload, and RmlUi payload all passed validation.

These results validate this default-off slice and its deterministic regression
coverage. They do not satisfy the open long-session, impairment, cross-platform,
or release-default gates, and the validated Windows staging tree is not by
itself packaged-release acceptance.

## Follow-up work

FR-10-T16 remains open for server-side rate feedback, impairment-matrix tuning
of the default thresholds, long-session evidence, and promotion policy.
Snapshot cadence, lineage, and keyframe recovery remain FR-10-T06 work.
FR-10-T14 should consume the V1 telemetry value record and correlate decisions
with correction magnitude, command age, snapshot stalls, and authoritative
consumed-command cursors.
