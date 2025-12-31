#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Share handle 性能矩阵（手工/本地）：
- 复用 run_perf_ab_share_handle.py，在“协议 × 并发 × share 组合”维度批量执行并汇总结果。

约束：
- 不纳入一致性 Gate（避免耗时/波动点进入 CI 门禁）
- 仅依赖 curl testenv（本地 httpd / 可选 nghttpx），不访问外网
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional

import subprocess


@dataclass(frozen=True)
class MatrixConfig:
    repo_root: Path
    qcurl_build_dir: Path
    reports_root: Path
    case_id: str
    docname: str
    protos: List[str]
    counts: List[int]
    share_b_list: List[str]
    runs: int
    warmup: int
    timeout_s: float
    threshold_pass: float
    threshold_warn: float
    no_disk: bool


def _resolve_repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def _default_reports_root(qcurl_build_dir: Path) -> Path:
    return qcurl_build_dir / "libcurl_consistency" / "reports"


def _split_csv(text: str) -> List[str]:
    return [t.strip() for t in text.split(",") if t.strip()]


def _split_counts(text: str) -> List[int]:
    out: List[int] = []
    for token in _split_csv(text):
        out.append(int(token))
    return out


def _split_share_list(text: str) -> List[str]:
    # 支持用 ';' 分隔多个组合，组合内仍用 ',' 分隔 token（与 QCURL_LC_SHARE_HANDLE 语义一致）。
    return [t.strip() for t in text.split(";") if t.strip()]


def _sanitize_tag(text: str) -> str:
    normalized = text.strip().lower()
    if not normalized:
        return "empty"
    out = []
    for ch in normalized:
        if ch.isalnum():
            out.append(ch)
        else:
            out.append("_")
    tag = "".join(out).strip("_")
    return tag or "tag"


def _classify_ratio(ratio: Optional[float], *, pass_th: float, warn_th: float) -> str:
    if ratio is None:
        return "unknown"
    if ratio <= pass_th:
        return "pass"
    if ratio <= warn_th:
        return "warn"
    return "fail"


def _load_json(path: Path) -> Dict[str, object]:
    return json.loads(path.read_text(encoding="utf-8"))


def _extract_ratio(report: Dict[str, object]) -> Optional[float]:
    try:
        compare = report["results"]["compare"]
        ratio = compare.get("median_ratio_b_over_a")
        return float(ratio) if ratio is not None else None
    except Exception:
        return None


def _extract_summary(report: Dict[str, object], group: str) -> Dict[str, object]:
    try:
        return dict(report["results"][group]["summary"])
    except Exception:
        return {}


def _run_perf_ab(perf_ab_script: Path,
                 *,
                 repo_root: Path,
                 qcurl_build_dir: Path,
                 reports_dir: Path,
                 case_id: str,
                 proto: str,
                 docname: str,
                 count: int,
                 share_b: str,
                 runs: int,
                 warmup: int,
                 timeout_s: float,
                 threshold_ratio: float,
                 no_disk: bool) -> Dict[str, object]:
    reports_dir.mkdir(parents=True, exist_ok=True)

    cmd = [
        sys.executable,
        str(perf_ab_script),
        "--qcurl-build",
        str(qcurl_build_dir),
        "--reports-dir",
        str(reports_dir),
        "--case-id",
        case_id,
        "--proto",
        proto,
        "--docname",
        docname,
        "--count",
        str(int(count)),
        "--runs",
        str(int(runs)),
        "--warmup",
        str(int(warmup)),
        "--timeout-s",
        str(float(timeout_s)),
        "--threshold-ratio",
        str(float(threshold_ratio)),
        "--share-b",
        share_b,
    ]
    if no_disk:
        cmd.append("--no-disk")

    started = time.perf_counter()
    proc = subprocess.run(
        cmd,
        cwd=str(repo_root),
        capture_output=True,
        text=True,
        check=False,
    )
    elapsed_s = time.perf_counter() - started

    report_path = reports_dir / "perf_ab_share_handle.json"
    report: Optional[Dict[str, object]] = None
    if report_path.exists():
        try:
            report = _load_json(report_path)
        except Exception:
            report = None

    return {
        "cmd": cmd,
        "returncode": int(proc.returncode),
        "elapsed_s": round(float(elapsed_s), 6),
        "report_path": str(report_path),
        "report": report,
    }


def _resolve_config(args: argparse.Namespace) -> MatrixConfig:
    repo_root = _resolve_repo_root()
    qcurl_build_dir = (repo_root / args.qcurl_build).resolve()
    reports_root = (repo_root / args.reports_dir).resolve() if args.reports_dir else _default_reports_root(qcurl_build_dir)

    return MatrixConfig(
        repo_root=repo_root,
        qcurl_build_dir=qcurl_build_dir,
        reports_root=reports_root,
        case_id=str(args.case_id),
        docname=str(args.docname),
        protos=_split_csv(str(args.protos)),
        counts=_split_counts(str(args.counts)),
        share_b_list=_split_share_list(str(args.share_b_list)),
        runs=int(args.runs),
        warmup=int(args.warmup),
        timeout_s=float(args.timeout_s),
        threshold_pass=float(args.threshold_pass),
        threshold_warn=float(args.threshold_warn),
        no_disk=bool(args.no_disk),
    )


def main(argv: List[str]) -> int:
    parser = argparse.ArgumentParser(description="Share handle 性能矩阵（手工）")
    parser.add_argument("--qcurl-build", default="build", help="QCurl CMake build 目录（默认 build）")
    parser.add_argument("--reports-dir", default="", help="报告输出目录（默认 <qcurl_build>/libcurl_consistency/reports）")
    parser.add_argument("--case-id", default="ext_download_parallel_stress", help="Qt Test case id（默认 ext_download_parallel_stress）")
    parser.add_argument("--docname", default="data-10k", help="下载资源名（默认 data-10k）")
    parser.add_argument("--protos", default="http/1.1,h2", help="协议列表（逗号分隔；默认 http/1.1,h2）")
    parser.add_argument("--counts", default="8,32,64", help="并发/请求数列表（逗号分隔；默认 8,32,64）")
    parser.add_argument("--share-b-list", default="off;dns;ssl_session;cookie;dns,cookie,ssl_session",
                        help="B 组 share 组合列表（分号分隔；组合内用逗号；默认 off;dns;ssl_session;cookie;dns,cookie,ssl_session）")
    parser.add_argument("--runs", type=int, default=4, help="每组轮次（默认 4，含预热）")
    parser.add_argument("--warmup", type=int, default=1, help="预热轮次（默认 1）")
    parser.add_argument("--timeout-s", type=float, default=120.0, help="单轮超时秒数（默认 120）")
    parser.add_argument("--no-disk", action="store_true",
                        help="性能矩阵不写入 download_*.data（减少磁盘 I/O 噪声；透传给 perf A-B 脚本）")
    parser.add_argument("--threshold-pass", type=float, default=1.05, help="pass 阈值（默认 1.05）")
    parser.add_argument("--threshold-warn", type=float, default=1.10, help="warn 阈值（默认 1.10）")
    args = parser.parse_args(argv)

    cfg = _resolve_config(args)

    perf_ab_script = cfg.repo_root / "tests" / "libcurl_consistency" / "run_perf_ab_share_handle.py"
    if not perf_ab_script.exists():
        raise RuntimeError(f"perf A-B 脚本不存在：{perf_ab_script}")

    matrix_id = time.strftime("%Y%m%d%H%M%S")
    base_dir = cfg.reports_root / "perf_matrix_share_handle" / matrix_id
    base_dir.mkdir(parents=True, exist_ok=True)

    records: List[Dict[str, object]] = []

    for proto in cfg.protos:
        for count in cfg.counts:
            for share_b in cfg.share_b_list:
                share_tag = _sanitize_tag(share_b)
                proto_tag = _sanitize_tag(proto)
                run_dir = base_dir / f"proto_{proto_tag}" / f"count_{int(count)}" / f"b_{share_tag}"

                run = _run_perf_ab(
                    perf_ab_script,
                    repo_root=cfg.repo_root,
                    qcurl_build_dir=cfg.qcurl_build_dir,
                    reports_dir=run_dir,
                    case_id=cfg.case_id,
                    proto=proto,
                    docname=cfg.docname,
                    count=int(count),
                    share_b=share_b,
                    runs=cfg.runs,
                    warmup=cfg.warmup,
                    timeout_s=cfg.timeout_s,
                    threshold_ratio=cfg.threshold_pass,
                    no_disk=cfg.no_disk,
                )

                report = run.get("report") if isinstance(run.get("report"), dict) else None
                ratio = _extract_ratio(report) if report else None
                summary_a = _extract_summary(report, "A") if report else {}
                summary_b = _extract_summary(report, "B") if report else {}
                verdict = _classify_ratio(ratio, pass_th=cfg.threshold_pass, warn_th=cfg.threshold_warn)

                records.append({
                    "proto": proto,
                    "count": int(count),
                    "share_b": share_b,
                    "verdict": verdict,
                    "median_ratio_b_over_a": ratio,
                    "summary_a": summary_a,
                    "summary_b": summary_b,
                    "returncode": int(run.get("returncode", -1)),
                    "report_path": str(run.get("report_path", "")),
                    "run_dir": str(run_dir),
                })

    report_path = base_dir / "perf_matrix_share_handle.json"
    report: Dict[str, object] = {
        "schema": "qcurl-lc/perf_matrix@v1",
        "generated_at": time.strftime("%Y-%m-%dT%H:%M:%S%z", time.localtime()),
        "inputs": {
            "case_id": cfg.case_id,
            "docname": cfg.docname,
            "protos": cfg.protos,
            "counts": cfg.counts,
            "share_b_list": cfg.share_b_list,
            "runs": cfg.runs,
            "warmup": cfg.warmup,
            "timeout_s": cfg.timeout_s,
            "no_disk": cfg.no_disk,
            "threshold_pass": cfg.threshold_pass,
            "threshold_warn": cfg.threshold_warn,
        },
        "artifacts": {
            "matrix_id": matrix_id,
            "base_dir": str(base_dir),
            "report_path": str(report_path),
        },
        "records": records,
    }

    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
