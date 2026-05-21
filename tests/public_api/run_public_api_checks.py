#!/usr/bin/env python3
"""Run QCurl public API guardrail checks."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tests.public_api.blocking_extras_contracts import check_blocking_extras_install as _check_blocking_extras_install
from tests.public_api.blocking_extras_contracts import run_blocking_extras_consumer_smoke
from tests.public_api.test_support_contracts import check_test_support_install as _check_test_support_install
from tests.public_api.test_support_contracts import run_test_support_consumer_smoke
from tests.public_api.other_extras_contracts import check_other_extras_install as _check_other_extras_install
from tests.public_api.other_extras_contracts import run_other_extras_consumer_smoke
from tests.public_api.consumer_contracts import run_consumer_smoke
from tests.public_api.export_contracts import check_export_contract as _check_export_contract
from tests.public_api.layout_scan import GuardrailFinding
from tests.public_api.layout_scan import collect_cpp_sources
from tests.public_api.layout_scan import collect_layout_findings
from tests.public_api.layout_scan import load_allowlist
from tests.public_api.layout_scan import strip_comments_and_strings
from tests.public_api.stage_contracts import build_target as _build_target
from tests.public_api.stage_contracts import check_installed_headers as _check_installed_headers
from tests.public_api.stage_contracts import install_stage as _install_stage
from tests.public_api.stage_contracts import read_manifest
from tests.public_api.surface_manifest import validate_surface_manifest


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


def scan_headers(args: argparse.Namespace) -> int:
    """Scan public headers for forbidden code-level tokens."""

    manifest_violations: list[str] = []
    include_rules = [
        ("curl include", re.compile(r'^\s*#\s*include\s*<curl/')),
        ("private header include", re.compile(r'^\s*#\s*include\s*[<"].*_p\.h[">]')),
        ("Qt private include", re.compile(r'^\s*#\s*include\s*[<"]Qt.*/private/')),
        ("tuple include", re.compile(r'^\s*#\s*include\s*<tuple>')),
        ("qcpimpl helper include", re.compile(r'^\s*#\s*include\s*[<"]QCPimpl\.h[">]')),
        ("Qt threading include", re.compile(r'^\s*#\s*include\s*<Q(Mutex|ReadWriteLock|WaitCondition|Thread|ThreadPool|Semaphore|Promise|FutureWatcher)\b')),
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


def scan_hard_break_guards(args: argparse.Namespace) -> int:
    """Scan source/documentation surfaces for APIs removed by hard-break cleanup."""

    deny_rules = [
        ("old fromSingleFileDevice ownerThread",
         re.compile(r"fromSingleFileDevice\s*\(\s*QThread\s*\*")),
        ("old fromSingleFileDevice return",
         re.compile(r"(?:static\s+)?QCNetworkMultipartBody\s+fromSingleFileDevice\s*\(")),
        ("old multipart invalid-object predicate",
         re.compile(r"fromSingleFileDevice.*(?:isValid|errorString)\s*\(")),
        ("releaseDevice",
         re.compile(r"\breleaseDevice\s*\(")),
        ("old QCNetworkAccessManager exportCookies return",
         re.compile(r"QList\s*<\s*QNetworkCookie\s*>\s+exportCookies\s*\(")),
        ("old QCCurlMultiManager exportCookiesForManager return",
         re.compile(r"QList\s*<\s*QNetworkCookie\s*>\s+exportCookiesForManager\s*\(")),
        ("QCNetworkRequest virtual destructor",
         re.compile(r"\bvirtual\s+~\s*QCNetworkRequest\s*\(")),
        ("Blocking Extras std::function progress callback",
         re.compile(r"QCBlockingProgressCallback\s*=\s*std::function\b")),
    ]

    include_paths = [
        "src",
        "include",
        "examples",
        "tests/public_api/consumer_smoke",
        "docs/user",
        "docs/arch",
        "tests/qcurl",
        "tests/libcurl_consistency",
    ]
    excluded_parts = {
        ".git",
        "build",
        "generated",
        ".helloagents",
        "__pycache__",
    }

    violations: list[str] = []
    for include_path in include_paths:
        root = args.repo_root / include_path
        if not root.exists():
            continue
        files = root.rglob("*") if root.is_dir() else [root]
        for path in files:
            if not path.is_file() or any(part in excluded_parts for part in path.parts):
                continue
            try:
                content = path.read_text(encoding="utf-8")
            except UnicodeDecodeError:
                continue
            for line_number, line in enumerate(content.splitlines(), start=1):
                for rule_name, pattern in deny_rules:
                    if pattern.search(line):
                        violations.append(
                            f"{path.relative_to(args.repo_root)}:{line_number}: {rule_name}: {line.strip()}"
                        )

    if violations:
        print("\n".join(violations), file=sys.stderr)
        return 1

    print("[public_api] hard-break guard scan passed")
    return 0


def install_stage(args: argparse.Namespace) -> int:
    """Install the current build into a clean staging prefix."""
    return _install_stage(args, run_command=run, fail_func=fail)


def build_target(args: argparse.Namespace) -> int:
    """Build a single target in the configured build tree."""
    return _build_target(args, run_command=run, fail_func=fail)


def check_installed_headers(args: argparse.Namespace) -> int:
    """Verify staged public headers exactly match the manifest plus QCurlConfig.h."""
    return _check_installed_headers(args, fail_func=fail)


def check_export_contract(args: argparse.Namespace) -> int:
    """Verify installed export files expose only expected dependency targets."""
    return _check_export_contract(args.stage_dir, fail_func=fail)


def check_blocking_extras_install(args: argparse.Namespace) -> int:
    """Verify Blocking Extras headers are opt-in and absent from the default Core stage."""

    return _check_blocking_extras_install(args, fail_func=fail)


def consumer_smoke(args: argparse.Namespace) -> int:
    """Verify positive and negative staged consumer builds."""

    return run_consumer_smoke(args, run_command=run, fail_func=fail)


def blocking_extras_consumer_smoke(args: argparse.Namespace) -> int:
    """Verify opt-in Blocking Extras consumer builds and default Core negative builds."""

    return run_blocking_extras_consumer_smoke(args, run_command=run, fail_func=fail)


def check_test_support_install(args: argparse.Namespace) -> int:
    """Verify Test Support headers are opt-in and absent from the default Core stage."""

    return _check_test_support_install(args, fail_func=fail)


def test_support_consumer_smoke(args: argparse.Namespace) -> int:
    """Verify opt-in Test Support consumer builds and default Core negative builds."""

    return run_test_support_consumer_smoke(args, run_command=run, fail_func=fail)


def check_other_extras_install(args: argparse.Namespace) -> int:
    """Verify Other Extras headers are opt-in and absent from the default Core stage."""

    return _check_other_extras_install(args, fail_func=fail)


def other_extras_consumer_smoke(args: argparse.Namespace) -> int:
    """Verify opt-in Other Extras consumer builds and default Core negative builds."""

    return run_other_extras_consumer_smoke(args, run_command=run, fail_func=fail)


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

    hard_break = subparsers.add_parser("hard-break-guards")
    hard_break.add_argument("--repo-root", type=Path, required=True)
    hard_break.set_defaults(func=scan_hard_break_guards)

    surface = subparsers.add_parser("surface-manifest")
    surface.add_argument("--surface-manifest", type=Path, required=True)
    surface.add_argument("--core-manifest", type=Path, required=True)
    surface.add_argument("--extras-manifest", type=Path, required=True)
    surface.set_defaults(func=lambda args: validate_surface_manifest(args, fail_func=fail))

    install = subparsers.add_parser("install")
    install.add_argument("--cmake", required=True)
    install.add_argument("--build-dir", type=Path, required=True)
    install.add_argument("--stage-dir", type=Path, required=True)
    install.add_argument("--component", action="append", dest="components")
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

    blocking_install = subparsers.add_parser("check-blocking-extras-install")
    blocking_install.add_argument("--default-stage-dir", type=Path, required=True)
    blocking_install.add_argument("--blocking-stage-dir", type=Path, required=True)
    blocking_install.add_argument("--manifest", type=Path, required=True)
    blocking_install.set_defaults(func=check_blocking_extras_install)

    test_support_install = subparsers.add_parser("check-test-support-install")
    test_support_install.add_argument("--default-stage-dir", type=Path, required=True)
    test_support_install.add_argument("--test-support-stage-dir", type=Path, required=True)
    test_support_install.add_argument("--manifest", type=Path, required=True)
    test_support_install.set_defaults(func=check_test_support_install)

    other_extras_install = subparsers.add_parser("check-other-extras-install")
    other_extras_install.add_argument("--default-stage-dir", type=Path, required=True)
    other_extras_install.add_argument("--other-extras-stage-dir", type=Path, required=True)
    other_extras_install.add_argument("--manifest", type=Path, required=True)
    other_extras_install.set_defaults(func=check_other_extras_install)

    smoke = subparsers.add_parser("consumer-smoke")
    smoke.add_argument("--cmake", required=True)
    smoke.add_argument("--stage-dir", type=Path, required=True)
    smoke.add_argument("--positive-source-dir", type=Path, required=True)
    smoke.add_argument("--positive-build-dir", type=Path, required=True)
    smoke.add_argument("--negative-source-dir", type=Path, required=True)
    smoke.add_argument("--negative-build-dir", type=Path, required=True)
    smoke.add_argument("--config", default="")
    smoke.set_defaults(func=consumer_smoke)

    blocking_smoke = subparsers.add_parser("blocking-extras-consumer-smoke")
    blocking_smoke.add_argument("--cmake", required=True)
    blocking_smoke.add_argument("--blocking-stage-dir", type=Path, required=True)
    blocking_smoke.add_argument("--default-stage-dir", type=Path, required=True)
    blocking_smoke.add_argument("--positive-source-dir", type=Path, required=True)
    blocking_smoke.add_argument("--positive-build-dir", type=Path, required=True)
    blocking_smoke.add_argument("--negative-source-dir", type=Path, required=True)
    blocking_smoke.add_argument("--negative-build-dir", type=Path, required=True)
    blocking_smoke.add_argument("--config", default="")
    blocking_smoke.set_defaults(func=blocking_extras_consumer_smoke)

    test_support_smoke = subparsers.add_parser("test-support-consumer-smoke")
    test_support_smoke.add_argument("--cmake", required=True)
    test_support_smoke.add_argument("--test-support-stage-dir", type=Path, required=True)
    test_support_smoke.add_argument("--default-stage-dir", type=Path, required=True)
    test_support_smoke.add_argument("--positive-source-dir", type=Path, required=True)
    test_support_smoke.add_argument("--positive-build-dir", type=Path, required=True)
    test_support_smoke.add_argument("--negative-source-dir", type=Path, required=True)
    test_support_smoke.add_argument("--negative-build-dir", type=Path, required=True)
    test_support_smoke.add_argument("--config", default="")
    test_support_smoke.set_defaults(func=test_support_consumer_smoke)

    other_extras_smoke = subparsers.add_parser("other-extras-consumer-smoke")
    other_extras_smoke.add_argument("--cmake", required=True)
    other_extras_smoke.add_argument("--other-extras-stage-dir", type=Path, required=True)
    other_extras_smoke.add_argument("--default-stage-dir", type=Path, required=True)
    other_extras_smoke.add_argument("--positive-source-dir", type=Path, required=True)
    other_extras_smoke.add_argument("--positive-build-dir", type=Path, required=True)
    other_extras_smoke.add_argument("--negative-source-dir", type=Path, required=True)
    other_extras_smoke.add_argument("--negative-build-dir", type=Path, required=True)
    other_extras_smoke.add_argument("--config", default="")
    other_extras_smoke.set_defaults(func=other_extras_consumer_smoke)

    return parser


def main() -> int:
    """Entry point."""

    parser = build_parser()
    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
