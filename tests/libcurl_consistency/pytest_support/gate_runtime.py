"""一致性 gate 的配置、子进程与能力清单运行时。"""

from __future__ import annotations

import argparse
import json
import os
import shlex
import subprocess
import sys
import time
import uuid
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional

from .capability_manifest import capture_identity
from .capability_manifest import seal_manifest
from .capability_manifest import validate_manifest
from .gate_planner import plan_pytest_files
from .gate_planner import pytest_files
from .gate_preflight import evaluate_http3_preflight as evaluate_http3_preflight_result
from .gate_preflight import first_existing_path
from .gate_preflight import forbid_local_httpbin
from .gate_preflight import preflight_required_inputs
from .gate_postflight import postflight_artifacts_schema_check
from .gate_postflight import postflight_redaction_scan
from .gate_report import artifacts_dir
from .gate_report import parse_junit_counts
from .gate_report import redact_text
from .gate_report import redaction_scan_roots


FORBIDDEN_LOCAL_HTTPBIN_ENDPOINTS = (
    "localhost:8935",
    "127.0.0.1:8935",
)
EXPECTED_ARTIFACTS_SCHEMA = "qcurl-lc/artifacts@v1"


@dataclass(frozen=True)
class GateConfig:
    """一次一致性 gate 运行的不可变配置。"""

    repo_root: Path
    qcurl_build_dir: Path
    curl_build_dir: Path
    capability_manifest: Path
    suite: str
    build: bool
    with_ext: bool
    junit_xml: Path
    json_report: Path
    qt_timeout_s: float
    reports_root: Optional[Path] = None
    run_id: str = ""
    run_dir: Optional[Path] = None
    artifacts_dir: Optional[Path] = None
    execution_token: str = ""


def print_command(command: List[str]) -> None:
    """将待执行命令以可复制形式写入标准错误。"""

    sys.stderr.write("+ " + " ".join(shlex.quote(value) for value in command) + "\n")


def run_command(
    command: List[str],
    *,
    cwd: Optional[Path] = None,
    env: Optional[Dict[str, str]] = None,
    capture: bool = False,
) -> subprocess.CompletedProcess:
    """执行 gate 子进程，并保留调用方对退出码的控制。"""

    print_command(command)
    return subprocess.run(
        command,
        cwd=str(cwd) if cwd else None,
        env=env,
        check=False,
        text=True,
        capture_output=capture,
    )


def ensure_parent(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def report_artifacts_dir(config: GateConfig) -> Path:
    return config.artifacts_dir or artifacts_dir(config.repo_root)


def report_redaction_roots(config: GateConfig) -> List[Path]:
    reports_dir = config.run_dir or config.json_report.parent
    return redaction_scan_roots(config.repo_root, reports_dir, report_artifacts_dir(config))


def check_artifact_schema(config: GateConfig, *, since_ts: float) -> Dict[str, object]:
    return postflight_artifacts_schema_check(
        repo_root=config.repo_root,
        artifacts_dir=report_artifacts_dir(config),
        since_ts=since_ts,
        expected_schema=EXPECTED_ARTIFACTS_SCHEMA,
    )


def check_redaction(config: GateConfig, *, since_ts: float) -> Dict[str, object]:
    return postflight_redaction_scan(
        config.repo_root,
        report_redaction_roots(config),
        since_ts=since_ts,
    )


def forbid_gate_httpbin(config: GateConfig) -> List[Dict[str, object]]:
    entrypoint = Path(__file__).resolve().parents[1] / "run_gate.py"
    return forbid_local_httpbin(
        config,
        forbidden_endpoints=FORBIDDEN_LOCAL_HTTPBIN_ENDPOINTS,
        current_file=entrypoint,
        additional_excluded_files=(
            Path(__file__),
            Path(__file__).with_name("gate_execution.py"),
        ),
    )


def default_reports_dir(qcurl_build_dir: Path) -> Path:
    return qcurl_build_dir / "libcurl_consistency" / "reports"


def default_capability_manifest(qcurl_build_dir: Path) -> Path:
    return default_reports_dir(qcurl_build_dir) / "capabilities.json"


def detect_repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def resolve_config(args: argparse.Namespace, *, repo_root: Optional[Path] = None) -> GateConfig:
    """解析 CLI 参数并生成 run-scoped 输出路径。"""

    repo_root = repo_root or detect_repo_root()
    qcurl_build_dir = (repo_root / args.qcurl_build).resolve()
    curl_build_dir = (
        (repo_root / args.curl_build).resolve()
        if args.curl_build
        else (qcurl_build_dir / "curl").resolve()
    )
    reports_root = (
        (repo_root / args.reports_dir).resolve()
        if args.reports_dir
        else default_reports_dir(qcurl_build_dir)
    )
    run_id = str(args.run_id or "").strip() or (
        f"{time.strftime('%Y%m%dT%H%M%SZ', time.gmtime())}-{uuid.uuid4().hex[:12]}"
    )
    if any(character not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_" for character in run_id):
        raise ValueError("--run-id 只能包含字母、数字、连字符和下划线")
    run_dir = reports_root / "runs" / run_id
    try:
        run_dir.mkdir(parents=True, exist_ok=False)
    except FileExistsError as exc:
        raise ValueError(f"run-id 已存在，禁止复用: {run_id}") from exc
    return GateConfig(
        repo_root=repo_root,
        qcurl_build_dir=qcurl_build_dir,
        curl_build_dir=curl_build_dir,
        capability_manifest=run_dir / "capabilities.json",
        suite=args.suite,
        build=bool(args.build),
        with_ext=bool(args.with_ext),
        junit_xml=run_dir / f"junit_{args.suite}.xml",
        json_report=run_dir / f"gate_{args.suite}.json",
        qt_timeout_s=float(args.qt_timeout_s),
        reports_root=reports_root,
        run_id=run_id,
        run_dir=run_dir,
        artifacts_dir=run_dir / "artifacts",
        execution_token=uuid.uuid4().hex,
    )


def build_targets(config: GateConfig) -> None:
    """构建一致性门禁依赖与可选 HTTP/3 服务端。"""

    if not config.qcurl_build_dir.exists():
        raise RuntimeError(f"QCurl build dir 不存在: {config.qcurl_build_dir}")
    if not config.curl_build_dir.exists():
        raise RuntimeError(
            "curl build dir 不存在（需要将 curl 构建到 <qcurl_build>/curl，例如默认 build/curl）。\n"
            f"- 当前 qcurl_build_dir: {config.qcurl_build_dir}\n"
            f"- 当前 curl_build_dir: {config.curl_build_dir}\n"
            "请先配置：cmake -B <qcurl_build> -DQCURL_BUILD_LIBCURL_CONSISTENCY=ON"
        )
    jobs = str(os.cpu_count() or 4)
    result = run_command(
        ["cmake", "--build", str(config.qcurl_build_dir), "--target", "qcurl_lc_deps", "-j", jobs]
    )
    if result.returncode != 0:
        raise RuntimeError("build qcurl_lc_deps failed. Please check the build log above for details.")
    result = run_command(
        ["cmake", "--build", str(config.qcurl_build_dir), "--target", "qcurl_nghttpx_h3", "-j", jobs]
    )
    if result.returncode != 0:
        sys.stderr.write("[warn] build qcurl_nghttpx_h3 failed (h3 variants may be skipped)\n")


def capability_probe_binary(config: GateConfig) -> Path:
    candidates = [
        config.qcurl_build_dir / "tests" / "qcurl_lc_capability_probe",
        config.qcurl_build_dir / "tests" / "qcurl_lc_capability_probe.exe",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return candidates[0]


def current_capability_identity(config: GateConfig) -> Dict[str, object]:
    return capture_identity(
        repo_root=config.repo_root,
        run_id=config.run_id,
        producer=capability_probe_binary(config),
        qcurl_binary=config.qcurl_build_dir / "tests" / "tst_LibcurlConsistency",
        libcurl_binary=config.curl_build_dir / "src" / "curl",
        cmake_cache=config.qcurl_build_dir / "CMakeCache.txt",
    )


def gate_environment(config: GateConfig) -> Dict[str, str]:
    """生成测试执行环境并注入 run-scoped 身份与工件目录。"""

    env = os.environ.copy()
    env["QCURL_QTTEST"] = str(config.qcurl_build_dir / "tests" / "tst_LibcurlConsistency")
    env["QCURL_BUILD_DIR"] = str(config.qcurl_build_dir)
    env["CURL_BUILD_DIR"] = str(config.curl_build_dir)
    env["CURL"] = str(config.curl_build_dir / "src" / "curl")
    env["CURLINFO"] = str(config.curl_build_dir / "src" / "curlinfo")
    env["QCURL_LC_CAPABILITY_MANIFEST"] = str(config.capability_manifest)
    if config.run_id:
        env["QCURL_LC_RUN_ID"] = config.run_id
    if config.execution_token:
        env["QCURL_LC_EXECUTION_TOKEN"] = config.execution_token
    if config.artifacts_dir:
        env["QCURL_LC_ARTIFACTS_DIR"] = str(config.artifacts_dir)
    env.setdefault("QCURL_LC_COLLECT_LOGS", "1")
    env.setdefault("QCURL_LC_QTTEST_TIMEOUT", str(config.qt_timeout_s))
    if config.suite in ("p2", "all"):
        env.setdefault("QCURL_LC_EXPECT100_REPEAT", "2")
    if config.with_ext:
        env["QCURL_LC_EXT"] = "1"
    return env


def generate_capability_manifest(config: GateConfig) -> None:
    """运行能力探针并为清单绑定当前源码与构建身份。"""

    probe_binary = capability_probe_binary(config)
    if not probe_binary.exists():
        raise RuntimeError(f"capability probe binary not found: {probe_binary}")
    ensure_parent(config.capability_manifest)
    result = run_command(
        [str(probe_binary), "--output", str(config.capability_manifest)],
        cwd=config.repo_root,
        env=gate_environment(config),
        capture=True,
    )
    if result.stdout:
        sys.stdout.write(redact_text(result.stdout))
    if result.stderr:
        sys.stderr.write(redact_text(result.stderr))
    if result.returncode != 0:
        raise RuntimeError(f"capability probe failed: rc={result.returncode}")
    try:
        probe_manifest = json.loads(config.capability_manifest.read_text(encoding="utf-8"))
    except Exception as exc:
        raise RuntimeError(f"failed to read generated capability manifest: {exc}") from exc
    sealed = seal_manifest(
        probe_manifest,
        identity=current_capability_identity(config),
        generated_at_epoch=time.time(),
    )
    config.capability_manifest.write_text(
        json.dumps(sealed, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )


def load_capability_manifest(
    config: GateConfig,
    *,
    gate_started_epoch: float,
) -> Dict[str, object]:
    """加载并验证当前运行的能力清单。"""

    if config.build or not config.capability_manifest.exists():
        generate_capability_manifest(config)
    try:
        manifest = json.loads(config.capability_manifest.read_text(encoding="utf-8"))
    except Exception as exc:
        raise RuntimeError(f"failed to read capability manifest: {exc}") from exc
    errors = validate_manifest(
        manifest,
        expected_identity=current_capability_identity(config),
        gate_started_epoch=gate_started_epoch,
        now_epoch=time.time(),
    )
    if errors:
        raise RuntimeError("capability manifest validation failed:\n" + "\n".join(errors))
    return manifest


def evaluate_http3_preflight(
    config: GateConfig,
    gate_env: Dict[str, str],
    *,
    require_http3_enabled: bool,
) -> Dict[str, object]:
    """探测本轮 HTTP/3 执行环境，不修改 capability manifest。"""

    return evaluate_http3_preflight_result(
        config,
        gate_env,
        require_http3_enabled=require_http3_enabled,
        run_command=run_command,
    )


def plan_files(
    config: GateConfig,
    capability_manifest: Dict[str, object],
    *,
    planner_overrides: Dict[str, object] | None = None,
) -> tuple[List[str], Dict[str, str]]:
    return plan_pytest_files(
        config,
        capability_manifest,
        planner_overrides=planner_overrides,
    )


def check_required_inputs(
    config: GateConfig,
    gate_env: Dict[str, str],
    planned_pytest_files: List[str],
    report: Dict[str, object],
) -> None:
    preflight_required_inputs(config, gate_env, planned_pytest_files, report)


def candidate_files(config: GateConfig) -> List[str]:
    return pytest_files(config)


def existing_path(candidates: List[Path]) -> Optional[Path]:
    return first_existing_path(candidates)
