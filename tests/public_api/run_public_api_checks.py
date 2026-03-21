#!/usr/bin/env python3
"""Run QCurl public API guardrail checks."""

from __future__ import annotations

import argparse
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


def scan_headers(args: argparse.Namespace) -> int:
    """Scan public headers for forbidden code-level tokens."""
    include_rules = [
        ("curl include", re.compile(r'^\s*#\s*include\s*<curl/')),
        ("private header include", re.compile(r'^\s*#\s*include\s*[<"].*_p\.h[">]')),
        ("Qt private include", re.compile(r'^\s*#\s*include\s*[<"]Qt.*/private/')),
        ("tuple include", re.compile(r'^\s*#\s*include\s*<tuple>')),
    ]
    code_rules = [
        ("curl type leak", re.compile(r"\bCURL[A-Za-z_0-9]*\b")),
        ("curl function leak", re.compile(r"\bcurl_[A-Za-z_0-9]+\b")),
        ("std::tuple leak", re.compile(r"\bstd::tuple\b")),
    ]

    violations: list[str] = []
    for header in read_manifest(args.manifest):
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

    if violations:
        print("\n".join(violations), file=sys.stderr)
        return 1

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


def consumer_smoke(args: argparse.Namespace) -> int:
    """Verify positive and negative staged consumer builds."""
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
