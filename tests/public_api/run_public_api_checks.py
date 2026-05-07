#!/usr/bin/env python3
"""Run QCurl public API guardrail checks."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import re
import shutil
import subprocess
import sys
from pathlib import Path


def fail(message: str) -> int:
    """Print an error message to stderr and return a failing status."""
    print(f"[public_api] {message}", file=sys.stderr)
    return 1


def run(command: list[str], *, expect_success: bool = True) -> subprocess.CompletedProcess[str]:
    """Run a subprocess command with captured output."""
    proc = subprocess.run(command, text=True, capture_output=True)
    if expect_success and proc.returncode != 0:
        raise RuntimeError(
            f"command failed ({proc.returncode}): {' '.join(command)}\n"
            f"stdout:\n{proc.stdout}\n"
            f"stderr:\n{proc.stderr}"
        )
    return proc


def read_manifest(path: Path) -> list[str]:
    """Read a one-header-per-line manifest file."""
    return [line.strip() for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]


def strip_comments_and_strings(source: str) -> str:
    """Strip comments and string literals while preserving line structure."""
    result: list[str] = []
    i = 0
    length = len(source)
    state = "normal"
    quote = ""

    while i < length:
        ch = source[i]
        nxt = source[i + 1] if i + 1 < length else ""

        if state == "normal":
            if ch == "/" and nxt == "/":
                result.extend("  ")
                i += 2
                state = "line_comment"
                continue
            if ch == "/" and nxt == "*":
                result.extend("  ")
                i += 2
                state = "block_comment"
                continue
            if ch in {'"', "'"}:
                result.append(" ")
                quote = ch
                i += 1
                state = "string"
                continue
            result.append(ch)
            i += 1
            continue

        if state == "line_comment":
            if ch == "\n":
                result.append("\n")
                state = "normal"
            else:
                result.append(" ")
            i += 1
            continue

        if state == "block_comment":
            if ch == "*" and nxt == "/":
                result.extend("  ")
                i += 2
                state = "normal"
            else:
                result.append("\n" if ch == "\n" else " ")
                i += 1
            continue

        if state == "string":
            if ch == "\\" and i + 1 < length:
                result.extend("  ")
                i += 2
                continue
            if ch == quote:
                result.append(" ")
                i += 1
                state = "normal"
                continue
            result.append("\n" if ch == "\n" else " ")
            i += 1

    return "".join(result)


@dataclass(frozen=True)
class ExportedType:
    """Structured metadata for an exported type declaration."""

    kind: str
    name: str
    start: int
    end: int
    body_start: int
    body_end: int
    line: int
    inherits_qobject: bool


@dataclass(frozen=True)
class GuardrailFinding:
    """A single public API guardrail finding."""

    key: str
    message: str


def line_number_for_offset(text: str, offset: int) -> int:
    """Map a byte offset in `text` to a 1-based line number."""
    return text.count("\n", 0, offset) + 1


def find_matching_brace(text: str, opening_brace: int) -> int:
    """Find the matching `}` for `text[opening_brace] == '{'`."""
    depth = 0
    for index in range(opening_brace, len(text)):
        ch = text[index]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return index
    return -1


def collect_exported_types(stripped_header: str) -> list[ExportedType]:
    """Collect `class|struct QCURL_EXPORT ... { ... }` declarations."""
    export_pattern = re.compile(r"\b(class|struct)\s+QCURL_EXPORT\s+([A-Za-z_][A-Za-z_0-9]*)\b")
    exported: list[ExportedType] = []
    for match in export_pattern.finditer(stripped_header):
        body_start = stripped_header.find("{", match.end())
        if body_start == -1:
            continue
        body_end = find_matching_brace(stripped_header, body_start)
        if body_end == -1:
            continue
        end = stripped_header.find(";", body_end)
        if end == -1:
            end = body_end

        exported.append(
            ExportedType(
                kind=match.group(1),
                name=match.group(2),
                start=match.start(),
                end=end,
                body_start=body_start,
                body_end=body_end,
                line=line_number_for_offset(stripped_header, match.start()),
                inherits_qobject="QObject" in stripped_header[match.start() : body_start],
            )
        )
    return exported


def split_public_statements(body: str, *, default_public: bool) -> list[str]:
    """Split type body into top-level statements that are in public section."""
    statements: list[str] = []
    current_access = "public" if default_public else "private"
    depth = 0
    statement_chars: list[str] = []

    for line in body.splitlines(keepends=True):
        access = re.match(r"^\s*(public|private|protected)\s*:\s*$", line)
        if depth == 0 and access:
            current_access = access.group(1)
            continue

        for ch in line:
            statement_chars.append(ch)
            if ch == "{":
                depth += 1
            elif ch == "}":
                depth = max(depth - 1, 0)

            if ch == ";" and depth == 0:
                statement = "".join(statement_chars).strip()
                if current_access == "public" and statement:
                    statements.append(statement)
                statement_chars = []

    trailing = "".join(statement_chars).strip()
    if trailing and current_access == "public":
        statements.append(trailing)
    return statements


def type_public_field_exists(statements: list[str]) -> bool:
    """Heuristically detect if public statements contain data fields."""
    skip_prefixes = (
        "using ",
        "typedef ",
        "static_assert",
        "Q_OBJECT",
        "Q_GADGET",
        "Q_PROPERTY",
        "Q_DECLARE_",
        "friend ",
        "enum ",
        "class ",
        "struct ",
    )
    for statement in statements:
        normalized = " ".join(statement.split())
        if not normalized:
            continue
        if normalized.startswith(skip_prefixes):
            continue
        if "(" in normalized or ")" in normalized:
            continue
        if normalized.startswith("operator "):
            continue
        if normalized.endswith(";"):
            return True
    return False


def collect_nested_public_structs(body: str, *, default_public: bool) -> list[str]:
    """Collect names of nested structs declared in public section."""
    nested: list[str] = []
    statements = split_public_statements(body, default_public=default_public)
    nested_pattern = re.compile(
        r"^(?:[A-Z_][A-Z_0-9]*\s+)*struct\s+(?:QCURL_EXPORT\s+)?([A-Za-z_][A-Za-z_0-9]*)\b"
    )
    for statement in statements:
        normalized = " ".join(statement.split())
        match = nested_pattern.match(normalized)
        if match and "{" in normalized:
            nested.append(match.group(1))
    return nested


def has_top_level_exported_signature_with_type(stripped_header: str, type_name: str) -> bool:
    """Return True if a free exported signature references `type_name`."""
    signature_pattern = re.compile(r"\bQCURL_EXPORT\b[^;{}]*\([^;{}]*\)\s*[^;{}]*;")
    type_pattern = re.compile(rf"\b{re.escape(type_name)}\b")
    for match in signature_pattern.finditer(stripped_header):
        statement = match.group(0)
        if statement.lstrip().startswith(("class ", "struct ")):
            continue
        if type_pattern.search(statement):
            return True
    return False


def has_public_method_signature_with_type(body: str, type_name: str, *, default_public: bool) -> bool:
    """Return True if a public method declaration references `type_name`."""
    type_pattern = re.compile(rf"\b{re.escape(type_name)}\b")
    for statement in split_public_statements(body, default_public=default_public):
        normalized = " ".join(statement.split())
        if "(" not in normalized or ")" not in normalized:
            continue
        if type_pattern.search(normalized):
            return True
    return False


def collect_cpp_sources(source_root: Path) -> str:
    """Concatenate C++ sources for out-of-line definition checks."""
    parts: list[str] = []
    for suffix in ("*.cpp", "*.cc", "*.cxx"):
        for path in source_root.rglob(suffix):
            parts.append(path.read_text(encoding="utf-8"))
    return "\n".join(parts)


def has_out_of_line_definition(source_text: str, type_name: str, member_kind: str) -> bool:
    """Check whether a special member has an out-of-line definition."""
    patterns = {
        "destructor": rf"\b{re.escape(type_name)}::\s*~{re.escape(type_name)}\s*\(",
        "copy_ctor": (
            rf"\b{re.escape(type_name)}::\s*{re.escape(type_name)}\s*"
            rf"\(\s*const\s+{re.escape(type_name)}\s*&"
        ),
        "move_ctor": (
            rf"\b{re.escape(type_name)}::\s*{re.escape(type_name)}\s*"
            rf"\(\s*{re.escape(type_name)}\s*&&"
        ),
        "copy_assign": (
            rf"\b{re.escape(type_name)}::\s*operator=\s*"
            rf"\(\s*const\s+{re.escape(type_name)}\s*&"
        ),
        "move_assign": (
            rf"\b{re.escape(type_name)}::\s*operator=\s*"
            rf"\(\s*{re.escape(type_name)}\s*&&"
        ),
    }
    pattern = patterns.get(member_kind)
    if not pattern:
        return False
    return re.search(pattern, source_text) is not None


def find_member_fragment(body: str, type_name: str, member_kind: str) -> str | None:
    """Return the first declaration/definition fragment for a special member."""
    compact = " ".join(body.split())
    pattern_map = {
        "destructor": rf"~{re.escape(type_name)}\s*\([^)]*\)\s*[^;{{}}]*(?:;|\{{)",
        "copy_ctor": (
            rf"{re.escape(type_name)}\s*\(\s*const\s+{re.escape(type_name)}\s*&[^)]*\)"
            rf"\s*[^;{{}}]*(?:;|\{{)"
        ),
        "move_ctor": (
            rf"{re.escape(type_name)}\s*\(\s*{re.escape(type_name)}\s*&&[^)]*\)"
            rf"\s*[^;{{}}]*(?:;|\{{)"
        ),
        "copy_assign": (
            rf"{re.escape(type_name)}\s*&\s*operator=\s*"
            rf"\(\s*const\s+{re.escape(type_name)}\s*&[^)]*\)\s*[^;{{}}]*(?:;|\{{)"
        ),
        "move_assign": (
            rf"{re.escape(type_name)}\s*&\s*operator=\s*"
            rf"\(\s*{re.escape(type_name)}\s*&&[^)]*\)\s*[^;{{}}]*(?:;|\{{)"
        ),
    }
    pattern = pattern_map.get(member_kind)
    if not pattern:
        return None
    match = re.search(pattern, compact)
    return match.group(0) if match else None


def load_allowlist(path: Path) -> set[str]:
    """Load allowlisted finding keys from file."""
    if not path.is_file():
        return set()
    keys: set[str] = set()
    for line in path.read_text(encoding="utf-8").splitlines():
        normalized = line.strip()
        if not normalized or normalized.startswith("#"):
            continue
        keys.add(normalized)
    return keys


def collect_layout_findings(
    header: str,
    stripped_header: str,
    source_text: str,
) -> list[GuardrailFinding]:
    """Collect ABI/layout findings for one public header."""
    findings: list[GuardrailFinding] = []
    exported_types = collect_exported_types(stripped_header)

    exported_struct_names = [t.name for t in exported_types if t.kind == "struct"]
    for exported in exported_types:
        body = stripped_header[exported.body_start + 1 : exported.body_end]
        default_public = exported.kind == "struct"
        public_statements = split_public_statements(body, default_public=default_public)

        if type_public_field_exists(public_statements):
            findings.append(
                GuardrailFinding(
                    key=f"public-fields:{header}:{exported.name}",
                    message=f"{header}:{exported.line}: exported type `{exported.name}` has public data fields",
                )
            )

        nested_structs = collect_nested_public_structs(body, default_public=default_public)
        for nested in nested_structs:
            findings.append(
                GuardrailFinding(
                    key=f"struct-layout:nested:{header}:{exported.name}::{nested}",
                    message=(
                        f"{header}:{exported.line}: exported type `{exported.name}` exposes nested struct "
                        f"`{nested}` in public API"
                    ),
                )
            )

        signature_struct_names = exported_struct_names + nested_structs
        for struct_name in sorted(set(signature_struct_names)):
            if has_public_method_signature_with_type(
                body,
                struct_name,
                default_public=default_public,
            ):
                findings.append(
                    GuardrailFinding(
                        key=f"struct-layout:signature:{header}:{exported.name}:{struct_name}",
                        message=(
                            f"{header}:{exported.line}: exported signature in `{exported.name}` references "
                            f"struct type `{struct_name}`"
                        ),
                    )
                )

        has_incomplete_holder = "QScopedPointer<" in body or "QSharedDataPointer<" in body
        if not has_incomplete_holder:
            continue

        non_copyable = "Q_DISABLE_COPY" in body or exported.inherits_qobject

        required_members = ["destructor"]
        optional_members: list[str] = ["move_ctor", "move_assign"]
        if not non_copyable:
            required_members.extend(["copy_ctor", "copy_assign"])
        else:
            optional_members.extend(["copy_ctor", "copy_assign"])

        for member in required_members:
            fragment = find_member_fragment(body, exported.name, member)
            if fragment is None:
                findings.append(
                    GuardrailFinding(
                        key=f"out-of-line:implicit:{header}:{exported.name}:{member}",
                        message=(
                            f"{header}:{exported.line}: `{exported.name}` is missing explicit `{member}` "
                            "declaration for incomplete-type holder"
                        ),
                    )
                )
                continue

            if "= delete" in fragment:
                continue
            if "= default" in fragment or "{" in fragment:
                findings.append(
                    GuardrailFinding(
                        key=f"out-of-line:inline-or-default:{header}:{exported.name}:{member}",
                        message=(
                            f"{header}:{exported.line}: `{exported.name}` has `{member}` inline/default "
                            "in header; must be out-of-line"
                        ),
                    )
                )
                continue

            if not has_out_of_line_definition(source_text, exported.name, member):
                findings.append(
                    GuardrailFinding(
                        key=f"out-of-line:missing-def:{header}:{exported.name}:{member}",
                        message=(
                            f"{header}:{exported.line}: `{exported.name}` declares `{member}` but no "
                            "out-of-line definition was found in source files"
                        ),
                    )
                )

        for member in optional_members:
            fragment = find_member_fragment(body, exported.name, member)
            if fragment is None or "= delete" in fragment:
                continue
            if "= default" in fragment or "{" in fragment:
                findings.append(
                    GuardrailFinding(
                        key=f"out-of-line:inline-or-default:{header}:{exported.name}:{member}",
                        message=(
                            f"{header}:{exported.line}: `{exported.name}` has `{member}` inline/default "
                            "in header; must be out-of-line"
                        ),
                    )
                )
                continue
            if not has_out_of_line_definition(source_text, exported.name, member):
                findings.append(
                    GuardrailFinding(
                        key=f"out-of-line:missing-def:{header}:{exported.name}:{member}",
                        message=(
                            f"{header}:{exported.line}: `{exported.name}` declares `{member}` but no "
                            "out-of-line definition was found in source files"
                        ),
                    )
                )

    for struct_name in sorted(set(exported_struct_names)):
        if has_top_level_exported_signature_with_type(stripped_header, struct_name):
            findings.append(
                GuardrailFinding(
                    key=f"struct-layout:free-signature:{header}:{struct_name}",
                    message=(
                        f"{header}: top-level exported signature references exported struct `{struct_name}`"
                    ),
                )
            )

    return findings


def scan_headers(args: argparse.Namespace) -> int:
    """Scan public headers for forbidden code-level tokens."""
    manifest_violations: list[str] = []
    include_rules = [
        ("curl include", re.compile(r'^\s*#\s*include\s*<curl/')),
        ("private header include", re.compile(r'^\s*#\s*include\s*[<"].*_p\.h[">]')),
        ("Qt private include", re.compile(r'^\s*#\s*include\s*[<"]Qt.*/private/')),
        ("tuple include", re.compile(r'^\s*#\s*include\s*<tuple>')),
        ("qcpimpl helper include", re.compile(r'^\s*#\s*include\s*[<"]QCPimpl\.h[">]')),
        # "heavy include leak" 规则：install headers 中不应直接引入实现级组件。
        #
        # 目标：把线程/文件系统/进程/JSON 等实现细节留在 .cpp 或 non-core headers，
        # 以降低 public ABI 面的耦合与编译成本（Qt/KF 风格）。
        ("Qt threading include", re.compile(r'^\s*#\s*include\s*<Q(Mutex|ReadWriteLock|WaitCondition|Thread|ThreadPool|Semaphore|Future|Promise|FutureWatcher)\b')),
        ("Qt filesystem include", re.compile(r'^\s*#\s*include\s*<Q(File|Dir|FileInfo|SaveFile|TemporaryFile|StandardPaths)\b')),
        ("Qt process include", re.compile(r'^\s*#\s*include\s*<QProcess\b')),
        ("Qt JSON include", re.compile(r'^\s*#\s*include\s*<QJson')),
    ]
    code_rules = [
        ("curl type leak", re.compile(r"\bCURL[A-Za-z_0-9]*\b")),
        ("curl function leak", re.compile(r"\bcurl_[A-Za-z_0-9]+\b")),
        ("std::tuple leak", re.compile(r"\bstd::tuple\b")),
        ("qcpimpl helper macro", re.compile(r"\bQCURL_DECLARE_(?:DPTR|SHARED_DATA)\s*\(")),
    ]

    violations: list[str] = []
    layout_findings: list[GuardrailFinding] = []
    source_text = collect_cpp_sources(args.source_root)
    for header in read_manifest(args.manifest):
        normalized = header.replace("\\", "/")
        if normalized == "QCPimpl.h":
            manifest_violations.append("QCPimpl.h must not appear in QCURL_INSTALL_HEADERS")
            continue
        if normalized.endswith("_p.h"):
            manifest_violations.append(f"{header}: private header leaked into QCURL_INSTALL_HEADERS")
            continue
        if "/private/" in f"/{normalized}":
            manifest_violations.append(f"{header}: private path leaked into QCURL_INSTALL_HEADERS")
            continue

        header_path = args.source_root / header
        stripped = strip_comments_and_strings(header_path.read_text(encoding="utf-8"))
        for line_number, line in enumerate(stripped.splitlines(), start=1):
            if line.lstrip().startswith("#"):
                for rule_name, pattern in include_rules:
                    if pattern.search(line):
                        violations.append(f"{header}:{line_number}: {rule_name}: {line.strip()}")
            else:
                for rule_name, pattern in code_rules:
                    if pattern.search(line):
                        violations.append(f"{header}:{line_number}: {rule_name}: {line.strip()}")

        layout_findings.extend(
            collect_layout_findings(
                header,
                stripped,
                source_text,
            )
        )

    allowlisted = load_allowlist(args.layout_allowlist)
    layout_violations = [finding for finding in layout_findings if finding.key not in allowlisted]

    if manifest_violations or violations or layout_violations:
        all_violations = manifest_violations + violations + [f"{item.message} [{item.key}]" for item in layout_violations]
        print("\n".join(all_violations), file=sys.stderr)
        return 1

    stale_allowlist = sorted(allowlisted - {finding.key for finding in layout_findings})
    if stale_allowlist:
        return fail("layout allowlist has stale entries: " + ", ".join(stale_allowlist))

    print("[public_api] header scan passed")
    return 0


def install_stage(args: argparse.Namespace) -> int:
    """Install the current build into a clean staging prefix."""
    if args.stage_dir.exists():
        shutil.rmtree(args.stage_dir)
    args.stage_dir.mkdir(parents=True, exist_ok=True)

    try:
        for component in ("Development", "Runtime", "BundledRuntime"):
            command = [
                args.cmake,
                "--install",
                str(args.build_dir),
                "--prefix",
                str(args.stage_dir),
                "--component",
                component,
            ]
            if args.config:
                command.extend(["--config", args.config])
            run(command)
    except RuntimeError as exc:
        return fail(str(exc))

    print(f"[public_api] staged install at {args.stage_dir}")
    return 0


def build_target(args: argparse.Namespace) -> int:
    """Build a single target in the configured build tree."""
    command = [args.cmake, "--build", str(args.build_dir), "--target", args.target]
    if args.config:
        command.extend(["--config", args.config])

    try:
        run(command)
    except RuntimeError as exc:
        return fail(str(exc))

    print(f"[public_api] built target {args.target}")
    return 0


def check_installed_headers(args: argparse.Namespace) -> int:
    """Verify staged public headers exactly match the manifest plus QCurlConfig.h."""
    include_dir = args.stage_dir / "include" / "qcurl"
    if not include_dir.is_dir():
        return fail(f"missing include directory: {include_dir}")

    expected = set(read_manifest(args.manifest))
    expected.add(Path(args.generated_header).name)
    actual = {path.name for path in include_dir.iterdir() if path.is_file()}

    missing = sorted(expected - actual)
    extra = sorted(actual - expected)
    if missing or extra:
        details = []
        if missing:
            details.append(f"missing={missing}")
        if extra:
            details.append(f"extra={extra}")
        return fail("installed header set mismatch: " + ", ".join(details))

    print("[public_api] installed header set matches manifest")
    return 0


def check_export_contract(args: argparse.Namespace) -> int:
    """Verify installed export files do not leak bundled libcurl targets."""
    target_files = sorted(args.stage_dir.rglob("QCurlTargets*.cmake"))
    if not target_files:
        return fail(f"no QCurlTargets*.cmake files found under {args.stage_dir}")

    forbidden_patterns = [
        ("QCurl::libcurl_shared", re.compile(r"\bQCurl::libcurl_shared\b")),
        ("CURL::libcurl", re.compile(r"\bCURL::libcurl\b")),
        ("raw libcurl dependency", re.compile(r"IMPORTED_LINK_DEPENDENT_LIBRARIES[^\n]*libcurl", re.IGNORECASE)),
    ]

    violations: list[str] = []
    for target_file in target_files:
        content = target_file.read_text(encoding="utf-8")
        for rule_name, pattern in forbidden_patterns:
            if pattern.search(content):
                violations.append(f"{target_file.name}: {rule_name}")

    if violations:
        print("\n".join(violations), file=sys.stderr)
        return 1

    print("[public_api] export contract passed")
    return 0


def configure_and_build(source_dir: Path, build_dir: Path, stage_dir: Path, cmake: str, config: str) -> None:
    """Configure and build a fixture consumer project against the staged package."""
    if build_dir.exists():
        shutil.rmtree(build_dir)

    configure = [
        cmake,
        "-S",
        str(source_dir),
        "-B",
        str(build_dir),
        f"-DQCURL_STAGE_PREFIX={stage_dir}",
    ]
    run(configure)

    build = [cmake, "--build", str(build_dir)]
    if config:
        build.extend(["--config", config])
    run(build)


def validate_scheduler_core_contract_fixture(source_dir: Path) -> None:
    """Ensure consumer smoke keeps scheduler Core contract coverage."""
    fixture = source_dir / "main.cpp"
    if not fixture.is_file():
        raise RuntimeError(f"missing consumer smoke fixture source: {fixture}")

    source = fixture.read_text(encoding="utf-8")
    required_snippets = [
        "#include <QCNetworkRequestScheduler.h>",
        "#include <QCNetworkProxyConfig.h>",
        "#include <QCNetworkRetryPolicy.h>",
        "#include <QCNetworkSslConfig.h>",
        "#include <QCNetworkTimeoutConfig.h>",
        "manager.scheduler()",
        "manager.schedulerOnOwnerThread()",
        ".setMaxConcurrentRequests(",
        ".maxConcurrentRequests()",
        ".setWeight(",
        ".weight()",
        "QCNetworkProxyConfig::ProxyTlsConfig",
        ".setTlsConfig({})",
        ".setTlsConfig(",
        ".clearTlsConfig(",
        ".setPinnedPublicKey(",
        ".setConnectTimeout(",
        ".setTotalTimeout(",
        ".setRetryHttpStatusErrorsForGetOnly(",
        ".setHttpAuth(",
        ".httpAuth()",
    ]
    missing = [snippet for snippet in required_snippets if snippet not in source]
    if missing:
        raise RuntimeError(
            "consumer smoke fixture is missing required scheduler contract coverage: "
            + ", ".join(missing)
        )

    fallback_pattern = re.compile(
        r"=\s*[A-Za-z_][A-Za-z_0-9]*\s*!=\s*nullptr\s*\?\s*[A-Za-z_][A-Za-z_0-9]*\s*:\s*[A-Za-z_][A-Za-z_0-9]*\s*;"
    )
    if fallback_pattern.search(source):
        raise RuntimeError(
            "consumer smoke fixture must use explicit scheduler contract assertions, "
            "not ternary fallback logic"
        )

    field_names = [
        "maxConcurrentRequests",
        "maxRequestsPerHost",
        "maxBandwidthBytesPerSec",
        "enableThrottling",
        "weight",
        "quantum",
        "reservedGlobal",
        "reservedPerHost",
    ]
    violations: list[str] = []
    for field_name in field_names:
        pattern = re.compile(rf"\.\s*{field_name}\b(?!\s*\()")
        if pattern.search(source):
            violations.append(field_name)
    if violations:
        raise RuntimeError(
            "consumer smoke fixture must use accessor API only; found direct field usage: "
            + ", ".join(sorted(set(violations)))
        )


def validate_logger_core_contract_fixture(source_dir: Path) -> None:
    """Ensure consumer smoke keeps logger Core contract coverage."""
    fixture = source_dir / "main.cpp"
    if not fixture.is_file():
        raise RuntimeError(f"missing consumer smoke fixture source: {fixture}")

    source = fixture.read_text(encoding="utf-8")
    required_snippets = [
        "#include <QCNetworkLogger.h>",
        "class ConsumerSmokeLogger : public QCurl::QCNetworkLogger",
        "QCurl::NetworkLogEntry entry(",
        "manager.setLogger(&logger)",
        "manager.logger()",
        "manager.setDebugTraceEnabled(true)",
        "manager.debugTraceEnabled()",
        "entry.level()",
        "entry.category()",
        "entry.message()",
        "entry.timestampUtc()",
    ]
    missing = [snippet for snippet in required_snippets if snippet not in source]
    if missing:
        raise RuntimeError(
            "consumer smoke fixture is missing required logger contract coverage: "
            + ", ".join(missing)
        )

    field_names = ["level", "category", "message", "timestampUtc"]
    violations: list[str] = []
    for field_name in field_names:
        pattern = re.compile(rf"\.\s*{field_name}\b(?!\s*\()")
        if pattern.search(source):
            violations.append(field_name)
    if violations:
        raise RuntimeError(
            "consumer smoke fixture must use NetworkLogEntry accessor API only; found direct field usage: "
            + ", ".join(sorted(set(violations)))
        )


def validate_default_logger_core_contract_fixture(source_dir: Path) -> None:
    """Ensure consumer smoke keeps DefaultLogger Core helper coverage."""
    fixture = source_dir / "main.cpp"
    if not fixture.is_file():
        raise RuntimeError(f"missing consumer smoke fixture source: {fixture}")

    source = fixture.read_text(encoding="utf-8")
    required_snippets = [
        "#include <QCNetworkDefaultLogger.h>",
        "QCurl::QCNetworkDefaultLogger defaultLogger",
        "defaultLogger.enableConsoleOutput(false)",
        "defaultLogger.setMinLogLevel(QCurl::NetworkLogLevel::Warning)",
        "manager.setLogger(&defaultLogger)",
        "defaultLogger.entries()",
    ]
    missing = [snippet for snippet in required_snippets if snippet not in source]
    if missing:
        raise RuntimeError(
            "consumer smoke fixture is missing required DefaultLogger contract coverage: "
            + ", ".join(missing)
        )


def validate_cancel_token_core_contract_fixture(source_dir: Path) -> None:
    """Ensure consumer smoke keeps CancelToken reply-level coverage."""
    fixture = source_dir / "main.cpp"
    if not fixture.is_file():
        raise RuntimeError(f"missing consumer smoke fixture source: {fixture}")

    source = fixture.read_text(encoding="utf-8")
    required_snippets = [
        "#include <QCNetworkCancelToken.h>",
        "QCurl::QCNetworkCancelToken cancelToken",
        "QCurl::QCNetworkReply *replyToCancel",
        "QList<QCurl::QCNetworkReply *> repliesToCancel",
        "cancelToken.attach(replyToCancel)",
        "cancelToken.attachMultiple(repliesToCancel)",
        "cancelToken.setAutoTimeout(0)",
        "cancelToken.cancel()",
        "cancelToken.isCancelled()",
    ]
    missing = [snippet for snippet in required_snippets if snippet not in source]
    if missing:
        raise RuntimeError(
            "consumer smoke fixture is missing required CancelToken contract coverage: "
            + ", ".join(missing)
        )


def validate_cache_policy_core_contract_fixture(source_dir: Path) -> None:
    """Ensure consumer smoke keeps CachePolicy Core type coverage."""
    fixture = source_dir / "main.cpp"
    if not fixture.is_file():
        raise RuntimeError(f"missing consumer smoke fixture source: {fixture}")

    source = fixture.read_text(encoding="utf-8")
    required_snippets = [
        "#include <QCNetworkCachePolicy.h>",
        "request.setCachePolicy(QCurl::QCNetworkCachePolicy::OnlyNetwork)",
        "request.cachePolicy()",
    ]
    missing = [snippet for snippet in required_snippets if snippet not in source]
    if missing:
        raise RuntimeError(
            "consumer smoke fixture is missing required CachePolicy contract coverage: "
            + ", ".join(missing)
        )


def validate_cache_core_contract_fixture(source_dir: Path) -> None:
    """Ensure consumer smoke keeps concrete Cache Core coverage."""
    fixture = source_dir / "main.cpp"
    if not fixture.is_file():
        raise RuntimeError(f"missing consumer smoke fixture source: {fixture}")

    source = fixture.read_text(encoding="utf-8")
    required_snippets = [
        "#include <QCNetworkCache.h>",
        "#include <QCNetworkMemoryCache.h>",
        "#include <QCNetworkDiskCache.h>",
        "QCurl::QCNetworkMemoryCache memoryCache",
        "QCurl::QCNetworkCache *cacheInterface",
        "QCurl::QCNetworkCacheMetadata cacheMetadata",
        "cacheMetadata.setUrl(request.url())",
        "cacheMetadata.setHeader(QByteArrayLiteral(\"Content-Type\")",
        "cacheInterface->insert(request.url(), cacheBody, cacheMetadata)",
        "cacheInterface->lookup(request.url()",
        "cacheLookup.status()",
        "cacheLookup.metadata().url()",
        "cacheLookup.body()",
        "QCurl::QCNetworkDiskCache *diskCacheTypeProbe",
    ]
    missing = [snippet for snippet in required_snippets if snippet not in source]
    if missing:
        raise RuntimeError(
            "consumer smoke fixture is missing required Cache contract coverage: "
            + ", ".join(missing)
        )

    forbidden_snippets = [
        ".status =",
        ".metadata =",
        ".body =",
        ".url =",
        ".headers =",
        ".expirationDate =",
        ".creationDate =",
        ".lastModified =",
    ]
    present = [snippet for snippet in forbidden_snippets if snippet in source]
    if present:
        raise RuntimeError(
            "consumer smoke fixture must use Cache accessor API only; found: "
            + ", ".join(present)
        )


def validate_multipart_core_contract_fixture(source_dir: Path) -> None:
    """Ensure consumer smoke keeps Multipart Core builder coverage."""
    fixture = source_dir / "main.cpp"
    if not fixture.is_file():
        raise RuntimeError(f"missing consumer smoke fixture source: {fixture}")

    source = fixture.read_text(encoding="utf-8")
    required_snippets = [
        "#include <QCMultipartFormData.h>",
        "QCurl::QCMultipartFormData formData",
        "if (!formData.setBoundary(QStringLiteral(\"----QCurlConsumerSmokeBoundary\")))",
        "formData.addTextField(QStringLiteral(\"name\")",
        "formData.addFileField(QStringLiteral(\"file\")",
        "formData.contentType()",
        "formData.size()",
        "formData.toByteArray()",
        "formData.fieldCount()",
    ]
    missing = [snippet for snippet in required_snippets if snippet not in source]
    if missing:
        raise RuntimeError(
            "consumer smoke fixture is missing required Multipart contract coverage: "
            + ", ".join(missing)
        )


def validate_connection_pool_core_contract_fixture(source_dir: Path) -> None:
    """Ensure consumer smoke keeps ConnectionPool accessor-only Core coverage."""
    fixture = source_dir / "main.cpp"
    if not fixture.is_file():
        raise RuntimeError(f"missing consumer smoke fixture source: {fixture}")

    source = fixture.read_text(encoding="utf-8")
    required_snippets = [
        "#include <QCNetworkConnectionPoolConfig.h>",
        "#include <QCNetworkConnectionPoolManager.h>",
        "QCurl::QCNetworkConnectionPoolConfig poolConfig",
        "poolConfig.setMaxConnectionsPerHost(4)",
        "poolConfig.setMaxTotalConnections(12)",
        "poolConfig.setMaxIdleTime(45)",
        "poolConfig.setMaxConnectionLifetime(90)",
        "poolConfig.setMultiplexingEnabled(true)",
        "poolConfig.setDnsCacheEnabled(true)",
        "poolConfig.setDnsCacheTimeout(30)",
        "poolConfig.setMultiMaxTotalConnections(6)",
        "poolConfig.setMultiMaxHostConnections(2)",
        "poolConfig.setMultiMaxConcurrentStreams(8)",
        "poolConfig.setMultiMaxConnects(16)",
        "poolConfig.maxConnectionsPerHost()",
        "poolConfig.maxTotalConnections()",
        "poolConfig.multiMaxTotalConnections()",
        "QCurl::QCNetworkConnectionPoolManager::instance()",
        "poolManager->setConfig(poolConfig)",
        "poolManager->config()",
        "poolManager->statistics()",
        "poolStats.totalRequests()",
        "poolStats.reusedConnections()",
        "poolStats.reuseRate()",
        "poolStats.activeConnections()",
        "poolStats.idleConnections()",
    ]
    missing = [snippet for snippet in required_snippets if snippet not in source]
    if missing:
        raise RuntimeError(
            "consumer smoke fixture is missing required ConnectionPool contract coverage: "
            + ", ".join(missing)
        )

    forbidden_snippets = [
        ".maxConnectionsPerHost =",
        ".maxTotalConnections =",
        ".maxIdleTime =",
        ".maxConnectionLifetime =",
        ".enablePipelining =",
        ".enableMultiplexing =",
        ".enableDnsCache =",
        ".dnsCacheTimeout =",
        ".enableConnectionWarming =",
        ".multiMaxTotalConnections =",
        ".multiMaxHostConnections =",
        ".multiMaxConcurrentStreams =",
        ".multiMaxConnects =",
        ".totalRequests =",
        ".reusedConnections =",
        ".reuseRate =",
        ".activeConnections =",
        ".idleConnections =",
    ]
    present = [snippet for snippet in forbidden_snippets if snippet in source]
    if present:
        raise RuntimeError(
            "consumer smoke fixture must use ConnectionPool accessor API only; found: "
            + ", ".join(present)
        )


def validate_middleware_core_contract_fixture(source_dir: Path) -> None:
    """Ensure consumer smoke keeps Middleware Core base coverage."""
    fixture = source_dir / "main.cpp"
    if not fixture.is_file():
        raise RuntimeError(f"missing consumer smoke fixture source: {fixture}")

    source = fixture.read_text(encoding="utf-8")
    required_snippets = [
        "#include <QCNetworkMiddleware.h>",
        "class ConsumerSmokeMiddleware : public QCurl::QCNetworkMiddleware",
        "manager.addMiddleware(&middleware)",
        "manager.middlewares()",
        "manager.removeMiddleware(&middleware)",
        "middleware.name()",
    ]
    missing = [snippet for snippet in required_snippets if snippet not in source]
    if missing:
        raise RuntimeError(
            "consumer smoke fixture is missing required Middleware contract coverage: "
            + ", ".join(missing)
        )


def validate_mock_handler_core_test_support_fixture(source_dir: Path) -> None:
    """Ensure consumer smoke keeps MockHandler Core Test Support coverage."""
    fixture = source_dir / "main.cpp"
    if not fixture.is_file():
        raise RuntimeError(f"missing consumer smoke fixture source: {fixture}")

    source = fixture.read_text(encoding="utf-8")
    required_snippets = [
        "#include <QCNetworkMockHandler.h>",
        "QCurl::QCNetworkMockHandler mockHandler",
        "QCurl::QCNetworkCapturedRequest capturedRequest",
        "capturedRequest.setUrl(request.url())",
        "capturedRequest.setMethod(QCurl::HttpMethod::Post)",
        "capturedRequest.addHeader(",
        "capturedRequest.setBodySize(",
        "capturedRequest.setBodyPreview(",
        "mockHandler.recordRequest(capturedRequest)",
        "mockHandler.takeCapturedRequests()",
        "capturedRequests.first().url()",
        "capturedRequests.first().method()",
        "capturedRequests.first().headers()",
        "capturedRequests.first().bodySize()",
        "capturedRequests.first().bodyPreview()",
        "mockHandler.mockResponse(",
        "mockHandler.hasMock(",
        "mockHandler.getMockResponse(",
        "manager.setMockHandler(&mockHandler)",
        "manager.mockHandler()",
    ]
    missing = [snippet for snippet in required_snippets if snippet not in source]
    if missing:
        raise RuntimeError(
            "consumer smoke fixture is missing required MockHandler contract coverage: "
            + ", ".join(missing)
        )

    forbidden_snippets = [
        ".url =",
        ".method =",
        ".headers =",
        ".bodyPreview =",
        ".bodySize =",
        ".followLocation =",
        ".connectTimeoutMs =",
        ".totalTimeoutMs =",
    ]
    present = [snippet for snippet in forbidden_snippets if snippet in source]
    if present:
        raise RuntimeError(
            "consumer smoke fixture must use CapturedRequest accessor API only; found: "
            + ", ".join(present)
        )


def consumer_smoke(args: argparse.Namespace) -> int:
    """Verify positive and negative staged consumer builds."""
    try:
        validate_scheduler_core_contract_fixture(args.positive_source_dir)
        validate_logger_core_contract_fixture(args.positive_source_dir)
        validate_cache_policy_core_contract_fixture(args.positive_source_dir)
        validate_cache_core_contract_fixture(args.positive_source_dir)
        validate_multipart_core_contract_fixture(args.positive_source_dir)
        validate_default_logger_core_contract_fixture(args.positive_source_dir)
        validate_cancel_token_core_contract_fixture(args.positive_source_dir)
        validate_connection_pool_core_contract_fixture(args.positive_source_dir)
        validate_middleware_core_contract_fixture(args.positive_source_dir)
        validate_mock_handler_core_test_support_fixture(args.positive_source_dir)
    except RuntimeError as exc:
        return fail(f"consumer smoke fixture check failed: {exc}")

    try:
        configure_and_build(
            args.positive_source_dir,
            args.positive_build_dir,
            args.stage_dir,
            args.cmake,
            args.config,
        )
    except RuntimeError as exc:
        return fail(f"positive consumer smoke failed: {exc}")

    if args.negative_build_dir.exists():
        shutil.rmtree(args.negative_build_dir)

    configure = [
        args.cmake,
        "-S",
        str(args.negative_source_dir),
        "-B",
        str(args.negative_build_dir),
        f"-DQCURL_STAGE_PREFIX={args.stage_dir}",
    ]
    try:
        run(configure)
    except RuntimeError as exc:
        return fail(f"negative consumer configure failed unexpectedly: {exc}")

    build = [args.cmake, "--build", str(args.negative_build_dir)]
    if args.config:
        build.extend(["--config", args.config])

    proc = run(build, expect_success=False)
    if proc.returncode == 0:
        return fail("negative consumer unexpectedly built successfully")

    print("[public_api] consumer smoke passed")
    return 0


def build_parser() -> argparse.ArgumentParser:
    """Create the CLI parser."""
    parser = argparse.ArgumentParser(description="QCurl public API guardrail checks")
    subparsers = parser.add_subparsers(dest="command", required=True)

    scan = subparsers.add_parser("scan")
    scan.add_argument("--source-root", type=Path, required=True)
    scan.add_argument("--manifest", type=Path, required=True)
    scan.add_argument(
        "--layout-allowlist",
        type=Path,
        default=Path(__file__).resolve().parent / "public_api_layout_allowlist.txt",
    )
    scan.set_defaults(func=scan_headers)

    install = subparsers.add_parser("install")
    install.add_argument("--cmake", required=True)
    install.add_argument("--build-dir", type=Path, required=True)
    install.add_argument("--stage-dir", type=Path, required=True)
    install.add_argument("--config", default="")
    install.set_defaults(func=install_stage)

    build = subparsers.add_parser("build-target")
    build.add_argument("--cmake", required=True)
    build.add_argument("--build-dir", type=Path, required=True)
    build.add_argument("--target", required=True)
    build.add_argument("--config", default="")
    build.set_defaults(func=build_target)

    headers = subparsers.add_parser("check-installed-headers")
    headers.add_argument("--stage-dir", type=Path, required=True)
    headers.add_argument("--manifest", type=Path, required=True)
    headers.add_argument("--generated-header", required=True)
    headers.set_defaults(func=check_installed_headers)

    export = subparsers.add_parser("check-export-contract")
    export.add_argument("--stage-dir", type=Path, required=True)
    export.set_defaults(func=check_export_contract)

    smoke = subparsers.add_parser("consumer-smoke")
    smoke.add_argument("--cmake", required=True)
    smoke.add_argument("--stage-dir", type=Path, required=True)
    smoke.add_argument("--positive-source-dir", type=Path, required=True)
    smoke.add_argument("--positive-build-dir", type=Path, required=True)
    smoke.add_argument("--negative-source-dir", type=Path, required=True)
    smoke.add_argument("--negative-build-dir", type=Path, required=True)
    smoke.add_argument("--config", default="")
    smoke.set_defaults(func=consumer_smoke)

    return parser


def main() -> int:
    """Entry point."""
    parser = build_parser()
    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
