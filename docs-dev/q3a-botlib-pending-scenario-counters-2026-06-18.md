# Q3A BotLib pending scenario counters

Date: 2026-06-18

Task: DV-03-T05

This note specifies the smoke setup, status counters, and pass criteria needed to
promote the remaining scenario harness placeholders from `pending` to
`implemented`. It is a planning artifact only. It does not require or imply any
source edits in this change.

## Shared conventions

The four scenarios should continue to report through the existing
`q3a_bot_frame_command_status` line whenever practical. The scenario harness can
then promote a pending row by adding the smoke mode number, required metrics, and
`MetricCheck` entries to the catalog.

Recommended smoke mode allocation:

| Scenario | Smoke mode | Purpose |
| --- | ---: | --- |
| `engage_enemy` | `20` | Acquire a live enemy, decide to fire, press attack, and record damage. |
| `switch_weapons` | `21` | Prefer a better available weapon and complete a weapon switch. |
| `health_armor_pickup` | `22` | Prioritize and pick up health and armor when bot state needs them. |
| `team_objective` | `23` | Select and reach a CTF/team objective, with at least one objective pickup. |

Status counter names should stay lowercase `snake_case`. Counters that already
exist in bot action, combat, item, or route status structures can be surfaced
under the names below rather than duplicated. Counters that represent completion
of world events, such as damage, pickup, or flag pickup, should be incremented by
the game-side owner that observes the event and then copied into the smoke status
line.

The harness catalog can represent all four with the current check model if each
scenario emits numeric status metrics. `team_objective` may also need either a
scenario-level map override in the harness catalog or a server smoke mode that
performs its own deterministic CTF map transition.

## `engage_enemy`

### Smoke setup requirements

Add smoke mode `20` with a deterministic combat lane:

1. Start on a small map with reliable line of sight and AAS coverage.
2. Spawn an attacker bot and a target enemy client or bot on opposing teams.
3. Give the attacker a ready weapon and ammo. Blaster-only is acceptable if it
   can produce a reliable damage event; machinegun is preferred for short smoke
   timeouts.
4. Assign the target as the attacker's enemy, or configure acquisition so the
   attacker deterministically chooses the target during the smoke.
5. Place both entities so visibility and shootability checks should pass.
6. Run until the attacker has had enough frames to evaluate combat, apply an
   attack command, and inflict damage.

No route traversal is required for the first implementation. If the setup also
routes the attacker into range, route failures should remain zero.

### Required status counters

| Counter | Meaning |
| --- | --- |
| `combat_enemy_acquisitions` | A bot selected or accepted a live enemy. |
| `combat_enemy_visible` | The current enemy passed the visibility check. |
| `combat_enemy_shootable` | The current enemy passed the shootability or damage trace check. |
| `combat_fire_decisions` | Combat evaluation decided to fire. |
| `action_attack_decisions` | Action arbitration selected an attack intent. |
| `action_applied_attack_buttons` | The bot command had `BUTTON_ATTACK` applied. |
| `combat_damage_events` | The target took damage attributed to the attacking bot. |
| `last_combat_enemy_client` | Last enemy client index, or `-1` when none. |
| `last_combat_weapon_item` | Item id for the weapon used by the last fire decision. |
| `last_combat_damage` | Damage value from the last attributed hit. |

### Pass criteria

`engage_enemy` can be marked implemented when one smoke run satisfies:

| Check | Required value |
| --- | ---: |
| `pass` | `1` |
| `combat_enemy_acquisitions` | `>= 1` |
| `combat_enemy_visible` | `>= 1` |
| `combat_enemy_shootable` | `>= 1` |
| `combat_fire_decisions` | `>= 1` |
| `action_attack_decisions` | `>= 1` |
| `action_applied_attack_buttons` | `>= 1` |
| `combat_damage_events` | `>= 1` |
| `last_combat_enemy_client` | `>= 0` |
| `last_combat_damage` | `>= 1` |
| `route_failures` | `0` |

### Likely owner modules

- `src/server/main.c`: smoke mode `20` setup, bot/client spawning, deterministic
  placement, and status aggregation.
- `src/game/sgame/bots/bot_actions.cpp`: action arbitration and applied command
  counters.
- `src/game/sgame/bots/bot_combat.cpp`: enemy visibility, shootability, weapon,
  and fire-decision counters.
- `src/game/sgame/bots/bot_brain.cpp`: status line emission.
- `src/game/sgame/client/client_session_service_impl.cpp`,
  `src/game/sgame/gameplay/g_weapon.cpp`, and
  `src/game/sgame/gameplay/g_combat.cpp`: attack execution and attributed damage
  observation.

### Harness catalog representation

```python
Scenario(
    name="engage_enemy",
    status="implemented",
    task_ids=("DV-03-T05",),
    smoke_mode=20,
    required_metrics=(
        "combat_enemy_acquisitions",
        "combat_enemy_visible",
        "combat_enemy_shootable",
        "combat_fire_decisions",
        "action_applied_attack_buttons",
        "combat_damage_events",
    ),
    checks=(
        MetricCheck("pass", "eq", 1),
        MetricCheck("combat_enemy_acquisitions", "ge", 1),
        MetricCheck("combat_enemy_visible", "ge", 1),
        MetricCheck("combat_enemy_shootable", "ge", 1),
        MetricCheck("combat_fire_decisions", "ge", 1),
        MetricCheck("action_applied_attack_buttons", "ge", 1),
        MetricCheck("combat_damage_events", "ge", 1),
        MetricCheck("last_combat_enemy_client", "ge", 0),
        MetricCheck("last_combat_damage", "ge", 1),
        MetricCheck("route_failures", "eq", 0),
    ),
)
```

## `switch_weapons`

### Smoke setup requirements

Add smoke mode `21` with a forced preferred-weapon choice:

1. Spawn one bot and one live enemy or dummy enemy so combat evaluation runs.
2. Give the bot a current low-priority weapon, for example blaster.
3. Give the bot a preferred higher-priority weapon plus ammo, for example
   machinegun.
4. Set the bot profile, userinfo, or smoke setup so the preferred weapon is the
   expected weapon for the run.
5. Run long enough for action arbitration to request a switch and for the weapon
   system to complete it.

The smoke mode should emit both the expected weapon item id and the actual final
weapon item id. That keeps the harness from hardcoding game item constants.

### Required status counters

| Counter | Meaning |
| --- | --- |
| `combat_weapon_switch_decisions` | Combat evaluation chose to switch to a preferred weapon. |
| `action_weapon_switch_decisions` | Action arbitration selected a switch intent. |
| `action_pending_weapon_switches` | Bot action application requested or latched a pending switch. |
| `weapon_switch_requests` | Weapon switch requests submitted by bot command/action code. |
| `weapon_switch_completions` | Weapon system completed the requested switch. |
| `weapon_switch_failures` | Requested switch could not be completed. |
| `weapon_switch_expected_item` | Expected item id configured by the smoke. |
| `weapon_switch_actual_item` | Final active weapon item id. |
| `weapon_switch_expected_match` | `1` when actual final item matches expected item. |

### Pass criteria

`switch_weapons` can be marked implemented when one smoke run satisfies:

| Check | Required value |
| --- | ---: |
| `pass` | `1` |
| `combat_weapon_switch_decisions` | `>= 1` |
| `action_weapon_switch_decisions` | `>= 1` |
| `action_pending_weapon_switches` | `>= 1` |
| `weapon_switch_requests` | `>= 1` |
| `weapon_switch_completions` | `>= 1` |
| `weapon_switch_failures` | `0` |
| `weapon_switch_expected_item` | `>= 1` |
| `weapon_switch_actual_item` | `>= 1` |
| `weapon_switch_expected_match` | `1` |

### Likely owner modules

- `src/server/main.c`: smoke mode `21` setup, inventory/weapon seeding, enemy
  setup, and status aggregation.
- `src/game/sgame/bots/bot_combat.cpp`: preferred weapon decision counters.
- `src/game/sgame/bots/bot_actions.cpp`: switch intent and pending switch
  counters.
- `src/game/sgame/bots/bot_brain.cpp`: status line emission.
- `src/game/sgame/player/p_weapon.cpp`: actual pending weapon and completed
  weapon switch tracking.

### Harness catalog representation

```python
Scenario(
    name="switch_weapons",
    status="implemented",
    task_ids=("DV-03-T05",),
    smoke_mode=21,
    required_metrics=(
        "combat_weapon_switch_decisions",
        "action_pending_weapon_switches",
        "weapon_switch_completions",
        "weapon_switch_expected_match",
    ),
    checks=(
        MetricCheck("pass", "eq", 1),
        MetricCheck("combat_weapon_switch_decisions", "ge", 1),
        MetricCheck("action_weapon_switch_decisions", "ge", 1),
        MetricCheck("action_pending_weapon_switches", "ge", 1),
        MetricCheck("weapon_switch_requests", "ge", 1),
        MetricCheck("weapon_switch_completions", "ge", 1),
        MetricCheck("weapon_switch_failures", "eq", 0),
        MetricCheck("weapon_switch_expected_match", "eq", 1),
    ),
)
```

## `health_armor_pickup`

### Smoke setup requirements

Add smoke mode `22` with deterministic health and armor pickup phases:

1. Spawn one low-health bot near a reachable health item and one low-armor bot
   near a reachable armor item, or run those phases sequentially with one bot.
2. Force health below the low-health threshold used by bot item scoring.
3. Force armor below the low-armor threshold while keeping the bot alive.
4. Ensure each target pickup is reachable through the current AAS route setup.
5. Run until both pickup events have completed or the smoke timeout is reached.

Using two bots keeps the health and armor outcomes independent and makes the
status line easier for the harness to evaluate in one run.

### Required status counters

| Counter | Meaning |
| --- | --- |
| `item_low_health_boosts` | Item scoring boosted a candidate because the bot had low health. |
| `item_low_armor_boosts` | Item scoring boosted a candidate because the bot had low armor. |
| `item_health_goal_assignments` | A health item was selected as a bot item goal. |
| `item_armor_goal_assignments` | An armor item was selected as a bot item goal. |
| `item_health_pickups` | A bot picked up a health item during the smoke. |
| `item_armor_pickups` | A bot picked up an armor item during the smoke. |
| `last_health_before` | Health before the last health pickup. |
| `last_health_after` | Health after the last health pickup. |
| `last_health_pickup_delta` | Positive health delta from the last pickup. |
| `last_armor_before` | Armor before the last armor pickup. |
| `last_armor_after` | Armor after the last armor pickup. |
| `last_armor_pickup_delta` | Positive armor delta from the last pickup. |

### Pass criteria

`health_armor_pickup` can be marked implemented when one smoke run satisfies:

| Check | Required value |
| --- | ---: |
| `pass` | `1` |
| `item_low_health_boosts` | `>= 1` |
| `item_low_armor_boosts` | `>= 1` |
| `item_health_goal_assignments` | `>= 1` |
| `item_armor_goal_assignments` | `>= 1` |
| `item_health_pickups` | `>= 1` |
| `item_armor_pickups` | `>= 1` |
| `last_health_pickup_delta` | `>= 1` |
| `last_armor_pickup_delta` | `>= 1` |
| `route_failures` | `0` |

### Likely owner modules

- `src/server/main.c`: smoke mode `22` setup, health/armor seeding, target item
  selection, and status aggregation.
- `src/game/sgame/bots/bot_items.cpp`: low health/armor scoring counters.
- `src/game/sgame/bots/bot_nav.cpp` and `bot_nav.hpp`: item goal assignment
  counters split by item class.
- `src/game/sgame/bots/bot_brain.cpp`: status line emission.
- `src/game/sgame/gameplay/g_items.cpp` and `g_item_list.cpp`: pickup event and
  pickup delta observation.

### Harness catalog representation

```python
Scenario(
    name="health_armor_pickup",
    status="implemented",
    task_ids=("DV-03-T05",),
    smoke_mode=22,
    required_metrics=(
        "item_low_health_boosts",
        "item_low_armor_boosts",
        "item_health_pickups",
        "item_armor_pickups",
    ),
    checks=(
        MetricCheck("pass", "eq", 1),
        MetricCheck("item_low_health_boosts", "ge", 1),
        MetricCheck("item_low_armor_boosts", "ge", 1),
        MetricCheck("item_health_goal_assignments", "ge", 1),
        MetricCheck("item_armor_goal_assignments", "ge", 1),
        MetricCheck("item_health_pickups", "ge", 1),
        MetricCheck("item_armor_pickups", "ge", 1),
        MetricCheck("last_health_pickup_delta", "ge", 1),
        MetricCheck("last_armor_pickup_delta", "ge", 1),
        MetricCheck("route_failures", "eq", 0),
    ),
)
```

## `team_objective`

### Smoke setup requirements

Add smoke mode `23` with a deterministic CTF objective:

1. Run on a packaged CTF map with AAS coverage, or have the smoke mode perform a
   deterministic map transition to such a map before spawning bots.
2. Enable the CTF game mode and spawn at least one red bot and one blue bot.
3. Assign the active smoke bot an attacker role and a reachable enemy flag
   objective.
4. Ensure the enemy flag exists, is reachable, and starts at base.
5. Run until the bot selects the objective, issues route commands, reaches the
   objective, and picks up the enemy flag.

Full flag capture is a good later extension, but the first implemented scenario
should treat enemy flag pickup as the required team-objective completion. That
keeps the pass criteria deterministic while still proving objective selection,
route command production, and game-side objective interaction.

The objective type counter should use stable numeric values:

| Value | Objective type |
| ---: | --- |
| `1` | Enemy flag pickup. |
| `2` | Own flag return. |
| `3` | Neutral flag pickup. |
| `4` | Base defense. |

### Required status counters

| Counter | Meaning |
| --- | --- |
| `team_objective_evaluations` | Team objective policy evaluated at least one bot. |
| `team_objective_assignments` | A bot received a team objective assignment. |
| `team_objective_route_requests` | The objective requested route planning. |
| `team_objective_route_commands` | The bot received movement commands for the objective route. |
| `team_objective_reaches` | The bot reached the objective entity or area. |
| `team_objective_flag_pickups` | A bot picked up the enemy or neutral flag during the smoke. |
| `team_objective_flag_captures` | A bot captured a flag during the smoke, optional for the first pass gate. |
| `team_objective_role_attacker` | Number of attacker-role assignments. |
| `team_objective_role_defender` | Number of defender-role assignments, optional for this scenario. |
| `last_team_objective_type` | Objective type enum from the table above. |
| `last_team_objective_client` | Client index for the bot with the latest objective event. |
| `last_team_objective_team` | Team id for the bot with the latest objective event. |
| `last_team_objective_item` | Item id for the objective item, such as the enemy flag. |
| `last_team_objective_area` | AAS area for the latest objective goal. |

### Pass criteria

`team_objective` can be marked implemented when one smoke run satisfies:

| Check | Required value |
| --- | ---: |
| `pass` | `1` |
| `team_objective_evaluations` | `>= 1` |
| `team_objective_assignments` | `>= 1` |
| `team_objective_route_requests` | `>= 1` |
| `team_objective_route_commands` | `>= 1` |
| `team_objective_reaches` | `>= 1` |
| `team_objective_flag_pickups` | `>= 1` |
| `last_team_objective_type` | `1` |
| `last_team_objective_client` | `>= 0` |
| `last_team_objective_item` | `>= 1` |
| `route_failures` | `0` |

`team_objective_flag_captures >= 1` should be added as a stricter follow-up gate
after return-to-base routing and capture timing are stable enough for a short
smoke timeout.

### Likely owner modules

- `src/server/main.c`: smoke mode `23`, CTF map/mode setup, team bot spawning,
  role seeding, and status aggregation.
- `src/game/sgame/bots/bot_brain.cpp`: objective status emission and frame
  command routing.
- `src/game/sgame/bots/bot_nav.cpp` and `bot_nav.hpp`: objective route request
  and route command counters.
- Existing bot team policy code under `src/game/sgame/bots/`: role/objective
  assignment and high-level team decision counters.
- `src/game/sgame/gameplay/g_capture.cpp`: flag pickup and later flag capture
  event observation.

### Harness catalog representation

If the server smoke mode handles its own map transition, the current catalog
shape can represent the scenario directly:

```python
Scenario(
    name="team_objective",
    status="implemented",
    task_ids=("DV-03-T05",),
    smoke_mode=23,
    required_metrics=(
        "team_objective_assignments",
        "team_objective_route_commands",
        "team_objective_flag_pickups",
    ),
    checks=(
        MetricCheck("pass", "eq", 1),
        MetricCheck("team_objective_evaluations", "ge", 1),
        MetricCheck("team_objective_assignments", "ge", 1),
        MetricCheck("team_objective_route_requests", "ge", 1),
        MetricCheck("team_objective_route_commands", "ge", 1),
        MetricCheck("team_objective_reaches", "ge", 1),
        MetricCheck("team_objective_flag_pickups", "ge", 1),
        MetricCheck("last_team_objective_type", "eq", 1),
        MetricCheck("last_team_objective_client", "ge", 0),
        MetricCheck("last_team_objective_item", "ge", 1),
        MetricCheck("route_failures", "eq", 0),
    ),
)
```

If the smoke mode must run on a specific CTF map chosen by the harness, add a
small catalog field such as `map_name="q2ctf1"` or `map_name="<ctf-smoke-map>"`
before promoting the scenario.

## Harness pending rows before promotion

Until modes `20` through `23` and their counters exist in the game smoke output,
the harness catalog should keep these rows as pending with blockers similar to:

```python
PendingScenario(
    name="engage_enemy",
    task_ids=("DV-03-T05",),
    smoke_mode=20,
    required_metrics=("combat_damage_events", "action_applied_attack_buttons"),
    blockers=("needs mode 20 combat setup and combat/damage status counters",),
)
```

The same pattern applies to:

| Scenario | Pending blocker |
| --- | --- |
| `switch_weapons` | Needs mode `21` weapon setup and switch completion counters. |
| `health_armor_pickup` | Needs mode `22` low-health/low-armor setup and pickup delta counters. |
| `team_objective` | Needs mode `23` CTF setup, objective route counters, and flag pickup counters. |

Once the source-side smoke modes land, the scenario-tools owner can promote each
row by changing only the harness catalog and validation docs.
