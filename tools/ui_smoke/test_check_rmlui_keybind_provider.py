from __future__ import annotations

import sys
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_keybind_provider as keybind_provider  # noqa: E402


SOURCE_ROOT = SCRIPT_DIR.parents[1]


def _write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def _write_valid_repo(repo_root: Path) -> None:
    paths = (
        keybind_provider.RUNTIME,
        keybind_provider.UTILITY_THEME,
        keybind_provider.ACCESSIBILITY_THEME,
        keybind_provider.BASE_THEME,
        keybind_provider.CAPTURE_HARNESS,
        keybind_provider.COMPONENT,
        *(path for path, _ in keybind_provider.DOCUMENTS.values()),
    )
    for relative in paths:
        _write(
            repo_root / relative,
            (SOURCE_ROOT / relative).read_text(encoding="utf-8"),
        )


def test_valid_keybind_provider_passes(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    assert keybind_provider.main(["--repo-root", str(repo_root)]) == 0
    output = capsys.readouterr().out
    assert "Commands checked: 38" in output
    assert "Result: RmlUi live keybind-provider check passed." in output


def test_single_slot_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / keybind_provider.DOCUMENTS["keys"][0]
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            'data-bind-slot="1"', 'data-bind-slot="0"', 1
        ),
        encoding="utf-8",
    )
    assert keybind_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "Primary=0 and Alternate=1" in capsys.readouterr().err


def test_destructive_replacement_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    runtime = repo_root / keybind_provider.RUNTIME
    runtime.write_text(
        runtime.read_text(encoding="utf-8").replace(
            "const int other_key =", "const int discarded_other_key =", 1
        ),
        encoding="utf-8",
    )
    assert keybind_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "const int other_key =" in capsys.readouterr().err


def test_timeout_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    runtime = repo_root / keybind_provider.RUNTIME
    runtime.write_text(
        runtime.read_text(encoding="utf-8").replace("8000u", "0u", 1),
        encoding="utf-8",
    )
    assert keybind_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "8000u" in capsys.readouterr().err


def test_missing_conflict_cancel_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / keybind_provider.DOCUMENTS["weapons"][0]
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            'data-keybind-conflict-action="cancel"',
            'data-keybind-conflict-action="dismiss"',
            1,
        ),
        encoding="utf-8",
    )
    assert keybind_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "Replace and Cancel" in capsys.readouterr().err


def test_device_icon_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    runtime = repo_root / keybind_provider.RUNTIME
    runtime.write_text(
        runtime.read_text(encoding="utf-8").replace(
            '"/gfx/controller/generic/f%04x.png"',
            '"/gfx/controller/missing/f%04x.png"',
            1,
        ),
        encoding="utf-8",
    )
    assert keybind_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "/gfx/controller/generic" in capsys.readouterr().err


def test_reduced_motion_invisible_state_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    theme = repo_root / keybind_provider.ACCESSIBILITY_THEME
    theme.write_text(
        theme.read_text(encoding="utf-8").replace(
            "Pin animated containers to their completed",
            "Do not pin interrupted animation states",
            1,
        ),
        encoding="utf-8",
    )
    assert keybind_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "completed" in capsys.readouterr().err


def test_reduced_motion_preload_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    runtime = repo_root / keybind_provider.RUNTIME
    runtime.write_text(
        runtime.read_text(encoding="utf-8").replace(
            "UI_Rml_ApplyPreloadAccessibilityClasses(document_contents);",
            "// accessibility state applied too late",
            1,
        ),
        encoding="utf-8",
    )
    assert keybind_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "ApplyPreloadAccessibilityClasses" in capsys.readouterr().err


def test_load_time_entrance_animation_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    theme = repo_root / keybind_provider.BASE_THEME
    theme.write_text(
        theme.read_text(encoding="utf-8").replace(
            "decorator: ninepatch(grime, grime-inner) border-box,",
            "animation: 0.18s cubic-out route-enter;\n\tdecorator: ninepatch(grime, grime-inner) border-box,",
            1,
        ),
        encoding="utf-8",
    )
    assert keybind_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "load-time entrance animation" in capsys.readouterr().err
