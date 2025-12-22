"""
P2：固定 HTTP 错误码（404/401/503）一致性（含错误字段归一化）。

目的：
- 验证 QCurl 与 libcurl baseline 在“状态码 + 响应字节”一致的前提下，
  能以统一方式输出“HTTP 错误”的归一化描述（kind/http_status）。

服务端：repo 内置 http_observe_server.py（/status/<code>）
基线：repo 内置 qcurl_lc_http_baseline
QCurl：tst_LibcurlConsistency（p2_fixed_http_error）
"""

from __future__ import annotations

import os
import uuid
from pathlib import Path

import pytest

from tests.libcurl_consistency.pytest_support.artifacts import build_request_semantic, write_json
from tests.libcurl_consistency.pytest_support.baseline import run_libtest_case
from tests.libcurl_consistency.pytest_support.compare import assert_artifacts_match
from tests.libcurl_consistency.pytest_support.observed import observe_http_observed_for_id
from tests.libcurl_consistency.pytest_support.qcurl_runner import run_qt_test
from tests.libcurl_consistency.pytest_support.service_logs import collect_service_logs_for_case, should_collect_service_logs


def _append_req_id(url: str, req_id: str) -> str:
    sep = "&" if "?" in url else "?"
    return f"{url}{sep}id={req_id}"


def _error_from_status(status: int) -> dict:
    if status >= 400:
        return {"kind": "http", "http_status": int(status)}
    return {"kind": "none", "http_status": 0}


@pytest.mark.parametrize("status_code", [404, 401, 503])
def test_p2_fixed_http_errors(status_code: int, env, lc_services, lc_logs, lc_observe_http, tmp_path):
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("QCURL_QTTEST 未设置或可执行不存在")

    collect_logs = should_collect_service_logs()
    port = int(lc_observe_http["port"])
    observe_log = Path(str(lc_observe_http["log_file"]))

    suite = "p2_fixed_http_errors"
    proto = "http/1.1"
    case_variant = f"lc_status_{status_code}_http_1.1"

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_status_{status_code}"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    url = f"http://localhost:{port}/status/{status_code}"
    baseline_url = _append_req_id(url, baseline_req_id)

    req_meta = {"method": "GET", "url": baseline_url, "headers": {}, "body": b""}
    resp_meta = {"status": status_code, "http_version": proto, "headers": {}, "body": None}

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
                baseline_url,
            ],
            request_meta=req_meta,
            response_meta=resp_meta,
            download_count=1,
        )

        obs = observe_http_observed_for_id(observe_log, baseline_req_id)
        baseline["payload"]["request"] = build_request_semantic(obs.method, obs.url, obs.headers, b"")
        baseline["payload"]["response"]["status"] = obs.status
        baseline["payload"]["response"]["http_version"] = proto
        baseline["payload"]["response"]["headers"] = obs.response_headers
        baseline["payload"]["error"] = _error_from_status(obs.status)
        write_json(baseline["path"], baseline["payload"])

        observe_log.write_text("", encoding="utf-8")
        qcurl_url = _append_req_id(url, qcurl_req_id)
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
                "QCURL_LC_CASE_ID": "p2_fixed_http_error",
                "QCURL_LC_PROTO": proto,
                "QCURL_LC_REQ_ID": qcurl_req_id,
                "QCURL_LC_OBSERVE_HTTP_PORT": str(port),
                "QCURL_LC_STATUS_CODE": str(status_code),
            },
        )

        obs = observe_http_observed_for_id(observe_log, qcurl_req_id)
        qcurl["payload"]["request"] = build_request_semantic(obs.method, obs.url, obs.headers, b"")
        qcurl["payload"]["response"]["status"] = obs.status
        qcurl["payload"]["response"]["http_version"] = proto
        qcurl["payload"]["response"]["headers"] = obs.response_headers
        qcurl["payload"]["error"] = _error_from_status(obs.status)
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
                    "case_id": "p2_fixed_http_error",
                    "case_variant": case_variant,
                    "proto": proto,
                    "baseline_req_id": baseline_req_id,
                    "qcurl_req_id": qcurl_req_id,
                    "observe_http_port": port,
                    "status_code": status_code,
                },
            )
        raise
