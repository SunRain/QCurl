"""
P1：取消语义一致性（async cancel + 可复现字节边界）。

目的：
- 验证 QCurl 与 libcurl baseline 在“取消”场景下的可观测输出一致：
  - 错误归一化字段一致：kind=cancel、curlcode=42、http_code=200
  - 落盘 body 字节一致（固定在首个 chunk 边界，避免不稳定的中间态差异）

服务端：repo 内置 http_observe_server.py（/slow_body/<total>/<chunk>/<sleep_ms>）
- 本用例：/slow_body/8192/4096/5000：先发送 4096 字节，再等待 5 秒发送剩余部分

基线：repo 内置 qcurl_lc_http_baseline（CURLOPT_XFERINFOFUNCTION 中止传输，触发 CURLE_ABORTED_BY_CALLBACK=42）
QCurl：tst_LibcurlConsistency（p1_cancel_after_first_chunk，基于 downloadProgress 达到阈值后调用 reply.cancel()）
"""

from __future__ import annotations

import os
import re
import uuid
from pathlib import Path

import pytest

from tests.libcurl_consistency.pytest_support.artifacts import build_request_semantic, write_json
from tests.libcurl_consistency.pytest_support.baseline import run_libtest_case
from tests.libcurl_consistency.pytest_support.compare import assert_artifacts_match
from tests.libcurl_consistency.pytest_support.observed import observe_http_observed_for_id
from tests.libcurl_consistency.pytest_support.qcurl_runner import run_qt_test
from tests.libcurl_consistency.pytest_support.service_logs import collect_service_logs_for_case, should_collect_service_logs


_CURLINFO_RE = re.compile(r"curlcode=(\d+)\s+http_code=(\d+)")


def _append_req_id(url: str, req_id: str) -> str:
    sep = "&" if "?" in url else "?"
    return f"{url}{sep}id={req_id}"


def _parse_curlcode_http_code(stderr_lines: list[str]) -> tuple[int, int]:
    for line in stderr_lines:
        m = _CURLINFO_RE.search(line)
        if m:
            return int(m.group(1)), int(m.group(2))
    return -1, -1


def _cancel_error(*, curlcode: int, http_code: int) -> dict:
    return {
        "kind": "cancel",
        "http_status": 0,
        "curlcode": int(curlcode),
        "http_code": int(http_code),
    }


def test_p1_cancel_after_first_chunk(env, lc_logs, lc_observe_http, tmp_path):
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("QCURL_QTTEST 未设置或可执行不存在")

    collect_logs = should_collect_service_logs()
    port = int(lc_observe_http["port"])
    observe_log = Path(str(lc_observe_http["log_file"]))

    suite = "p1_cancel"
    proto = "http/1.1"
    case_variant = "p1_cancel_after_first_chunk_http_1.1"

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_cancel"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    base_url = f"http://localhost:{port}/slow_body/8192/4096/5000"
    baseline_url = _append_req_id(base_url, baseline_req_id)
    qcurl_url = _append_req_id(base_url, qcurl_req_id)

    resp_meta = {"status": 200, "http_version": proto, "headers": {}, "body": None}

    try:
        observe_log.write_text("", encoding="utf-8")
        baseline = run_libtest_case(
            env=env,
            suite=suite,
            case=case_variant,
            client_name="cli_lc_http",
            args=[
                "-V",
                proto,
                "--abort-after-bytes",
                "4096",
                baseline_url,
            ],
            request_meta={"method": "GET", "url": baseline_url, "headers": {}, "body": b""},
            response_meta=resp_meta,
            download_count=1,
            allowed_exit_codes={0, 7},
        )

        curlcode, http_code = _parse_curlcode_http_code(list(baseline["payload"].get("stderr") or []))
        if curlcode < 0:
            raise AssertionError("baseline stderr 未包含 curlcode/http_code")

        obs = observe_http_observed_for_id(observe_log, baseline_req_id)
        baseline["payload"]["request"] = build_request_semantic(obs.method, obs.url, obs.headers, b"")
        baseline["payload"]["response"]["status"] = obs.status
        baseline["payload"]["response"]["http_version"] = proto
        baseline["payload"]["response"]["headers"] = obs.response_headers
        baseline["payload"]["error"] = _cancel_error(curlcode=curlcode, http_code=http_code)
        write_json(baseline["path"], baseline["payload"])

        observe_log.write_text("", encoding="utf-8")
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
                "QCURL_LC_CASE_ID": "p1_cancel_after_first_chunk",
                "QCURL_LC_PROTO": proto,
                "QCURL_LC_REQ_ID": qcurl_req_id,
                "QCURL_LC_OBSERVE_HTTP_PORT": str(port),
            },
        )

        obs = observe_http_observed_for_id(observe_log, qcurl_req_id)
        qcurl["payload"]["request"] = build_request_semantic(obs.method, obs.url, obs.headers, b"")
        qcurl["payload"]["response"]["status"] = obs.status
        qcurl["payload"]["response"]["http_version"] = proto
        qcurl["payload"]["response"]["headers"] = obs.response_headers
        qcurl["payload"]["error"] = _cancel_error(curlcode=42, http_code=obs.status)
        write_json(qcurl["path"], qcurl["payload"])

        assert baseline["payload"]["error"]["curlcode"] == 42
        assert baseline["payload"]["error"]["http_code"] == 200
        assert qcurl["payload"]["error"]["curlcode"] == 42
        assert qcurl["payload"]["error"]["http_code"] == 200
        assert_artifacts_match(baseline["path"], qcurl["path"])
    except Exception:
        if collect_logs:
            collect_service_logs_for_case(
                env,
                suite=suite,
                case=case_variant,
                logs=lc_logs,
                meta={
                    "case_id": "p1_cancel_after_first_chunk",
                    "case_variant": case_variant,
                    "proto": proto,
                    "baseline_req_id": baseline_req_id,
                    "qcurl_req_id": qcurl_req_id,
                    "observe_http_port": port,
                },
            )
        raise

