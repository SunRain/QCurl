"""release gate 的 manifest 和 ABI 参数定义。"""

from __future__ import annotations

import argparse
from pathlib import Path


def add_manifest_arguments(parser: argparse.ArgumentParser) -> None:
    """添加 manifest、authority 和扫描参数。"""

    parser.add_argument(
        "--stage",
        choices=("remediation", "final"),
        default="remediation",
        help="manifest stage; final requires a clean candidate and full qualification",
    )

    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="print the selected gate plan without running it",
    )
    parser.add_argument(
        "--manifest", type=Path, help="machine QA manifest output or verification path"
    )
    parser.add_argument(
        "--authority",
        type=Path,
        action="append",
        default=[],
        help="authority input; full requires only docs/dev/release/2.1.0-release-contract.md",
    )
    parser.add_argument(
        "--snapshot-only",
        action="store_true",
        help="write a T0 identity snapshot without claiming that gates passed",
    )
    parser.add_argument(
        "--verify-manifest",
        action="store_true",
        help="recompute identity and required gate/artifact status from --manifest",
    )
    parser.add_argument(
        "--required-artifact",
        type=Path,
        action="append",
        default=[],
        help="additional gate output that must exist and retain its content digest",
    )
    parser.add_argument(
        "--scan-metadata",
        action="store_true",
        help="run only the release metadata scan",
    )


def add_abi_arguments(parser: argparse.ArgumentParser) -> None:
    """添加首版参考和后续 patch 兼容参数。"""

    parser.add_argument(
        "--abi-mode",
        choices=("none", "snapshot", "current"),
        default="none",
        help=(
            "snapshot captures a version-line first release; current compares "
            "a patch with its first and latest published references; none is diagnostic only"
        ),
    )
    parser.add_argument(
        "--abi-reference-first",
        type=Path,
        help="verified compatibility-reference directory for vM.m.0",
    )
    parser.add_argument(
        "--abi-reference-latest",
        type=Path,
        help="verified reference for the highest published patch before this candidate",
    )
