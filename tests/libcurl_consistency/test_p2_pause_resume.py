"""
P2：pause/resume 弱判据一致性（LC-15a）。

判定核心（可观测数据层面）：
- 最终下载文件字节一致（hash/len）
- pause/resume 事件序列一致（pause -> resume -> finished）

说明：
- 不比较“暂停持续时间”（时序/调度不稳定）
- 不比较“pause window 内是否仍有数据回调/进度事件”：
  - baseline（`cli_hx_download -P`）的 RESUMED 打点发生在 `curl_easy_pause(..., CONT)` 之后，
    在该调用期间可能出现 RECV 日志，从而导致以 stderr 文本窗口推断 pause window 不稳定。
  - 因此该用例只做“事件存在性/顺序 + 终态文件字节一致”的弱判据门禁；更强过程一致性留给 LC-15b。
"""

from __future__ import annotations

import json
import os
import re
import uuid
from pathlib import Path

import pytest

from tests.libcurl_consistency.pytest_support.artifacts import write_json
from tests.libcurl_consistency.pytest_support.baseline import run_libtest_case
from tests.libcurl_consistency.pytest_support.compare import assert_artifacts_match
from tests.libcurl_consistency.pytest_support.observed import httpd_observed_for_id
from tests.libcurl_consistency.pytest_support.qcurl_runner import run_qt_test
from tests.libcurl_consistency.pytest_support.service_logs import collect_service_logs_for_case, should_collect_service_logs


_RECV_RE = re.compile(r"^\[t-(\d+)\]\s+RECV\s+(\d+)\s+bytes,\s+total=(\d+),\s+pause_at=(\d+)")


def _parse_pause_resume(stderr_lines: list[str], *, pause_offset: int) -> dict:
    pause_count = 0
    resume_count = 0
    finished_idx = -1

    paused_total = -1
    last_total = -1
    paused_total_increase_events = 0
    in_pause = False

    for idx, line in enumerate(stderr_lines or []):
        m = _RECV_RE.match(line.strip())
        if m and m.group(1) == "0":
            last_total = int(m.group(3))
            if in_pause and paused_total >= 0 and last_total > paused_total:
                paused_total_increase_events += 1
                paused_total = last_total

        if "[t-0] PAUSE" in line:
            pause_count += 1
            in_pause = True
            # PAUSE 发生在某次 RECV 回调内部；以“上一条 RECV 的 total”为暂停时刻已落盘字节
            if paused_total < 0 and last_total >= 0:
                paused_total = last_total
        if "[t-0] RESUMED" in line:
            resume_count += 1
            in_pause = False
        if "[t-0] FINISHED" in line:
            if finished_idx < 0:
                finished_idx = idx

    event_seq: list[str] = []
    if pause_count > 0:
        event_seq.append("pause")
    if resume_count > 0:
        event_seq.append("resume")
    if finished_idx >= 0:
        event_seq.append("finished")

    return {
        "pause_offset": int(pause_offset),
        "pause_count": int(pause_count),
        "resume_count": int(resume_count),
        # 诊断字段：以 stderr 文本窗口推断的“pause 与 RESUMED 之间 total 增长次数”
        # ⚠️ 不作为一致性判据（见文件头说明）
        "paused_data_events": int(paused_total_increase_events),
        "event_seq": event_seq,
    }


def test_p2_pause_resume_h2(env, lc_logs, tmp_path):
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("QCURL_QTTEST 未设置或可执行不存在")

    collect_logs = should_collect_service_logs()
    suite = "p2_pause_resume"
    proto = "h2"
    case_id = "p2_pause_resume"
    case_variant = "p2_pause_resume_h2"

    pause_offset = 100 * 1023
    docname = "data-1m"

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_{case_id}"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    baseline_url = f"https://localhost:{int(env.https_port)}/{docname}?id={baseline_req_id}"
    qcurl_url = f"https://localhost:{int(env.https_port)}/{docname}?id={qcurl_req_id}"
    resp_meta = {"status": 200, "http_version": proto, "headers": {}, "body": None}

    try:
        baseline = run_libtest_case(
            env=env,
            suite=suite,
            case=case_variant,
            client_name="cli_hx_download",
            args=[
                "-n",
                "1",
                "-P",
                str(pause_offset),
                "-V",
                proto,
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
        baseline["payload"]["pause_resume"] = _parse_pause_resume(
            list(baseline["payload"].get("stderr") or []),
            pause_offset=pause_offset,
        )
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
            },
        )

        obs = httpd_observed_for_id(access_log, qcurl_req_id, require_range=False)
        assert obs.http_version == proto
        qcurl["payload"]["request"]["method"] = obs.method
        qcurl["payload"]["request"]["url"] = obs.url
        qcurl["payload"]["request"]["headers"] = obs.headers
        qcurl["payload"]["response"]["status"] = obs.status
        qcurl["payload"]["response"]["http_version"] = obs.http_version

        pause_resume_path = qcurl["path"].parent / "qcurl_run" / "pause_resume.json"
        qcurl_pause = json.loads(pause_resume_path.read_text(encoding="utf-8"))
        qcurl["payload"]["pause_resume"] = qcurl_pause
        write_json(qcurl["path"], qcurl["payload"])

        assert int(qcurl_pause.get("pause_count") or 0) == 1
        assert int(qcurl_pause.get("resume_count") or 0) == 1
        assert list(qcurl_pause.get("event_seq") or []) == ["pause", "resume", "finished"]

        assert baseline["payload"]["pause_resume"]["pause_count"] == 1
        assert baseline["payload"]["pause_resume"]["resume_count"] == 1
        assert baseline["payload"]["pause_resume"]["event_seq"] == ["pause", "resume", "finished"]

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
                    "baseline_req_id": baseline_req_id,
                    "qcurl_req_id": qcurl_req_id,
                },
            )
        raise
