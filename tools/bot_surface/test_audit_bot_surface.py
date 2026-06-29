#!/usr/bin/env python3
"""Regression tests for the public bot surface audit."""

from __future__ import annotations

import json
import pathlib
import sys
import tempfile
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import audit_bot_surface as audit


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]


def write_complete_surface_fixture(root: pathlib.Path) -> None:
    server = root / "src" / "server"
    docs = root / "docs-user"
    server.mkdir(parents=True, exist_ok=True)
    docs.mkdir(parents=True, exist_ok=True)

    cvar_lines = [
        f'                    Cvar_Get("{name}", "{default}", 0);'
        for name, default in sorted(audit.PUBLIC_CVAR_DEFAULTS.items())
    ]
    (server / "main.c").write_text(
        "\n".join(
            [
                "void demo(void) {",
                *cvar_lines,
                "}",
            ]
        ),
        encoding="utf-8",
    )
    (server / "commands.c").write_text(
        """
        static const cmdreg_t c_server[] = {
            { "addbot", SV_BotAdd_f },
            { "removebot", SV_BotRemove_f },
            { "kickbots", SV_BotKickAll_f },
            { "botlist", SV_BotList_f },
            { "bot_reload_profiles", SV_BotReloadProfiles_f },
            { NULL }
        };
        """,
        encoding="utf-8",
    )
    rows = [
        f"| `{name}` | `{audit.doc_default_value(default)}` | demo |"
        for name, default in sorted(audit.PUBLIC_CVAR_DEFAULTS.items())
    ]
    (docs / "bot-cvars.md").write_text(
        "\n".join(
            [
                "# Bot Cvars",
                "",
                "| Cvar | Default | Use |",
                "|---|---|---|",
                *rows,
                "",
            ]
        ),
        encoding="utf-8",
    )


class BotSurfaceAuditTests(unittest.TestCase):
    def test_current_source_surface_passes(self) -> None:
        result = audit.audit_repo(REPO_ROOT)

        self.assertEqual([], result.violations)
        for name, expected_default in audit.PUBLIC_CVAR_DEFAULTS.items():
            self.assertIn(name, result.cvars)
            self.assertIn(expected_default, result.cvars[name].defaults)
        for name in audit.REQUIRED_COMMANDS:
            self.assertIn(name, result.commands)
            self.assertGreaterEqual(len(result.commands[name].registrations), 1)

    def test_forbidden_active_source_prefix_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            write_complete_surface_fixture(root)
            main = root / "src" / "server" / "main.c"
            main.write_text(
                main.read_text(encoding="utf-8")
                + '\nvoid bad(void) { Cvar_Get("sv_bot_min_players", "0", 0); }\n',
                encoding="utf-8",
            )

            result = audit.audit_repo(root)

        self.assertTrue(
            any("sv_bot_min_players" in violation for violation in result.violations)
        )

    def test_user_docs_legacy_prefix_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            docs = root / "docs-user"
            write_complete_surface_fixture(root)
            (docs / "bots.md").write_text(
                "Old docs said sv_bot_min_players and bot_min_players_smoke.\n",
                encoding="utf-8",
            )

            result = audit.audit_repo(root)

        self.assertTrue(any("sv_bot_min_players" in violation for violation in result.violations))
        self.assertTrue(any("bot_min_players_smoke" in violation for violation in result.violations))

    def test_user_docs_missing_public_cvar_default_row_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            write_complete_surface_fixture(root)
            defaults_doc = root / "docs-user" / "bot-cvars.md"
            defaults_doc.write_text(
                defaults_doc.read_text(encoding="utf-8").replace(
                    "| `bot_skill` | `3` | demo |\n",
                    "",
                ),
                encoding="utf-8",
            )

            result = audit.audit_repo(root)

        self.assertTrue(any("bot_skill" in violation for violation in result.violations))

    def test_classification_buckets_are_stable(self) -> None:
        self.assertEqual("public", audit.classify_cvar("bot_min_players"))
        self.assertEqual("smoke-only", audit.classify_cvar("bot_min_players_smoke"))
        self.assertEqual("debug", audit.classify_cvar("bot_debug_navigation"))
        self.assertEqual("experimental", audit.classify_cvar("bot_duel_live_pacing"))
        self.assertEqual("forbidden-prefix", audit.classify_cvar("sg_bot_min_players"))

    def test_json_report_shape(self) -> None:
        result = audit.audit_repo(REPO_ROOT)
        payload = result.to_json()
        dumped = json.dumps(payload, sort_keys=True)

        self.assertIn('"required_cvars"', dumped)
        self.assertIn("bot_min_players", dumped)
        self.assertIn("addbot", dumped)
        self.assertEqual(payload["summary"]["violations"], len(result.violations))


if __name__ == "__main__":
    unittest.main()
