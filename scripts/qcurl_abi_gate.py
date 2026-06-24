#!/usr/bin/env python3
"""Generate and compare QCurl ABI baselines without using git."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path


DEFAULT_LIBRARY = Path("build/src/libQCurl.so.1.0.0")
DEFAULT_HEADERS_DIR = Path("src")
DEFAULT_BASELINE = Path("abi/baseline/qcurl-core-v1.abi.xml")
DEFAULT_REPORT = Path("build/abi/qcurl-core-v1.abidiff.txt")
DEFAULT_CURRENT_SNAPSHOT = Path("build/abi/qcurl-core-v1.current.abi.xml")
DEFAULT_HARDBREAK_REPORT = Path("build/abi/qcurl-core-v1.hardbreak-from-previous.abidiff.txt")
DEFAULT_HARDBREAK_SNAPSHOT = Path("build/abi/qcurl-core-v1.hardbreak-current.abi.xml")

ABIDIFF_ABI_CHANGE = 4
ABIDIFF_ABI_INCOMPATIBLE_CHANGE = 8
ABIDIFF_HARDBREAK_ACCEPTED_MASK = ABIDIFF_ABI_CHANGE | ABIDIFF_ABI_INCOMPATIBLE_CHANGE


class AbiGateError(RuntimeError):
    """Raised when the ABI gate cannot produce release-grade evidence."""


def _tool_path(name: str) -> str:
    path = shutil.which(name)
    if not path:
        raise AbiGateError(f"required ABI tool not found: {name}")
    return path


def _resolve_existing_file(path: Path, description: str) -> Path:
    resolved = path.resolve()
    if not resolved.is_file():
        raise AbiGateError(f"{description} not found: {resolved}")
    return resolved


def _resolve_existing_dir(path: Path, description: str) -> Path:
    resolved = path.resolve()
    if not resolved.is_dir():
        raise AbiGateError(f"{description} not found: {resolved}")
    return resolved


def _run(command: list[str], *, output_file: Path | None = None) -> subprocess.CompletedProcess[str]:
    proc = subprocess.run(command, text=True, capture_output=True)
    if output_file is not None:
        output_file.parent.mkdir(parents=True, exist_ok=True)
        output_file.write_text((proc.stdout or "") + (proc.stderr or ""), encoding="utf-8")
    if proc.returncode != 0:
        details = (proc.stdout or "") + (proc.stderr or "")
        raise AbiGateError(
            "command failed: " + " ".join(command) + f"\nreturncode={proc.returncode}\n" + details
        )
    return proc


def _abidiff_command(baseline: Path, current_snapshot: Path, headers_dir: Path) -> list[str]:
    return [
        _tool_path("abidiff"),
        "--exported-interfaces-only",
        "--fail-no-debug-info",
        "--headers-dir1",
        str(headers_dir),
        "--headers-dir2",
        str(headers_dir),
        str(baseline),
        str(current_snapshot),
    ]


def abidiff_returncode_is_hardbreak_evidence(returncode: int) -> bool:
    """Return whether abidiff reported only ABI change evidence bits."""

    return returncode & ~ABIDIFF_HARDBREAK_ACCEPTED_MASK == 0


def _run_abidiff_hardbreak_report(command: list[str], report: Path) -> subprocess.CompletedProcess[str]:
    proc = subprocess.run(command, text=True, capture_output=True)
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text((proc.stdout or "") + (proc.stderr or ""), encoding="utf-8")
    if not abidiff_returncode_is_hardbreak_evidence(proc.returncode):
        details = (proc.stdout or "") + (proc.stderr or "")
        raise AbiGateError(
            "hard-break ABI report failed: "
            + " ".join(command)
            + f"\nreturncode={proc.returncode}\n"
            + details
        )
    if proc.returncode != 0 and not report.read_text(encoding="utf-8").strip():
        raise AbiGateError(f"hard-break ABI report is empty: {report}")
    return proc


def _abidw_command(args: argparse.Namespace, output: Path) -> list[str]:
    return [
        _tool_path("abidw"),
        "--exported-interfaces-only",
        "--no-corpus-path",
        "--no-comp-dir-path",
        "--headers-dir",
        str(_resolve_existing_dir(args.headers_dir, "headers directory")),
        "--out-file",
        str(output),
        str(_resolve_existing_file(args.library, "QCurl shared library")),
    ]


def command_baseline(args: argparse.Namespace) -> int:
    """Generate a baseline ABI XML corpus for the selected QCurl library."""

    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    _run(_abidw_command(args, output))
    print(f"[qcurl_abi_gate] baseline written: {output}")
    return 0


def command_diff(args: argparse.Namespace) -> int:
    """Compare a saved baseline against the selected QCurl library."""

    baseline = _resolve_existing_file(args.baseline, "ABI baseline")
    report = args.report.resolve()
    headers_dir = _resolve_existing_dir(args.headers_dir, "headers directory")
    current_snapshot = args.current_snapshot.resolve()
    current_snapshot.parent.mkdir(parents=True, exist_ok=True)
    _run(_abidw_command(args, current_snapshot))
    command = [
        *_abidiff_command(baseline, current_snapshot, headers_dir),
    ]
    _run(command, output_file=report)
    print(f"[qcurl_abi_gate] current snapshot written: {current_snapshot}")
    print(f"[qcurl_abi_gate] ABI diff passed: {report}")
    return 0


def command_snapshot(args: argparse.Namespace) -> int:
    """Generate an ABI XML snapshot for diagnostics without updating the baseline."""

    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    _run(_abidw_command(args, output))
    print(f"[qcurl_abi_gate] snapshot written: {output}")
    return 0


def command_hardbreak_report(args: argparse.Namespace) -> int:
    """Write an old-baseline-to-current hard-break ABI report.

    ABI changes and incompatible ABI changes are accepted for this command,
    while tool errors, usage errors, missing debug information and missing
    inputs still fail closed.
    """

    baseline = _resolve_existing_file(args.baseline, "previous ABI baseline")
    report = args.report.resolve()
    headers_dir = _resolve_existing_dir(args.headers_dir, "headers directory")
    current_snapshot = args.current_snapshot.resolve()
    current_snapshot.parent.mkdir(parents=True, exist_ok=True)
    _run(_abidw_command(args, current_snapshot))
    proc = _run_abidiff_hardbreak_report(
        _abidiff_command(baseline, current_snapshot, headers_dir),
        report,
    )
    print(f"[qcurl_abi_gate] hard-break current snapshot written: {current_snapshot}")
    print(f"[qcurl_abi_gate] hard-break ABI report written: {report}")
    print(f"[qcurl_abi_gate] hard-break abidiff returncode: {proc.returncode}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    """Create the CLI parser."""

    parser = argparse.ArgumentParser(
        description="QCurl Core ABI gate. Missing tools, libraries, headers or debug info fail closed."
    )
    parser.add_argument("--library", type=Path, default=DEFAULT_LIBRARY)
    parser.add_argument("--headers-dir", type=Path, default=DEFAULT_HEADERS_DIR)

    subparsers = parser.add_subparsers(dest="command", required=True)

    baseline = subparsers.add_parser("baseline", help="write the release ABI baseline XML")
    baseline.add_argument("--output", type=Path, default=DEFAULT_BASELINE)
    baseline.set_defaults(func=command_baseline)

    diff = subparsers.add_parser("diff", help="compare current library against an ABI baseline")
    diff.add_argument("--baseline", type=Path, default=DEFAULT_BASELINE)
    diff.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    diff.add_argument("--current-snapshot", type=Path, default=DEFAULT_CURRENT_SNAPSHOT)
    diff.set_defaults(func=command_diff)

    snapshot = subparsers.add_parser("snapshot", help="write a diagnostic ABI snapshot XML")
    snapshot.add_argument("--output", type=Path, default=DEFAULT_CURRENT_SNAPSHOT)
    snapshot.set_defaults(func=command_snapshot)

    hardbreak = subparsers.add_parser(
        "hardbreak-report",
        help="write an old-baseline-to-current hard-break ABI report without treating ABI differences as failure",
    )
    hardbreak.add_argument("--baseline", type=Path, default=DEFAULT_BASELINE)
    hardbreak.add_argument("--report", type=Path, default=DEFAULT_HARDBREAK_REPORT)
    hardbreak.add_argument("--current-snapshot", type=Path, default=DEFAULT_HARDBREAK_SNAPSHOT)
    hardbreak.set_defaults(func=command_hardbreak_report)

    return parser


def main(argv: list[str] | None = None) -> int:
    """CLI entry point."""

    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except AbiGateError as exc:
        print(f"[qcurl_abi_gate] ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
