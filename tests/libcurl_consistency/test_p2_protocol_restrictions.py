"""
P2：协议白名单与重定向协议限制一致性。

验证被禁止协议的直接访问和重定向跟随都显式失败。
"""

from __future__ import annotations

import os
import re
import uuid
from pathlib import Path

import pytest

from tests.libcurl_consistency.pytest_support.artifacts import (
    apply_error_namespaces,
    build_request_semantic,
    write_json,
)
from tests.libcurl_consistency.pytest_support.baseline import run_libtest_case
from tests.libcurl_consistency.pytest_support.compare import assert_artifacts_match
from tests.libcurl_consistency.pytest_support.observed import (
    observe_http_observed_for_id,
    observe_http_observed_list_for_id,
    parse_observe_http_log,
)
from tests.libcurl_consistency.pytest_support.qcurl_runner import run_qt_test
from tests.libcurl_consistency.pytest_support.service_logs import collect_service_logs_for_case, should_collect_service_logs


_CURLINFO_RE = re.compile(r"curlcode=(\d+)\s+http_code=(\d+)")


def _parse_curlcode_http_code(stderr_lines: list[str]) -> tuple[int, int]:
    for line in stderr_lines:
        m = _CURLINFO_RE.search(line)
        if m:
            return int(m.group(1)), int(m.group(2))
    return -1, -1


def _append_req_id(url: str, req_id: str) -> str:
    sep = "&" if "?" in url else "?"
    return f"{url}{sep}id={req_id}"


def test_p2_protocols_block_http_http_1_1(env, lc_logs, lc_observe_http):
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("当前环境未提供 QCURL_QTTEST 可执行文件，跳过该用例")

    collect_logs = should_collect_service_logs()
    port = int(lc_observe_http["port"])
    observe_log = Path(str(lc_observe_http["log_file"]))

    suite = "p2_protocol_restrictions"
    proto = "http/1.1"
    case_variant = "p2_protocols_block_http_http_1.1"

    url = f"http://localhost:{port}/status/200"
    resp_meta = {"status": 0, "http_version": proto, "headers": {}, "body": None}

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
                "--allowed-protocols",
                "https",
                url,
            ],
            request_meta={"method": "GET", "url": url, "headers": {}, "body": b""},
            response_meta=resp_meta,
            download_count=1,
            allowed_exit_codes={0, 7},
        )

        assert not parse_observe_http_log(observe_log), "协议白名单拒绝场景不应产生真实 HTTP 请求（baseline）"
        curlcode, http_code = _parse_curlcode_http_code(list(baseline["payload"].get("stderr") or []))
        assert curlcode == 1, f"baseline curlcode 期望为 1（UNSUPPORTED_PROTOCOL），实际为 {curlcode}"
        assert http_code == 0, f"baseline http_code 期望为 0，实际为 {http_code}"
        apply_error_namespaces(baseline["payload"], kind="protocol", http_status=0, curlcode=curlcode, http_code=http_code)
        write_json(baseline["path"], baseline["payload"])

        observe_log.write_text("", encoding="utf-8")
        qcurl = run_qt_test(
            env=env,
            suite=suite,
            case=case_variant,
            qt_executable=qt_path,
            args=[],
            request_meta={"method": "GET", "url": url, "headers": {}, "body": b""},
            response_meta=resp_meta,
            download_count=1,
            case_env={
                "QCURL_LC_CASE_ID": "p2_protocols_block_http",
                "QCURL_LC_PROTO": proto,
                "QCURL_LC_OBSERVE_HTTP_PORT": str(port),
            },
        )
        assert not parse_observe_http_log(observe_log), "协议白名单拒绝场景不应产生真实 HTTP 请求（qcurl）"
        apply_error_namespaces(qcurl["payload"], kind="protocol", http_status=0, curlcode=1, http_code=0)
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
                    "case_id": "p2_protocols_block_http",
                    "proto": proto,
                    "url": url,
                    "observe_http_port": port,
                },
            )
        raise


def test_p2_redir_protocols_block_http_http_1_1(env, lc_logs, lc_observe_http):
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("当前环境未提供 QCURL_QTTEST 可执行文件，跳过该用例")

    collect_logs = should_collect_service_logs()
    port = int(lc_observe_http["port"])
    observe_log = Path(str(lc_observe_http["log_file"]))

    suite = "p2_protocol_restrictions"
    proto = "http/1.1"
    case_variant = "p2_redir_protocols_block_http_http_1.1"

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_redir_protocols"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    url = f"http://localhost:{port}/redir/1"
    baseline_url = _append_req_id(url, baseline_req_id)
    qcurl_url = _append_req_id(url, qcurl_req_id)

    resp_meta = {"status": 302, "http_version": proto, "headers": {}, "body": None}

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
                "--follow",
                "--max-redirs",
                "3",
                "--allowed-redir-protocols",
                "https",
                baseline_url,
            ],
            request_meta={"method": "GET", "url": baseline_url, "headers": {}, "body": b""},
            response_meta=resp_meta,
            download_count=1,
            allowed_exit_codes={0, 7},
        )

        obs_list = observe_http_observed_list_for_id(observe_log, baseline_req_id, expected_count=1)
        obs = obs_list[0]
        curlcode, http_code = _parse_curlcode_http_code(list(baseline["payload"].get("stderr") or []))
        assert curlcode == 1, f"baseline curlcode 期望为 1（UNSUPPORTED_PROTOCOL），实际为 {curlcode}"
        assert http_code == obs.status, f"baseline http_code 与服务端观测不一致：{http_code} != {obs.status}"

        baseline["payload"]["request"] = build_request_semantic(obs.method, obs.url, dict(obs.headers), b"")
        baseline["payload"]["response"]["status"] = obs.status
        baseline["payload"]["response"]["http_version"] = proto
        baseline["payload"]["response"]["headers"] = obs.response_headers
        apply_error_namespaces(baseline["payload"], kind="protocol", http_status=0, curlcode=curlcode, http_code=http_code)
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
                "QCURL_LC_CASE_ID": "p2_redir_protocols_block_http",
                "QCURL_LC_PROTO": proto,
                "QCURL_LC_REQ_ID": qcurl_req_id,
                "QCURL_LC_OBSERVE_HTTP_PORT": str(port),
            },
        )

        obs = observe_http_observed_for_id(observe_log, qcurl_req_id)
        qcurl["payload"]["request"] = build_request_semantic(obs.method, obs.url, dict(obs.headers), b"")
        qcurl["payload"]["response"]["status"] = obs.status
        qcurl["payload"]["response"]["http_version"] = proto
        qcurl["payload"]["response"]["headers"] = obs.response_headers
        apply_error_namespaces(qcurl["payload"], kind="protocol", http_status=0, curlcode=1, http_code=obs.status)
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
                    "case_id": "p2_redir_protocols_block_http",
                    "proto": proto,
                    "observe_http_port": port,
                    "baseline_req_id": baseline_req_id,
                    "qcurl_req_id": qcurl_req_id,
                },
            )
        raise
