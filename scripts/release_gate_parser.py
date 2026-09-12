"""release gate 的 manifest 和 ABI 参数定义。"""

from __future__ import annotations

import argparse
from pathlib import Path


def add_manifest_arguments(parser: argparse.ArgumentParser) -> None:
    """添加 manifest、authority 和扫描参数。"""

    parser.add_argument(
        "--stage",
        choices=("remediation", "promotion", "final"),
        default="remediation",
        help="manifest stage; promotion and final are independently verified",
    )

    parser.add_argument("--dry-run", action="store_true", help="print the selected gate plan without running it")
    parser.add_argument("--manifest", type=Path, help="machine QA manifest output or verification path")
    parser.add_argument(
        "--authority",
        type=Path,
        action="append",
        default=[],
        help="authority input; full requires only docs/arch/2.0.0-hard-break-release-contract.md",
    )
    parser.add_argument("--snapshot-only", action="store_true", help="write a T0 identity snapshot without claiming that gates passed")
    parser.add_argument("--verify-manifest", action="store_true", help="recompute identity and required gate/artifact status from --manifest")
    parser.add_argument(
        "--required-artifact",
        type=Path,
        action="append",
        default=[],
        help="additional gate output that must exist and retain its content digest",
    )
    parser.add_argument("--scan-metadata", action="store_true", help="run only the release metadata scan")


def add_abi_arguments(parser: argparse.ArgumentParser) -> None:
    """添加默认非稳定 ABI 与显式诊断参数。"""

    parser.add_argument(
        "--abi-mode",
        choices=("none", "current", "promotion-candidate"),
        default="none",
        help=(
            "ABI compatibility mode; none is the 2.0 source-compatible, "
            "rebuild-required release contract"
        ),
    )
    parser.add_argument(
        "--abi-hardbreak-baseline",
        type=Path,
        help="archived pre-1.0 baseline XML used only for internal ABI comparison",
    )
    parser.add_argument("--abi-hardbreak-report", type=Path, help="output path for the old-baseline ABI report")
    parser.add_argument(
        "--abi-hardbreak-current-snapshot",
        type=Path,
        help="output path for the promotion candidate ABI snapshot",
    )
