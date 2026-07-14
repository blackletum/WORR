from __future__ import annotations

import json
import sys
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_tournament_provider as tournament_provider  # noqa: E402


SOURCE_ROOT = SCRIPT_DIR.parents[1]


def _write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def _write_valid_repo(repo_root: Path) -> None:
    paths = (
        tournament_provider.RUNTIME,
        tournament_provider.RUNTIME_ROUTES,
        tournament_provider.CGAME_UI,
        tournament_provider.PUBLISHER,
        tournament_provider.UI_LIST_PROVIDER,
        tournament_provider.CLIENT_COMMANDS,
        tournament_provider.REPLAY_STATE,
        tournament_provider.SESSION_THEME,
        tournament_provider.BASE_THEME,
        tournament_provider.ACCESSIBILITY_THEME,
        tournament_provider.SESSION_ROUTES,
        tournament_provider.MANIFEST,
        tournament_provider.CAPTURE_HARNESS,
        *tournament_provider.DOCUMENTS.values(),
    )
    for relative in paths:
        _write(repo_root / relative, (SOURCE_ROOT / relative).read_text(encoding="utf-8"))


def test_valid_tournament_provider_passes(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    assert tournament_provider.main(["--repo-root", str(repo_root)]) == 0
    output = capsys.readouterr().out
    assert "Routes checked: 4" in output
    assert "Map-choice rows checked: 10" in output
    assert "Veto bindings checked: 6" in output


def test_missing_veto_publication_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    publisher = repo_root / tournament_provider.PUBLISHER
    publisher.write_text(
        publisher.read_text(encoding="utf-8").replace(
            '"ui_tourney_veto_can_ban"', '"ui_tourney_veto_can_ban_lost"'
        ),
        encoding="utf-8",
    )
    assert tournament_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "publisher is missing ui_tourney_veto_can_ban" in capsys.readouterr().err


def test_mapchoice_row_binding_drift_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / tournament_provider.DOCUMENTS["tourney_mapchoices"]
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            'data-bind-cvar="ui_tourney_mapchoice_line_7"',
            'data-bind-cvar="ui_tourney_mapchoice_line_8"',
            1,
        ),
        encoding="utf-8",
    )
    assert tournament_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "map-choice row 7" in capsys.readouterr().err


def test_veto_action_drift_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / tournament_provider.DOCUMENTS["tourney_veto"]
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            'data-command="worr_tourney_pick"', 'data-command="tourney_pick"', 1
        ),
        encoding="utf-8",
    )
    assert tournament_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "must dispatch worr_tourney_pick" in capsys.readouterr().err


def test_false_embedded_veto_list_contract_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / tournament_provider.DOCUMENTS["tourney_veto"]
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            'id="tourney-veto-content"',
            'id="tourney-veto-content" data-list-provider="session.tournament.veto.maps"',
            1,
        ),
        encoding="utf-8",
    )
    assert tournament_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "must not claim an embedded candidate-list" in capsys.readouterr().err


def test_ban_locked_action_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / tournament_provider.DOCUMENTS["tourney_veto"]
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            'disabled="disabled" data-visible-if=',
            'data-command="worr_tourney_ban" data-visible-if=',
            1,
        ),
        encoding="utf-8",
    )
    assert tournament_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "Ban-locked state must be a disabled" in capsys.readouterr().err


def test_replay_warning_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / tournament_provider.DOCUMENTS["tourney_replay_confirm"]
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            "Results from this game onward will be discarded.", "Continue?", 1
        ),
        encoding="utf-8",
    )
    assert tournament_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "fallback must explain result truncation" in capsys.readouterr().err


def test_replay_admin_guard_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    commands = repo_root / tournament_provider.CLIENT_COMMANDS
    commands.write_text(
        commands.read_text(encoding="utf-8").replace(
            "AllowDead | AllowSpectator | AdminOnly);\n"
            '\t\tRegisterCommand("worr_tourney_replay_confirm"',
            "AllowDead | AllowSpectator);\n"
            '\t\tRegisterCommand("worr_tourney_replay_confirm"',
            1,
        ),
        encoding="utf-8",
    )
    assert tournament_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "admin-only replay command registration" in capsys.readouterr().err


def test_duplicate_back_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / tournament_provider.DOCUMENTS["tourney_info"]
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            "    </div>", '      <button data-command="popmenu">Back</button>\n    </div>', 1
        ),
        encoding="utf-8",
    )
    assert tournament_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "duplicate footer Back" in capsys.readouterr().err


def test_metadata_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    routes_path = repo_root / tournament_provider.SESSION_ROUTES
    routes = json.loads(routes_path.read_text(encoding="utf-8"))
    next(route for route in routes["routes"] if route["id"] == "tourney_veto")[
        "status"
    ] = "starter_round3"
    routes_path.write_text(json.dumps(routes), encoding="utf-8")
    assert tournament_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "promote tourney_veto" in capsys.readouterr().err


def test_capture_registration_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    harness = repo_root / tournament_provider.CAPTURE_HARNESS
    harness.write_text(
        harness.read_text(encoding="utf-8").replace(
            '    "tourney_mapchoices": Path("session/tourney_mapchoices.rml"),\n',
            "",
            1,
        ),
        encoding="utf-8",
    )
    assert tournament_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "capture harness is missing the tourney_mapchoices" in capsys.readouterr().err
