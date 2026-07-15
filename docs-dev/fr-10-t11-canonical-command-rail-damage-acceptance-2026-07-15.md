# FR-10-T11 canonical-command rail-damage acceptance seam

Task references: `FR-10-T10`, `FR-10-T11`, `FR-10-T14`, and `FR-10-T15`.

## Purpose

The existing rail diagnostics could prove a rewind decision or invoke a legacy
server-side weapon path. They could not prove that a real client command was
decoded by the WORR sideband, admitted into an active authoritative command
scope, processed by `ClientThink`, and then fired by the ordinary Railgun
weapon callback. This seam closes that observability gap without granting the
test fixture any command, trace, or weapon authority.

## Production seam

`LagCompensation_PrepareCanonicalWeaponDamageCommand` is called at the
`ClientThink` command-to-game boundary, before button latching. A console
command only arms the fixture. It cannot construct a command context, alter a
received attack bit, choose a rewind time, trace, or call `fire_rail`.

Retained pose capture and canonical command policy now share the engine's
authoritative simulation-clock domain. `ServerSimulationTimeUs` gives sgame
the time of the live completed server state; end-frame captures use the next
completed tick while command traces use the current tick. This prevents the
game-local clock's pre-admission pause from making a valid canonical decision
refer to a future history instant. Capture also self-heals an empty bounded
track during a lifecycle handoff rather than silently omitting that pose.

After an active valid command context and an actual `BUTTON_ATTACK` arrive,
the fixture establishes a tightly isolated two-player geometry:

1. Six ordinary target end-frame captures retain the target at the historical
   pose.
2. The real shooter command moves only the fixture target's live collider,
   equips the shooter with a Railgun, and resets stale button bookkeeping so
   the same received attack edge is latched by normal `ClientThink` code.
3. The standard `Item::weaponThink` Railgun path runs. The observation hook
   binds its trace to the same command ID, requires canonical/historical flags,
   verifies the target identity and unchanged live geometry, and requires
   exactly 80 damage.

The fixture restores the minimum normal player invariants required by the
weapon animation and collision systems (`MODELINDEX_PLAYER`, live health,
linked player-solid bounds). This is fixture-local server state, not a
substitute for a client command or weapon call.

## Headless runtime gate

`tools/networking/run_canonical_rail_damage_runtime_gate.py --weapon railgun`
starts a
dedicated server and two named real UDP clients:

- `rail_shooter` receives the real `+attack` console action after it joins;
- `rail_target` supplies independent player history;
- both clients launch with `win_headless=1`, `in_enable=0`, `in_grab=0`,
  `s_enable=0`, `stdin=DEVNULL`, and Windows no-window creation flags;
- the runner uses only loopback RCON after both clients are admitted, never an
  interactive console or physical device input.

The shooter runs deterministic loss-free upstream impairment and the server
has a matching test-only interpolation bias. This is intended to demand an
earlier authoritative selection rather than treating zero-latency loopback as
lag-compensation evidence.

The version-2 status cvar is ordered by the runner and reports the
terminal proof, command/weapon state, retained history, applied age, real
client counts, target-bearing observation path/outcome/fallback/flags, and the
observed policy plus exact expected/observed damage.
Railgun can continue tracing after it hits a damageable entity, so the fixture
records the canonical historical observation that names the target rather than
mistaking a later clear piercing segment for the shot result.

## Evidence

The staged three-repeat gate passes. Each run proves:

- two real headless, input-disabled clients connect and join;
- six target history captures occur;
- a valid canonical scope sees the real attack command;
- the normal Railgun callback fires, selects a positive-age canonical
  historical target hit, and applies exactly 80 damage;
- the live target remains at its intentionally different current-world
  position and collision authority is unchanged.

The latest repeatable evidence is
`.tmp/networking/canonical-rail-damage-runtime-v2.json`: all three runs report
`status=pass`, `failure_code=0`, canonical historical-hit outcome, no fallback,
policy `5`, exact 80 damage, and a positive applied age (40-56 ms in the
recorded run). This completes the
canonical-command Railgun acceptance seam, not `FR-10-T11` as a whole: the
remaining weapons, fairness scenarios, abuse/load gates, operator policy, and
release promotion remain open.

## Verification

Focused contracts:

```powershell
python -m unittest tools/networking/test_run_canonical_rail_damage_runtime_gate.py `
  tools/networking/test_lag_compensation_canonical_rail_contract.py `
  tools/networking/test_headless_input_contract.py
```

The runtime command is documented in the runner help and must be executed
against a freshly staged runtime. It only passes when every status proof is
true, including positive applied age and a canonical historical hit.

The focused source contracts also lock the C/C++ game-import ABI and server
provider for `ServerSimulationTimeUs`, plus the completed-frame timestamps
used for player and mover pose capture. The shared fixture contract also
requires the selected policy and exact expected/observed damage. Result: 15/15
focused tests passed.

Final verification for this increment also passed:

```powershell
meson test -C builddir-win --no-rebuild
```

Result: 137/137 tests passed.
