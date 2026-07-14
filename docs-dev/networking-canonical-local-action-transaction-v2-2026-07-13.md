# Canonical local-action transaction v2

Date: 2026-07-13
Project tasks: `FR-10-T08`, `FR-10-T09`
Status: shared-core implementation complete; live cgame/sgame authority cutover
is deliberately not included in this slice.

## Outcome

WORR now has one transport-neutral, pointer-free transaction model for a
predictable local weapon/action step. The predictor and authority can feed the
same prior state, canonical command, semantic intent, and weapon rules into the
same C11 implementation and obtain identical gameplay state, event keys,
event payloads, and semantic hashes.

This is an idTech3-inspired separation of shared deterministic gameplay from
client presentation, adapted to WORR's existing canonical command and event
ABIs rather than a port of Quake 3 weapon code. It does not create another
command identity, event identity, snapshot schema, or wire protocol.

The implementation is intentionally a shadow-ready foundation. Legacy
`p_weapon`/cgame weapon behavior remains authoritative until both modules can
provide faithful adapters for the complete Rerelease weapon catalog and prove
parity. Cutting only one module over would create two competing weapon
simulations, so that unsafe partial integration was not performed.

## Files

- `inc/shared/local_action_abi.h` defines the pointer-free v2 ABI and public
  validators/builders.
- `src/common/net/local_action.c` implements the deterministic transaction,
  canonical-event adapter, fieldwise hashes, and correction classifier.
- `tools/networking/local_action_test.c` provides hostile validation,
  transition, event-correlation, correction, wrap, and replay-parity coverage.
- `tools/networking/local_action_layout_c.c` and
  `tools/networking/local_action_layout_cpp.cpp` freeze the C/C++ layout and
  module-safety assumptions.
- `meson.build` builds the isolated `canonical_local_action_core` static
  library and registers its three networking tests.

This local-action slice does not modify `q2proto/`, server/client
snapshot-shadow code, either networking roadmap, or any legacy packet/demo
representation.

## ABI and ownership

All records use fixed-width scalar fields, embed no pointers, own no memory,
and are valid across the engine/cgame/sgame DLL boundary.

| Record | Size | Role |
|---|---:|---|
| `worr_local_action_state_v2` | 72 bytes | Predictable weapon, ammo, phase, latch, sequence, presentation-frame, command-cursor, and simulation-time state |
| `worr_local_action_weapon_rule_v2` | 64 bytes | Validated weapon timing, ammo, frame, and predictable audiovisual policy; weapon ID zero is the one canonical absent rule |
| `worr_local_action_intent_v2` | 24 bytes | Semantic attack-held and switch request input, intentionally independent of legacy button bit assignments |
| `worr_local_action_event_v2` | 48 bytes | Command-correlated gameplay/audio/effect emitter output |
| `worr_local_action_transaction_v2` | 1,216 bytes | Immutable before/input/rules/after/events/hashes proof for one command |
| `worr_local_action_event_record_context_v2` | 48 bytes | Explicit predicted or authoritative adapter context for the canonical event journal |
| `worr_local_action_correction_v2` | 64 bytes | Correction class, exact difference mask, cursor pair, bounded deltas, and both transaction hashes |

The model revision is independent of the ABI version. A gameplay behavior
change therefore advances the model revision instead of silently changing the
meaning of an existing transaction layout.

## Deterministic command transaction

`Worr_LocalActionBuildTransactionV2` accepts exactly one
`worr_command_record_v1`. It requires that command ID to be the wrap-safe next
ID after the state's `applied_cursor`, and requires:

`command.sample_time_us == state.sample_time_us + duration_ms * 1000`

The addition is overflow checked. Packet sequence, packet acknowledgement,
retry number, frame number, and wall time are absent from the transition.

The transition first advances an existing raising, firing, or lowering phase
through the command interval, including bounded zero-duration normalization.
It then applies the command-end semantic switch/attack intent. This explicit
ordering gives client and server one answer at every canonical command
boundary. One fire action may be emitted by one command; automatic weapons can
emit again when a later command advances the refire phase to ready while the
trigger remains held. Semi-automatic weapons require a release edge. Dry fire
is latched for one hold and re-arms on release.

Switches are a deterministic lower/commit/raise/ready state machine. A switch
can target the holstered state, and zero-time rules normalize in the same
transaction without exposing an invalid transient state. A switch already in
the lowering phase cannot be retargeted implicitly; its pending rule and ammo
snapshot must remain exact.

The transaction records `shot_sequence` separately from `action_sequence`.
Fire increments both; dry fire and switch begin increment only the action
sequence. Sequence exhaustion, event-capacity exhaustion, malformed rules,
rule/state disagreement, and every command/time invariant failure reject the
whole transaction.

## Predicted and authoritative event identity

Every semantic emitter gets a monotonically increasing ordinal within its
command. Its gameplay lane is mandatory; audio and effect lanes are emitted
only when the validated weapon rule declares them predictable. All lanes for
one emitter use:

```text
prediction key = {
    command_epoch:    command.command_id.epoch,
    command_sequence: command.command_id.sequence,
    emitter_ordinal:  semantic emitter ordinal,
    lane:             gameplay | audio | effect
}
```

Producer role is provenance only. It does not change state, events, or hashes.
Predicted and authoritative transactions built from equal semantic inputs
therefore correlate without packet-derived guesses.

`Worr_LocalActionBuildEventRecordV2` is the explicit bridge to the existing
canonical event ABI. Predicted records must be ID-less candidates;
authoritative records must carry a non-zero authority event ID. Both retain the
same prediction key and semantic payload. Existing `ESM1` semantic hashing and
equality ignore only the authority allocation, so predicted and authoritative
records compare equal while the normal authoritative validator remains strict.

Gameplay lanes use `GAMEPLAY_CUE/U32X4`, audio lanes use
`AUDIO_CUE/AUDIO`, and effect lanes use `VISUAL_EFFECT/EFFECT`. Adapter context
owns source tick, bounded transient lifetime, and generation-checked entity
references; none is inferred from a packet or mutable module pointer.

## Correction policy

`Worr_LocalActionCorrectionClassifyV2` accepts only a validated predicted
transaction followed by a validated authoritative transaction. It returns one
of four policies:

| Class | Meaning | Required response |
|---|---|---|
| `NONE` | State and emitted semantics agree | Keep prediction |
| `PRESENTATION_ONLY` | Only the presentation frame differs | Adopt authority and permit presentation smoothing |
| `GAMEPLAY_IMMEDIATE` | Weapon, phase, timer, ammo, latch, shot/action sequence, event count, or event payload differs | Apply authoritative gameplay immediately; do not smooth gameplay state |
| `HARD_RESYNC` | Command identity, sample time, or prediction-key invariant differs | Abandon the affected replay chain and restart from authority |

The result includes exact difference bits plus saturated signed ammo/timer
deltas for instrumentation. Classification never trusts a caller-supplied
transaction hash: both input transactions are fully re-executed and compared
against their stored bytes before classification.

## Fail-closed properties

- All schemas, model revisions, enums, flag masks, reserved fields, ranges,
  cursor identities, and canonical nested records are validated.
- Transient phases cannot escape with zero or over-limit timers.
- The absent weapon rule has exactly one byte representation.
- Output buffers for transactions, event records, and corrections must be
  completely zero. A stale or partially reused output is rejected.
- Public builders construct into local scratch and copy only after complete
  validation. Every failure leaves the caller's output byte-identical.
- Transaction validation deterministically rebuilds the transaction and
  compares all bytes, including unused event capacity, eliminating accepted
  stale tails or padding-dependent proofs.
- State, event, and transaction hashes are fieldwise FNV-1a domain hashes.
  Producer role is excluded; transport and authority allocation never enter
  semantic identity.

## Test evidence

The focused Meson build and tests passed on Windows x86-64 with Clang 20.1.7:

```text
meson compile -C builddir-win \
  local_action_test local_action_layout_c_test local_action_layout_cpp_test

meson test -C builddir-win --no-rebuild --print-errorlogs \
  network-local-action-v2 \
  network-local-action-layout-c \
  network-local-action-layout-cpp

3/3 passed
```

The behavior suite covers:

- strict state/rule/intent validation and byte-atomic failure;
- semi-automatic trigger edges, automatic hold, refire timing, ammo use, dry
  latch/re-arm, and shot/action exhaustion;
- timed switch, holster, equip, and zero-duration phase normalization;
- exact command successor, sequence-to-epoch wrap, final epoch exhaustion,
  sample-time mismatch/overflow, nested command rejection, stale tail, and hash
  corruption;
- gameplay/audio/effect lane ordinals and predicted/authority event-record
  semantic equality, including wrap-safe expiry;
- none, presentation-only, immediate-gameplay, and hard-resync corrections;
- 4,096 pseudo-random command steps, starting near sequence wrap, built once as
  prediction and once as authority at every step and replayed twice to prove
  stable state, events, and transaction digest.

The six directly related pre-existing command/event tests also passed:

```text
network-command-stream
network-command-schema-layout-c
network-command-schema-layout-cpp
network-event-journal
network-event-schema-layout-c
network-event-schema-layout-cpp
```

## Remaining live cutover

This slice does not claim `FR-10-T08` or `FR-10-T09` complete. The following
work remains before the shared core can become authoritative:

1. Define a bgame-owned catalog adapter for every Rerelease weapon, including
   inventory/ammo semantics, multi-shot and charge behavior, animation timing,
   projectiles, muzzle/effect assets, powerups, death/respawn, and weapon-wheel
   selection. The adapter must produce the semantic intent and immutable rule
   records above without retaining engine pointers.
2. Retain local-action state/transactions beside cgame's canonical command
   history, replay them from the exact consumed cursor, and route predicted
   canonical events through the existing deduplication/presentation path.
3. Run the same core in sgame at the canonical `ClientThink` command boundary,
   initially shadowing current weapon results. Export authoritative state and
   event keys without making snapshot or packet ownership part of the weapon
   model.
4. Add per-weapon deterministic golden scenarios and impaired-link parity
   telemetry. Promote only after state/event divergence is zero for the agreed
   catalog and hard-resync/correction budgets pass.
5. Cut authority over as one planned project task, then remove only the legacy
   paths made redundant by proven parity. Until that gate, current cgame/sgame
   behavior remains the source of gameplay truth.

The new core is therefore an explicit, testable convergence target—not a
second live weapon system.
