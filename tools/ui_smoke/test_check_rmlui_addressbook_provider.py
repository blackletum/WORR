from __future__ import annotations

import sys
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_addressbook_provider as addressbook_provider  # noqa: E402


SOURCE_ROOT = SCRIPT_DIR.parents[1]


def _write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def _write_valid_repo(repo_root: Path) -> None:
    for relative in (
        addressbook_provider.RUNTIME,
        addressbook_provider.CLIENT_MAIN,
        addressbook_provider.DOCUMENT,
        addressbook_provider.UTILITY_THEME,
        addressbook_provider.CAPTURE_HARNESS,
    ):
        _write(
            repo_root / relative,
            (SOURCE_ROOT / relative).read_text(encoding="utf-8"),
        )


def test_valid_addressbook_provider_passes(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    assert addressbook_provider.main(["--repo-root", str(repo_root)]) == 0
    output = capsys.readouterr().out
    assert "Archived address fields checked: 16" in output
    assert "Result: RmlUi live Address Book provider check passed." in output


def test_missing_last_address_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / addressbook_provider.DOCUMENT
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            'id="addressbook-adr15"', 'id="addressbook-last-missing"', 1
        ),
        encoding="utf-8",
    )
    assert addressbook_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "addressbook-adr15" in capsys.readouterr().err


def test_non_archived_address_registration_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    client = repo_root / addressbook_provider.CLIENT_MAIN
    client.write_text(
        client.read_text(encoding="utf-8").replace(
            'Cvar_Get(va("adr%i", i), "", CVAR_ARCHIVE)',
            'Cvar_Get(va("adr%i", i), "", 0)',
            1,
        ),
        encoding="utf-8",
    )
    assert addressbook_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "CVAR_ARCHIVE" in capsys.readouterr().err


def test_address_limit_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / addressbook_provider.DOCUMENT
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            'maxlength="32"', 'maxlength="12"', 1
        ),
        encoding="utf-8",
    )
    assert addressbook_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "32-character" in capsys.readouterr().err


def test_missing_broadcast_source_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / addressbook_provider.DOCUMENT
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            " &quot;broadcast://&quot;", "", 1
        ),
        encoding="utf-8",
    )
    assert addressbook_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "three legacy server sources" in capsys.readouterr().err


def test_live_provider_identity_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / addressbook_provider.DOCUMENT
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            'data-document-status="live-provider"',
            'data-document-status="round2-scaffold"',
            1,
        ),
        encoding="utf-8",
    )
    assert addressbook_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "live-provider" in capsys.readouterr().err
