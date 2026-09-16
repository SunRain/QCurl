"""Release metadata and legacy identity scanning."""

from __future__ import annotations

import re
import sys
from pathlib import Path


def _is_release_identity_scan_target(path: Path) -> bool:
    excluded_parts = {
        ".git",
        ".helloagents",
        ".claude",
        "curl",
        "node_modules",
        "__pycache__",
    }
    if any(
        part in excluded_parts or part == "build" or part.startswith("build-")
        for part in path.parts
    ):
        return False
    if path.suffix in {".pyc", ".so", ".a", ".o", ".png", ".jpg", ".jpeg", ".gif", ".pdf"}:
        return False
    return path.is_file()


def _is_allowed_release_identity_match(relative: str, line: str) -> bool:
    allowed_file_prefixes = (
        "docs/dev/archive/",
        "docs/user/migration-2.0.md",
        "docs/dev/architecture/overview.md",
        "scripts/qcurl_abi_gate.py",
        "scripts/release_gate_steps.py",
        "scripts/release_metadata_gate.py",
        "scripts/run_release_gate.py",
        "tests/public_api/test_run_public_api_checks.py",
        "tests/public_api/test_hard_break_docs.py",  # Contains intentional negative identity fixtures.
        "abi/baseline/qcurl-core-v3.abi.xml",
    )
    if relative.startswith(allowed_file_prefixes):
        return True
    external_context = (
        "HTTP/2",
        "HTTP/3",
        "Qt 6",
        "Qt6",
        "libcurl",
        "CMake >= 3.0.0",
        "cmake_policy(VERSION 3.0.0",
        "License",
        "spdx",
        "SPDX",
    )
    return any(token in line for token in external_context)


def _is_historical_release_document(relative: str) -> bool:
    return relative.startswith("docs/dev/archive/")


def _fixed_metadata_violations(repo_root: Path) -> list[str]:
    checks = {
        "CMakeLists.txt": ["WebSocket support\")", "HTTP/2 and WebSocket support"],
        "README.md": ["单请求延迟", "31,000 ms", "~15,000 ms", "~10,000 ms"],
        "docs/dev/architecture/overview.md": [
            "提供同步和异步两种网络请求方式",
            "| **执行模式** | 同步、异步 |",
            "enum class ExecutionMode",
            "同时支持所有 HTTP 方法和同步/异步模式",
            "QCWebSocket (WebSocket 客户端)",
            "QCNetworkDiagnostics (网络诊断)",
            "sendOptions(",
        ],
    }
    violations: list[str] = []
    for relative, forbidden_values in checks.items():
        path = repo_root / relative
        if not path.is_file():
            violations.append(f"missing metadata scan target: {path}")
            continue
        text = path.read_text(encoding="utf-8")
        for needle in forbidden_values:
            if needle in text:
                violations.append(f"{relative}: forbidden release metadata text: {needle}")
    return violations


def _legacy_identity_violations(repo_root: Path) -> list[str]:
    forbidden_patterns = [
        re.compile(r"\b3\.0\.0(?:-rc\.1)?\b"),
        re.compile(r"\bqcurl-core-v3\b"),
        re.compile(r"\blibQCurl\.so\.3\b"),
        re.compile(r"\bSOVERSION\s+3\b"),
        re.compile(r"\bQCurl\s+v2\.[0-9]+\.[0-9]+\b"),
        re.compile(r"\bv2\.x\b"),
    ]
    violations: list[str] = []
    for path in repo_root.rglob("*"):
        if not _is_release_identity_scan_target(path):
            continue
        relative = path.relative_to(repo_root).as_posix()
        try:
            lines = path.read_text(encoding="utf-8").splitlines()
        except UnicodeDecodeError:
            continue
        for line_number, line in enumerate(lines, start=1):
            if _is_allowed_release_identity_match(relative, line):
                continue
            if any(pattern.search(line) for pattern in forbidden_patterns):
                violations.append(
                    f"{relative}:{line_number}: forbidden legacy release identity: {line.strip()}"
                )
    return violations


def _current_identity_violations(repo_root: Path) -> list[str]:
    forbidden_patterns = [
        re.compile(r"\bproject\s*\(\s*QCurl\s+VERSION\s+1\.0\.0\b", re.IGNORECASE),
        re.compile(r"\bSOVERSION\s+1\b"),
        re.compile(r"\blibQCurl\.so\.1\.0\.0\b"),
        re.compile(
            r"\bQCurl\s+1\.0\.0\s+first\s+stable\s+candidate\b",
            re.IGNORECASE,
        ),
        re.compile(
            r"\bv1\.0\.0\b[^;；\n]*(?:candidate|not created|not published|尚未创建|尚未发布)",
            re.IGNORECASE,
        ),
    ]
    violations: list[str] = []
    for path in repo_root.rglob("*"):
        if not _is_release_identity_scan_target(path):
            continue
        relative = path.relative_to(repo_root).as_posix()
        if _is_historical_release_document(relative):
            continue
        try:
            lines = path.read_text(encoding="utf-8").splitlines()
        except UnicodeDecodeError:
            continue
        for line_number, line in enumerate(lines, start=1):
            if _is_allowed_release_identity_match(relative, line):
                continue
            if any(pattern.search(line) for pattern in forbidden_patterns):
                violations.append(
                    f"{relative}:{line_number}: forbidden current release identity: {line.strip()}"
                )
    return violations


def scan_metadata(repo_root: Path) -> int:
    """Scan release-owned metadata while excluding generated build trees."""

    violations = _fixed_metadata_violations(repo_root)
    violations.extend(_current_identity_violations(repo_root))
    violations.extend(_legacy_identity_violations(repo_root))
    if violations:
        print("\n".join(violations), file=sys.stderr)
        return 1
    print("[release_gate] metadata scan passed")
    return 0
