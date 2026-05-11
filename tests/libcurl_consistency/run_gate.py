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
import json
import os
import shlex
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tests.libcurl_consistency.pytest_support.gate_evidence import add_python_lock_summary
from tests.libcurl_consistency.pytest_support.gate_evidence import write_nghttpx_version_snapshot
from tests.libcurl_consistency.pytest_support.gate_evidence import write_python_env_snapshot
from tests.libcurl_consistency.pytest_support.gate_planner import plan_pytest_files
from tests.libcurl_consistency.pytest_support.gate_planner import pytest_files
from tests.libcurl_consistency.pytest_support.gate_preflight import apply_http3_preflight_to_manifest
from tests.libcurl_consistency.pytest_support.gate_preflight import first_existing_path
from tests.libcurl_consistency.pytest_support.gate_preflight import forbid_local_httpbin
from tests.libcurl_consistency.pytest_support.gate_preflight import preflight_required_inputs
from tests.libcurl_consistency.pytest_support.gate_report import artifacts_dir
from tests.libcurl_consistency.pytest_support.gate_report import create_initial_report
from tests.libcurl_consistency.pytest_support.gate_report import parse_junit_counts
from tests.libcurl_consistency.pytest_support.gate_report import policy_violations_from_report
from tests.libcurl_consistency.pytest_support.gate_report import postflight_artifacts_schema_check
from tests.libcurl_consistency.pytest_support.gate_report import postflight_redaction_scan
from tests.libcurl_consistency.pytest_support.gate_report import redact_text
from tests.libcurl_consistency.pytest_support.gate_report import redaction_scan_roots


_FORBIDDEN_LOCAL_HTTPBIN_ENDPOINTS = (
    "localhost:8935",
    "127.0.0.1:8935",
)

_EXPECTED_ARTIFACTS_SCHEMA = "qcurl-lc/artifacts@v1"


@dataclass(frozen=True)
class GateConfig:
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


def _print_cmd(cmd: List[str]) -> None:
    sys.stderr.write("+ " + " ".join(shlex.quote(x) for x in cmd) + "\n")


def _run(
    cmd: List[str],
    *,
    cwd: Optional[Path] = None,
    env: Optional[Dict[str, str]] = None,
    capture: bool = False,
) -> subprocess.CompletedProcess:
    _print_cmd(cmd)
    return subprocess.run(
        cmd,
        cwd=str(cwd) if cwd else None,
        env=env,
        check=False,
        text=True,
        capture_output=capture,
    )


def _parse_junit_counts(junit_xml: Path) -> Dict[str, object]:
    return parse_junit_counts(junit_xml)


def _ensure_parent(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)

def _redact_text(text: str) -> str:
    return redact_text(text)

def _redaction_scan_roots(cfg: GateConfig) -> List[Path]:
    return redaction_scan_roots(cfg.repo_root, cfg.json_report.parent)

def _artifacts_dir(cfg: GateConfig) -> Path:
    return artifacts_dir(cfg.repo_root)

def _postflight_artifacts_schema_check(cfg: GateConfig, *, since_ts: float) -> Dict[str, object]:
    return postflight_artifacts_schema_check(
        repo_root=cfg.repo_root,
        artifacts_dir=_artifacts_dir(cfg),
        since_ts=since_ts,
        expected_schema=_EXPECTED_ARTIFACTS_SCHEMA,
    )


def _postflight_redaction_scan(cfg: GateConfig, *, since_ts: float) -> Dict[str, object]:
    return postflight_redaction_scan(cfg.repo_root, _redaction_scan_roots(cfg), since_ts=since_ts)


def _preflight_forbid_local_httpbin(cfg: GateConfig) -> List[Dict[str, object]]:
    return forbid_local_httpbin(
        cfg,
        forbidden_endpoints=_FORBIDDEN_LOCAL_HTTPBIN_ENDPOINTS,
        current_file=Path(__file__),
    )


def _default_reports_dir(qcurl_build_dir: Path) -> Path:
    return qcurl_build_dir / "libcurl_consistency" / "reports"


def _default_capability_manifest(qcurl_build_dir: Path) -> Path:
    return _default_reports_dir(qcurl_build_dir) / "capabilities.json"


def _detect_repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def _resolve_config(args: argparse.Namespace) -> GateConfig:
    repo_root = _detect_repo_root()
    qcurl_build_dir = (repo_root / args.qcurl_build).resolve()
    curl_build_dir = (repo_root / args.curl_build).resolve() if args.curl_build else (qcurl_build_dir / "curl").resolve()

    reports_dir = (repo_root / args.reports_dir).resolve() if args.reports_dir else _default_reports_dir(qcurl_build_dir)
    capability_manifest = _default_capability_manifest(qcurl_build_dir)
    junit_xml = reports_dir / f"junit_{args.suite}.xml"
    json_report = reports_dir / f"gate_{args.suite}.json"
    return GateConfig(
        repo_root=repo_root,
        qcurl_build_dir=qcurl_build_dir,
        curl_build_dir=curl_build_dir,
        capability_manifest=capability_manifest,
        suite=args.suite,
        build=bool(args.build),
        with_ext=bool(args.with_ext),
        junit_xml=junit_xml,
        json_report=json_report,
        qt_timeout_s=float(args.qt_timeout_s),
    )


def _build_targets(cfg: GateConfig) -> None:
    if not cfg.qcurl_build_dir.exists():
        raise RuntimeError(f"QCurl build dir 不存在: {cfg.qcurl_build_dir}")
    if not cfg.curl_build_dir.exists():
        raise RuntimeError(
            "curl build dir 不存在（需要将 curl 构建到 <qcurl_build>/curl，例如默认 build/curl）。\n"
            f"- 当前 qcurl_build_dir: {cfg.qcurl_build_dir}\n"
            f"- 当前 curl_build_dir: {cfg.curl_build_dir}\n"
            "请先配置：cmake -B <qcurl_build> -DQCURL_BUILD_LIBCURL_CONSISTENCY=ON"
        )

    jobs = str(os.cpu_count() or 4)
    rc = _run(["cmake", "--build", str(cfg.qcurl_build_dir), "--target", "qcurl_lc_deps", "-j", jobs])
    if rc.returncode != 0:
        raise RuntimeError(
            "build qcurl_lc_deps failed. "
            "Please check the build log above for details."
        )

    # best-effort: 构建 h3-capable nghttpx（依赖不足时允许失败 -> h3 变体将 skip）
    rc = _run(["cmake", "--build", str(cfg.qcurl_build_dir), "--target", "qcurl_nghttpx_h3", "-j", jobs])
    if rc.returncode != 0:
        sys.stderr.write("[warn] build qcurl_nghttpx_h3 failed (h3 variants may be skipped)\n")


def _capability_probe_bin(cfg: GateConfig) -> Path:
    candidates = [
        cfg.qcurl_build_dir / "tests" / "qcurl_lc_capability_probe",
        cfg.qcurl_build_dir / "tests" / "qcurl_lc_capability_probe.exe",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return candidates[0]


def _generate_capability_manifest(cfg: GateConfig) -> None:
    probe_bin = _capability_probe_bin(cfg)
    if not probe_bin.exists():
        raise RuntimeError(f"capability probe binary not found: {probe_bin}")
    _ensure_parent(cfg.capability_manifest)
    rc = _run(
        [str(probe_bin), "--output", str(cfg.capability_manifest)],
        cwd=cfg.repo_root,
        env=_gate_env(cfg),
        capture=True,
    )
    if rc.stdout:
        sys.stdout.write(_redact_text(rc.stdout))
    if rc.stderr:
        sys.stderr.write(_redact_text(rc.stderr))
    if rc.returncode != 0:
        raise RuntimeError(f"capability probe failed: rc={rc.returncode}")


def _load_capability_manifest(cfg: GateConfig) -> Dict[str, object]:
    if cfg.build or not cfg.capability_manifest.exists():
        _generate_capability_manifest(cfg)
    try:
        return json.loads(cfg.capability_manifest.read_text(encoding="utf-8"))
    except Exception as exc:
        raise RuntimeError(f"failed to read capability manifest: {exc}") from exc


def _first_existing_path(candidates: List[Path]) -> Optional[Path]:
    return first_existing_path(candidates)


def _pytest_files(cfg: GateConfig) -> List[str]:
    return pytest_files(cfg)


def _plan_pytest_files(cfg: GateConfig,
                       capability_manifest: Dict[str, object]) -> tuple[List[str], Dict[str, str]]:
    return plan_pytest_files(cfg, capability_manifest)


def _preflight_required_inputs(
    cfg: GateConfig,
    gate_env: Dict[str, str],
    planned_pytest_files: List[str],
    report: Dict[str, object],
) -> None:
    preflight_required_inputs(cfg, gate_env, planned_pytest_files, report)


def _gate_env(cfg: GateConfig) -> Dict[str, str]:
    env = os.environ.copy()
    qt_bin = cfg.qcurl_build_dir / "tests" / "tst_LibcurlConsistency"
    env["QCURL_QTTEST"] = str(qt_bin)
    env["CURL_BUILD_DIR"] = str(cfg.curl_build_dir)
    env["CURL"] = str(cfg.curl_build_dir / "src" / "curl")
    env["CURLINFO"] = str(cfg.curl_build_dir / "src" / "curlinfo")
    env["QCURL_LC_CAPABILITY_MANIFEST"] = str(cfg.capability_manifest)
    env.setdefault("QCURL_LC_COLLECT_LOGS", "1")
    env.setdefault("QCURL_LC_QTTEST_TIMEOUT", str(cfg.qt_timeout_s))
    if cfg.suite in ("p2", "all"):
        env.setdefault("QCURL_LC_EXPECT100_REPEAT", "2")
    if cfg.with_ext:
        env["QCURL_LC_EXT"] = "1"
    return env


def _apply_http3_preflight_to_manifest(
    cfg: GateConfig,
    gate_env: Dict[str, str],
    capability_manifest: Dict[str, object],
    report: Dict[str, object],
    *,
    require_http3_enabled: bool,
) -> Dict[str, object]:
    return apply_http3_preflight_to_manifest(
        cfg,
        gate_env,
        capability_manifest,
        report,
        require_http3_enabled=require_http3_enabled,
        run_command=_run,
    )


def main(argv: List[str]) -> int:
    parser = argparse.ArgumentParser(description="QCurl ↔ libcurl 一致性 Gate（P0 优先）")
    parser.add_argument("--suite", choices=["p0", "p1", "p2", "all"], default="p0", help="选择要跑的套件（默认 p0）")
    parser.add_argument("--with-ext", action="store_true", help="同时运行 ext suite（需要 QCURL_LC_EXT=1）")
    parser.add_argument("--build", action="store_true", help="先构建一致性门禁所需依赖（qcurl_lc_deps；含 curl testdeps/libtests；nghttpx-h3 best-effort）")
    parser.add_argument("--qcurl-build", default="build", help="QCurl CMake build 目录（默认 build）")
    parser.add_argument("--curl-build", default="", help="curl build 目录（默认 <qcurl_build>/curl，即 build/curl）")
    parser.add_argument("--reports-dir", default="", help="报告输出目录（默认 <qcurl_build>/libcurl_consistency/reports）")
    parser.add_argument("--qt-timeout-s", default="90", help="Qt Test 运行超时秒数（默认 90）")
    args = parser.parse_args(argv)

    cfg = _resolve_config(args)
    _ensure_parent(cfg.junit_xml)
    _ensure_parent(cfg.json_report)

    started = time.time()
    require_http3_raw = (os.environ.get("QCURL_REQUIRE_HTTP3") or "").strip()
    require_http3_enabled = require_http3_raw.lower() in ("1", "true", "yes", "on")
    gate_env = _gate_env(cfg)
    report = create_initial_report(
        cfg,
        gate_env,
        require_http3_raw=require_http3_raw,
        require_http3_enabled=require_http3_enabled,
    )

    add_python_lock_summary(report, cfg.repo_root)

    try:
        violations = _preflight_forbid_local_httpbin(cfg)
        report["preflight_forbid_local_httpbin_8935"] = {
            "forbidden_endpoints": list(_FORBIDDEN_LOCAL_HTTPBIN_ENDPOINTS),
            "violations": violations,
        }
        if violations:
            lines = [
                "preflight failed: forbidden local httpbin dependency detected (localhost:8935).",
                "consistency gate must not depend on httpbin; use curl testenv + http_observe_server.py instead.",
                "violations:",
            ]
            for v in violations:
                lines.append(f" - {v.get('file')}: {v.get('hits')}")
            raise RuntimeError("\n".join(lines))

        if cfg.build:
            _build_targets(cfg)

        capability_manifest = _load_capability_manifest(cfg)
        capability_manifest = _apply_http3_preflight_to_manifest(
            cfg,
            gate_env,
            capability_manifest,
            report,
            require_http3_enabled=require_http3_enabled,
        )
        planned_pytest_files, planner_exclusions = _plan_pytest_files(cfg, capability_manifest)
        report["capability_manifest"] = capability_manifest
        report["planner_exclusions"] = planner_exclusions
        report["pytest_files"] = planned_pytest_files
        if not planned_pytest_files:
            raise RuntimeError("capability planner excluded every pytest file; gate would have no executable cases")
        _preflight_required_inputs(cfg, gate_env, planned_pytest_files, report)

        write_python_env_snapshot(cfg, gate_env, report, run_command=_run)
        write_nghttpx_version_snapshot(cfg, gate_env, report, run_command=_run)

        pytest_cmd = [
            "pytest",
            "-q",
            "--maxfail=1",
            "--junitxml",
            str(cfg.junit_xml),
            *planned_pytest_files,
        ]
        report["commands"].append(pytest_cmd)
        rc = _run(pytest_cmd, cwd=cfg.repo_root, env=gate_env, capture=True)
        report["pytest_returncode"] = rc.returncode
        redacted_stdout = _redact_text(rc.stdout)
        redacted_stderr = _redact_text(rc.stderr)
        report["pytest_stdout"] = redacted_stdout
        report["pytest_stderr"] = redacted_stderr
        if redacted_stdout:
            sys.stdout.write(redacted_stdout)
        if redacted_stderr:
            sys.stderr.write(redacted_stderr)
    except Exception as exc:
        report["exception"] = _redact_text(str(exc))
        report["pytest_returncode"] = 2
    finally:
        report["duration_s"] = round(time.time() - started, 3)
        report["junit_counts"] = _parse_junit_counts(cfg.junit_xml)
        # 先落盘一次 report（stdout/stderr 已脱敏），使 redaction scan 也能覆盖 gate_*.json 本身。
        cfg.json_report.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

        report["postflight_artifacts_schema_check"] = _postflight_artifacts_schema_check(cfg, since_ts=started)
        report["postflight_redaction_scan"] = _postflight_redaction_scan(cfg, since_ts=started)
        gate_rc = int(report.get("pytest_returncode", 2))
        policy_violations = policy_violations_from_report(report)
        report["policy_violations"] = policy_violations
        if policy_violations:
            gate_rc = 3
        report["gate_returncode"] = gate_rc

        # 写回包含 scan 结果的最终报告。
        cfg.json_report.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

    return int(report.get("gate_returncode", report.get("pytest_returncode", 2)))


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
