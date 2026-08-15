#!/usr/bin/env python3
"""生成、比较并受控提升 QCurl ABI baseline。"""

from __future__ import annotations

import argparse
import platform
import subprocess
import sys
from pathlib import Path

if __package__:
    from .qcurl_abi_common import AbiGateError
    from .qcurl_abi_common import path_is_controlled_baseline
    from .qcurl_abi_common import resolve_existing_dir as _resolve_existing_dir
    from .qcurl_abi_common import resolve_existing_file as _resolve_existing_file
    from .qcurl_abi_common import run as _run
    from .qcurl_abi_common import tool_path as _tool_path
    from .qcurl_abi_common import validate_snapshot_output
    from .qcurl_abi_promotion import PROMOTION_INPUT_ARTIFACTS
    from .qcurl_abi_promotion import PROMOTION_REQUIRED_GATES
    from .qcurl_abi_promotion import assert_promotion_inputs_unchanged
    from .qcurl_abi_promotion import atomic_copy_verified_snapshot
    from .qcurl_abi_promotion import capture_promotion_inputs
    from .qcurl_abi_promotion import load_promotion_manifest
    from .qcurl_abi_promotion import verify_promotion_manifest
    from .qcurl_abi_symbols import collect_dynamic_symbols as _collect_dynamic_symbols
    from .qcurl_abi_symbols import public_symbol_owners
    from .qcurl_abi_symbols import validate_dynamic_symbol_contract
    from .qcurl_abi_symbols import (
        validate_library_dynamic_symbols as _validate_library_dynamic_symbols,
    )
else:
    from qcurl_abi_common import AbiGateError
    from qcurl_abi_common import path_is_controlled_baseline
    from qcurl_abi_common import resolve_existing_dir as _resolve_existing_dir
    from qcurl_abi_common import resolve_existing_file as _resolve_existing_file
    from qcurl_abi_common import run as _run
    from qcurl_abi_common import tool_path as _tool_path
    from qcurl_abi_common import validate_snapshot_output
    from qcurl_abi_promotion import PROMOTION_INPUT_ARTIFACTS
    from qcurl_abi_promotion import PROMOTION_REQUIRED_GATES
    from qcurl_abi_promotion import assert_promotion_inputs_unchanged
    from qcurl_abi_promotion import atomic_copy_verified_snapshot
    from qcurl_abi_promotion import capture_promotion_inputs
    from qcurl_abi_promotion import load_promotion_manifest
    from qcurl_abi_promotion import verify_promotion_manifest
    from qcurl_abi_symbols import collect_dynamic_symbols as _collect_dynamic_symbols
    from qcurl_abi_symbols import public_symbol_owners
    from qcurl_abi_symbols import validate_dynamic_symbol_contract
    from qcurl_abi_symbols import (
        validate_library_dynamic_symbols as _validate_library_dynamic_symbols,
    )


DEFAULT_LIBRARY = Path("build/src/libQCurl.so.2.0.0")
DEFAULT_HEADERS_DIR = Path("src")
DEFAULT_BASELINE = Path("abi/baseline/qcurl-core-v2.abi.xml")
DEFAULT_BASELINE_CANDIDATE = Path("build/abi/qcurl-core-v2.candidate.abi.xml")
DEFAULT_REPORT = Path("build/abi/qcurl-core-v2.abidiff.txt")
DEFAULT_CURRENT_SNAPSHOT = Path("build/abi/qcurl-core-v2.current.abi.xml")
DEFAULT_HARDBREAK_BASELINE = Path("abi/baseline/qcurl-core-v1.abi.xml")
DEFAULT_HARDBREAK_REPORT = Path("build/abi/qcurl-core-v1-to-v2.abidiff.txt")
DEFAULT_HARDBREAK_SNAPSHOT = Path("build/abi/qcurl-core-v2.candidate.abi.xml")
DEFAULT_SURFACE_MANIFEST = Path("tests/public_api/surface_manifest.json")
DEFAULT_SYMBOL_REPORT = Path("build/abi/qcurl-core-v2.dynamic-symbols.json")
DEFAULT_PROMOTION_ABIDIFF = Path("build/abi/qcurl-core-v1-to-v2.abidiff.txt")

ABIDIFF_ABI_CHANGE = 4
ABIDIFF_ABI_INCOMPATIBLE_CHANGE = 8
ABIDIFF_HARDBREAK_ACCEPTED_MASK = (
    ABIDIFF_ABI_CHANGE | ABIDIFF_ABI_INCOMPATIBLE_CHANGE
)


def _abidiff_command(
    baseline: Path,
    current_snapshot: Path,
    headers_dir: Path,
) -> list[str]:
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
    """判断 abidiff 返回码是否只包含 ABI 变化位。"""

    return returncode & ~ABIDIFF_HARDBREAK_ACCEPTED_MASK == 0


def _run_abidiff_hardbreak_report(
    command: list[str],
    report: Path,
) -> subprocess.CompletedProcess[str]:
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
    if proc.returncode == 0 and not report.read_text(encoding="utf-8").strip():
        report.write_text(
            "No ABI changes detected (abidiff returncode=0).\n",
            encoding="utf-8",
        )
    if not report.read_text(encoding="utf-8").strip():
        raise AbiGateError(f"hard-break ABI report is empty: {report}")
    return proc


def _abidw_command(args: argparse.Namespace, output: Path) -> list[str]:
    library = _resolve_existing_file(args.library, "QCurl shared library")
    _validate_library_dynamic_symbols(args, library)
    return [
        _tool_path("abidw"),
        "--exported-interfaces-only",
        "--no-corpus-path",
        "--no-comp-dir-path",
        "--headers-dir",
        str(_resolve_existing_dir(args.headers_dir, "headers directory")),
        "--out-file",
        str(output),
        str(library),
    ]


def command_baseline(args: argparse.Namespace) -> int:
    """生成所选 QCurl 动态库的诊断 ABI baseline。"""

    output = args.output.resolve()
    validate_snapshot_output(output, repo_root=Path(__file__).resolve().parent.parent)
    output.parent.mkdir(parents=True, exist_ok=True)
    _run(_abidw_command(args, output))
    print(f"[qcurl_abi_gate] baseline written: {output}")
    return 0


def command_diff(args: argparse.Namespace) -> int:
    """比较受控 baseline 与所选 QCurl 动态库。"""

    baseline = _resolve_existing_file(args.baseline, "ABI baseline")
    report = args.report.resolve()
    headers_dir = _resolve_existing_dir(args.headers_dir, "headers directory")
    current_snapshot = args.current_snapshot.resolve()
    repo_root = Path(__file__).resolve().parent.parent
    validate_snapshot_output(report, repo_root=repo_root)
    validate_snapshot_output(current_snapshot, repo_root=repo_root)
    current_snapshot.parent.mkdir(parents=True, exist_ok=True)
    _run(_abidw_command(args, current_snapshot))
    _run(
        _abidiff_command(baseline, current_snapshot, headers_dir),
        output_file=report,
    )
    print(f"[qcurl_abi_gate] current snapshot written: {current_snapshot}")
    print(f"[qcurl_abi_gate] ABI diff passed: {report}")
    return 0


def command_snapshot(args: argparse.Namespace) -> int:
    """生成诊断 ABI snapshot，不修改受控 baseline。"""

    output = args.output.resolve()
    validate_snapshot_output(output, repo_root=Path(__file__).resolve().parent.parent)
    output.parent.mkdir(parents=True, exist_ok=True)
    _run(_abidw_command(args, output))
    print(f"[qcurl_abi_gate] snapshot written: {output}")
    return 0


def command_hardbreak_report(args: argparse.Namespace) -> int:
    """生成旧 baseline 到当前 snapshot 的 hard-break ABI 报告。"""

    baseline = _resolve_existing_file(args.baseline, "previous ABI baseline")
    report = args.report.resolve()
    headers_dir = _resolve_existing_dir(args.headers_dir, "headers directory")
    current_snapshot = args.current_snapshot.resolve()
    repo_root = Path(__file__).resolve().parent.parent
    validate_snapshot_output(report, repo_root=repo_root)
    validate_snapshot_output(current_snapshot, repo_root=repo_root)
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


def command_symbols(args: argparse.Namespace) -> int:
    """验证并记录 Linux ELF 动态符号 allowlist。"""

    library = _resolve_existing_file(args.library, "QCurl shared library")
    _validate_library_dynamic_symbols(args, library)
    print(
        "[qcurl_abi_gate] dynamic symbol allowlist passed: "
        f"{args.symbol_report.resolve()}"
    )
    return 0


def _validate_checked_out_candidate(candidate_commit: str) -> None:
    current_head = _run([_tool_path("git"), "rev-parse", "HEAD"]).stdout.strip()
    if current_head != candidate_commit:
        raise AbiGateError(
            "checked-out commit differs from the immutable recorded candidate"
        )
    status = _run(
        [_tool_path("git"), "status", "--porcelain", "--untracked-files=all"]
    ).stdout
    if status.strip():
        raise AbiGateError("baseline promotion requires a clean checked-out candidate")


def _validate_promotion_target(target: Path, repo_root: Path) -> None:
    expected = (repo_root / "abi" / "baseline" / "qcurl-core-v2.abi.xml").resolve()
    if target != expected:
        raise AbiGateError(
            "promote output must be exactly abi/baseline/qcurl-core-v2.abi.xml"
        )
    if not path_is_controlled_baseline(target, repo_root=repo_root):
        raise AbiGateError("promote output must be below abi/baseline")


def command_promote(args: argparse.Namespace) -> int:
    """把已验证 snapshot 原子复制到受控 ABI baseline。"""

    if platform.system() != "Linux":
        raise AbiGateError("baseline promotion requires Linux")
    manifest = load_promotion_manifest(
        args.candidate_manifest,
        candidate_commit=args.candidate_commit,
    )
    inputs = capture_promotion_inputs(manifest, args.candidate_manifest)
    verify_promotion_manifest(
        manifest,
        manifest_path=args.candidate_manifest,
        library=args.library,
        old_baseline=args.old_baseline,
        candidate_snapshot=args.candidate_snapshot,
        old_to_new_report=args.old_to_new_report,
    )
    _validate_checked_out_candidate(args.candidate_commit)
    assert_promotion_inputs_unchanged(inputs)
    snapshot = inputs.artifact(PROMOTION_INPUT_ARTIFACTS["candidate_snapshot"])
    repo_root = Path(__file__).resolve().parent.parent
    validate_snapshot_output(snapshot.path, repo_root=repo_root)
    target = args.output.resolve()
    _validate_promotion_target(target, repo_root)
    atomic_copy_verified_snapshot(
        snapshot.path,
        target,
        snapshot.sha256,
        before_replace=lambda: assert_promotion_inputs_unchanged(inputs),
    )
    print(f"[qcurl_abi_gate] promoted baseline written: {target}")
    print(
        "[qcurl_abi_gate] validated old-to-new ABI report: "
        f"{args.old_to_new_report.resolve()}"
    )
    return 0


def _add_common_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--library", type=Path, default=DEFAULT_LIBRARY)
    parser.add_argument("--headers-dir", type=Path, default=DEFAULT_HEADERS_DIR)
    parser.add_argument(
        "--surface-manifest",
        type=Path,
        default=DEFAULT_SURFACE_MANIFEST,
    )
    parser.add_argument("--source-root", type=Path, default=DEFAULT_HEADERS_DIR)
    parser.add_argument("--component", choices=("core", "other-extras"), default="core")
    parser.add_argument("--symbol-report", type=Path, default=DEFAULT_SYMBOL_REPORT)


def _add_diagnostic_commands(subparsers: argparse._SubParsersAction) -> None:
    baseline = subparsers.add_parser(
        "baseline", help="write a diagnostic baseline candidate XML"
    )
    baseline.add_argument("--output", type=Path, default=DEFAULT_BASELINE_CANDIDATE)
    baseline.set_defaults(func=command_baseline)

    diff = subparsers.add_parser(
        "diff", help="compare current library against an ABI baseline"
    )
    diff.add_argument("--baseline", type=Path, default=DEFAULT_BASELINE)
    diff.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    diff.add_argument(
        "--current-snapshot", type=Path, default=DEFAULT_CURRENT_SNAPSHOT
    )
    diff.set_defaults(func=command_diff)

    snapshot = subparsers.add_parser(
        "snapshot", help="write a diagnostic ABI snapshot XML"
    )
    snapshot.add_argument("--output", type=Path, default=DEFAULT_CURRENT_SNAPSHOT)
    snapshot.set_defaults(func=command_snapshot)


def _add_hardbreak_and_symbol_commands(
    subparsers: argparse._SubParsersAction,
) -> None:
    hardbreak = subparsers.add_parser(
        "hardbreak-report",
        help=(
            "write an old-baseline-to-current hard-break ABI report without "
            "treating ABI differences as failure"
        ),
    )
    hardbreak.add_argument("--baseline", type=Path, default=DEFAULT_HARDBREAK_BASELINE)
    hardbreak.add_argument("--report", type=Path, default=DEFAULT_HARDBREAK_REPORT)
    hardbreak.add_argument(
        "--current-snapshot", type=Path, default=DEFAULT_HARDBREAK_SNAPSHOT
    )
    hardbreak.set_defaults(func=command_hardbreak_report)

    symbols = subparsers.add_parser(
        "symbols", help="validate Linux dynamic symbols against the public allowlist"
    )
    symbols.set_defaults(func=command_symbols)


def _add_promotion_command(subparsers: argparse._SubParsersAction) -> None:
    promote = subparsers.add_parser(
        "promote",
        help="copy a verified immutable candidate snapshot into the v2 baseline",
    )
    promote.add_argument("--candidate-manifest", type=Path, required=True)
    promote.add_argument("--candidate-commit", required=True)
    promote.add_argument("--old-baseline", type=Path, required=True)
    promote.add_argument(
        "--candidate-snapshot",
        type=Path,
        default=DEFAULT_BASELINE_CANDIDATE,
    )
    promote.add_argument(
        "--old-to-new-report",
        type=Path,
        default=DEFAULT_PROMOTION_ABIDIFF,
    )
    promote.add_argument(
        "--output",
        type=Path,
        default=Path("abi/baseline/qcurl-core-v2.abi.xml"),
    )
    promote.set_defaults(func=command_promote)


def build_parser() -> argparse.ArgumentParser:
    """创建 ABI gate 命令行解析器。"""

    parser = argparse.ArgumentParser(
        description=(
            "QCurl Core ABI gate. Missing tools, libraries, headers or debug info "
            "fail closed."
        )
    )
    _add_common_arguments(parser)
    subparsers = parser.add_subparsers(dest="command", required=True)
    _add_diagnostic_commands(subparsers)
    _add_hardbreak_and_symbol_commands(subparsers)
    _add_promotion_command(subparsers)
    return parser


def main(argv: list[str] | None = None) -> int:
    """执行 ABI gate 命令。"""

    args = build_parser().parse_args(argv)
    try:
        return args.func(args)
    except AbiGateError as exc:
        print(f"[qcurl_abi_gate] ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
