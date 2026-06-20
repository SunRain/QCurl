#!/usr/bin/env python3
"""Fail when QCurl production sources bypass the libcurl binding contract."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from check_deprecated_curl_apis import strip_comments_and_strings


SOURCE_SUFFIXES = {
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
    ".ipp",
    ".inl",
}

EASY_INIT_TOKEN = "curl_easy_init"
NOSIGNAL_TOKEN = "CURLOPT_NOSIGNAL"
GLOBAL_INIT_TOKEN = "CurlGlobalConstructor::instance"
QT_NETWORK_MANAGER_TOKEN = "QNetworkAccessManager"
HANDLE_MANAGER_PATH = Path("src/QCCurlHandleManager.cpp")


def iter_source_files(source_root: Path) -> list[Path]:
    """Return production source files under the given source root."""
    return [
        path
        for path in sorted(source_root.rglob("*"))
        if path.is_file() and path.suffix in SOURCE_SUFFIXES
    ]


def line_occurrences(path: Path, token: str) -> list[int]:
    """Return line numbers that contain token after stripping comments and strings."""
    stripped = strip_comments_and_strings(path.read_text(encoding="utf-8"))
    return [
        line_number
        for line_number, line in enumerate(stripped.splitlines(), start=1)
        if token in line
    ]


def collect_token_violations(source_root: Path, token: str, allowed_relative: Path) -> list[str]:
    """Return diagnostics for token occurrences outside the allowed file."""
    violations: list[str] = []
    for path in iter_source_files(source_root):
        relative = path.relative_to(source_root.parent)
        for line_number in line_occurrences(path, token):
            if relative == allowed_relative:
                continue
            violations.append(f"{relative}:{line_number}: forbidden {token}")
    return violations


def collect_forbidden_token_violations(source_root: Path, token: str) -> list[str]:
    """Return diagnostics for token occurrences that are never allowed in production sources."""
    violations: list[str] = []
    for path in iter_source_files(source_root):
        relative = path.relative_to(source_root.parent)
        for line_number in line_occurrences(path, token):
            violations.append(f"{relative}:{line_number}: forbidden {token}")
    return violations


def check_handle_manager_order(source_root: Path) -> list[str]:
    """Validate global init and common defaults order inside QCCurlHandleManager."""
    path = source_root.parent / HANDLE_MANAGER_PATH
    if not path.exists():
        return [f"{HANDLE_MANAGER_PATH}: missing handle manager source"]

    global_init_lines = line_occurrences(path, GLOBAL_INIT_TOKEN)
    easy_init_lines = line_occurrences(path, EASY_INIT_TOKEN)
    nosignal_lines = line_occurrences(path, NOSIGNAL_TOKEN)
    diagnostics: list[str] = []

    if len(easy_init_lines) != 1:
        diagnostics.append(
            f"{HANDLE_MANAGER_PATH}: expected exactly one {EASY_INIT_TOKEN}, "
            f"found {len(easy_init_lines)}"
        )
    if len(nosignal_lines) != 1:
        diagnostics.append(
            f"{HANDLE_MANAGER_PATH}: expected exactly one {NOSIGNAL_TOKEN}, "
            f"found {len(nosignal_lines)}"
        )
    if not global_init_lines:
        diagnostics.append(f"{HANDLE_MANAGER_PATH}: missing {GLOBAL_INIT_TOKEN}")

    if diagnostics or not easy_init_lines:
        return diagnostics

    easy_init_line = easy_init_lines[0]
    if min(global_init_lines, default=sys.maxsize) > easy_init_line:
        diagnostics.append(
            f"{HANDLE_MANAGER_PATH}: {GLOBAL_INIT_TOKEN} must precede {EASY_INIT_TOKEN}"
        )
    if nosignal_lines and nosignal_lines[0] < easy_init_line:
        diagnostics.append(f"{HANDLE_MANAGER_PATH}: {NOSIGNAL_TOKEN} must follow handle creation")

    return diagnostics


def build_parser() -> argparse.ArgumentParser:
    """Create the CLI parser."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    return parser


def main(argv: list[str]) -> int:
    """CLI entry point."""
    parser = build_parser()
    args = parser.parse_args(argv)

    source_root = args.source_root.resolve()
    violations: list[str] = []
    violations.extend(collect_token_violations(source_root, EASY_INIT_TOKEN, HANDLE_MANAGER_PATH))
    violations.extend(collect_token_violations(source_root, NOSIGNAL_TOKEN, HANDLE_MANAGER_PATH))
    violations.extend(collect_forbidden_token_violations(source_root, QT_NETWORK_MANAGER_TOKEN))
    violations.extend(check_handle_manager_order(source_root))

    if violations:
        print("[libcurl_binding_contract_guard] violations found:", file=sys.stderr)
        for violation in violations:
            print(violation, file=sys.stderr)
        return 1

    print("[libcurl_binding_contract_guard] ok: easy handle creation and NOSIGNAL are centralized")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
