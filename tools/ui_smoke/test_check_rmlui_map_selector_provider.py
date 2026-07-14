from __future__ import annotations

import json
import sys
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_map_selector_provider as map_selector_provider  # noqa: E402


SOURCE_ROOT = SCRIPT_DIR.parents[1]


def _write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def _write_valid_repo(repo_root: Path) -> None:
    paths = (
        map_selector_provider.RUNTIME,
        map_selector_provider.RUNTIME_ROUTES,
        map_selector_provider.CGAME_UI,
        map_selector_provider.PUBLISHER,
        map_selector_provider.CLIENT_COMMANDS,
        map_selector_provider.PLAYER_VIEW,
        map_selector_provider.MAP_MANAGER,
        map_selector_provider.G_LOCAL,
        map_selector_provider.DOCUMENT,
        map_selector_provider.SESSION_THEME,
        map_selector_provider.ACCESSIBILITY_THEME,
        map_selector_provider.SESSION_ROUTES,
        map_selector_provider.MANIFEST,
        map_selector_provider.CAPTURE_HARNESS,
    )
    for relative in paths:
        _write(repo_root / relative, (SOURCE_ROOT / relative).read_text(encoding="utf-8"))


def test_valid_map_selector_provider_passes(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    assert map_selector_provider.main(["--repo-root", str(repo_root)]) == 0
    output = capsys.readouterr().out
    assert "Routes checked: 1" in output
    assert "Candidate controls checked: 3" in output


def test_missing_numeric_countdown_publication_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    publisher = repo_root / map_selector_provider.PUBLISHER
    publisher.write_text(
        publisher.read_text(encoding="utf-8").replace(
            'AppendCvar("ui_mapselector_time_left"',
            'AppendCvar("ui_mapselector_time_missing"',
            1,
        ),
        encoding="utf-8",
    )
    assert map_selector_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "publisher is missing token" in capsys.readouterr().err


def test_reopen_suppression_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    view = repo_root / map_selector_provider.PLAYER_VIEW
    view.write_text(
        view.read_text(encoding="utf-8").replace(
            "!cl->ui.mapSelectorActive && !cl->ui.mapSelectorDismissed",
            "!cl->ui.mapSelectorActive",
            1,
        ),
        encoding="utf-8",
    )
    assert map_selector_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "per-frame map-selector lifecycle" in capsys.readouterr().err


def test_close_dismissal_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    commands = repo_root / map_selector_provider.CLIENT_COMMANDS
    commands.write_text(
        commands.read_text(encoding="utf-8").replace(
            "ent->client->ui.mapSelectorDismissed = true;", "", 1
        ),
        encoding="utf-8",
    )
    assert map_selector_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "vote/close commands" in capsys.readouterr().err


def test_disconnected_close_command_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    runtime = repo_root / map_selector_provider.RUNTIME
    runtime.write_text(
        runtime.read_text(encoding="utf-8").replace(
            '        !strcmp(command, "worr_mapselector_close") ||',
            '        !strcmp(command, "worr_mapselector_close_missing") ||',
            1,
        ),
        encoding="utf-8",
    )
    assert map_selector_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "disconnected-close bridge" in capsys.readouterr().err


def test_blankable_heading_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / map_selector_provider.DOCUMENT
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            '<h1 data-loc-key="worr_ui_next_map_vote">Next Map Vote</h1>',
            '<h1 data-loc-key="worr_ui_next_map_vote" '
            'data-bind-cvar="ui_mapselector_title">Next Map Vote</h1>',
            1,
        ),
        encoding="utf-8",
    )
    assert map_selector_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "heading must not be blanked" in capsys.readouterr().err


def test_candidate_command_drift_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / map_selector_provider.DOCUMENT
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            'data-command="worr_mapselector_vote 2"',
            'data-command="worr_mapselector_vote 3"',
            1,
        ),
        encoding="utf-8",
    )
    assert map_selector_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "option 2" in capsys.readouterr().err


def test_acknowledgement_empty_state_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / map_selector_provider.DOCUMENT
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            "!ui_mapselector_option_show_0;ui_mapselector_ack_show=0",
            "!ui_mapselector_option_show_0",
            1,
        ),
        encoding="utf-8",
    )
    assert map_selector_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "empty state must not reappear" in capsys.readouterr().err


def test_duplicate_close_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / map_selector_provider.DOCUMENT
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            "    </div>",
            '      <button data-command="popmenu; worr_mapselector_close">Close</button>\n    </div>',
            1,
        ),
        encoding="utf-8",
    )
    assert map_selector_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "duplicate Close action" in capsys.readouterr().err


def test_metadata_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    routes_path = repo_root / map_selector_provider.SESSION_ROUTES
    routes = json.loads(routes_path.read_text(encoding="utf-8"))
    next(route for route in routes["routes"] if route["id"] == "map_selector")[
        "status"
    ] = "starter_round3"
    routes_path.write_text(json.dumps(routes), encoding="utf-8")
    assert map_selector_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "promote map_selector" in capsys.readouterr().err


def test_capture_registration_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    harness = repo_root / map_selector_provider.CAPTURE_HARNESS
    harness.write_text(
        harness.read_text(encoding="utf-8").replace(
            '    "map_selector": Path("session/map_selector.rml"),\n', "", 1
        ),
        encoding="utf-8",
    )
    assert map_selector_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "capture harness is missing" in capsys.readouterr().err
