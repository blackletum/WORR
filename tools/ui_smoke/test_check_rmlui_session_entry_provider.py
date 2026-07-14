from __future__ import annotations

import sys
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_session_entry_provider as session_provider  # noqa: E402


SOURCE_ROOT = SCRIPT_DIR.parents[1]


def _write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def _write_valid_repo(repo_root: Path) -> None:
    paths = (
        session_provider.RUNTIME,
        session_provider.CLIENT_RML,
        session_provider.SESSION_PUBLISHER,
        session_provider.SESSION_COMMANDS,
        session_provider.SESSION_THEME,
        session_provider.ACCESSIBILITY_THEME,
        session_provider.SESSION_ROUTES,
        session_provider.MANIFEST,
        session_provider.CAPTURE_HARNESS,
        *session_provider.DOCUMENTS.values(),
    )
    for relative in paths:
        _write(
            repo_root / relative,
            (SOURCE_ROOT / relative).read_text(encoding="utf-8"),
        )


def test_valid_session_entry_provider_passes(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    assert session_provider.main(["--repo-root", str(repo_root)]) == 0
    output = capsys.readouterr().out
    assert "Routes checked: 5" in output
    assert "Sgame-published cvars checked: 49" in output


def test_missing_published_state_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    publisher = repo_root / session_provider.SESSION_PUBLISHER
    publisher.write_text(
        publisher.read_text(encoding="utf-8").replace(
            'AppendCvar("ui_dm_show_admin"', 'AppendCvar("ui_dm_admin_lost"', 1
        ),
        encoding="utf-8",
    )
    assert session_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "ui_dm_show_admin" in capsys.readouterr().err


def test_team_condition_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / session_provider.DOCUMENTS["dm_join"]
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            "ui_dm_show_join!=0;ui_dm_teamplay!=0",
            "ui_dm_show_join!=0",
            1,
        ),
        encoding="utf-8",
    )
    assert session_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "team join controls" in capsys.readouterr().err


def test_ready_command_cvar_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / session_provider.DOCUMENTS["join"]
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            'data-command-cvar="ui_dm_ready_command"',
            'data-command="ready"',
            1,
        ),
        encoding="utf-8",
    )
    assert session_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "Ready must resolve" in capsys.readouterr().err


def test_match_hub_close_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / session_provider.DOCUMENTS["dm_join"]
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            'data-close-command="worr_dm_join_close"',
            'data-close-command="popmenu"',
            1,
        ),
        encoding="utf-8",
    )
    assert session_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "close command" in capsys.readouterr().err


def test_first_connect_modal_guard_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    runtime = repo_root / session_provider.RUNTIME
    runtime.write_text(
        runtime.read_text(encoding="utf-8").replace(
            '!Cvar_VariableInteger("ui_dm_initial")',
            'Cvar_VariableInteger("ui_dm_initial")',
            1,
        ),
        encoding="utf-8",
    )
    assert session_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "ui_dm_initial" in capsys.readouterr().err


def test_disconnected_remote_command_guard_regression_fails(
    tmp_path: Path, capsys
) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    runtime = repo_root / session_provider.RUNTIME
    runtime.write_text(
        runtime.read_text(encoding="utf-8").replace(
            "cls.state >= ca_connected",
            "true",
            1,
        ),
        encoding="utf-8",
    )
    assert session_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "ca_connected" in capsys.readouterr().err


def test_duplicate_info_back_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / session_provider.DOCUMENTS["dm_hostinfo"]
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            "</section>",
            '<button id="duplicate-back" data-command="popmenu">Back</button></section>',
            1,
        ),
        encoding="utf-8",
    )
    assert session_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "duplicate footer Back" in capsys.readouterr().err


def test_missing_capture_route_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    harness = repo_root / session_provider.CAPTURE_HARNESS
    harness.write_text(
        harness.read_text(encoding="utf-8").replace(
            '"dm_matchinfo": Path("session/dm_matchinfo.rml"),',
            "",
            1,
        ),
        encoding="utf-8",
    )
    assert session_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "dm_matchinfo route" in capsys.readouterr().err


def test_direct_info_text_flex_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    theme = repo_root / session_provider.SESSION_THEME
    theme.write_text(
        theme.read_text(encoding="utf-8").replace(
            ".session-info-line\n{\n\tdisplay: block",
            ".session-info-line\n{\n\tdisplay: flex",
            1,
        ),
        encoding="utf-8",
    )
    assert session_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "display: block" in capsys.readouterr().err


def test_scaffold_metadata_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    metadata = repo_root / session_provider.SESSION_ROUTES
    metadata.write_text(
        metadata.read_text(encoding="utf-8").replace(
            '"status": "live_provider"',
            '"status": "starter_round3"',
            1,
        ),
        encoding="utf-8",
    )
    assert session_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "promote dm_welcome" in capsys.readouterr().err


def test_accessibility_contract_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    accessibility = repo_root / session_provider.ACCESSIBILITY_THEME
    accessibility.write_text(
        accessibility.read_text(encoding="utf-8").replace(
            ".ui-high-visibility .session-info-row,",
            ".session-info-row,",
            1,
        ),
        encoding="utf-8",
    )
    assert session_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "session-info-row" in capsys.readouterr().err
