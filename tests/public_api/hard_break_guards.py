"""Hard-break API guard checks for source, test, and documentation surfaces."""

from __future__ import annotations

import re
import sys
from collections.abc import Iterable
from pathlib import Path

from tests.public_api.component_contracts import FailFunc


DENY_RULES: tuple[tuple[str, re.Pattern[str]], ...] = (
    (
        "old fromSingleFileDevice ownerThread",
        re.compile(r"fromSingleFileDevice\s*\(\s*QThread\s*\*"),
    ),
    (
        "old fromSingleFileDevice return",
        re.compile(r"(?:static\s+)?QCNetworkMultipartBody\s+fromSingleFileDevice\s*\("),
    ),
    (
        "old multipart invalid-object predicate",
        re.compile(r"fromSingleFileDevice.*(?:isValid|errorString)\s*\("),
    ),
    ("releaseDevice", re.compile(r"\breleaseDevice\s*\(")),
    (
        "old QCNetworkAccessManager exportCookies return",
        re.compile(r"QList\s*<\s*QNetworkCookie\s*>\s+exportCookies\s*\("),
    ),
    (
        "old QCCurlMultiManager exportCookiesForManager return",
        re.compile(r"QList\s*<\s*QNetworkCookie\s*>\s+exportCookiesForManager\s*\("),
    ),
    ("QCNetworkRequest virtual destructor", re.compile(r"\bvirtual\s+~\s*QCNetworkRequest\s*\(")),
    (
        "Blocking Extras std::function progress callback",
        re.compile(r"QCBlockingProgressCallback\s*=\s*std::function\b"),
    ),
    ("removed QCNetworkReply ExecutionMode", re.compile(r"\bExecutionMode\b")),
    ("removed QCNetworkReply DataFunction typedef", re.compile(r"\busing\s+DataFunction\b")),
    ("removed QCNetworkReply SeekFunction typedef", re.compile(r"\busing\s+SeekFunction\b")),
    ("removed QCNetworkReply ProgressFunction typedef", re.compile(r"\busing\s+ProgressFunction\b")),
    (
        "removed QCNetworkReply callback setter",
        re.compile(r"\bQCNetworkReply\s*::\s*set(?:Write|Header|Seek|Progress)Callback\s*\("),
    ),
    (
        "removed QCNetworkReply callback setter call",
        re.compile(r"\breply\s*(?:->|\.)\s*set(?:Write|Header|Seek|Progress)Callback\s*\("),
    ),
    (
        "removed QCNetworkAccessManager sendDelete",
        re.compile(r"\bsendDelete\s*\("),
    ),
    (
        "removed Blocking Extras generic send",
        re.compile(r"\bQCBlockingNetworkClient\s*::\s*send\s*\(|\bclient\s*\.\s*send\s*\("),
    ),
)

INCLUDE_PATHS: tuple[str, ...] = (
    "README.md",
    "SYSTEM_DOCUMENTATION.md",
    "src",
    "include",
    "examples",
    "tests/public_api/consumer_smoke",
    "tests/public_api/consumer_blocking_extras_smoke",
    "docs/user",
    "docs/arch",
    "tests/qcurl",
    "tests/libcurl_consistency",
)

EXCLUDED_PARTS = {".git", "build", "generated", ".helloagents", "__pycache__", "node_modules"}

ALLOWLIST: dict[str, tuple[str, ...]] = {
    "SYSTEM_DOCUMENTATION.md": (
        "enum class ExecutionMode { Async, Sync };",
        "#### ExecutionMode",
        "enum class ExecutionMode {",
        "return createReply(request, HttpMethod::Options, ExecutionMode::Async);",
    ),
}


def _is_allowlisted(relative: str, line: str, allowlist: dict[str, tuple[str, ...]]) -> bool:
    return any(snippet in line for snippet in allowlist.get(relative, ()))


def _scan_file(repo_root: Path, path: Path, allowlist: dict[str, tuple[str, ...]]) -> list[str]:
    try:
        content = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return []

    findings: list[str] = []
    relative = path.relative_to(repo_root).as_posix()
    for line_number, line in enumerate(content.splitlines(), start=1):
        if _is_allowlisted(relative, line, allowlist):
            continue
        for rule_name, pattern in DENY_RULES:
            if pattern.search(line):
                findings.append(f"{relative}:{line_number}: {rule_name}: {line.strip()}")
    return findings


def scan_hard_break_guards(
    repo_root: Path,
    fail_func: FailFunc,
    *,
    include_paths: Iterable[str] = INCLUDE_PATHS,
    allowlist: dict[str, tuple[str, ...]] | None = None,
) -> int:
    """Scan live surfaces for APIs removed by hard-break cleanup."""

    active_allowlist = ALLOWLIST if allowlist is None else allowlist
    violations: list[str] = []
    for include_path in include_paths:
        root = repo_root / include_path
        if not root.exists():
            continue
        files = root.rglob("*") if root.is_dir() else [root]
        for path in files:
            if not path.is_file() or any(part in EXCLUDED_PARTS for part in path.parts):
                continue
            violations.extend(_scan_file(repo_root, path, active_allowlist))

    if violations:
        print("\n".join(violations), file=sys.stderr)
        return 1

    print("[public_api] hard-break guard scan passed")
    return 0
