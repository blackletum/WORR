#!/usr/bin/env python3
"""Validate canonical WORR UX/UI structure and localizable authored copy."""

from __future__ import annotations

import argparse
import html
import json
import re
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


DEFAULT_MANIFEST = Path("tools/ui_smoke/rmlui_manifest.json")
DEFAULT_BASE_THEME = Path("assets/ui/rml/common/theme/base.rcss")
DEFAULT_ACCESSIBILITY_THEME = Path("assets/ui/rml/common/theme/accessibility.rcss")
DEFAULT_SHELL_THEME = Path("assets/ui/rml/common/theme/shell.rcss")
DEFAULT_SESSION_THEME = Path("assets/ui/rml/common/theme/session.rcss")
DEFAULT_ENGLISH_CATALOG = Path("assets/localization/loc_english.txt")
POPUP_ROUTES = frozenset({
    "quit_confirm", "forfeit_confirm", "leave_match_confirm",
    "tourney_replay_confirm",
})
PRIMARY_SETTINGS_ROUTES = frozenset({
    "video", "screen", "sound", "input", "effects", "performance",
    "accessibility", "language",
})
LEAF_COPY = re.compile(
    r"<(?P<tag>h1|h2|h3|p|span|label|button|option|th|td)"
    r"(?P<attrs>[^>]*)>(?P<text>[^<>]+)</(?P=tag)>",
    re.DOTALL,
)
LOC_KEY_ATTRIBUTE = re.compile(r'\bdata-loc-key\s*=\s*(["\'])(?P<key>[^"\']+)\1')
CATALOG_ASSIGNMENT = re.compile(r"^([A-Za-z0-9_]+)(?:\s+[^=]+)?\s*=")
DYNAMIC_COPY_ATTRIBUTES = (
    "data-bind=", "data-bind-cvar=", "data-label-cvar=", "data-text-cvar=",
)
COPY_EXEMPTIONS = frozenset({"WORR", "dev", "BO1", "BO3", "BO5", "BO7", "BO9"})


@dataclass
class DesignComplianceReport:
    repo_root: Path
    routes: int = 0
    documents: int = 0
    localizable_leaf_copy: int = 0
    localization_hooks: int = 0
    localization_catalog_keys: int = 0
    standard_chrome_routes: int = 0
    session_chrome_routes: int = 0
    popup_routes: int = 0
    settings_tab_routes: int = 0
    errors: list[str] = field(default_factory=list)

    def ok(self) -> bool:
        return not self.errors


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[2]


def resolve(path: Path, repo_root: Path) -> Path:
    return path.resolve() if path.is_absolute() else (repo_root / path).resolve()


def class_tokens(element: ET.Element) -> set[str]:
    return set(element.attrib.get("class", "").split())


def elements_with_class(root: ET.Element, token: str) -> list[ET.Element]:
    return [element for element in root.iter() if token in class_tokens(element)]


def visible_leaf_copy(source: str) -> list[tuple[str, str, str]]:
    leaves: list[tuple[str, str, str]] = []
    for match in LEAF_COPY.finditer(source):
        attributes = match.group("attrs")
        text = " ".join(html.unescape(match.group("text")).split())
        if any(token in attributes for token in DYNAMIC_COPY_ATTRIBUTES):
            continue
        if (
            text in COPY_EXEMPTIONS
            or len(text) == 1
            or not re.search(r"[A-Za-z]", text)
        ):
            continue
        leaves.append((match.group("tag"), attributes, text))
    return leaves


def validate_design_compliance(
    repo_root: Path,
    manifest_path: Path = DEFAULT_MANIFEST,
    base_theme_path: Path = DEFAULT_BASE_THEME,
    accessibility_theme_path: Path = DEFAULT_ACCESSIBILITY_THEME,
    shell_theme_path: Path = DEFAULT_SHELL_THEME,
    session_theme_path: Path = DEFAULT_SESSION_THEME,
    english_catalog_path: Path = DEFAULT_ENGLISH_CATALOG,
) -> DesignComplianceReport:
    repo_root = repo_root.resolve()
    report = DesignComplianceReport(repo_root=repo_root)
    manifest_file = resolve(manifest_path, repo_root)
    base_file = resolve(base_theme_path, repo_root)
    accessibility_file = resolve(accessibility_theme_path, repo_root)
    shell_file = resolve(shell_theme_path, repo_root)
    session_file = resolve(session_theme_path, repo_root)
    english_catalog_file = resolve(english_catalog_path, repo_root)

    catalog_keys: set[str] = set()
    if english_catalog_file.is_file():
        for line in english_catalog_file.read_text(
            encoding="utf-8", errors="replace"
        ).splitlines():
            match = CATALOG_ASSIGNMENT.match(line.strip())
            if match:
                catalog_keys.add(match.group(1))
    else:
        report.errors.append("English localization catalog is missing")
    report.localization_catalog_keys = len(catalog_keys)

    try:
        manifest = json.loads(manifest_file.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        report.errors.append(f"manifest could not be read: {exc}")
        return report

    routes = manifest.get("routes")
    if not isinstance(routes, list):
        report.errors.append("manifest routes must be an array")
        return report
    report.routes = len(routes)

    for route in routes:
        route_id = route.get("id", "<missing>")
        document_value = route.get("document")
        if not isinstance(document_value, str):
            report.errors.append(f"{route_id}: document path is missing")
            continue
        document = resolve(Path(document_value), repo_root)
        if not document.is_file():
            report.errors.append(f"{route_id}: document is missing")
            continue

        source = document.read_text(encoding="utf-8", errors="replace")
        try:
            root = ET.fromstring(source)
        except ET.ParseError as exc:
            report.errors.append(f"{route_id}: invalid RML: {exc}")
            continue
        report.documents += 1

        body = root.find("body")
        if body is None or body.attrib.get("data-route-id") != route_id:
            report.errors.append(f"{route_id}: body data-route-id is missing or mismatched")

        links = root.findall("./head/link")
        if not links or not links[-1].attrib.get("href", "").endswith("accessibility.rcss"):
            report.errors.append(f"{route_id}: accessibility.rcss must be the final theme import")

        screens = elements_with_class(root, "screen")
        if not screens:
            report.errors.append(f"{route_id}: screen root is missing")

        for tag, attributes, text in visible_leaf_copy(source):
            report.localizable_leaf_copy += 1
            if "data-loc-key=" not in attributes:
                report.errors.append(
                    f"{route_id}: unlocalizable {tag} copy: {text[:80]}"
                )
            else:
                report.localization_hooks += 1
                key_match = LOC_KEY_ATTRIBUTE.search(attributes)
                key = key_match.group("key") if key_match else ""
                if key not in catalog_keys:
                    report.errors.append(
                        f"{route_id}: localization key is absent from English catalog: {key}"
                    )

        for button in root.iter("button"):
            provider_action = any(
                name.startswith("data-") and (
                    name.endswith("-action")
                    or name in {"data-bind-command", "data-event-click"}
                )
                for name in button.attrib
            )
            if not button.attrib.get("id") and not provider_action:
                report.errors.append(f"{route_id}: button without stable id")
            if not (
                button.attrib.get("data-command")
                or button.attrib.get("data-command-cvar")
                or provider_action
                or button.attrib.get("disabled")
            ):
                report.errors.append(
                    f"{route_id}: button {button.attrib.get('id', '<missing>')} has no command"
                )

        if route_id in POPUP_ROUTES:
            report.popup_routes += 1
            dialogs = elements_with_class(root, "popup-dialog")
            actions = elements_with_class(root, "popup-actions")
            if not elements_with_class(root, "popup-screen") or not dialogs or not actions:
                report.errors.append(f"{route_id}: popup archetype structure is incomplete")
            elif not dialogs[0].attrib.get("data-confirm-kind"):
                report.errors.append(f"{route_id}: popup intent underglow is not declared")
            else:
                buttons = list(actions[0].iter("button"))
                if len(buttons) < 2 or "popup-secondary" not in class_tokens(buttons[0]):
                    report.errors.append(f"{route_id}: safe popup action must be first")
            continue

        if route_id == "main":
            if not elements_with_class(root, "hero-menu"):
                report.errors.append("main: hero archetype class is missing")
            main_actions = [
                element for element in root.iter()
                if element.attrib.get("id") == "main-menu-actions"
            ]
            main_action_ids = [
                button.attrib.get("id")
                for button in (list(main_actions[0].iter("button")) if main_actions else [])
            ]
            if main_action_ids != ["main-singleplayer", "main-multiplayer"]:
                report.errors.append(
                    "main: fixed hero must contain exactly Single Player and Multiplayer"
                )
            if not any(element.attrib.get("id") == "main-logo" for element in root.iter()):
                report.errors.append("main: primary WORR logo is missing")
            required_utilities = {
                "shell-main-topbar-settings", "shell-main-topbar-quit",
            }
            authored_ids = {
                element.attrib.get("id") for element in root.iter()
                if element.attrib.get("id")
            }
            if not required_utilities.issubset(authored_ids):
                report.errors.append("main: Settings and power utilities must remain available")
            if "main-close" in authored_ids or elements_with_class(root, "main-brandplate"):
                report.errors.append("main: redundant close or corner brandplate chrome is present")
            if elements_with_class(root, "menu-header"):
                report.errors.append("main: redundant title header is present")
            continue

        if route.get("wave") == "C":
            report.session_chrome_routes += 1
            if not elements_with_class(root, "session-screen"):
                report.errors.append(f"{route_id}: session chrome is missing")
        else:
            report.standard_chrome_routes += 1
            for token in ("worr-topbar", "worr-statusbar"):
                if not elements_with_class(root, token):
                    report.errors.append(f"{route_id}: {token} is missing")

        if route_id not in {"dm_join", "join"}:
            for token in ("worr-titlerow", "worr-backplate"):
                if not elements_with_class(root, token):
                    report.errors.append(f"{route_id}: {token} is missing")

        if route_id in PRIMARY_SETTINGS_ROUTES:
            report.settings_tab_routes += 1
            tabs = elements_with_class(root, "worr-tab")
            if not elements_with_class(root, "worr-tabstrip") or len(tabs) != 8:
                report.errors.append(f"{route_id}: eight-tab Settings strip is incomplete")

    base = base_file.read_text(encoding="utf-8", errors="replace") if base_file.is_file() else ""
    accessibility = (
        accessibility_file.read_text(encoding="utf-8", errors="replace")
        if accessibility_file.is_file() else ""
    )
    shell = shell_file.read_text(encoding="utf-8", errors="replace") if shell_file.is_file() else ""
    session = (
        session_file.read_text(encoding="utf-8", errors="replace")
        if session_file.is_file() else ""
    )
    for token in (
        ".worr-topbar", ".worr-statusbar", ".worr-backplate",
        ".ui-button-cta", ".popup-dialog",
    ):
        if token not in base:
            report.errors.append(f"base theme is missing {token}")
    for token in (
        ".ui-high-visibility", ".ui-reduced-motion", ".ui-a11y-large-text",
        "animation: none", "transition: none", "min-height: 48px",
    ):
        if token not in accessibility:
            report.errors.append(f"accessibility theme is missing {token}")
    for token in (
        "#main-menu-stack", "#main-menu-actions", "overflow: hidden",
        ".main-choice-caption",
    ):
        if token not in shell:
            report.errors.append(f"shell theme is missing fixed main-menu contract token {token}")
    for token in (
        'body[data-route-group="session"]', ".session-screen",
        "top: 32px", "right: 40px", "bottom: 32px", "left: 40px",
    ):
        if token not in session:
            report.errors.append(f"session theme is missing in-world frame token {token}")
    if ".ui-session-overlay" not in base:
        report.errors.append("base theme is missing live-match session overlay styling")
    return report


def json_payload(report: DesignComplianceReport) -> dict[str, Any]:
    return {
        "ok": report.ok(),
        "counts": {
            "routes": report.routes,
            "documents": report.documents,
            "localizable_leaf_copy": report.localizable_leaf_copy,
            "localization_hooks": report.localization_hooks,
            "localization_catalog_keys": report.localization_catalog_keys,
            "standard_chrome_routes": report.standard_chrome_routes,
            "session_chrome_routes": report.session_chrome_routes,
            "popup_routes": report.popup_routes,
            "settings_tab_routes": report.settings_tab_routes,
            "errors": len(report.errors),
        },
        "errors": report.errors,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=repo_root_from_script())
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--format", choices=("text", "json"), default="text")
    args = parser.parse_args(argv)
    report = validate_design_compliance(args.repo_root, args.manifest)
    if args.format == "json":
        print(json.dumps(json_payload(report), indent=2, sort_keys=True))
    else:
        counts = json_payload(report)["counts"]
        print("RmlUi canonical UX/UI design compliance:")
        for name, value in counts.items():
            print(f"  {name}: {value}")
        for error in report.errors:
            print(f"  - {error}")
        print(f"Result: RmlUi design compliance check {'passed' if report.ok() else 'failed'}.")
    return 0 if report.ok() else 1


if __name__ == "__main__":
    raise SystemExit(main())
