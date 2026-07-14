from __future__ import annotations

import json
import sys
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_vote_callvote_provider as vote_provider  # noqa: E402


SOURCE_ROOT = SCRIPT_DIR.parents[1]


def _write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def _write_valid_repo(repo_root: Path) -> None:
    paths = (
        vote_provider.RUNTIME,
        vote_provider.VOTE_PUBLISHER,
        vote_provider.CALLVOTE_PUBLISHER,
        vote_provider.UI_LIST,
        vote_provider.COMMANDS,
        vote_provider.SESSION_THEME,
        vote_provider.ACCESSIBILITY_THEME,
        vote_provider.SESSION_ROUTES,
        vote_provider.MANIFEST,
        vote_provider.CAPTURE_HARNESS,
        *vote_provider.DOCUMENTS.values(),
    )
    for relative in paths:
        _write(repo_root / relative, (SOURCE_ROOT / relative).read_text(encoding="utf-8"))


def test_valid_vote_callvote_provider_passes(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    assert vote_provider.main(["--repo-root", str(repo_root)]) == 0
    output = capsys.readouterr().out
    assert "Routes checked: 8" in output
    assert "Sgame-published cvars checked: 41" in output


def test_missing_vote_cvar_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    publisher = repo_root / vote_provider.VOTE_PUBLISHER
    publisher.write_text(
        publisher.read_text(encoding="utf-8").replace(
            'AppendCvar("ui_vote_time_left"', 'AppendCvar("ui_vote_timeout_lost"', 1
        ),
        encoding="utf-8",
    )
    assert vote_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "ui_vote_time_left" in capsys.readouterr().err


def test_incomplete_empty_state_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / vote_provider.DOCUMENTS["callvote_main"]
    text = document.read_text(encoding="utf-8")
    text = text.replace(";!ui_callvote_show_arena", "", 1)
    document.write_text(text, encoding="utf-8")
    assert vote_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "every option" in capsys.readouterr().err


def test_vote_idle_action_leak_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / vote_provider.DOCUMENTS["vote_menu"]
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            "ui_vote_can_vote!=0;ui_vote_line_0",
            "ui_vote_can_vote!=0",
            1,
        ),
        encoding="utf-8",
    )
    assert vote_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "active, open vote" in capsys.readouterr().err


def test_duplicate_back_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / vote_provider.DOCUMENTS["callvote_ruleset"]
    text = document.read_text(encoding="utf-8").replace(
        "      </nav>",
        '      </nav>\n      <button data-command="popmenu">Back</button>',
        1,
    )
    document.write_text(text, encoding="utf-8")
    assert vote_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "duplicate footer" in capsys.readouterr().err


def test_map_flag_binding_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / vote_provider.DOCUMENTS["callvote_map_flags"]
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            'data-label-cvar="ui_callvote_flag_bfg"',
            'data-label-cvar="ui_callvote_flag_missing"',
            1,
        ),
        encoding="utf-8",
    )
    assert vote_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "live label for: bfg" in capsys.readouterr().err


def test_grid_layout_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / vote_provider.DOCUMENTS["callvote_random"]
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            "menu-list session-vote-option-grid", "menu-list", 1
        ),
        encoding="utf-8",
    )
    assert vote_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "bounded two-column vote grid" in capsys.readouterr().err


def test_disconnected_close_guard_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    runtime = repo_root / vote_provider.RUNTIME
    runtime.write_text(
        runtime.read_text(encoding="utf-8").replace(
            '!strcmp(command, "worr_vote_close")',
            '!strcmp(command, "worr_vote_close_lost")',
            1,
        ),
        encoding="utf-8",
    )
    assert vote_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "worr_vote_close" in capsys.readouterr().err


def test_popmenu_tail_guard_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    runtime = repo_root / vote_provider.RUNTIME
    runtime.write_text(
        runtime.read_text(encoding="utf-8").replace(
            "UI_Rml_RemoteSessionCommandWhenConnected(tail)", "tail", 1
        ),
        encoding="utf-8",
    )
    assert vote_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "click and key-driven popmenu tails" in capsys.readouterr().err


def test_cinematic_is_not_treated_as_server_connection(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    runtime = repo_root / vote_provider.RUNTIME
    runtime.write_text(
        runtime.read_text(encoding="utf-8").replace(
            "cls.state <= ca_active", "cls.state <= ca_cinematic", 1
        ),
        encoding="utf-8",
    )
    assert vote_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "cls.state <= ca_active" in capsys.readouterr().err


def test_metadata_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    routes_path = repo_root / vote_provider.SESSION_ROUTES
    routes = json.loads(routes_path.read_text(encoding="utf-8"))
    next(route for route in routes["routes"] if route["id"] == "vote_menu")[
        "status"
    ] = "starter"
    routes_path.write_text(json.dumps(routes), encoding="utf-8")
    assert vote_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "promote vote_menu" in capsys.readouterr().err


def test_capture_registration_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    harness = repo_root / vote_provider.CAPTURE_HARNESS
    harness.write_text(
        harness.read_text(encoding="utf-8").replace(
            '    "callvote_unlagged": Path("session/callvote_unlagged.rml"),\n',
            "",
            1,
        ),
        encoding="utf-8",
    )
    assert vote_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "capture harness is missing the callvote_unlagged" in capsys.readouterr().err
