#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Share handle 性能 A/B（手工/本地）：
- A：share 关闭（默认行为）
- B：share 开启（默认 dns,cookie,ssl_session；可通过 --share-b 覆盖）

约束：
- 不纳入一致性 Gate（避免耗时/波动点进入 CI 门禁）
- 仅依赖 curl testenv（本地 httpd / 可选 nghttpx），不访问外网
- 产物输出到 build/libcurl_consistency/reports/ 下（默认）
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import statistics
import subprocess
import sys
import time
import uuid
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional


@dataclass(frozen=True)
class PerfConfig:
    repo_root: Path
    qcurl_build_dir: Path
    qt_bin: Path
    reports_dir: Path
    case_id: str
    proto: str
    docname: str
    count: int
    runs: int
    warmup: int
    threshold_ratio: float
    timeout_s: float
    share_b: str
    report_name: str
    no_disk: bool


def _print(msg: str) -> None:
    sys.stderr.write(msg + "\n")


def _ensure_parent(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def _resolve_repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def _default_reports_dir(qcurl_build_dir: Path) -> Path:
    return qcurl_build_dir / "libcurl_consistency" / "reports"


def _setup_testenv_import(repo_root: Path) -> None:
    """
    复用 tests/libcurl_consistency/conftest.py 的关键策略：
    - import testenv 前切 cwd，使 curl/tests/http/testenv 能推导 TOP_PATH=<qcurl_build>/curl（默认 build/curl）
    - 将 curl/tests/http 加入 sys.path
    - 注入 out-of-source 的 <qcurl_build>/curl 路径与 curl 二进制（若用户未显式设置）
    """
    default_curl_build_dir = repo_root / "build" / "curl"
    curl_build_dir = Path(os.environ.get("CURL_BUILD_DIR", str(default_curl_build_dir))).resolve()
    curl_build_src_dir = curl_build_dir / "src"
    import_cwd = curl_build_src_dir if curl_build_src_dir.exists() else repo_root
    os.chdir(str(import_cwd))

    curl_http_dir = repo_root / "curl" / "tests" / "http"
    if str(curl_http_dir) not in sys.path:
        sys.path.insert(0, str(curl_http_dir))

    os.environ.setdefault("CURL_BUILD_DIR", str(default_curl_build_dir))
    os.environ.setdefault("CURL", str(default_curl_build_dir / "src" / "curl"))


def _patch_testenv_paths(repo_root: Path) -> None:
    """
    curl testenv 的 EnvConfig 不支持通过环境变量覆盖 curlinfo 路径，这里按一致性 conftest 的做法 patch 常量。
    """
    default_curl_build_dir = repo_root / "build" / "curl"
    curl_build_dir = Path(os.environ.get("CURL_BUILD_DIR", str(default_curl_build_dir))).resolve()
    curlinfo_bin = curl_build_dir / "src" / "curlinfo"
    if not curlinfo_bin.exists():
        return
    try:
        import testenv.env as testenv_env  # type: ignore

        testenv_env.CURLINFO = str(curlinfo_bin)
    except Exception:
        # 不阻断：如 testenv 尚未导入成功，后续会在启动阶段报错
        return


def _seed_http_docs(env, httpd, *, docname: str) -> None:
    """
    为性能用例准备必要的静态资源（参考 tests/libcurl_consistency/conftest.py:lc_seed_http_docs）。
    """
    indir = httpd.docs_dir
    # 兼容：若 docname 不存在则生成，避免因环境残留/不同规模导致 404。
    if docname.startswith("data-"):
        if docname == "data-10k":
            env.make_data_file(indir=indir, fname=docname, fsize=10 * 1024)
        elif docname == "data-100k":
            env.make_data_file(indir=indir, fname=docname, fsize=100 * 1024)
        elif docname == "data-1m":
            env.make_data_file(indir=indir, fname=docname, fsize=1024 * 1024)
        elif docname == "data-10m":
            env.make_data_file(indir=indir, fname=docname, fsize=10 * 1024 * 1024)
        else:
            # 保守：未知 data-* 文件默认生成 100KiB，避免意外写入过大。
            env.make_data_file(indir=indir, fname=docname, fsize=100 * 1024)
    else:
        # 非 data-*：由调用方自行保证存在（例如 curltest/echo 等动态 handler）
        pass


def _run_once(cfg: PerfConfig,
              *,
              https_port: int,
              ws_port: int,
              share_handle: str,
              out_dir: Path,
              run_timeout_s: float) -> Dict[str, object]:
    out_dir.mkdir(parents=True, exist_ok=True)
    req_id = uuid.uuid4().hex[:12]

    run_env = os.environ.copy()
    run_env.update({
        "QCURL_LC_CASE_ID": cfg.case_id,
        "QCURL_LC_PROTO": cfg.proto,
        "QCURL_LC_HTTPS_PORT": str(int(https_port)),
        "QCURL_LC_WS_PORT": str(int(ws_port)),
        "QCURL_LC_COUNT": str(int(cfg.count)),
        "QCURL_LC_DOCNAME": str(cfg.docname),
        "QCURL_LC_REQ_ID": req_id,
        "QCURL_LC_OUT_DIR": str(out_dir),
        "QCURL_LC_SHARE_HANDLE": str(share_handle),
    })
    if cfg.no_disk:
        run_env["QCURL_LC_PERF_NO_DISK"] = "1"

    started = time.perf_counter()
    proc = subprocess.run(
        [str(cfg.qt_bin)],
        cwd=str(out_dir),
        env=run_env,
        capture_output=True,
        text=True,
        timeout=run_timeout_s,
        check=False,
    )
    elapsed_s = time.perf_counter() - started

    # 仅保留尾部，避免大量日志污染报告；Qt Test 输出不应包含敏感信息。
    def tail(text: str, limit: int = 4000) -> str:
        if not text:
            return ""
        return text[-limit:]

    return {
        "req_id": req_id,
        "share_handle": share_handle,
        "elapsed_s": round(float(elapsed_s), 6),
        "returncode": int(proc.returncode),
        "stdout_tail": tail(proc.stdout),
        "stderr_tail": tail(proc.stderr),
    }


def _summarize(runs: List[Dict[str, object]]) -> Dict[str, object]:
    ok = [r for r in runs if int(r.get("returncode", 1)) == 0]
    elapsed = [float(r["elapsed_s"]) for r in ok if "elapsed_s" in r]
    if not elapsed:
        return {
            "runs": len(runs),
            "ok": len(ok),
            "median_s": None,
            "p95_s": None,
            "min_s": None,
            "max_s": None,
        }
    elapsed_sorted = sorted(elapsed)
    p95_idx = max(0, int(round(0.95 * (len(elapsed_sorted) - 1))))
    return {
        "runs": len(runs),
        "ok": len(ok),
        "median_s": float(statistics.median(elapsed_sorted)),
        "p95_s": float(elapsed_sorted[p95_idx]),
        "min_s": float(elapsed_sorted[0]),
        "max_s": float(elapsed_sorted[-1]),
    }


def _resolve_config(args: argparse.Namespace) -> PerfConfig:
    repo_root = _resolve_repo_root()
    qcurl_build_dir = (repo_root / args.qcurl_build).resolve()
    qt_bin = qcurl_build_dir / "tests" / "tst_LibcurlConsistency"
    reports_dir = (repo_root / args.reports_dir).resolve() if args.reports_dir else _default_reports_dir(qcurl_build_dir)

    if not qt_bin.exists():
        raise RuntimeError(f"Qt Test binary 不存在：{qt_bin}（先构建：cmake --build <build> --target tst_LibcurlConsistency）")

    return PerfConfig(
        repo_root=repo_root,
        qcurl_build_dir=qcurl_build_dir,
        qt_bin=qt_bin,
        reports_dir=reports_dir,
        case_id=str(args.case_id),
        proto=str(args.proto),
        docname=str(args.docname),
        count=int(args.count),
        runs=int(args.runs),
        warmup=int(args.warmup),
        threshold_ratio=float(args.threshold_ratio),
        timeout_s=float(args.timeout_s),
        share_b=str(args.share_b),
        report_name=str(args.report_name),
        no_disk=bool(args.no_disk),
    )


def main(argv: List[str]) -> int:
    parser = argparse.ArgumentParser(description="Share handle 性能 A/B（手工）")
    parser.add_argument("--qcurl-build", default="build", help="QCurl CMake build 目录（默认 build）")
    parser.add_argument("--reports-dir", default="", help="报告输出目录（默认 <qcurl_build>/libcurl_consistency/reports）")
    parser.add_argument("--report-name", default="perf_ab_share_handle.json",
                        help="报告文件名（相对于 reports-dir；默认 perf_ab_share_handle.json）")
    parser.add_argument("--case-id", default="ext_download_parallel_stress", help="Qt Test case id（默认 ext_download_parallel_stress）")
    parser.add_argument("--proto", default="h2", help="协议：http/1.1|h2|h3（默认 h2）")
    parser.add_argument("--docname", default="data-10k", help="下载资源名（默认 data-10k）")
    parser.add_argument("--count", type=int, default=50, help="每轮请求数（默认 50）")
    parser.add_argument("--runs", type=int, default=6, help="总轮次（默认 6，含预热）")
    parser.add_argument("--warmup", type=int, default=1, help="预热轮次（默认 1）")
    parser.add_argument("--threshold-ratio", type=float, default=1.05, help="回退阈值（B/A，中位数；默认 1.05）")
    parser.add_argument("--timeout-s", type=float, default=120.0, help="单轮超时秒数（默认 120）")
    parser.add_argument("--share-b", default="dns,cookie,ssl_session",
                        help="B 组 share handle 配置（默认 dns,cookie,ssl_session；例如 off/dns/ssl_session/cookie/...）")
    parser.add_argument("--no-disk", action="store_true",
                        help="性能测试不写入 download_*.data（减少磁盘 I/O 噪声；仅影响本脚本的测试进程）")
    args = parser.parse_args(argv)

    cfg = _resolve_config(args)
    report_path = cfg.reports_dir / cfg.report_name
    _ensure_parent(report_path)

    started_wall = time.time()

    # 启动本地 httpd（仅本地 loopback；不访问外网）
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

    try:
        _seed_http_docs(env, httpd, docname=cfg.docname)

        # 避免短时间内多次执行导致 runs_dir 冲突（矩阵模式可能连续触发多次）。
        runs_dir = cfg.reports_dir / "perf_ab_share_handle_runs" / f"{time.strftime('%Y%m%d%H%M%S')}_{uuid.uuid4().hex[:6]}"
        runs_dir.mkdir(parents=True, exist_ok=True)

        group_map = {
            "A": "off",
            "B": cfg.share_b,
        }

        for name, share_handle in group_map.items():
            _print(f"[perf] group {name}: share_handle={share_handle}")

        all_results: Dict[str, List[Dict[str, object]]] = {"A": [], "B": []}
        ws_port = int(env.CONFIG.ports.get("ws", 0))

        # 为降低“先跑的组更慢/后跑的组更快”的顺序偏差，按轮次交替 AB / BA。
        for i in range(cfg.runs):
            order = ["A", "B"] if (i % 2 == 0) else ["B", "A"]
            for name in order:
                share_handle = group_map[name]
                group_dir = runs_dir / name
                group_dir.mkdir(parents=True, exist_ok=True)

                out_dir = group_dir / f"run_{i:02d}"
                result = _run_once(
                    cfg,
                    https_port=int(env.https_port),
                    ws_port=ws_port,
                    share_handle=share_handle,
                    out_dir=out_dir,
                    run_timeout_s=float(cfg.timeout_s),
                )
                result["round_i"] = i
                result["order"] = "".join(order)
                all_results[name].append(result)

                # 预热轮次不纳入统计（但仍记录到报告）
                is_warmup = i < cfg.warmup
                tag = "warmup" if is_warmup else "measured"
                _print(f"[perf] {name} run#{i} ({tag}) [{result['order']}]: rc={result['returncode']} elapsed_s={result['elapsed_s']}")

        def measured(name: str) -> List[Dict[str, object]]:
            return all_results[name][cfg.warmup:]

        sum_a = _summarize(measured("A"))
        sum_b = _summarize(measured("B"))

        median_a = sum_a.get("median_s")
        median_b = sum_b.get("median_s")
        ratio = None
        verdict = "unknown"
        if isinstance(median_a, (int, float)) and isinstance(median_b, (int, float)) and float(median_a) > 0.0:
            ratio = float(median_b) / float(median_a)
            verdict = "pass" if ratio <= float(cfg.threshold_ratio) else "fail"

        report: Dict[str, object] = {
            "schema": "qcurl-lc/perf_ab@v1",
            "started_at": time.strftime("%Y-%m-%dT%H:%M:%S%z", time.localtime(started_wall)),
            "duration_s": round(time.time() - started_wall, 3),
            "host": {
                "platform": platform.platform(),
                "python": sys.version.split()[0],
            },
            "inputs": {
                "qt_bin": str(cfg.qt_bin),
                "case_id": cfg.case_id,
                "proto": cfg.proto,
                "docname": cfg.docname,
                "count": cfg.count,
                "runs": cfg.runs,
                "warmup": cfg.warmup,
                "share_b": cfg.share_b,
                "threshold_ratio": cfg.threshold_ratio,
                "timeout_s": cfg.timeout_s,
                "report_name": cfg.report_name,
                "no_disk": cfg.no_disk,
            },
            "env": {
                "https_port": int(env.https_port),
                "ws_port": int(env.CONFIG.ports.get("ws", 0)),
            },
            "artifacts": {
                "runs_dir": str(runs_dir),
                "report_path": str(report_path),
            },
            "results": {
                "A": {
                    "share_handle": "off",
                    "runs": all_results["A"],
                    "summary": sum_a,
                },
                "B": {
                    "share_handle": cfg.share_b,
                    "runs": all_results["B"],
                    "summary": sum_b,
                },
                "compare": {
                    "median_ratio_b_over_a": ratio,
                    "verdict": verdict,
                },
            },
        }

        report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

        if verdict == "fail":
            _print(f"[perf] FAIL: median_ratio={ratio} > threshold={cfg.threshold_ratio}")
            return 3
        if verdict == "pass":
            _print(f"[perf] PASS: median_ratio={ratio} <= threshold={cfg.threshold_ratio}")
            return 0
        _print("[perf] UNKNOWN: insufficient data (all failed?)")
        return 2
    finally:
        httpd.stop()


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
