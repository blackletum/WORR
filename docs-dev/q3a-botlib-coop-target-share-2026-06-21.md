# Q3A BotLib Coop Target Sharing Round

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round adds a default-off cooperative target-sharing bridge for bots that
are acting in support/follow/wait/regroup coop contexts. The feature is gated by
`sg_bot_coop_target_share` and gives a support bot a controlled way to adopt a
teammate's current hostile target when it has no current enemy of its own.

The implementation is WORR-native. No Q3A, Gladiator, BSPC, idTech3, or
`q2proto/` source files were imported or modified.

## Runtime Changes

- `bot_brain.*` now treats hostile non-client `SVF_MONSTER` entities as valid
  combat targets for blackboard perception when coop target sharing is enabled.
- The blackboard target-sharing path evaluates the existing coop policy with a
  support role and only scans teammate blackboards when that policy supports
  follow, wait, regroup, or support-combat behavior.
- A support bot can adopt a teammate's current enemy when the teammate is alive,
  close enough, has a valid current target memory, and the shared target is
  still alive and damageable.
- Compact coop-command status now exposes `coop_target_share_*` and
  `last_coop_target_share_*` fields for requests, policy support, source scans,
  source candidates, adoptions, invalid skips, selected client, source client,
  target entity/client, target distance, and coop intent.
- Server smoke mode `30` enables `sg_bot_coop_target_share`, runs a two-bot coop
  proof, and resets the cvar with the other scenario-only coop bridges.
- The mode `30` proof creates a lightweight smoke target flagged as
  `SVF_MONSTER`. The target is proof-only and gets a positive `spawn_count`
  before blackboard seeding so stale-memory guards can distinguish it from an
  uninitialized entity.

## Scenario Coverage

`tools/bot_scenarios/run_bot_scenarios.py` now has an implemented
`coop_target_share` scenario row over mode `30`. The scenario requires:

- coop readiness with two bots and non-deathmatch match state,
- `target_share=1` in the scenario begin marker,
- at least one target-share request, source candidate, and adoption,
- valid source-client and target-entity evidence, and
- support-combat intent evidence from the coop policy.

The scenario harness optional-field discovery recognizes the new
`coop_target_share_counters` family, and the README records mode `30` as the
next promoted scenario mode.

## Validation

- `python -m py_compile tools/bot_scenarios/run_bot_scenarios.py tools/bot_scenarios/test_run_bot_scenarios.py` passed.
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios` passed 32 tests.
- `meson compile -C builddir-win` passed.
- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --package-q2aas-aas` passed.
- Focused `coop_target_share` scenario passed from refreshed `.install` with 1 passed, 0 failed.
- Full implemented scenario suite passed from refreshed `.install` with 22 passed, 0 failed, 0 timeout, 0 error, and 0 pending.

## Follow-Up

This round proves target sharing through blackboard adoption and a deterministic
monster target lane. It does not claim full campaign monster tactics,
multi-monster focus fire, route-to-teammate combat positioning, or
door/elevator/trigger coordination. Those remain deeper coop behavior work.
