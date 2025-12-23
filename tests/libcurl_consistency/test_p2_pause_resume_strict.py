"""
P2：pause/resume 强判据一致性（LC-15b：语义合同测试）。

判定核心（可观测数据层面，且以“语义合同”定义 PauseEffective 边界）：
- 最终下载文件字节一致（hash/len）
- 结构化事件边界一致（start/pause_req/pause_effective/resume_req/finished）
- 合同条款（强判据）：
  - PauseEffective 之后到 ResumeReq 之前：bytes_delivered_total / bytes_written_total 严格不变（Δ=0）
  - 允许 PauseReq → PauseEffective 之间存在一次性“收尾交付/写盘”（通过 PauseEffective 的事件边界吸收）

baseline：repo 内置 `qcurl_lc_pause_resume_baseline`（libcurl easy+multi，结构化 events JSON）
QCurl：`tst_LibcurlConsistency` case `p2_pause_resume_strict`（输出同构 events JSON）
"""

from __future__ import annotations

import json
import os
import uuid
from pathlib import Path

import pytest

from tests.libcurl_consistency.pytest_support.artifacts import artifacts_root, ensure_case_dir, write_json
from tests.libcurl_consistency.pytest_support.baseline import run_libtest_case
from tests.libcurl_consistency.pytest_support.compare import assert_artifacts_match
from tests.libcurl_consistency.pytest_support.observed import httpd_observed_for_id
from tests.libcurl_consistency.pytest_support.qcurl_runner import run_qt_test
from tests.libcurl_consistency.pytest_support.service_logs import collect_service_logs_for_case, should_collect_service_logs


def _load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def test_p2_pause_resume_strict_h2(env, lc_logs, tmp_path):
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("QCURL_QTTEST 未设置或可执行不存在")

    collect_logs = should_collect_service_logs()
    suite = "p2_pause_resume_strict"
    proto = "h2"
    case_id = "p2_pause_resume_strict"
    case_variant = "p2_pause_resume_strict_h2"

    pause_offset = 100 * 1023
    resume_delay_ms = 200
    docname = "data-1m"

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_{case_id}"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    baseline_url = f"https://localhost:{int(env.https_port)}/{docname}?id={baseline_req_id}"
    qcurl_url = f"https://localhost:{int(env.https_port)}/{docname}?id={qcurl_req_id}"
    resp_meta = {"status": 200, "http_version": proto, "headers": {}, "body": None}

    case_dir = ensure_case_dir(artifacts_root(env), suite=suite, case=case_variant)
    baseline_events = case_dir / "baseline_pause_resume_events.json"

    try:
        baseline = run_libtest_case(
            env=env,
            suite=suite,
            case=case_variant,
            client_name="cli_lc_pause_resume",
            args=[
                "-V",
                proto,
                "--pause-offset",
                str(pause_offset),
                "--resume-delay-ms",
                str(resume_delay_ms),
                "--events-out",
                str(baseline_events),
                baseline_url,
            ],
            request_meta={"method": "GET", "url": baseline_url, "headers": {}, "body": b""},
            response_meta=resp_meta,
            download_count=1,
        )

        access_log = Path(lc_logs["httpd_access_log"])
        obs = httpd_observed_for_id(access_log, baseline_req_id, require_range=False)
        assert obs.http_version == proto
        baseline["payload"]["request"]["method"] = obs.method
        baseline["payload"]["request"]["url"] = obs.url
        baseline["payload"]["request"]["headers"] = obs.headers
        baseline["payload"]["response"]["status"] = obs.status
        baseline["payload"]["response"]["http_version"] = obs.http_version
        baseline["payload"]["pause_resume_strict"] = _load_json(baseline_events)
        write_json(baseline["path"], baseline["payload"])

        qcurl = run_qt_test(
            env=env,
            suite=suite,
            case=case_variant,
            qt_executable=qt_path,
            args=[],
            request_meta={"method": "GET", "url": qcurl_url, "headers": {}, "body": b""},
            response_meta=resp_meta,
            download_count=1,
            case_env={
                "QCURL_LC_CASE_ID": case_id,
                "QCURL_LC_PROTO": proto,
                "QCURL_LC_HTTPS_PORT": str(int(env.https_port)),
                "QCURL_LC_DOCNAME": docname,
                "QCURL_LC_REQ_ID": qcurl_req_id,
                "QCURL_LC_PAUSE_OFFSET": str(pause_offset),
                "QCURL_LC_RESUME_DELAY_MS": str(resume_delay_ms),
            },
        )

        obs = httpd_observed_for_id(access_log, qcurl_req_id, require_range=False)
        assert obs.http_version == proto
        qcurl["payload"]["request"]["method"] = obs.method
        qcurl["payload"]["request"]["url"] = obs.url
        qcurl["payload"]["request"]["headers"] = obs.headers
        qcurl["payload"]["response"]["status"] = obs.status
        qcurl["payload"]["response"]["http_version"] = obs.http_version

        qcurl_events_path = qcurl["path"].parent / "qcurl_run" / "pause_resume_events.json"
        qcurl["payload"]["pause_resume_strict"] = _load_json(qcurl_events_path)
        write_json(qcurl["path"], qcurl["payload"])

        assert_artifacts_match(baseline["path"], qcurl["path"])
    except Exception:
        if collect_logs:
            collect_service_logs_for_case(
                env,
                suite=suite,
                case=case_variant,
                logs=lc_logs,
                meta={
                    "case_id": case_id,
                    "proto": proto,
                    "docname": docname,
                    "pause_offset": pause_offset,
                    "resume_delay_ms": resume_delay_ms,
                    "baseline_req_id": baseline_req_id,
                    "qcurl_req_id": qcurl_req_id,
                },
            )
        raise

