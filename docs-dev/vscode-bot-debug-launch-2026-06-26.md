# VS Code Bot Debug Launch

Date: 2026-06-26

Task IDs: `FR-04-T16`, `DV-03-T05`, `DV-07-T06`

## Summary

Added a VS Code debugging path for the current bot validation lane. The new
launch configuration starts `.install/worr_ded_x86_64.exe` under `cppvsdbg`
with the latest source-backed bot scenario, `coop_campaign_interaction_matrix`,
using `basew`, `base1`, `sg_bot_coop_live_loop 1`, and
`sv_bot_frame_command_smoke 91`.

This keeps bot debugging close to the existing `.install` workflow: the launch
configuration depends on a bot-specific build alias, which in turn reuses the
default Meson compile and install-refresh task.

## VS Code Entries

- `.vscode/launch.json`: `WORR Bot Debug (coop campaign matrix)`
- `.vscode/tasks.json`: `bot debug: build`
- `.vscode/tasks.json`: `bot debug: coop campaign matrix smoke`

## Runtime Shape

The debug launch mirrors the bot scenario harness command shape:

- `+set game basew`
- `+set basedir ${workspaceFolder}\.install`
- `+set developer 1`
- `+set sg_bot_enable 1`
- `+set sg_bot_debug_route 1`
- `+set sg_bot_debug_goal 1`
- `+set deathmatch 0`
- `+set coop 1`
- `+set sg_bot_coop_live_loop 1`
- `+set sv_bot_frame_command_smoke 91`
- `+map base1`

The companion smoke task runs:

```powershell
python tools/bot_scenarios/run_bot_scenarios.py --scenario coop_campaign_interaction_matrix --timeout 120 --base-port 28000 --format text --json-out .tmp/bot_scenarios/vscode_bot_debug_latest.json
```

## Notes

Use the launch entry when stepping through C++ server/game code. Use the smoke
task when checking the same scenario without attaching the debugger. The bot
completion stats remain unchanged by this tooling round: the current catalog is
still 99 implemented rows, 0 pending rows, highest smoke mode 91, and the
latest aggregate pass remains the 99/99 implemented run recorded in the bot
roadmap.
