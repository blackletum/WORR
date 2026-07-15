# Cgame prediction fail-closed recovery

Date: 2026-07-15

Project task: `FR-10-T08`

Status: implemented movement-prediction safety and observability increment;
not a weapon, event, protocol, or presentation cutover.

## Outcome

The live cgame movement predictor now treats all local prediction-invariant
failures as authoritative hard-resync conditions. In addition to the existing
immutable input-range resolver failures, these in-engine cases now clear the
entire retained movement-prediction ring and reset to the current authoritative
player state:

- the consumed authoritative command has no matching retained predicted state;
- the retained command was simulated under a different movement configuration;
  or
- the shared prediction step rejects a replayed or pending local command.

Before this increment, those latter cases cleared only the visible prediction
error or stopped replay. A stale sequence, origin, hash, or partially replayed
entry could therefore remain retained until a later frame overwrote it. The
recovery routine now clears command sequences, prediction states, origins,
state/collision/config/replay-chain hashes, error smoothing, step state, and
ground-contact cache before restoring the current authoritative state.

## Classified correction telemetry

`cg_snapshot_timeline` now retains and reports a machine-readable last
correction reason. `cg_net` debug output distinguishes:

- `input_range_invalid` for malformed, discontinuous, ambiguous, missing, or
  exhausted immutable input ranges;
- `retained_state_missing` when the authoritative consumed command no longer
  has a valid local predicted state;
- `config_discontinuity` when the stored prediction used a different movement
  configuration;
- `replay_rejected` when the deterministic shared movement step rejects a
  locally resolved command;
- `state_divergence` for a bounded visual correction; and
- `correction_threshold_exceeded` when positional divergence exceeds the
  existing immediate-authority threshold.

The existing `hard_reset_count` remains available, so operators can separate
bounded visual correction from fail-closed recovery without inferring a cause
from a generic counter. The new values are cgame-internal telemetry and do not
change the network wire, snapshot ABI, server authority, or demo format.

## Implementation

- `inc/shared/cgame_prediction.h` appends named result codes for replay
  rejection, missing retained state, and movement-configuration discontinuity.
- `src/game/cgame/cg_predict.cpp` routes each new invariant failure through
  `CG_PredictionHardResync` and clears every ring member during that resync.
- `src/game/cgame/cg_snapshot_timeline.hpp` and `.cpp` retain the correction
  reason and include it in the rate-limited `cg_net` diagnostic line.
- `tools/networking/test_cgame_prediction_fail_closed_contract.py` locks the
  live wiring: all ring members must clear, each invariant failure must invoke
  the hard-resync routine, both replay paths must recover, and all diagnostic
  reasons must remain named.

The contract test complements rather than replaces executable behavior tests:
the existing prediction-input range test compiles and exercises the appended
result values, while the cgame DLL build compiles the real engine integration.

## Validation

Focused Windows x86-64 validation passed:

```text
ninja -C builddir-win cgame_x86_64.dll

meson test -C builddir-win --no-rebuild --print-errorlogs \
  network-cgame-prediction-fail-closed-contract \
  network-prediction-input-range

2/2 passed; 0 failed
```

No interactive client was launched. The two focused tests execute only the
standalone networking test binaries and Python integration contract; the cgame
module was compiled but not launched.

## Remaining promotion gates

This does not complete `FR-10-T08`. It leaves open the full Rerelease
weapon/action catalogue adapter, command-time ownership for currently
frame-driven weapon advances, live cgame/sgame shadow transactions, predicted
audio/effect suppression, correction budgets, impairment-matrix evidence, and
the final cgame/sgame promotion decision. It also does not change the separate
`FR-10-T09` canonical command-identity or `FR-10-T16` pacing/batching scope.
