from __future__ import annotations

import sys
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_player_setup_provider as player_provider  # noqa: E402


SOURCE_ROOT = SCRIPT_DIR.parents[1]


def _write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def _write_valid_repo(repo_root: Path) -> None:
    for relative in (
        player_provider.RUNTIME,
        player_provider.RENDERER_BRIDGE,
        player_provider.DOCUMENT,
        player_provider.UTILITY_THEME,
    ):
        _write(repo_root / relative, (SOURCE_ROOT / relative).read_text(encoding="utf-8"))


def test_valid_player_setup_provider_passes(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    assert player_provider.main(["--repo-root", str(repo_root)]) == 0
    output = capsys.readouterr().out
    assert "Live cvar controls checked: 3" in output
    assert "Result: RmlUi Player Setup live-provider check passed." in output


def test_scalar_model_cvar_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / player_provider.DOCUMENT
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            '<select id="players-model"',
            '<select id="players-model" data-cvar="skin"',
            1,
        ),
        encoding="utf-8",
    )
    assert player_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "composite model/skin provider" in capsys.readouterr().err


def test_missing_empty_state_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / player_provider.DOCUMENT
    text = document.read_text(encoding="utf-8")
    text = text.replace('id="players-preview-empty"', 'id="players-preview-no-data"', 1)
    document.write_text(text, encoding="utf-8")
    assert player_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "ui-empty-state" in capsys.readouterr().err


def test_reduced_motion_preview_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    runtime = repo_root / player_provider.RUNTIME
    runtime.write_text(
        runtime.read_text(encoding="utf-8").replace(
            "ui_rml_reduced_motion->integer",
            "preview_motion_is_unconditionally_enabled",
        ),
        encoding="utf-8",
    )
    assert player_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "ui_rml_reduced_motion->integer" in capsys.readouterr().err


def test_empty_actions_footer_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / player_provider.DOCUMENT
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            '<footer class="worr-statusbar">',
            '<footer id="players-actions"></footer><footer class="worr-statusbar">',
            1,
        ),
        encoding="utf-8",
    )
    assert player_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "empty actions footer" in capsys.readouterr().err
