from __future__ import annotations

import json
import sys
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_admin_provider as admin_provider  # noqa: E402


SOURCE_ROOT = SCRIPT_DIR.parents[1]


def _write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def _write_valid_repo(repo_root: Path) -> None:
    paths = (
        admin_provider.RUNTIME,
        admin_provider.RUNTIME_ROUTES,
        admin_provider.CGAME_UI,
        admin_provider.ADMIN_PUBLISHER,
        admin_provider.COMMAND_REFERENCE_PUBLISHER,
        admin_provider.CLIENT_COMMANDS,
        admin_provider.ADMIN_COMMANDS,
        admin_provider.SESSION_THEME,
        admin_provider.ACCESSIBILITY_THEME,
        admin_provider.SESSION_ROUTES,
        admin_provider.MANIFEST,
        admin_provider.CAPTURE_HARNESS,
        *admin_provider.DOCUMENTS.values(),
    )
    for relative in paths:
        _write(repo_root / relative, (SOURCE_ROOT / relative).read_text(encoding="utf-8"))


def test_valid_admin_provider_passes(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    assert admin_provider.main(["--repo-root", str(repo_root)]) == 0
    output = capsys.readouterr().out
    assert "Routes checked: 2" in output
    assert "Admin commands checked: 28" in output


def test_missing_admin_registration_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    commands = repo_root / admin_provider.ADMIN_COMMANDS
    commands.write_text(
        commands.read_text(encoding="utf-8").replace(
            'RegisterCommand("unready_all"', 'RegisterCommand("unready_all_lost"', 1
        ),
        encoding="utf-8",
    )
    assert admin_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "admin command registry drifted" in capsys.readouterr().err


def test_missing_reference_row_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / admin_provider.DOCUMENTS["admin_commands"]
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            'data-admin-command="ready_all"', 'data-admin-command="ready_everyone"', 1
        ),
        encoding="utf-8",
    )
    assert admin_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "admin command reference drifted" in capsys.readouterr().err


def test_usage_drift_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / admin_provider.DOCUMENTS["admin_commands"]
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            "usage: set_map &lt;map&gt;", "usage: map &lt;map&gt;", 1
        ),
        encoding="utf-8",
    )
    assert admin_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "usage does not match registry: set_map" in capsys.readouterr().err


def test_replay_condition_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / admin_provider.DOCUMENTS["admin_menu"]
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            'data-visible-if="ui_admin_show_replay!=0"',
            'data-visible-if="ui_admin_show_replay=0"',
            1,
        ),
        encoding="utf-8",
    )
    assert admin_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "live tournament availability" in capsys.readouterr().err


def test_duplicate_back_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / admin_provider.DOCUMENTS["admin_menu"]
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            "    </div>", '      <button data-command="popmenu">Back</button>\n    </div>', 1
        ),
        encoding="utf-8",
    )
    assert admin_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "duplicate footer Back" in capsys.readouterr().err


def test_metadata_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    routes_path = repo_root / admin_provider.SESSION_ROUTES
    routes = json.loads(routes_path.read_text(encoding="utf-8"))
    next(route for route in routes["routes"] if route["id"] == "admin_menu")[
        "status"
    ] = "starter_round3"
    routes_path.write_text(json.dumps(routes), encoding="utf-8")
    assert admin_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "promote admin_menu" in capsys.readouterr().err


def test_capture_registration_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    harness = repo_root / admin_provider.CAPTURE_HARNESS
    harness.write_text(
        harness.read_text(encoding="utf-8").replace(
            '    "admin_commands": Path("session/admin_commands.rml"),\n', "", 1
        ),
        encoding="utf-8",
    )
    assert admin_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "capture harness is missing the admin_commands" in capsys.readouterr().err
