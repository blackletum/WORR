# Q3A BotLib Admin Bot Privilege Audit Scenario

Date: 2026-06-21

Tasks: `FR-04-T06`, `FR-07-T04`, `DV-03-T05`, `FR-04-T16`,
`DV-07-T06`

## Summary

This round adds a command-level audit proof for bot clients and admin-only
commands. A fake client can now be temporarily given an admin session bit by a
deterministic smoke helper, attempt the registered `lock_team red` command, and
still be denied before the command mutates match state.

The promoted scenario is `admin_bot_privilege_audit`. It runs through
`sv_bot_admin_audit_smoke 2`, spawns one bot-only FFA participant, enables
admin commands globally, prints `q3a_bot_admin_audit_status`, forces the bot's
admin session bit only for the synthetic command attempt, requires
`q3a_bot_admin_audit_attempt reason=bot_admin_blocked`, and verifies the red
team remains unlocked after cleanup.

## Implementation

- Added `BOT_ADMIN_AUDIT_STATUS_API_V1` under `inc/shared/` and exposed it
  through `G_GetExtension()`.
- Added `Commands::AuditRegisteredCommand()` so smoke/status code can inspect a
  registered client command through the normal permission flags without
  manufacturing engine argv state.
- Hardened `AdminOk()` so bot clients are rejected before session-admin and
  global admin checks. The bot rejection is intentionally side-effect free to
  avoid writing reliable messages to local fake clients during server smokes.
- Added `BotAdminAudit_PrintStatus()`,
  `BotAdminAudit_TryFirstBotAdminCommand()`, and `BotAdminAudit_ResetStatus()`.
  The attempt marker records command lookup, admin-only classification, forced
  admin session state, execution denial, reason, and before/after red-team lock
  state.
- Added `sv_bot_admin_audit_smoke` in the dedicated server. The smoke configures
  FFA, enables `g_allow_admin`, spawns one bot, captures pre-attempt status,
  runs the guarded command attempt, captures post-attempt and cleanup status,
  and exits in mode `2`.
- Added `admin_bot_privilege_audit` to the scenario harness with hard marker
  gates for bot-only setup, forced admin session observation, registered
  `lock_team` lookup, `bot_admin_blocked` denial, `executed=0`,
  `red_locked_after=0`, final cleanup, and optional
  `admin_audit_match_flow_signals`.

## Validation

- `git diff --check` passed.
- `python -m pytest tools\bot_scenarios\test_run_bot_scenarios.py` passed 42
  tests.
- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`
  passed.
- `meson compile -C builddir-win worr_ded_engine_x86_64 sgame_x86_64` passed.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas`
  passed and refreshed `.install/`.
- Focused `admin_bot_privilege_audit` passed from
  `.tmp\bot_scenarios\20260621T150348Z`.
- Full implemented scenario suite passed from
  `.tmp\bot_scenarios\20260621T150437Z`: 47 passed, 0 failed, 0 timed out,
  0 errored, and 0 pending.

No upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
imported or modified in this round.
