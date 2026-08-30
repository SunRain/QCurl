#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
一致性 gate 入口：
- 可选执行构建，再统一触发 pytest
- 输出 JUnit XML 和 JSON 报告
- 默认开启失败日志收集：QCURL_LC_COLLECT_LOGS=1
"""

from __future__ import annotations

import argparse
import sys
import uuid
from pathlib import Path
from typing import Dict, List, Optional

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tests.libcurl_consistency.pytest_support.gate_execution import execute_gate
from tests.libcurl_consistency.pytest_support.gate_runtime import GateConfig
from tests.libcurl_consistency.pytest_support.gate_runtime import build_targets as _build_targets
from tests.libcurl_consistency.pytest_support.gate_runtime import candidate_files as _pytest_files
from tests.libcurl_consistency.pytest_support.gate_runtime import capability_probe_binary as _capability_probe_bin
from tests.libcurl_consistency.pytest_support.gate_runtime import check_artifact_schema as _postflight_artifacts_schema_check
from tests.libcurl_consistency.pytest_support.gate_runtime import check_redaction as _postflight_redaction_scan
from tests.libcurl_consistency.pytest_support.gate_runtime import check_required_inputs as _preflight_required_inputs
from tests.libcurl_consistency.pytest_support.gate_runtime import current_capability_identity as _current_capability_identity
from tests.libcurl_consistency.pytest_support.gate_runtime import default_capability_manifest as _default_capability_manifest
from tests.libcurl_consistency.pytest_support.gate_runtime import default_reports_dir as _default_reports_dir
from tests.libcurl_consistency.pytest_support.gate_runtime import detect_repo_root as _detect_repo_root
from tests.libcurl_consistency.pytest_support.gate_runtime import ensure_parent as _ensure_parent
from tests.libcurl_consistency.pytest_support.gate_runtime import evaluate_http3_preflight as _evaluate_http3_preflight
from tests.libcurl_consistency.pytest_support.gate_runtime import existing_path as _first_existing_path
from tests.libcurl_consistency.pytest_support.gate_runtime import forbid_gate_httpbin as _preflight_forbid_local_httpbin
from tests.libcurl_consistency.pytest_support.gate_runtime import gate_environment as _gate_env
from tests.libcurl_consistency.pytest_support.gate_runtime import generate_capability_manifest as _generate_capability_manifest
from tests.libcurl_consistency.pytest_support.gate_runtime import load_capability_manifest as _load_capability_manifest
from tests.libcurl_consistency.pytest_support.gate_runtime import parse_junit_counts as _parse_junit_counts
from tests.libcurl_consistency.pytest_support.gate_runtime import plan_files as _plan_pytest_files
from tests.libcurl_consistency.pytest_support.gate_runtime import report_artifacts_dir as _artifacts_dir
from tests.libcurl_consistency.pytest_support.gate_runtime import report_redaction_roots as _redaction_scan_roots
from tests.libcurl_consistency.pytest_support.gate_runtime import resolve_config as _runtime_resolve_config
from tests.libcurl_consistency.pytest_support.gate_runtime import run_command as _run
from tests.libcurl_consistency.pytest_support.gate_report import redact_text as _redact_text


def _argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="QCurl ↔ libcurl 一致性 Gate（P0 优先）")
    parser.add_argument(
        "--suite",
        choices=["p0", "p1", "p2", "all"],
        default="p0",
        help="选择要跑的套件（默认 p0）",
    )
    parser.add_argument(
        "--with-ext",
        action="store_true",
        help="同时运行 ext suite（需要 QCURL_LC_EXT=1）",
    )
    parser.add_argument(
        "--build",
        action="store_true",
        help="先构建一致性门禁所需依赖（qcurl_lc_deps；含 curl testdeps/libtests；nghttpx-h3 best-effort）",
    )
    parser.add_argument(
        "--qcurl-build",
        default="build",
        help="QCurl CMake build 目录（默认 build）",
    )
    parser.add_argument(
        "--curl-build",
        default="",
        help="curl build 目录（默认 <qcurl_build>/curl，即 build/curl）",
    )
    parser.add_argument(
        "--reports-dir",
        default="",
        help="报告输出目录（默认 <qcurl_build>/libcurl_consistency/reports）",
    )
    parser.add_argument(
        "--run-id",
        default="",
        help="可复现的本轮证据 ID；默认自动生成",
    )
    parser.add_argument(
        "--summary-report",
        type=Path,
        help="gate 成功后原子发布本轮 JSON 报告到指定路径",
    )
    parser.add_argument(
        "--qt-timeout-s",
        default="90",
        help="Qt Test 运行超时秒数（默认 90）",
    )
    return parser


def _detect_repo_root() -> Path:
    """返回仓库根目录；保留可被基础设施测试替换的入口。"""

    return Path(__file__).resolve().parents[2]


def _resolve_config(args: argparse.Namespace) -> GateConfig:
    """解析配置并允许测试隔离仓库根目录。"""

    return _runtime_resolve_config(args, repo_root=_detect_repo_root())


def _publish_summary_report(config: GateConfig, destination: Path) -> None:
    """原子发布成功 run 的报告，不暴露部分写入内容。"""

    output = destination if destination.is_absolute() else config.repo_root / destination
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(f".{output.name}.{uuid.uuid4().hex}.tmp")
    try:
        temporary.write_bytes(config.json_report.read_bytes())
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)


def main(argv: List[str]) -> int:
    """解析命令行并执行一致性 gate。"""

    args = _argument_parser().parse_args(argv)
    config = _resolve_config(args)
    result = execute_gate(config)
    if result == 0 and args.summary_report is not None:
        try:
            _publish_summary_report(config, args.summary_report)
        except OSError as exc:
            print(f"failed to publish summary report: {exc}", file=sys.stderr)
            return 2
    return result


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
