"""
P1：`RESOLVE` / `CONNECT_TO` 网络路径一致性。

验证本地路由覆盖与 Host 语义在服务端视角下保持一致。
"""

from __future__ import annotations

import os
import uuid
from pathlib import Path

import pytest

from tests.libcurl_consistency.pytest_support.artifacts import build_request_semantic, write_json
from tests.libcurl_consistency.pytest_support.baseline import run_libtest_case
from tests.libcurl_consistency.pytest_support.compare import assert_artifacts_match
from tests.libcurl_consistency.pytest_support.observed import observe_http_observed_for_id, parse_observe_http_log
from tests.libcurl_consistency.pytest_support.qcurl_runner import run_qt_test
from tests.libcurl_consistency.pytest_support.service_logs import collect_service_logs_for_case, should_collect_service_logs


def _append_req_id(url: str, req_id: str) -> str:
    sep = "&" if "?" in url else "?"
    return f"{url}{sep}id={req_id}"


def test_p1_resolve_override_http_1_1(env, lc_logs, lc_observe_http):
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("当前环境未提供 QCURL_QTTEST 可执行文件，跳过该用例")

    collect_logs = should_collect_service_logs()

    port = int(lc_observe_http["port"])
    observe_log = Path(str(lc_observe_http["log_file"]))

    suite = "p1_network_path"
    proto = "http/1.1"
    case_variant = "p1_resolve_override_http_1.1"

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_resolve"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    host = "example.invalid"
    url = f"http://{host}:{port}/status/200"
    baseline_url = _append_req_id(url, baseline_req_id)
    qcurl_url = _append_req_id(url, qcurl_req_id)

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
                "--resolve",
                f"{host}:{port}:127.0.0.1",
                baseline_url,
            ],
            request_meta={"method": "GET", "url": baseline_url, "headers": {}, "body": b""},
            response_meta=resp_meta,
            download_count=1,
        )

        obs = observe_http_observed_for_id(observe_log, baseline_req_id)
        assert str(obs.headers.get("host") or "") == f"{host}:{port}", f"Host 观测异常（baseline）: {obs.headers}"
        baseline["payload"]["request"] = build_request_semantic(obs.method, obs.url, dict(obs.headers), b"")
        baseline["payload"]["response"]["status"] = obs.status
        baseline["payload"]["response"]["http_version"] = proto
        baseline["payload"]["response"]["headers"] = obs.response_headers
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
                "QCURL_LC_CASE_ID": "p1_resolve_override",
                "QCURL_LC_PROTO": proto,
                "QCURL_LC_REQ_ID": qcurl_req_id,
                "QCURL_LC_OBSERVE_HTTP_PORT": str(port),
            },
        )

        obs = observe_http_observed_for_id(observe_log, qcurl_req_id)
        assert str(obs.headers.get("host") or "") == f"{host}:{port}", f"Host 观测异常（qcurl）: {obs.headers}"
        qcurl["payload"]["request"] = build_request_semantic(obs.method, obs.url, dict(obs.headers), b"")
        qcurl["payload"]["response"]["status"] = obs.status
        qcurl["payload"]["response"]["http_version"] = proto
        qcurl["payload"]["response"]["headers"] = obs.response_headers
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
                    "case_id": "p1_resolve_override",
                    "proto": proto,
                    "observe_http_port": port,
                    "baseline_req_id": baseline_req_id,
                    "qcurl_req_id": qcurl_req_id,
                },
            )
        raise


def test_p1_connect_to_http_1_1(env, lc_logs, lc_observe_http_pair):
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("当前环境未提供 QCURL_QTTEST 可执行文件，跳过该用例")

    collect_logs = should_collect_service_logs()

    a = lc_observe_http_pair["a"]
    b = lc_observe_http_pair["b"]
    port_b = int(b["port"])
    log_a = Path(str(a["log_file"]))
    log_b = Path(str(b["log_file"]))

    suite = "p1_network_path"
    proto = "http/1.1"
    case_variant = "p1_connect_to_http_1.1"

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_connect_to"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    host = "example.invalid"
    logical_port = 18080
    url = f"http://{host}:{logical_port}/status/200"
    baseline_url = _append_req_id(url, baseline_req_id)
    qcurl_url = _append_req_id(url, qcurl_req_id)

    resp_meta = {"status": 200, "http_version": proto, "headers": {}, "body": None}

    try:
        log_a.write_text("", encoding="utf-8")
        log_b.write_text("", encoding="utf-8")
        baseline = run_libtest_case(
            env=env,
            suite=suite,
            case=case_variant,
            client_name="cli_lc_http",
            args=[
                "-V",
                proto,
                "--connect-to",
                f"{host}:{logical_port}:127.0.0.1:{port_b}",
                baseline_url,
            ],
            request_meta={"method": "GET", "url": baseline_url, "headers": {}, "body": b""},
            response_meta=resp_meta,
            download_count=1,
        )

        obs = observe_http_observed_for_id(log_b, baseline_req_id)
        assert str(obs.headers.get("host") or "") == f"{host}:{logical_port}", f"Host 观测异常（baseline）: {obs.headers}"
        assert not parse_observe_http_log(log_a), "connect-to 误命中 observe_http_pair.a（baseline）"
        baseline["payload"]["request"] = build_request_semantic(obs.method, obs.url, dict(obs.headers), b"")
        baseline["payload"]["response"]["status"] = obs.status
        baseline["payload"]["response"]["http_version"] = proto
        baseline["payload"]["response"]["headers"] = obs.response_headers
        write_json(baseline["path"], baseline["payload"])

        log_a.write_text("", encoding="utf-8")
        log_b.write_text("", encoding="utf-8")
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
                "QCURL_LC_CASE_ID": "p1_connect_to",
                "QCURL_LC_PROTO": proto,
                "QCURL_LC_REQ_ID": qcurl_req_id,
                "QCURL_LC_OBSERVE_HTTP_PORT": str(port_b),
            },
        )

        obs = observe_http_observed_for_id(log_b, qcurl_req_id)
        assert str(obs.headers.get("host") or "") == f"{host}:{logical_port}", f"Host 观测异常（qcurl）: {obs.headers}"
        assert not parse_observe_http_log(log_a), "connect-to 误命中 observe_http_pair.a（qcurl）"
        qcurl["payload"]["request"] = build_request_semantic(obs.method, obs.url, dict(obs.headers), b"")
        qcurl["payload"]["response"]["status"] = obs.status
        qcurl["payload"]["response"]["http_version"] = proto
        qcurl["payload"]["response"]["headers"] = obs.response_headers
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
                    "case_id": "p1_connect_to",
                    "proto": proto,
                    "observe_http_port": port_b,
                    "baseline_req_id": baseline_req_id,
                    "qcurl_req_id": qcurl_req_id,
                },
            )
        raise
