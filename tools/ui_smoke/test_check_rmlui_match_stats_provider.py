from __future__ import annotations

import json
import sys
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_match_stats_provider as match_stats_provider  # noqa: E402


SOURCE_ROOT = SCRIPT_DIR.parents[1]


def _write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def _write_valid_repo(repo_root: Path) -> None:
    paths = (
        match_stats_provider.RUNTIME,
        match_stats_provider.RUNTIME_ROUTES,
        match_stats_provider.CGAME_UI,
        match_stats_provider.PUBLISHER,
        match_stats_provider.CLIENT_COMMANDS,
        match_stats_provider.PLAYER_VIEW,
        match_stats_provider.G_LOCAL,
        match_stats_provider.DOCUMENT,
        match_stats_provider.SESSION_THEME,
        match_stats_provider.ACCESSIBILITY_THEME,
        match_stats_provider.SESSION_ROUTES,
        match_stats_provider.MANIFEST,
        match_stats_provider.CAPTURE_HARNESS,
    )
    for relative in paths:
        _write(repo_root / relative, (SOURCE_ROOT / relative).read_text(encoding="utf-8"))


def test_valid_match_stats_provider_passes(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    assert match_stats_provider.main(["--repo-root", str(repo_root)]) == 0
    output = capsys.readouterr().out
    assert "Routes checked: 1" in output
    assert "Semantic stat bindings checked: 10" in output


def test_missing_semantic_publisher_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    publisher = repo_root / match_stats_provider.PUBLISHER
    publisher.write_text(
        publisher.read_text(encoding="utf-8").replace(
            '"ui_matchstats_accuracy",',
            '"ui_matchstats_accuracy_missing",',
            1,
        ),
        encoding="utf-8",
    )
    assert match_stats_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "semantic match-stats publisher" in capsys.readouterr().err


def test_live_refresh_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    view = repo_root / match_stats_provider.PLAYER_VIEW
    view.write_text(
        view.read_text(encoding="utf-8").replace(
            "constexpr GameTime kMatchStatsUpdateInterval = 1_sec",
            "constexpr GameTime kMatchStatsUpdateInterval = 99_sec",
            1,
        ),
        encoding="utf-8",
    )
    assert match_stats_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "per-frame match-stats lifecycle" in capsys.readouterr().err


def test_disconnected_close_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    runtime = repo_root / match_stats_provider.RUNTIME
    runtime.write_text(
        runtime.read_text(encoding="utf-8").replace(
            '        !strcmp(command, "worr_matchstats_close")) {',
            '        !strcmp(command, "worr_matchstats_close_missing")) {',
            1,
        ),
        encoding="utf-8",
    )
    assert match_stats_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "disconnected-close bridge" in capsys.readouterr().err


def test_raw_compatibility_report_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / match_stats_provider.DOCUMENT
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            "</main>",
            '<p data-bind-cvar="ui_matchstats_line_0"></p>\n      </main>',
            1,
        ),
        encoding="utf-8",
    )
    assert match_stats_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "raw compatibility-line report" in capsys.readouterr().err


def test_stat_binding_drift_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / match_stats_provider.DOCUMENT
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            'data-bind-cvar="ui_matchstats_damage_ratio"',
            'data-bind-cvar="ui_matchstats_damage_ratio_missing"',
            1,
        ),
        encoding="utf-8",
    )
    assert match_stats_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "ui_matchstats_damage_ratio" in capsys.readouterr().err


def test_empty_state_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / match_stats_provider.DOCUMENT
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            'data-show-if="!ui_matchstats_player"',
            'data-show-if="ui_matchstats_player"',
            1,
        ),
        encoding="utf-8",
    )
    assert match_stats_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "truthful direct-route empty state" in capsys.readouterr().err


def test_duplicate_back_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / match_stats_provider.DOCUMENT
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            "    </div>",
            '      <button data-command="popmenu; worr_matchstats_close">Back</button>\n    </div>',
            1,
        ),
        encoding="utf-8",
    )
    assert match_stats_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "duplicate Back action" in capsys.readouterr().err


def test_metadata_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    routes_path = repo_root / match_stats_provider.SESSION_ROUTES
    routes = json.loads(routes_path.read_text(encoding="utf-8"))
    next(route for route in routes["routes"] if route["id"] == "match_stats")[
        "status"
    ] = "starter_round3"
    routes_path.write_text(json.dumps(routes), encoding="utf-8")
    assert match_stats_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "promote match_stats" in capsys.readouterr().err


def test_parity_phase_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    routes_path = repo_root / match_stats_provider.SESSION_ROUTES
    routes = json.loads(routes_path.read_text(encoding="utf-8"))
    next(route for route in routes["routes"] if route["id"] == "match_stats")[
        "migration_phase"
    ] = "controller_stub"
    routes_path.write_text(json.dumps(routes), encoding="utf-8")
    assert match_stats_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "must remain parity_ready" in capsys.readouterr().err


def test_capture_registration_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    harness = repo_root / match_stats_provider.CAPTURE_HARNESS
    harness.write_text(
        harness.read_text(encoding="utf-8").replace(
            '    "match_stats": Path("session/match_stats.rml"),\n', "", 1
        ),
        encoding="utf-8",
    )
    assert match_stats_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "capture harness is missing" in capsys.readouterr().err
