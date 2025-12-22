"""
P1：响应头“字节级/多值头”一致性（LC-26）。

目的：
- 验证 QCurl 与 libcurl baseline 在“原始响应头可观测数据”上的一致性：
  - 至少对齐 header 行集合（包含重复头）
  - 对齐大小写与顺序（在跳过动态头 Date/Server 的前提下）

服务端：repo 内置 http_observe_server.py（/resp_headers）
基线：repo 内置 qcurl_lc_http_baseline（CURLOPT_HEADERFUNCTION 写出 response_headers_0.data）
QCurl：tst_LibcurlConsistency（p1_resp_headers，写出 response_headers_0.data）
"""

from __future__ import annotations

import hashlib
import os
import uuid
from pathlib import Path

import pytest

from tests.libcurl_consistency.pytest_support.artifacts import (
    artifacts_root,
    build_request_semantic,
    ensure_case_dir,
    write_json,
)
from tests.libcurl_consistency.pytest_support.baseline import run_libtest_case
from tests.libcurl_consistency.pytest_support.compare import assert_artifacts_match
from tests.libcurl_consistency.pytest_support.observed import observe_http_observed_for_id
from tests.libcurl_consistency.pytest_support.qcurl_runner import run_qt_test
from tests.libcurl_consistency.pytest_support.service_logs import collect_service_logs_for_case, should_collect_service_logs


def _append_req_id(url: str, req_id: str) -> str:
    sep = "&" if "?" in url else "?"
    return f"{url}{sep}id={req_id}"


def _normalize_raw_header_lines(raw: bytes) -> list[str]:
    # headers 使用 latin-1 解码以“逐字节”保留（避免 utf-8 解码失败影响可比性）
    text = raw.decode("iso-8859-1", errors="replace")
    out: list[str] = []
    for line in text.splitlines():
        line = line.rstrip("\r\n")
        if not line:
            continue
        low = line.lower()
        if low.startswith("date:") or low.startswith("server:"):
            continue
        out.append(line)
    return out


def _raw_header_fields(lines: list[str]) -> dict:
    blob = "\n".join(lines).encode("utf-8")
    return {
        "headers_raw_lines": lines,
        "headers_raw_len": int(len(blob)),
        "headers_raw_sha256": hashlib.sha256(blob).hexdigest(),
    }


def _assert_server_headers_shape(lines: list[str]) -> None:
    # 该断言用于避免“对比器/用例失效却仍然通过”的情况（例如：意外没采集到 headers）。
    set_cookie = [l for l in lines if l.lower().startswith("set-cookie:")]
    x_dupe = [l for l in lines if l.lower().startswith("x-dupe:")]
    assert len(set_cookie) == 2, f"Set-Cookie 行数异常: {set_cookie}"
    assert len(x_dupe) == 2, f"X-Dupe 行数异常: {x_dupe}"
    assert "X-Case: A" in lines, "缺少 X-Case: A"
    assert "x-case: b" in lines, "缺少 x-case: b"


def test_p1_resp_headers_raw(env, lc_logs, lc_observe_http, tmp_path):
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("QCURL_QTTEST 未设置或可执行不存在")

    collect_logs = should_collect_service_logs()
    port = int(lc_observe_http["port"])
    observe_log = Path(str(lc_observe_http["log_file"]))

    suite = "p1_resp_headers"
    proto = "http/1.1"
    case_variant = "p1_resp_headers_http_1.1"

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_resp_headers"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    base_url = f"http://localhost:{port}/resp_headers"
    baseline_url = _append_req_id(base_url, baseline_req_id)
    qcurl_url = _append_req_id(base_url, qcurl_req_id)

    case_dir = ensure_case_dir(artifacts_root(env), suite=suite, case=case_variant)
    baseline_header_file = case_dir / "baseline_response_headers.data"
    baseline_header_file.write_bytes(b"")

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
                "--header-out",
                str(baseline_header_file),
                baseline_url,
            ],
            request_meta={"method": "GET", "url": baseline_url, "headers": {}, "body": b""},
            response_meta=resp_meta,
            download_count=1,
        )
        obs = observe_http_observed_for_id(observe_log, baseline_req_id)
        baseline["payload"]["request"] = build_request_semantic(obs.method, obs.url, obs.headers, b"")
        baseline["payload"]["response"]["status"] = obs.status
        baseline["payload"]["response"]["http_version"] = proto
        baseline["payload"]["response"]["headers"] = obs.response_headers
        raw_lines = _normalize_raw_header_lines(baseline_header_file.read_bytes())
        _assert_server_headers_shape(raw_lines)
        baseline["payload"]["response"].update(_raw_header_fields(raw_lines))
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
                "QCURL_LC_CASE_ID": "p1_resp_headers",
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

        qcurl_header_file = qcurl["path"].parent / "qcurl_run" / "response_headers_0.data"
        raw_lines = _normalize_raw_header_lines(qcurl_header_file.read_bytes())
        _assert_server_headers_shape(raw_lines)
        qcurl["payload"]["response"].update(_raw_header_fields(raw_lines))
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
                    "case_id": "p1_resp_headers",
                    "case_variant": case_variant,
                    "proto": proto,
                    "baseline_req_id": baseline_req_id,
                    "qcurl_req_id": qcurl_req_id,
                    "observe_http_port": port,
                },
            )
        raise
