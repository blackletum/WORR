from __future__ import annotations

import json
import sys
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_session_confirm_provider as confirm_provider  # noqa: E402


SOURCE_ROOT = SCRIPT_DIR.parents[1]


def _write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def _write_valid_repo(repo_root: Path) -> None:
    paths = (
        confirm_provider.RUNTIME,
        confirm_provider.RUNTIME_ROUTES,
        confirm_provider.CGAME_UI,
        confirm_provider.FORFEIT_PUBLISHER,
        confirm_provider.COMMANDS,
        confirm_provider.BASE_THEME,
        confirm_provider.ACCESSIBILITY_THEME,
        confirm_provider.SESSION_ROUTES,
        confirm_provider.MANIFEST,
        confirm_provider.CAPTURE_HARNESS,
        *confirm_provider.DOCUMENTS.values(),
    )
    for relative in paths:
        _write(repo_root / relative, (SOURCE_ROOT / relative).read_text(encoding="utf-8"))


def test_valid_session_confirmation_provider_passes(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    assert confirm_provider.main(["--repo-root", str(repo_root)]) == 0
    output = capsys.readouterr().out
    assert "Routes checked: 2" in output
    assert "Destructive actions checked: 2" in output


def test_forfeit_command_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / confirm_provider.DOCUMENTS["forfeit_confirm"]
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            'data-command="worr_forfeit_yes"', 'data-command="forfeit"', 1
        ),
        encoding="utf-8",
    )
    assert confirm_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "registered sgame action" in capsys.readouterr().err


def test_leave_command_order_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / confirm_provider.DOCUMENTS["leave_match_confirm"]
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            'data-command="forcemenuoff; disconnect"',
            'data-command="disconnect; forcemenuoff"',
            1,
        ),
        encoding="utf-8",
    )
    assert confirm_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "before disconnecting" in capsys.readouterr().err


def test_safe_action_order_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / confirm_provider.DOCUMENTS["forfeit_confirm"]
    text = document.read_text(encoding="utf-8")
    no_button = next(line for line in text.splitlines() if 'id="forfeit-confirm-no"' in line)
    yes_button = next(line for line in text.splitlines() if 'id="forfeit-confirm-yes"' in line)
    placeholder = "        <!-- safe-action-order-placeholder -->"
    text = text.replace(no_button, placeholder, 1)
    text = text.replace(yes_button, no_button, 1)
    text = text.replace(placeholder, yes_button, 1)
    document.write_text(text, encoding="utf-8")
    assert confirm_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "safe No action first" in capsys.readouterr().err


def test_popup_close_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / confirm_provider.DOCUMENTS["leave_match_confirm"]
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            'data-close-command="popmenu"', 'data-close-command="disconnect"', 1
        ),
        encoding="utf-8",
    )
    assert confirm_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "safe popup cancel/back behavior" in capsys.readouterr().err


def test_forcemenuoff_bridge_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    runtime = repo_root / confirm_provider.RUNTIME
    runtime.write_text(
        runtime.read_text(encoding="utf-8").replace(
            'UI_Rml_CommandStartsWithToken(command, "forcemenuoff")',
            'UI_Rml_CommandStartsWithToken(command, "forcemenuoff_lost")',
            1,
        ),
        encoding="utf-8",
    )
    assert confirm_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "forcemenuoff" in capsys.readouterr().err


def test_metadata_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    routes_path = repo_root / confirm_provider.SESSION_ROUTES
    routes = json.loads(routes_path.read_text(encoding="utf-8"))
    next(route for route in routes["routes"] if route["id"] == "forfeit_confirm")[
        "status"
    ] = "starter_round3"
    routes_path.write_text(json.dumps(routes), encoding="utf-8")
    assert confirm_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "promote forfeit_confirm" in capsys.readouterr().err


def test_capture_registration_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    harness = repo_root / confirm_provider.CAPTURE_HARNESS
    harness.write_text(
        harness.read_text(encoding="utf-8").replace(
            '    "leave_match_confirm": Path("session/leave_match_confirm.rml"),\n',
            "",
            1,
        ),
        encoding="utf-8",
    )
    assert confirm_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "capture harness is missing the leave_match_confirm" in capsys.readouterr().err
