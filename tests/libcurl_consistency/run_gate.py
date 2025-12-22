#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
一致性 Gate 入口（Task1 / LC-16）：
- 统一完成必要构建（可选）与 pytest 执行
- 输出 JUnit XML + JSON 报告
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


@dataclass(frozen=True)
class GateConfig:
    repo_root: Path
    qcurl_build_dir: Path
    curl_build_dir: Path
    suite: str
    build: bool
    with_ext: bool
    junit_xml: Path
    json_report: Path
    qt_timeout_s: float


def _print_cmd(cmd: List[str]) -> None:
    sys.stderr.write("+ " + " ".join(shlex.quote(x) for x in cmd) + "\n")


def _run(cmd: List[str], *, cwd: Optional[Path] = None, env: Optional[Dict[str, str]] = None) -> subprocess.CompletedProcess:
    _print_cmd(cmd)
    return subprocess.run(cmd, cwd=str(cwd) if cwd else None, env=env, check=False, text=True)


def _ensure_parent(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def _default_reports_dir(qcurl_build_dir: Path) -> Path:
    return qcurl_build_dir / "libcurl_consistency" / "reports"


def _detect_repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def _resolve_config(args: argparse.Namespace) -> GateConfig:
    repo_root = _detect_repo_root()
    qcurl_build_dir = (repo_root / args.qcurl_build).resolve()
    curl_build_dir = (repo_root / args.curl_build).resolve()

    reports_dir = (repo_root / args.reports_dir).resolve() if args.reports_dir else _default_reports_dir(qcurl_build_dir)
    junit_xml = reports_dir / f"junit_{args.suite}.xml"
    json_report = reports_dir / f"gate_{args.suite}.json"
    return GateConfig(
        repo_root=repo_root,
        qcurl_build_dir=qcurl_build_dir,
        curl_build_dir=curl_build_dir,
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
        raise RuntimeError(f"curl build dir 不存在: {cfg.curl_build_dir}")

    jobs = str(os.cpu_count() or 4)
    rc = _run(["cmake", "--build", str(cfg.qcurl_build_dir), "--target", "tst_LibcurlConsistency", "-j", jobs])
    if rc.returncode != 0:
        raise RuntimeError(f"build tst_LibcurlConsistency failed:\n{rc.stdout}\n{rc.stderr}")

    rc = _run(["cmake", "--build", str(cfg.qcurl_build_dir), "--target", "qcurl_lc_range_resume_baseline", "-j", jobs])
    if rc.returncode != 0:
        raise RuntimeError(f"build qcurl_lc_range_resume_baseline failed:\n{rc.stdout}\n{rc.stderr}")

    rc = _run(["cmake", "--build", str(cfg.qcurl_build_dir), "--target", "qcurl_lc_postfields_binary_baseline", "-j", jobs])
    if rc.returncode != 0:
        raise RuntimeError(f"build qcurl_lc_postfields_binary_baseline failed:\n{rc.stdout}\n{rc.stderr}")

    rc = _run(["cmake", "--build", str(cfg.qcurl_build_dir), "--target", "qcurl_lc_http_baseline", "-j", jobs])
    if rc.returncode != 0:
        raise RuntimeError(f"build qcurl_lc_http_baseline failed:\n{rc.stdout}\n{rc.stderr}")

    rc = _run(["cmake", "--build", str(cfg.qcurl_build_dir), "--target", "qcurl_lc_pause_resume_baseline", "-j", jobs])
    if rc.returncode != 0:
        raise RuntimeError(f"build qcurl_lc_pause_resume_baseline failed:\n{rc.stdout}\n{rc.stderr}")

    if cfg.with_ext:
        rc = _run(["cmake", "--build", str(cfg.qcurl_build_dir), "--target", "qcurl_lc_ws_baseline", "-j", jobs])
        if rc.returncode != 0:
            raise RuntimeError(f"build qcurl_lc_ws_baseline failed:\n{rc.stdout}\n{rc.stderr}")
        rc = _run(["cmake", "--build", str(cfg.qcurl_build_dir), "--target", "qcurl_lc_multi_get4_baseline", "-j", jobs])
        if rc.returncode != 0:
            raise RuntimeError(f"build qcurl_lc_multi_get4_baseline failed:\n{rc.stdout}\n{rc.stderr}")

    rc = _run(["cmake", "--build", str(cfg.curl_build_dir), "--target", "libtests", "-j", jobs])
    if rc.returncode != 0:
        raise RuntimeError(f"build libtests failed:\n{rc.stdout}\n{rc.stderr}")


def _pytest_files(cfg: GateConfig) -> List[str]:
    base = [
        "tests/libcurl_consistency/test_p0_consistency.py",
    ]
    if cfg.suite in ("p1", "all"):
        base.extend([
            "tests/libcurl_consistency/test_p1_proxy.py",
            "tests/libcurl_consistency/test_p1_redirect_and_login_flow.py",
            "tests/libcurl_consistency/test_p1_empty_body.py",
            "tests/libcurl_consistency/test_p1_resp_headers.py",
            "tests/libcurl_consistency/test_p1_progress.py",
            "tests/libcurl_consistency/test_p1_http_methods.py",
            "tests/libcurl_consistency/test_p1_multipart_formdata.py",
            "tests/libcurl_consistency/test_p1_timeouts.py",
            "tests/libcurl_consistency/test_p1_cancel.py",
            "tests/libcurl_consistency/test_p1_postfields_binary.py",
            "tests/libcurl_consistency/test_p1_cookiejar_1903.py",
        ])
    if cfg.suite == "all":
        base.extend([
            "tests/libcurl_consistency/test_p2_tls_verify.py",
            "tests/libcurl_consistency/test_p2_cookie_request_header.py",
            "tests/libcurl_consistency/test_p2_fixed_http_errors.py",
            "tests/libcurl_consistency/test_p2_error_paths.py",
            "tests/libcurl_consistency/test_p2_pause_resume.py",
            "tests/libcurl_consistency/test_p2_pause_resume_strict.py",
        ])
    if cfg.with_ext:
        base.extend([
            "tests/libcurl_consistency/test_ext_suite.py",
            "tests/libcurl_consistency/test_ext_ws_suite.py",
        ])
    return base


def _gate_env(cfg: GateConfig) -> Dict[str, str]:
    env = os.environ.copy()
    qt_bin = cfg.qcurl_build_dir / "tests" / "tst_LibcurlConsistency"
    env["QCURL_QTTEST"] = str(qt_bin)
    env.setdefault("QCURL_LC_COLLECT_LOGS", "1")
    env.setdefault("QCURL_LC_QTTEST_TIMEOUT", str(cfg.qt_timeout_s))
    if cfg.with_ext:
        env["QCURL_LC_EXT"] = "1"
    return env


def main(argv: List[str]) -> int:
    parser = argparse.ArgumentParser(description="QCurl ↔ libcurl 一致性 Gate（P0 优先）")
    parser.add_argument("--suite", choices=["p0", "p1", "all"], default="p0", help="选择要跑的套件（默认 p0）")
    parser.add_argument("--with-ext", action="store_true", help="同时运行 ext suite（需要 QCURL_LC_EXT=1）")
    parser.add_argument("--build", action="store_true", help="先构建 tst_LibcurlConsistency 与 curl libtests")
    parser.add_argument("--qcurl-build", default="build", help="QCurl CMake build 目录（默认 build）")
    parser.add_argument("--curl-build", default="curl/build", help="curl CMake build 目录（默认 curl/build）")
    parser.add_argument("--reports-dir", default="", help="报告输出目录（默认 <qcurl_build>/libcurl_consistency/reports）")
    parser.add_argument("--qt-timeout-s", default="90", help="Qt Test 运行超时秒数（默认 90）")
    args = parser.parse_args(argv)

    cfg = _resolve_config(args)
    _ensure_parent(cfg.junit_xml)
    _ensure_parent(cfg.json_report)

    started = time.time()
    report: Dict[str, object] = {
        "suite": cfg.suite,
        "with_ext": cfg.with_ext,
        "build": cfg.build,
        "repo_root": str(cfg.repo_root),
        "qcurl_build_dir": str(cfg.qcurl_build_dir),
        "curl_build_dir": str(cfg.curl_build_dir),
        "junit_xml": str(cfg.junit_xml),
        "json_report": str(cfg.json_report),
        "qt_timeout_s": cfg.qt_timeout_s,
        "commands": [],
        "pytest_files": _pytest_files(cfg),
        "env": {
            "QCURL_QTTEST": str(cfg.qcurl_build_dir / "tests" / "tst_LibcurlConsistency"),
            "QCURL_LC_COLLECT_LOGS": "1",
            "QCURL_LC_QTTEST_TIMEOUT": str(cfg.qt_timeout_s),
            "QCURL_LC_EXT": "1" if cfg.with_ext else "0",
        },
    }

    try:
        if cfg.build:
            _build_targets(cfg)

        env = _gate_env(cfg)
        pytest_cmd = [
            "pytest",
            "-q",
            "--maxfail=1",
            "--junitxml",
            str(cfg.junit_xml),
            *_pytest_files(cfg),
        ]
        report["commands"].append(pytest_cmd)
        rc = _run(pytest_cmd, cwd=cfg.repo_root, env=env)
        report["pytest_returncode"] = rc.returncode
        report["pytest_stdout"] = rc.stdout
        report["pytest_stderr"] = rc.stderr
    except Exception as exc:
        report["exception"] = str(exc)
        report["pytest_returncode"] = 2
    finally:
        report["duration_s"] = round(time.time() - started, 3)
        cfg.json_report.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

    return int(report.get("pytest_returncode", 2))


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
