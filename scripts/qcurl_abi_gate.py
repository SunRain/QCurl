#!/usr/bin/env python3
"""生成 QCurl 组件 snapshot、符号报告和保守 ABI 比较。"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

if __package__:
    from .check_release_contract import parse_project_version
    from .qcurl_abi_common import AbiGateError, path_is_controlled_baseline
    from .qcurl_abi_common import resolve_existing_dir, resolve_existing_file
    from .qcurl_abi_common import validate_snapshot_output
    from .qcurl_abi_compare import (
        ABIDIFF_CHANGE_MASK,
        COMPONENT_LIBRARIES,
        abidiff_command,
        compare_snapshots,
        run_abidiff_report,
        snapshot_library,
    )
    from .qcurl_abi_symbols import collect_dynamic_symbols as _collect_dynamic_symbols
    from .qcurl_abi_symbols import (
        public_symbol_owners,
        validate_dynamic_symbol_contract,
    )
    from .qcurl_abi_symbols import validate_library_dynamic_symbols
else:
    from check_release_contract import parse_project_version
    from qcurl_abi_common import AbiGateError, path_is_controlled_baseline
    from qcurl_abi_common import resolve_existing_dir, resolve_existing_file
    from qcurl_abi_common import validate_snapshot_output
    from qcurl_abi_compare import (
        ABIDIFF_CHANGE_MASK,
        COMPONENT_LIBRARIES,
        abidiff_command,
        compare_snapshots,
        run_abidiff_report,
        snapshot_library,
    )
    from qcurl_abi_symbols import collect_dynamic_symbols as _collect_dynamic_symbols
    from qcurl_abi_symbols import public_symbol_owners, validate_dynamic_symbol_contract
    from qcurl_abi_symbols import validate_library_dynamic_symbols


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_HEADERS_DIR = Path("src")
DEFAULT_HARDBREAK_BASELINE = Path("abi/baseline/qcurl-core-v1.abi.xml")
DEFAULT_SURFACE_MANIFEST = Path("tests/public_api/surface_manifest.json")


def _set_output_defaults(args: argparse.Namespace) -> None:
    if args.library is None:
        version = ".".join(str(part) for part in parse_project_version(REPO_ROOT))
        args.library = (
            Path("build/src") / f"{COMPONENT_LIBRARIES[args.component]}.so.{version}"
        )
    defaults = {
        "symbol_report": f"{args.component}.dynamic-symbols.json",
        "output": f"{args.component}.snapshot.xml",
        "current_snapshot": f"{args.component}.snapshot.xml",
        "report": f"{args.component}.abidiff.txt",
        "residual_report": f"{args.component}.residual.abidiff.txt",
        "result_report": f"{args.component}.comparison.json",
    }
    for name, filename in defaults.items():
        if hasattr(args, name) and getattr(args, name) is None:
            setattr(args, name, Path("build/abi") / filename)


def _snapshot(args: argparse.Namespace, output: Path) -> dict:
    return snapshot_library(
        args.library,
        args.headers_dir,
        output,
        component=args.component,
        surface_manifest=args.surface_manifest,
        source_root=args.source_root,
        symbol_report=args.symbol_report,
    )


def command_snapshot(args: argparse.Namespace) -> int:
    """捕获组件 ABI snapshot 和对应动态符号报告。"""

    _snapshot(args, args.output)
    print(f"[qcurl_abi_gate] snapshot written: {args.output.resolve()}")
    return 0


def command_diff(args: argparse.Namespace) -> int:
    """用独立旧、新公共头比较显式参考与当前组件库。"""

    _snapshot(args, args.current_snapshot)
    compare_snapshots(
        args.baseline,
        args.current_snapshot,
        args.old_headers_dir,
        args.headers_dir,
        component=args.component,
        baseline_library=args.baseline_library,
        current_library=args.library,
        raw_report=args.report,
        residual_report=args.residual_report,
        result_report=args.result_report,
    )
    print(
        f"[qcurl_abi_gate] conservative ABI comparison passed: {args.result_report.resolve()}"
    )
    return 0


def abidiff_returncode_is_hardbreak_evidence(returncode: int) -> bool:
    """诊断允许已披露的 ABI 变化位，工具和未知状态位仍失败。"""

    return returncode & ~ABIDIFF_CHANGE_MASK == 0


def command_hardbreak_report(args: argparse.Namespace) -> int:
    """保留历史跨线分析，不把 ABI 变化当作兼容通过。"""

    baseline = resolve_existing_file(args.baseline, "previous ABI baseline")
    old_headers = resolve_existing_dir(
        args.old_headers_dir, "old public headers directory"
    )
    new_headers = resolve_existing_dir(args.headers_dir, "new public headers directory")
    report = args.report.resolve()
    validate_snapshot_output(report, repo_root=REPO_ROOT)
    if report.exists():
        raise AbiGateError(f"ABI evidence output already exists: {report}")
    _snapshot(args, args.current_snapshot)
    report.parent.mkdir(parents=True, exist_ok=True)
    result = run_abidiff_report(
        abidiff_command(
            baseline, args.current_snapshot.resolve(), old_headers, new_headers
        ),
        report,
    )
    if (
        not abidiff_returncode_is_hardbreak_evidence(result["returncode"])
        or result["emptyErrorOutput"]
    ):
        raise AbiGateError(f"hard-break ABI analysis failed: {result}")
    print(f"[qcurl_abi_gate] hard-break diagnostic report written: {report}")
    print(f"[qcurl_abi_gate] hard-break abidiff returncode: {result['returncode']}")
    return 0


def command_symbols(args: argparse.Namespace) -> int:
    """验证并记录 Linux ELF 动态符号 allowlist。"""

    library = resolve_existing_file(args.library, "QCurl shared library")
    validate_snapshot_output(args.symbol_report, repo_root=REPO_ROOT)
    if args.symbol_report.exists():
        raise AbiGateError(f"ABI evidence output already exists: {args.symbol_report}")
    validate_library_dynamic_symbols(args, library)
    print(
        f"[qcurl_abi_gate] dynamic symbol allowlist passed: {args.symbol_report.resolve()}"
    )
    return 0


def _add_common_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--library", type=Path)
    parser.add_argument("--headers-dir", type=Path, default=DEFAULT_HEADERS_DIR)
    parser.add_argument(
        "--surface-manifest", type=Path, default=DEFAULT_SURFACE_MANIFEST
    )
    parser.add_argument("--source-root", type=Path, default=DEFAULT_HEADERS_DIR)
    parser.add_argument(
        "--component", choices=tuple(COMPONENT_LIBRARIES), default="core"
    )
    parser.add_argument("--symbol-report", type=Path)


def build_parser() -> argparse.ArgumentParser:
    """创建显式参考输入的组件 ABI 命令行。"""

    parser = argparse.ArgumentParser(
        description="QCurl shared-component ABI evidence; failures fail closed."
    )
    _add_common_arguments(parser)
    subparsers = parser.add_subparsers(dest="command", required=True)
    snapshot = subparsers.add_parser("snapshot", help="capture ABI and symbol evidence")
    snapshot.add_argument("--output", type=Path)
    snapshot.set_defaults(func=command_snapshot)
    diff = subparsers.add_parser(
        "diff", help="conservatively compare an explicit ABI reference"
    )
    diff.add_argument("--baseline", type=Path, required=True)
    diff.add_argument("--baseline-library", type=Path, required=True)
    diff.add_argument("--old-headers-dir", type=Path, required=True)
    diff.add_argument("--current-snapshot", type=Path)
    diff.add_argument("--report", type=Path)
    diff.add_argument("--residual-report", type=Path)
    diff.add_argument("--result-report", type=Path)
    diff.set_defaults(func=command_diff)
    hardbreak = subparsers.add_parser(
        "hardbreak-report", help="explicit cross-line diagnostic only"
    )
    hardbreak.add_argument("--baseline", type=Path, default=DEFAULT_HARDBREAK_BASELINE)
    hardbreak.add_argument("--old-headers-dir", type=Path, required=True)
    hardbreak.add_argument("--current-snapshot", type=Path)
    hardbreak.add_argument("--report", type=Path)
    hardbreak.set_defaults(func=command_hardbreak_report)
    symbols = subparsers.add_parser(
        "symbols", help="validate component dynamic symbols"
    )
    symbols.set_defaults(func=command_symbols)
    return parser


def main(argv: list[str] | None = None) -> int:
    """执行 ABI 命令并保留失败原因。"""

    args = build_parser().parse_args(argv)
    try:
        _set_output_defaults(args)
        return args.func(args)
    except (AbiGateError, OSError, ValueError) as exc:
        print(f"[qcurl_abi_gate] ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
