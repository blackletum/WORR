# FR-10-T07 live cgame canonical render promotion

Date: 2026-07-13  
Project task: `FR-10-T07`  
Related tasks: `FR-10-T05`, `FR-10-T06`

## Purpose

This change connects the copied canonical snapshot timeline to the production
cgame render frame. It is a progressive promotion seam: the established
`centity_t` renderer remains the default, a shadow-audit mode compares the two
timelines, and an explicit opt-in mode promotes only individually proven
canonical transforms.

The change deliberately does not claim completion of `FR-10-T07`. Canonical
event presentation, previous-only entity visibility, dynamic timeline arena
sizing, and broad live impairment evidence remain promotion prerequisites.

## Live frame path

`CL_AddEntities` now brackets every rendered frame with a canonical timeline
begin/end operation. When the engine has attached and populated the cgame
snapshot consumer, the begin operation:

1. Reads the value-only canonical timeline diagnostics without borrowing its
   storage.
2. Advances its deterministic clock using the same monotonic
   `cls.realtime * 1000` host-time domain used when snapshots are delivered.
3. Maps `sv_paused` transitions to the timeline's explicit pause/resume clock
   operations.
4. In audit or promotion mode, derives the desired render timestamp from the
   exact canonical times on `cl.oldframe` and `cl.frame` plus the legacy
   interpolation fraction. This avoids assuming that a server frame number is
   a simulation tick or that tick duration has always been constant.
5. Selects one immutable canonical pair with extrapolation disabled. The pair
   is then reused for every remote entity in that cgame frame.

The local controlled player continues through the established prediction path.
This slice only promotes remote entity transforms.

## Per-entity promotion gate

For every remote entity already selected by the legacy visibility path, cgame
requests a value-copy sample from the canonical pair. The sample is rejected
locally, without disturbing the frame, when it is missing, invisible, stale,
generation-discontinuous, component-incompatible, motion-blocked, or otherwise
unsafe.

The remaining candidate is compared against legacy interpolation for:

- origin;
- shortest-path Euler angles; and
- beam `old_origin` when that endpoint affects presentation.

Only a candidate within the configured epsilon can become authoritative in
promotion mode. All other entities retain their legacy transform for that
frame. Model-change, teleport, generation, missing-state, and timing failures
therefore degrade to the existing renderer independently instead of blanking
or corrupting an entire scene.

This parity gate is intentionally strict. It establishes a measurable and
reversible bridge before the later timeline policy is allowed to improve over
legacy interpolation under jitter.

## Cvars

`cg_snapshot_timeline_render` is non-archived and accepts:

- `0`: legacy transform authority; the canonical clock still advances when a
  consumer is active (default).
- `1`: select and sample the canonical pair, measure parity, and keep legacy
  transform authority.
- `2`: promote parity-proven canonical remote transforms; fall back per entity.

`cg_snapshot_timeline_render_epsilon` is the non-archived maximum component or
angular difference accepted by the promotion gate. It defaults to `0.125` and
is clamped to `0.0001..8.0`.

Audit and promotion modes emit one aggregate line per second. The line includes
clock/pair successes and failures, alignment failures, pair mode/block flags,
sample failures and discontinuities, parity matches/mismatches, promoted
transform count, and maximum origin/beam-end/angle errors for the active
snapshot epoch. No per-entity log spam is generated.

## Safety and compatibility

- No wire format or `q2proto/` source changed.
- Legacy servers and demos continue to build the canonical view through the
  existing legacy projector.
- The default presentation path is unchanged.
- No canonical pointer survives a call; pair refs remain generation checked
  and entity samples are copied values.
- The timeline clock advances even when transform auditing is disabled, so a
  future event or render consumer does not inherit a dormant clock.
- Promotion never applies to the predicted local player.
- Extrapolation is disabled in this parity stage.

## Verification

The Windows cgame production DLL was rebuilt from `builddir-win` with:

```text
ninja -C builddir-win cgame_x86_64.dll
```

Result: successful compile and link of `cgame_x86_64.dll`, including the
modified entity API and live entity renderer. Ninja reported the repository's
pre-existing recoverable `premature end of file` warning before completing all
99 requested steps.

## Remaining FR-10-T07 work

- Store canonical records in a presentation-owned event queue and replace the
  raw legacy presenter only after ordered/deduplicated parity is proven.
- Represent and render previous-only entities when the selected canonical time
  precedes a removal snapshot; the current loop still starts from the legacy
  current-frame visibility set.
- Add bounded extrapolation under an explicit production policy and impairment
  thresholds after zero-mismatch shadow evidence.
- Exercise pause, demo seek/rate, packet loss, base invalidation, entity reuse,
  teleport/model changes, and variable server rates in a live automated
  matrix.
- Remove the audit-stage 512-entity and 32-area-byte cgame arena limits through
  negotiated/dynamic sizing before canonical rendering can be enabled by
  default.

