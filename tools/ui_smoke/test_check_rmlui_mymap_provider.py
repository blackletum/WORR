from __future__ import annotations

import json
import sys
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_mymap_provider as mymap_provider  # noqa: E402


SOURCE_ROOT = SCRIPT_DIR.parents[1]


def _write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def _write_valid_repo(repo_root: Path) -> None:
    paths = (
        mymap_provider.RUNTIME,
        mymap_provider.PUBLISHER,
        mymap_provider.UI_LIST,
        mymap_provider.COMMANDS,
        mymap_provider.SESSION_THEME,
        mymap_provider.ACCESSIBILITY_THEME,
        mymap_provider.SESSION_ROUTES,
        mymap_provider.MANIFEST,
        mymap_provider.CAPTURE_HARNESS,
        *mymap_provider.DOCUMENTS.values(),
    )
    for relative in paths:
        _write(repo_root / relative, (SOURCE_ROOT / relative).read_text(encoding="utf-8"))


def test_valid_mymap_provider_passes(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    assert mymap_provider.main(["--repo-root", str(repo_root)]) == 0
    output = capsys.readouterr().out
    assert "Routes checked: 2" in output
    assert "Sgame-published cvars checked: 15" in output


def test_missing_availability_cvar_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    publisher = repo_root / mymap_provider.PUBLISHER
    publisher.write_text(
        publisher.read_text(encoding="utf-8").replace(
            'AppendCvar("ui_mymap_can_select"', 'AppendCvar("ui_mymap_select_lost"', 1
        ),
        encoding="utf-8",
    )
    assert mymap_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "ui_mymap_can_select" in capsys.readouterr().err


def test_enabled_state_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / mymap_provider.DOCUMENTS["mymap_main"]
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            'data-enable-if="ui_mymap_can_flags=1"',
            'data-enable-if="ui_mymap_can_select=1"',
            1,
        ),
        encoding="utf-8",
    )
    assert mymap_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "mymap-flags must use live enabled state" in capsys.readouterr().err


def test_duplicate_back_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / mymap_provider.DOCUMENTS["mymap_flags"]
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            "      </nav>", '      </nav>\n      <button data-command="popmenu">Back</button>', 1
        ),
        encoding="utf-8",
    )
    assert mymap_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "duplicate footer Back" in capsys.readouterr().err


def test_flag_binding_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / mymap_provider.DOCUMENTS["mymap_flags"]
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            'data-label-cvar="ui_mymap_flag_pb"',
            'data-label-cvar="ui_mymap_flag_missing"',
            1,
        ),
        encoding="utf-8",
    )
    assert mymap_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "live label for: pb" in capsys.readouterr().err


def test_grid_layout_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / mymap_provider.DOCUMENTS["mymap_flags"]
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            "menu-list session-vote-option-grid", "menu-list", 1
        ),
        encoding="utf-8",
    )
    assert mymap_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "bounded two-column flag grid" in capsys.readouterr().err


def test_metadata_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    routes_path = repo_root / mymap_provider.SESSION_ROUTES
    routes = json.loads(routes_path.read_text(encoding="utf-8"))
    next(route for route in routes["routes"] if route["id"] == "mymap_main")[
        "status"
    ] = "starter_round2"
    routes_path.write_text(json.dumps(routes), encoding="utf-8")
    assert mymap_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "promote mymap_main" in capsys.readouterr().err


def test_capture_registration_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    harness = repo_root / mymap_provider.CAPTURE_HARNESS
    harness.write_text(
        harness.read_text(encoding="utf-8").replace(
            '    "mymap_flags": Path("session/mymap_flags.rml"),\n', "", 1
        ),
        encoding="utf-8",
    )
    assert mymap_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "capture harness is missing the mymap_flags" in capsys.readouterr().err
