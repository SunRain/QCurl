"""Hard-break API guard checks for source, test, and documentation surfaces."""

from __future__ import annotations

import re
import sys
from collections.abc import Iterable
from pathlib import Path

from tests.public_api.component_contracts import FailFunc


QMAP_BYTE_ARRAY_TYPE = r"QMap\s*<\s*QByteArray\s*,\s*QByteArray\s*>"

CURRENT_RELEASE_DOC_PATHS = (
    "docs/dev/release/release-procedure.md",
    "docs/dev/build-and-test.md",
    "docs/dev/release/2.0.0-hard-break-release-contract.md",
    "CHANGELOG.md",
    "docs/dev/architecture/public-header-boundary.md",
    "docs/dev/uce/README.md",
)

POOL_SYNC_MUTATOR_PATHS = (
    "src/QCWebSocketPool.h",
    *CURRENT_RELEASE_DOC_PATHS,
)

WEBSOCKET_COMMAND_PATHS = (
    "src/QCWebSocket.h",
    "src/QCWebSocket.cpp",
    "docs/dev/architecture/public-header-boundary.md",
)


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
        "removed QCNetworkAccessManager sendHead",
        re.compile(r"\bsendHead\s*\("),
    ),
    (
        "removed QCNetworkAccessManager sendGet",
        re.compile(r"\bsendGet\s*\("),
    ),
    (
        "removed QCNetworkAccessManager sendPost",
        re.compile(r"\bsendPost\s*\("),
    ),
    (
        "removed QCNetworkAccessManager sendPut",
        re.compile(r"\bsendPut\s*\("),
    ),
    (
        "removed QCNetworkAccessManager sendPatch",
        re.compile(r"\bsendPatch\s*\("),
    ),
    (
        "removed Blocking Extras generic send",
        re.compile(r"\bQCBlockingNetworkClient\s*::\s*send\s*\(|\bclient\s*\.\s*send\s*\("),
    ),
)

CACHE_POLICY_NAMES = "parseExpirationDate|isCacheable|varyHeaderNames"
CACHE_POLICY_CALL_RE = re.compile(
    rf"\b(?:QCNetworkCache\s*::\s*)?(?:{CACHE_POLICY_NAMES})"
    rf"\s*\(\s*([A-Za-z_]\w*)\s*\)"
)
QMAP_VARIABLE_RE = re.compile(
    rf"\b(?:const\s+)?{QMAP_BYTE_ARRAY_TYPE}\s+(?:const\s+)?([A-Za-z_]\w*)\b"
)

SCHEDULER_PUBLIC_SURFACE_PATHS = (
    "README.md",
    "CHANGELOG.md",
    "docs/user/",
    "docs/dev/architecture/",
    "docs/dev/release/2.0.0-hard-break-release-contract.md",
    "examples/",
    "tests/public_api/consumer_smoke/",
)

SCHEDULER_PUBLIC_HEADER_PATHS = (
    "src/QCNetworkAccessManager.h",
)

SCHEDULER_COMMAND_IMPLEMENTATION_PATHS = (
    "src/private/QCNetworkRequestScheduler.cpp",
    "src/private/QCNetworkRequestSchedulerControl.cpp",
)

CACHE_POLICY_SOURCE_PATHS = (
    "src/",
    "include/",
    "examples/",
    "tests/",
)

SCOPED_DENY_RULES: tuple[tuple[str, re.Pattern[str], tuple[str, ...]], ...] = (
    (
        "logger owning smart pointer public ABI",
        re.compile(
            r"(?:QSharedPointer|std::shared_ptr)\s*<\s*QCNetwork(?:Default)?Logger\s*>"
        ),
        (
            "src/QCNetworkLogger.h",
            "src/QCNetworkDefaultLogger.h",
            "src/QCNetworkAccessManager.h",
            "src/QCNetworkReply.h",
        ),
    ),
    (
        "old void default logger file configuration",
        re.compile(r"\bvoid\s+enableFileOutput\s*\("),
        ("src/QCNetworkDefaultLogger.h",),
    ),
    (
        "old void logger result contract",
        re.compile(r"\bvirtual\s+void\s+log\s*\(\s*const\s+NetworkLogEntry"),
        ("src/QCNetworkLogger.h",),
    ),
    (
        "removed QCNetworkCache QMap parseExpirationDate overload",
        re.compile(
            rf"\bparseExpirationDate\s*\(\s*const\s+{QMAP_BYTE_ARRAY_TYPE}\s*&"
        ),
        CACHE_POLICY_SOURCE_PATHS,
    ),
    (
        "removed QCNetworkCache QMap isCacheable overload",
        re.compile(rf"\bisCacheable\s*\(\s*const\s+{QMAP_BYTE_ARRAY_TYPE}\s*&"),
        CACHE_POLICY_SOURCE_PATHS,
    ),
    (
        "removed QCNetworkCache QMap varyHeaderNames overload",
        re.compile(
            rf"\bvaryHeaderNames\s*\(\s*const\s+{QMAP_BYTE_ARRAY_TYPE}\s*&"
        ),
        CACHE_POLICY_SOURCE_PATHS,
    ),
    (
        "removed QCNetworkCache rawHeadersFromMap helper",
        re.compile(r"\brawHeadersFromMap\s*\("),
        CACHE_POLICY_SOURCE_PATHS,
    ),
    (
        "removed QCNetworkReply deleteLater declaration",
        re.compile(r"^\s*void\s+deleteLater\s*\(\s*\)\s*;\s*$"),
        ("src/QCNetworkReply.h",),
    ),
    (
        "removed QCNetworkReply deleteLater implementation",
        re.compile(r"\bQCNetworkReply\s*::\s*deleteLater\s*\("),
        ("src/QCNetworkReply.cpp", "src/private/QCNetworkReplyAccessors.cpp"),
    ),
    (
        "removed QCNetworkAccessManagerPrivate replyList field",
        re.compile(r"\breplyList\b"),
        ("src/QCNetworkAccessManager_p.h",),
    ),
    (
        "removed QCNetworkAccessManagerPrivate curlMultiHandle field",
        re.compile(r"\bcurlMultiHandle\b"),
        ("src/QCNetworkAccessManager_p.h",),
    ),
    (
        "removed QCNetworkAccessManagerPrivate timer field",
        re.compile(r"\btimer\b"),
        ("src/QCNetworkAccessManager_p.h",),
    ),
    (
        "removed QCNetworkAccessManagerPrivate socketDescriptor field",
        re.compile(r"\bsocketDescriptor\b"),
        ("src/QCNetworkAccessManager_p.h",),
    ),
    (
        "removed QCNetworkAccessManagerPrivate readNotifier field",
        re.compile(r"\breadNotifier\b"),
        ("src/QCNetworkAccessManager_p.h",),
    ),
    (
        "removed QCNetworkAccessManagerPrivate writeNotifier field",
        re.compile(r"\bwriteNotifier\b"),
        ("src/QCNetworkAccessManager_p.h",),
    ),
    (
        "removed QCNetworkAccessManagerPrivate errorNotifier field",
        re.compile(r"\berrorNotifier\b"),
        ("src/QCNetworkAccessManager_p.h",),
    ),
    (
        "removed QString lane request API",
        re.compile(r"\bsetLane\s*\(\s*(?:QStringLiteral|QString|QLatin1String|u?\"|\")"),
        SCHEDULER_PUBLIC_SURFACE_PATHS,
    ),
    (
        "removed manager scheduler workflow",
        re.compile(r"\bmanager\s*(?:\.|->)\s*scheduler\s*\("),
        SCHEDULER_PUBLIC_SURFACE_PATHS,
    ),
    (
        "removed scheduler direct config workflow",
        re.compile(
            r"#include\s*<QCNetworkRequestScheduler\.h>"
            r"|\bQCNetworkRequestScheduler\s*::\s*instance\b"
            r"|\bscheduler\s*(?:->|\.)\s*(?:setConfig|setLaneConfig)\s*\("
        ),
        SCHEDULER_PUBLIC_SURFACE_PATHS,
    ),
    (
        "removed scheduler direct config method from public header",
        re.compile(
            r"\bstatic\s+QCNetworkRequestScheduler\s*\*\s*instance\s*\("
            r"|\bvoid\s+setConfig\s*\("
            r"|\bConfig\s+config\s*\("
            r"|\bvoid\s+setLaneConfig\s*\("
            r"|\bLaneConfig\s+laneConfig\s*\("
        ),
        SCHEDULER_PUBLIC_HEADER_PATHS,
    ),
    (
        "old scheduler void command result",
        re.compile(
            r"\bvoid\s+(?:cancelScheduledRequest)\s*\("
        ),
        SCHEDULER_PUBLIC_HEADER_PATHS,
    ),
    (
        "old scheduler bool command result",
        re.compile(
            r"\bbool\s+(?:deferScheduledRequest|undeferScheduledRequest|setScheduledRequestPriority)\s*\("
        ),
        SCHEDULER_PUBLIC_HEADER_PATHS,
    ),
    (
        "old scheduler int lane cancel result",
        re.compile(r"\bint\s+cancelLaneRequests\s*\("),
        SCHEDULER_PUBLIC_HEADER_PATHS,
    ),
    (
        "removed scheduler command wrong-thread marshal",
        re.compile(
            r"invokeOnSchedulerOwnerThread\s*\([^;]*"
            r"(?:scheduleReply|deferPendingRequest|undeferRequest|cancelRequest|"
            r"cancelAllRequests|cancelLaneRequests|changePriority)"
        ),
        SCHEDULER_COMMAND_IMPLEMENTATION_PATHS,
    ),
    (
        "legacy release gate build-dir option",
        re.compile(
            r"run_release_gate\.py\b[^\n]*--build-dir\b"
            r"|--(?:static-build-dir|test-build-dir)\b"
        ),
        CURRENT_RELEASE_DOC_PATHS,
    ),
    (
        "legacy three-tree release topology",
        re.compile(r"三棵|three[- ]tree", re.IGNORECASE),
        CURRENT_RELEASE_DOC_PATHS,
    ),
    (
        "old QCWebSocketPool void mutator contract",
        re.compile(r"\bvoid\s+(?:release|clearPool|setConfig)\s*\("),
        POOL_SYNC_MUTATOR_PATHS,
    ),
    (
        "removed QCNetworkConnectionPoolManager closeIdleConnections",
        re.compile(r"\bcloseIdleConnections\s*\("),
        ("src/QCNetworkConnectionPoolManager.h", "src/QCNetworkConnectionPoolManager.cpp"),
    ),
    (
        "removed QCWebSocketPool pointer release contract",
        re.compile(r"\brelease\s*\(\s*(?:const\s+)?QCWebSocket\s*\*"),
        POOL_SYNC_MUTATOR_PATHS,
    ),
    (
        "old QCWebSocket void command contract",
        re.compile(r"\bvoid\s+(?:open|close|ping|pong)\s*\("),
        WEBSOCKET_COMMAND_PATHS,
    ),
    (
        "old QCWebSocket qint64 send contract",
        re.compile(r"\bqint64\s+send(?:Text|Binary)Message\s*\("),
        WEBSOCKET_COMMAND_PATHS,
    ),
)

SCOPED_CONTENT_DENY_RULES: tuple[
    tuple[str, re.Pattern[str], tuple[str, ...]], ...
] = ()

INCLUDE_PATHS: tuple[str, ...] = (
    "README.md",
    "CHANGELOG.md",
    "src",
    "include",
    "examples",
    "tests/public_api/consumer_smoke",
    "tests/public_api/consumer_blocking_extras_smoke",
    "docs/user",
    "docs/dev/architecture",
    "docs/dev/release/2.0.0-hard-break-release-contract.md",
    "docs/dev/release/release-procedure.md",
    "docs/dev/build-and-test.md",
    "docs/dev/uce/README.md",
    "tests/qcurl",
    "tests/libcurl_consistency",
)

EXCLUDED_PARTS = {".git", "build", "generated", ".helloagents", "__pycache__", "node_modules"}

ALLOWLIST: dict[str, tuple[str, ...]] = {
    "docs/dev/architecture/overview.md": (
        "enum class ExecutionMode { Async, Sync };",
        "#### ExecutionMode",
        "enum class ExecutionMode {",
        "return createReply(request, HttpMethod::Options, ExecutionMode::Async);",
    ),
}


def _is_allowlisted(relative: str, line: str, allowlist: dict[str, tuple[str, ...]]) -> bool:
    return any(snippet in line for snippet in allowlist.get(relative, ()))


def _scan_qmap_cache_policy_calls(
    relative: str,
    lines: list[str],
    allowlist: dict[str, tuple[str, ...]],
) -> list[str]:
    findings: list[str] = []
    variables_by_depth: dict[int, set[str]] = {}
    brace_depth = 0

    for line_number, line in enumerate(lines, start=1):
        if not _is_allowlisted(relative, line, allowlist):
            for match in QMAP_VARIABLE_RE.finditer(line):
                variables_by_depth.setdefault(brace_depth, set()).add(match.group(1))
            for match in CACHE_POLICY_CALL_RE.finditer(line):
                variable_name = match.group(1)
                if any(variable_name in names for names in variables_by_depth.values()):
                    findings.append(
                        f"{relative}:{line_number}: removed QCNetworkCache QMap policy call site: "
                        f"{line.strip()}"
                    )

        brace_depth += line.count("{") - line.count("}")
        for depth in tuple(variables_by_depth):
            if depth > brace_depth:
                del variables_by_depth[depth]

    return findings


def _scan_file(repo_root: Path, path: Path, allowlist: dict[str, tuple[str, ...]]) -> list[str]:
    try:
        content = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return []

    findings: list[str] = []
    relative = path.relative_to(repo_root).as_posix()
    lines = content.splitlines()
    for line_number, line in enumerate(lines, start=1):
        if _is_allowlisted(relative, line, allowlist):
            continue
        for rule_name, pattern in DENY_RULES:
            if pattern.search(line):
                findings.append(f"{relative}:{line_number}: {rule_name}: {line.strip()}")
        for rule_name, pattern, path_prefixes in SCOPED_DENY_RULES:
            if not any(relative == prefix.rstrip("/") or relative.startswith(prefix)
                       for prefix in path_prefixes):
                continue
            if pattern.search(line):
                findings.append(f"{relative}:{line_number}: {rule_name}: {line.strip()}")
    for rule_name, pattern, path_prefixes in SCOPED_CONTENT_DENY_RULES:
        if not any(
            relative == prefix.rstrip("/") or relative.startswith(prefix)
            for prefix in path_prefixes
        ):
            continue
        match = pattern.search(content)
        if match is not None:
            line_number = content.count("\n", 0, match.start()) + 1
            findings.append(f"{relative}:{line_number}: {rule_name}")
    if any(
        relative == prefix.rstrip("/") or relative.startswith(prefix)
        for prefix in CURRENT_RELEASE_DOC_PATHS
    ):
        for line_number, command in _cmake_commands(lines):
            if "-DBUILD_TESTING=ON" in command and "-DQCURL_BUILD_SHARED_LIBS=OFF" in command:
                findings.append(
                    f"{relative}:{line_number}: unsupported static testing command"
                )
    findings.extend(_scan_qmap_cache_policy_calls(relative, lines, allowlist))
    return findings


def _cmake_commands(lines: list[str]) -> Iterable[tuple[int, str]]:
    """逐条提取 CMake 续行命令，避免跨独立命令匹配。"""

    index = 0
    while index < len(lines):
        line = lines[index]
        if re.match(r"^\s*cmake(?:\s|$)", line) is None:
            index += 1
            continue
        command_lines = [line]
        end = index
        while command_lines[-1].rstrip().endswith("\\") and end + 1 < len(lines):
            end += 1
            command_lines.append(lines[end])
        yield index + 1, "\n".join(command_lines)
        index = end + 1


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
