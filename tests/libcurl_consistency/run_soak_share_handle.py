#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Share handle 长稳压测（手工/本地）：
- 通过 Qt Test case `lc_soak_parallel_get` 执行持续并发 GET
- 进程外采样 RSS / FD（Linux 优先；非 Linux 将标记为 unsupported）
- 产物输出到 build/libcurl_consistency/reports/ 下（默认）
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import signal
import subprocess
import sys
import time
import uuid
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Tuple


@dataclass(frozen=True)
class SoakConfig:
    repo_root: Path
    qcurl_build_dir: Path
    qt_bin: Path
    reports_dir: Path
    proto: str
    docname: str
    duration_s: int
    parallel: int
    max_errors: int
    sample_interval_s: float
    share_handle: str


def _print(msg: str) -> None:
    sys.stderr.write(msg + "\n")


def _ensure_dir(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


def _resolve_repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def _default_reports_dir(qcurl_build_dir: Path) -> Path:
    return qcurl_build_dir / "libcurl_consistency" / "reports"


def _setup_testenv_import(repo_root: Path) -> None:
    curl_build_src_dir = repo_root / "curl" / "build" / "src"
    import_cwd = curl_build_src_dir if curl_build_src_dir.exists() else repo_root
    os.chdir(str(import_cwd))

    curl_http_dir = repo_root / "curl" / "tests" / "http"
    if str(curl_http_dir) not in sys.path:
        sys.path.insert(0, str(curl_http_dir))

    os.environ.setdefault("CURL_BUILD_DIR", str(repo_root / "curl" / "build"))
    os.environ.setdefault("CURL", str(repo_root / "curl" / "build" / "src" / "curl"))


def _patch_testenv_paths(repo_root: Path) -> None:
    curlinfo_bin = repo_root / "curl" / "build" / "src" / "curlinfo"
    if not curlinfo_bin.exists():
        return
    import testenv.env as testenv_env  # type: ignore

    testenv_env.CURLINFO = str(curlinfo_bin)


def _seed_http_docs(env, httpd, *, docname: str) -> None:
    indir = httpd.docs_dir
    if docname == "data-10k":
        env.make_data_file(indir=indir, fname=docname, fsize=10 * 1024)
    elif docname == "data-100k":
        env.make_data_file(indir=indir, fname=docname, fsize=100 * 1024)
    elif docname == "data-1m":
        env.make_data_file(indir=indir, fname=docname, fsize=1024 * 1024)
    elif docname == "data-10m":
        env.make_data_file(indir=indir, fname=docname, fsize=10 * 1024 * 1024)
    else:
        # 保守：未知 docname 不自动生成，避免污染 docs_dir
        pass


def _read_proc_status_kv(pid: int) -> Dict[str, str]:
    status_path = Path("/proc") / str(pid) / "status"
    out: Dict[str, str] = {}
    try:
        text = status_path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return out
    for line in text.splitlines():
        if ":" not in line:
            continue
        k, v = line.split(":", 1)
        out[k.strip()] = v.strip()
    return out


def _read_rss_kb(pid: int) -> Optional[int]:
    kv = _read_proc_status_kv(pid)
    v = kv.get("VmRSS")
    if not v:
        return None
    # e.g. "12345 kB"
    parts = v.split()
    if not parts:
        return None
    try:
        return int(parts[0])
    except ValueError:
        return None


def _read_fd_count(pid: int) -> Optional[int]:
    fd_dir = Path("/proc") / str(pid) / "fd"
    try:
        return len(list(fd_dir.iterdir()))
    except OSError:
        return None


def _collect_proc_metrics(pid: int) -> Dict[str, object]:
    metrics: Dict[str, object] = {"pid": pid}
    if Path("/proc").exists():
        rss_kb = _read_rss_kb(pid)
        fd_count = _read_fd_count(pid)
        if rss_kb is not None:
            metrics["rss_kb"] = int(rss_kb)
        if fd_count is not None:
            metrics["fd_count"] = int(fd_count)
    return metrics


def _resolve_config(args: argparse.Namespace) -> SoakConfig:
    repo_root = _resolve_repo_root()
    qcurl_build_dir = (repo_root / args.qcurl_build).resolve()
    qt_bin = qcurl_build_dir / "tests" / "tst_LibcurlConsistency"
    reports_dir = (repo_root / args.reports_dir).resolve() if args.reports_dir else _default_reports_dir(qcurl_build_dir)

    if not qt_bin.exists():
        raise RuntimeError(f"Qt Test binary 不存在：{qt_bin}（先构建：cmake --build <build> --target tst_LibcurlConsistency）")

    duration_s = int(args.duration_s)
    parallel = int(args.parallel)
    max_errors = int(args.max_errors)
    if duration_s <= 0:
        raise RuntimeError("duration_s must be > 0")
    if parallel <= 0:
        raise RuntimeError("parallel must be > 0")
    if max_errors < 0:
        raise RuntimeError("max_errors must be >= 0")

    return SoakConfig(
        repo_root=repo_root,
        qcurl_build_dir=qcurl_build_dir,
        qt_bin=qt_bin,
        reports_dir=reports_dir,
        proto=str(args.proto),
        docname=str(args.docname),
        duration_s=duration_s,
        parallel=parallel,
        max_errors=max_errors,
        sample_interval_s=float(args.sample_interval_s),
        share_handle=str(args.share_handle),
    )


def main(argv: List[str]) -> int:
    parser = argparse.ArgumentParser(description="Share handle 长稳压测（手工）")
    parser.add_argument("--qcurl-build", default="build", help="QCurl CMake build 目录（默认 build）")
    parser.add_argument("--reports-dir", default="", help="报告输出目录（默认 <qcurl_build>/libcurl_consistency/reports）")
    parser.add_argument("--proto", default="h2", help="协议：http/1.1|h2|h3（默认 h2）")
    parser.add_argument("--docname", default="data-10k", help="下载资源名（默认 data-10k）")
    # 注意：Qt Test 对单个 testfunction 存在约 300s 的超时保护；默认 300s 以避免被框架中止。
    parser.add_argument("--duration-s", type=int, default=300, help="压测时长秒（默认 300）")
    parser.add_argument("--parallel", type=int, default=32, help="并发数（默认 32）")
    parser.add_argument("--max-errors", type=int, default=0, help="允许错误数（默认 0）")
    parser.add_argument("--sample-interval-s", type=float, default=5.0, help="采样间隔秒（默认 5）")
    parser.add_argument("--share-handle", default="dns,cookie,ssl_session", help="share handle 配置（默认全开）")
    args = parser.parse_args(argv)

    cfg = _resolve_config(args)
    report_path = cfg.reports_dir / "soak_share_handle.json"
    _ensure_dir(cfg.reports_dir)

    started_wall = time.time()

    _setup_testenv_import(cfg.repo_root)
    try:
        from testenv import Env, Httpd  # type: ignore
        from testenv.env import EnvConfig  # type: ignore
    finally:
        os.chdir(str(cfg.repo_root))

    _patch_testenv_paths(cfg.repo_root)

    env_cfg = EnvConfig(pytestconfig=None, testrun_uid=uuid.uuid4().hex[:8], worker_id="master")
    env_cfg.build_dir = str(cfg.repo_root / "curl" / "build")
    env = Env(pytestconfig=None, env_config=env_cfg)
    env.setup()

    httpd = Httpd(env=env)
    if not httpd.exists():
        raise RuntimeError(f"httpd 不可用：{env.httpd}")
    httpd.clear_logs()
    if not httpd.initial_start():
        raise RuntimeError("httpd 启动失败（端口分配/权限/依赖问题）")

    run_id = time.strftime("%Y%m%d%H%M%S")
    run_dir = cfg.reports_dir / "soak_share_handle_runs" / run_id
    _ensure_dir(run_dir)

    stdout_path = run_dir / "stdout"
    stderr_path = run_dir / "stderr"

    samples: List[Dict[str, object]] = []
    exit_info: Dict[str, object] = {}
    qt_summary: Dict[str, object] = {}

    try:
        _seed_http_docs(env, httpd, docname=cfg.docname)

        case_env = os.environ.copy()
        case_env.update({
            "QCURL_LC_CASE_ID": "lc_soak_parallel_get",
            "QCURL_LC_PROTO": cfg.proto,
            "QCURL_LC_HTTPS_PORT": str(int(env.https_port)),
            "QCURL_LC_WS_PORT": "0",
            "QCURL_LC_DOCNAME": cfg.docname,
            "QCURL_LC_REQ_ID": uuid.uuid4().hex[:12],
            "QCURL_LC_OUT_DIR": str(run_dir),
            "QCURL_LC_SHARE_HANDLE": cfg.share_handle,
            "QCURL_LC_SOAK_DURATION_S": str(int(cfg.duration_s)),
            "QCURL_LC_SOAK_PARALLEL": str(int(cfg.parallel)),
            "QCURL_LC_SOAK_MAX_ERRORS": str(int(cfg.max_errors)),
        })

        with open(stdout_path, "w", encoding="utf-8") as out, open(stderr_path, "w", encoding="utf-8") as err:
            proc = subprocess.Popen(
                [str(cfg.qt_bin)],
                cwd=str(run_dir),
                env=case_env,
                stdout=out,
                stderr=err,
                text=True,
            )

            start_perf = time.perf_counter()
            deadline = start_perf + float(cfg.duration_s) + 90.0

            while True:
                rc = proc.poll()
                now = time.perf_counter()
                samples.append({
                    "t_s": round(now - start_perf, 3),
                    **_collect_proc_metrics(int(proc.pid)),
                })
                if rc is not None:
                    exit_info = {
                        "returncode": int(rc),
                        "elapsed_s": round(now - start_perf, 3),
                    }
                    break
                if now >= deadline:
                    exit_info = {
                        "returncode": None,
                        "elapsed_s": round(now - start_perf, 3),
                        "timeout": True,
                    }
                    proc.send_signal(signal.SIGTERM)
                    try:
                        proc.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                        proc.wait(timeout=5)
                    break
                time.sleep(max(0.2, float(cfg.sample_interval_s)))

        summary_path = run_dir / "soak_summary.json"
        if summary_path.exists():
            try:
                qt_summary = json.loads(summary_path.read_text(encoding="utf-8", errors="replace"))
            except Exception:
                qt_summary = {"error": "failed to parse soak_summary.json"}

        rss_values = [int(s["rss_kb"]) for s in samples if "rss_kb" in s]
        fd_values = [int(s["fd_count"]) for s in samples if "fd_count" in s]

        report: Dict[str, object] = {
            "schema": "qcurl-lc/soak@v1",
            "started_at": time.strftime("%Y-%m-%dT%H:%M:%S%z", time.localtime(started_wall)),
            "duration_s": round(time.time() - started_wall, 3),
            "host": {
                "platform": platform.platform(),
                "python": sys.version.split()[0],
            },
            "inputs": {
                "qt_bin": str(cfg.qt_bin),
                "proto": cfg.proto,
                "docname": cfg.docname,
                "duration_s": cfg.duration_s,
                "parallel": cfg.parallel,
                "max_errors": cfg.max_errors,
                "sample_interval_s": cfg.sample_interval_s,
                "share_handle": cfg.share_handle,
            },
            "env": {
                "https_port": int(env.https_port),
            },
            "artifacts": {
                "run_dir": str(run_dir),
                "stdout": str(stdout_path),
                "stderr": str(stderr_path),
                "qt_summary": str(summary_path) if summary_path.exists() else "",
                "report_path": str(report_path),
            },
            "exit": exit_info,
            "qt_summary": qt_summary,
            "metrics": {
                "rss_kb_max": max(rss_values) if rss_values else None,
                "rss_kb_min": min(rss_values) if rss_values else None,
                "fd_count_max": max(fd_values) if fd_values else None,
                "fd_count_min": min(fd_values) if fd_values else None,
                "samples": len(samples),
                "sampling_supported": Path("/proc").exists(),
            },
            "samples": samples,
        }

        report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

        if exit_info.get("timeout"):
            _print("[soak] FAIL: timeout")
            return 3
        if int(exit_info.get("returncode", 1)) != 0:
            _print(f"[soak] FAIL: rc={exit_info.get('returncode')}")
            return 3
        _print("[soak] PASS")
        return 0
    finally:
        httpd.stop()


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
