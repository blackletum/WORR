# Q3A BotLib Roam, Retreat, And Combat Scrutiny - 2026-06-27

Task context: `FR-04-T03`, `FR-04-T04`, `FR-04-T15`, and `DV-03-T05`.

## Changes

- Combat context now carries the bot's current health and armor into
  `bot_combat`. This lets combat decisions compare the bot's own strength
  against the estimated enemy stack instead of treating every visible target as
  equally worth fighting.
- Added `BotCombat_ShouldAvoidWeakUnderpoweredFight()`. It withholds fire when
  the bot is weak, the enemy estimate is stacked, and the bot is stuck on an
  underpowered weapon such as the blaster. A pending or selected better weapon
  switch is allowed through, so the bot can still upgrade before re-engaging.
- Increased the enemy-estimate underpowered weapon penalty so low-tier weapons
  are less likely to look acceptable against armored, durable enemies.
- Role-combat bridges now defer when the base action layer is trying to switch
  weapons, and they also honor the weak/underpowered combat check. This prevents
  FFA/TDM/CTF role combat from recreating an attack decision with the current
  weapon after the richer combat scorer already decided to abstain or switch.
- Close-front spacing now has its own short duration and cooldown, separate
  from low-health threat retreat. Low-health retreat can preempt an active
  healthy spacing route, while healthy close-front spacing no longer inherits
  the long survival-retreat cooldown.
- The low-health retreat threshold is aligned with the item survival threshold
  at 45 health, making retreat and pickup pressure agree about when the bot is
  genuinely weak.
- The weapon-scoring scenario harness now supports marker `in` checks, used to
  accept either `melee` or `close` for close-quarters super-shotgun selection.
  The scenario still hard-gates the selected weapon, finisher estimate, splash
  safety pressure, and switch dispatch.

## Validation

- `meson compile -C builddir-win sgame_x86_64`
- `python tools\refresh_install.py --build-dir builddir-win`
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario ffa_roam_route,ffa_live_pacing,duel_live_pacing,weapon_scoring_arsenal,aim_fire_policy_depth,combat_survival_regression,combat_survival_regression_q2dm2,threat_retreat_avoidance,threat_retreat_avoidance_q2dm8 --timeout 120 --base-port 28440 --format text --json-out .tmp\bot_scenarios\bot_roam_retreat_combat_scrutiny_final.json --markdown-out .tmp\bot_scenarios\bot_roam_retreat_combat_scrutiny_final.md`
- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`
- `python tools\bot_scenarios\test_run_bot_scenarios.py`
