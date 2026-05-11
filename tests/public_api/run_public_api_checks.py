#!/usr/bin/env python3
"""Run QCurl public API guardrail checks."""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tests.public_api.consumer_contracts import configure_and_build
from tests.public_api.consumer_contracts import run_consumer_smoke
from tests.public_api.consumer_contracts import validate_cache_core_contract_fixture
from tests.public_api.consumer_contracts import validate_cache_policy_core_contract_fixture
from tests.public_api.consumer_contracts import validate_cancel_token_core_contract_fixture
from tests.public_api.consumer_contracts import validate_connection_pool_core_contract_fixture
from tests.public_api.consumer_contracts import validate_default_logger_core_contract_fixture
from tests.public_api.consumer_contracts import validate_logger_core_contract_fixture
from tests.public_api.consumer_contracts import validate_middleware_core_contract_fixture
from tests.public_api.consumer_contracts import validate_mock_handler_core_test_support_fixture
from tests.public_api.consumer_contracts import validate_multipart_core_contract_fixture
from tests.public_api.consumer_contracts import validate_scheduler_core_contract_fixture
from tests.public_api.layout_scan import GuardrailFinding
from tests.public_api.layout_scan import collect_cpp_sources
from tests.public_api.layout_scan import collect_exported_types
from tests.public_api.layout_scan import collect_layout_findings
from tests.public_api.layout_scan import collect_nested_public_structs
from tests.public_api.layout_scan import find_matching_brace
from tests.public_api.layout_scan import find_member_fragment
from tests.public_api.layout_scan import has_out_of_line_definition
from tests.public_api.layout_scan import has_public_method_signature_with_type
from tests.public_api.layout_scan import has_top_level_exported_signature_with_type
from tests.public_api.layout_scan import line_number_for_offset
from tests.public_api.layout_scan import load_allowlist
from tests.public_api.layout_scan import split_public_statements
from tests.public_api.layout_scan import strip_comments_and_strings
from tests.public_api.layout_scan import type_public_field_exists


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


def scan_headers(args: argparse.Namespace) -> int:
    """Scan public headers for forbidden code-level tokens."""

    manifest_violations: list[str] = []
    include_rules = [
        ("curl include", re.compile(r'^\s*#\s*include\s*<curl/')),
        ("private header include", re.compile(r'^\s*#\s*include\s*[<"].*_p\.h[">]')),
        ("Qt private include", re.compile(r'^\s*#\s*include\s*[<"]Qt.*/private/')),
        ("tuple include", re.compile(r'^\s*#\s*include\s*<tuple>')),
        ("qcpimpl helper include", re.compile(r'^\s*#\s*include\s*[<"]QCPimpl\.h[">]')),
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
        all_violations = manifest_violations + violations + [
            f"{item.message} [{item.key}]" for item in layout_violations
        ]
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


def consumer_smoke(args: argparse.Namespace) -> int:
    """Verify positive and negative staged consumer builds."""

    return run_consumer_smoke(args, run_command=run, fail_func=fail)


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
