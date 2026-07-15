# FR-10-T12 continuous-beam release acceptance

Task references: `FR-10-T10`, `FR-10-T11`, `FR-10-T12`, `FR-10-T14`, and
`FR-10-T15`.

## Purpose

This increment closes the bounded release seam that follows the three-tick
continuous-beam cadence gate. It proves that a real hidden client’s no-attack
command ends normal Plasma Beam policy `7` and dry-world Thunderbolt policy
`8` damage: after exact 24 cumulative damage, neither weapon may apply another
tick during a 250 ms authoritative grace interval.

It does not claim complete continuous-beam lifecycle coverage. Long-running
holds, water retrace, underwater Thunderbolt discharge, and broader interaction
matrices remain open.

## Production contract

Two real-command fixture arms add the same bounded lifecycle contract:

- `worr_rewind_canonical_plasma_beam_release_damage_arm`
- `worr_rewind_canonical_thunderbolt_release_damage_arm`

Each first requires three normal 8-damage ticks through the production
repeating weapon path (exact 24 cumulative damage). The fixture remains
pending until it receives a valid active command-context user command with no
`BUTTON_ATTACK`; it records the 24-damage baseline, waits 250 ms of
authoritative server time, then passes only if damage remains unchanged.
Missing release fails at the existing 1.5-second cadence deadline; post-release
damage fails immediately. The fixture never calls a weapon callback, builds
server input, or supplies a trace or damage result.

## Headless client delivery

While the three-tick cadence is pending, the runner refreshes a `-attack; +attack`
edge through the admitted hidden shooter, producing normal attack user
commands. Once the cvar reports exact 24 damage, it sends
`-attack; +moveup; -moveup` only to that same client. The temporary movement
edge requests immediate packet delivery but the matching release leaves the
outgoing command at zero movement and no attack. This is a real client command
under ordinary command-context admission, not server-side input.

All launches use a dedicated server, isolated homes, `win_headless=1`,
`in_enable=0`, `in_grab=0`, `s_enable=0`, `stdin=DEVNULL`, and Windows
no-window creation. No automated client initializes input or captures the
mouse.

## Evidence

Three staged-runtime repetitions pass in each report:

- `.tmp/networking/canonical-plasma-beam-release-damage-runtime.json`
- `.tmp/networking/canonical-thunderbolt-release-damage-runtime.json`

Every row uses schema `worr.networking.canonical-weapon-damage-runtime.v3`,
proves canonical historical target selection at positive applied age, no
fallback, unchanged current geometry, and exact 24 damage after the release
grace. Plasma Beam rows report 56 ms applied age; Thunderbolt rows report
40–56 ms. Source contracts bind terminal pass to the valid no-attack command
and unchanged post-release damage baseline.

## Verification

```powershell
python -m unittest tools/networking/test_lag_compensation_canonical_rail_contract.py `
  tools/networking/test_run_canonical_rail_damage_runtime_gate.py `
  tools/networking/test_headless_input_contract.py
ninja -C builddir-win sgame_x86_64.dll
python tools/refresh_install.py --build-dir builddir-win --install-dir .install `
  --base-game basew --platform-id windows-x86_64
python tools/networking/run_canonical_rail_damage_runtime_gate.py `
  --weapon plasma-beam-release --client-exe .install/worr_x86_64.exe `
  --dedicated-exe .install/worr_ded_x86_64.exe --working-dir .install `
  --output .tmp/networking/canonical-plasma-beam-release-damage-runtime.json `
  --repeat 3 --timeout 35
python tools/networking/run_canonical_rail_damage_runtime_gate.py `
  --weapon thunderbolt-release --client-exe .install/worr_x86_64.exe `
  --dedicated-exe .install/worr_ded_x86_64.exe --working-dir .install `
  --output .tmp/networking/canonical-thunderbolt-release-damage-runtime.json `
  --repeat 3 --timeout 35
meson test -C builddir-win --no-rebuild
```

Results: focused contracts 33/33; three Plasma Beam and three Thunderbolt
release repetitions passed; complete suite 137/137.

## Remaining scope

`FR-10-T11` and `FR-10-T12` remain incomplete. Outstanding work includes
long-duration holds, water/retrace, Thunderbolt underwater discharge,
projectile simulation, melee, splash/radius, movers, deployables, triggers,
cooperative interactions, fairness/abuse/load budgets, platform evidence, and
release promotion.
