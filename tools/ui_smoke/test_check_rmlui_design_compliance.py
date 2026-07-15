from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_design_compliance as design  # noqa: E402


def write_valid_repo(root: Path) -> Path:
    document = root / "assets/ui/rml/shell/options.rml"
    document.parent.mkdir(parents=True, exist_ok=True)
    document.write_text(
        """<rml><head>
<link type="text/rcss" href="../common/theme/base.rcss" />
<link type="text/rcss" href="../common/theme/accessibility.rcss" />
</head><body data-route-id="options"><div class="screen">
<header class="worr-topbar"></header>
<div class="worr-titlerow"><button class="worr-backplate" id="back" data-command="ui.back" data-loc-key="m_back">Back</button><h1 data-loc-key="m_options">Options</h1></div>
<footer class="worr-statusbar"></footer>
</div></body></rml>""",
        encoding="utf-8",
    )
    manifest = root / design.DEFAULT_MANIFEST
    manifest.parent.mkdir(parents=True, exist_ok=True)
    manifest.write_text(json.dumps({"routes": [{
        "id": "options", "document": "assets/ui/rml/shell/options.rml",
        "wave": "A",
    }]}), encoding="utf-8")
    base = root / design.DEFAULT_BASE_THEME
    base.parent.mkdir(parents=True, exist_ok=True)
    base.write_text(" ".join((
        ".worr-topbar", ".worr-statusbar", ".worr-backplate",
        ".ui-button-cta", ".popup-dialog", ".setting-row",
        ".ui-session-overlay",
    )), encoding="utf-8")
    shell = root / design.DEFAULT_SHELL_THEME
    shell.write_text(
        "#main-menu-stack { overflow: hidden; } "
        "#main-menu-actions { overflow: hidden; } .main-choice-caption {}",
        encoding="utf-8",
    )
    session = root / design.DEFAULT_SESSION_THEME
    session.write_text(
        'body[data-route-group="session"] {} .session-screen { '
        "top: 32px; right: 40px; bottom: 32px; left: 40px; }",
        encoding="utf-8",
    )
    accessibility = root / design.DEFAULT_ACCESSIBILITY_THEME
    accessibility.write_text(
        ".ui-high-visibility .ui-reduced-motion .ui-a11y-large-text "
        "animation: none; transition: none; min-height: 48px;",
        encoding="utf-8",
    )
    catalog = root / design.DEFAULT_ENGLISH_CATALOG
    catalog.parent.mkdir(parents=True, exist_ok=True)
    catalog.write_text(
        'm_back = "Back"\nm_options = "Options"\n', encoding="utf-8"
    )
    return manifest


def test_valid_design_contract_passes(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    root = tmp_path / "repo"
    write_valid_repo(root)
    result = design.main(["--repo-root", str(root), "--format", "json"])
    payload = json.loads(capsys.readouterr().out)
    assert result == 0
    assert payload["ok"] is True
    assert payload["counts"]["localizable_leaf_copy"] == 2


def test_unlocalizable_copy_fails(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    root = tmp_path / "repo"
    write_valid_repo(root)
    document = root / "assets/ui/rml/shell/options.rml"
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            ' data-loc-key="m_options"', ""
        ),
        encoding="utf-8",
    )
    result = design.main(["--repo-root", str(root)])
    assert result == 1
    assert "unlocalizable h1 copy" in capsys.readouterr().out


def test_uncataloged_localization_key_fails(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    root = tmp_path / "repo"
    write_valid_repo(root)
    catalog = root / design.DEFAULT_ENGLISH_CATALOG
    catalog.write_text('m_back = "Back"\n', encoding="utf-8")
    result = design.main(["--repo-root", str(root)])
    assert result == 1
    assert "absent from English catalog: m_options" in capsys.readouterr().out


def test_main_requires_fixed_uncluttered_hero(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    root = tmp_path / "repo"
    write_valid_repo(root)
    document = root / "assets/ui/rml/shell/main.rml"
    document.write_text(
        """<rml><head>
<link type="text/rcss" href="../common/theme/base.rcss" />
<link type="text/rcss" href="../common/theme/accessibility.rcss" />
</head><body data-route-id="main"><div class="screen">
<header class="worr-topbar"><button id="shell-main-topbar-settings" data-command="pushmenu options"></button><button id="shell-main-topbar-quit" data-command="ui.popup"></button></header>
<div class="hero-menu"><img id="main-logo" /><div id="main-menu-actions"><button id="main-singleplayer" data-command="pushmenu singleplayer"></button><button id="main-multiplayer" data-command="pushmenu multiplayer"></button></div></div>
<footer class="worr-statusbar"></footer>
</div></body></rml>""",
        encoding="utf-8",
    )
    manifest = root / design.DEFAULT_MANIFEST
    manifest.write_text(json.dumps({"routes": [{
        "id": "main", "document": "assets/ui/rml/shell/main.rml", "wave": "A",
    }]}), encoding="utf-8")

    result = design.main(["--repo-root", str(root), "--format", "json"])
    payload = json.loads(capsys.readouterr().out)
    assert result == 0
    assert payload["ok"] is True

    document.write_text(
        document.read_text(encoding="utf-8").replace(
            '<button id="main-multiplayer" data-command="pushmenu multiplayer"></button>',
            '<button id="main-close" data-command="ui.back"></button>',
        ),
        encoding="utf-8",
    )
    result = design.main(["--repo-root", str(root)])
    output = capsys.readouterr().out
    assert result == 1
    assert "fixed hero must contain exactly" in output
    assert "redundant close" in output
