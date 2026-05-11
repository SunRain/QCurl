"""
P1: request raw header observable consistency.

The server log is the source of truth. Sensitive values are represented only by
their auth scheme, so artifacts never contain credentials.
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
from tests.libcurl_consistency.pytest_support.qcurl_runner import require_qcurl_qttest, run_qt_test
from tests.libcurl_consistency.pytest_support.service_logs import collect_service_logs_for_case, should_collect_service_logs


def _append_req_id(url: str, req_id: str) -> str:
    sep = "&" if "?" in url else "?"
    return f"{url}{sep}id={req_id}"


def _observed_request(obs) -> dict:
    return build_request_semantic(
        obs.method,
        obs.url,
        dict(obs.headers),
        b"",
        raw_lines=obs.headers_raw_lines,
    )


def _assert_raw_request_header_evidence(obs) -> None:
    lines = list(obs.headers_raw_lines)
    assert lines, "request raw header lines missing"
    assert obs.headers_raw_len > 0, "request raw header len missing"
    assert obs.headers_raw_sha256, "request raw header sha256 missing"
    assert lines == [
        "Authorization: Basic <redacted>",
        "X-QCurl-Case: Camel",
        "X-QCurl-One: alpha",
        "X-QCurl-Override: second",
    ]
    assert "X-QCurl-Override: first" not in lines
    assert not any(line == "Authorization: Basic" for line in lines)
    assert not any("Basic dXN" in line or "passwd" in line for line in lines)


def test_p1_request_raw_headers_http_1_1(env, lc_logs, lc_observe_http):
    qt_path = require_qcurl_qttest()

    collect_logs = should_collect_service_logs()
    port = int(lc_observe_http["port"])
    observe_log = Path(str(lc_observe_http["log_file"]))

    suite = "p1_request_headers"
    proto = "http/1.1"
    case_variant = "lc_request_headers_raw_http_1.1"
    case_id = "p1_request_raw_headers"

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_request_headers"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    url = f"http://localhost:{port}/request_headers"
    baseline_url = _append_req_id(url, baseline_req_id)
    qcurl_url = _append_req_id(url, qcurl_req_id)
    resp_meta = {"status": 200, "http_version": proto, "headers": {}, "body": None}
    header_args = [
        "--header",
        "Authorization: Basic",
        "--header",
        "X-QCurl-Case: Camel",
        "--header",
        "X-QCurl-One: alpha",
        "--header",
        "X-QCurl-Override: second",
    ]

    try:
        observe_log.write_text("", encoding="utf-8")
        baseline = run_libtest_case(
            env=env,
            suite=suite,
            case=case_variant,
            client_name="cli_lc_http",
            args=["-V", proto, *header_args, baseline_url],
            request_meta={"method": "GET", "url": baseline_url, "headers": {}, "body": b""},
            response_meta=resp_meta,
            download_count=1,
        )
        obs = observe_http_observed_for_id(observe_log, baseline_req_id)
        assert obs.headers.get("x-qcurl-one") == "alpha"
        assert obs.headers.get("x-qcurl-override") == "second"
        assert obs.headers.get("x-qcurl-case") == "Camel"
        assert obs.headers.get("authorization") == "Basic"
        _assert_raw_request_header_evidence(obs)
        baseline["payload"]["request"] = _observed_request(obs)
        baseline["payload"]["response"]["status"] = obs.status
        baseline["payload"]["response"]["http_version"] = proto
        baseline["payload"]["response"]["headers"] = dict(obs.response_headers)
        write_json(baseline["path"], baseline["payload"])

        observe_log.write_text("", encoding="utf-8")
        qcurl = run_qt_test(
            env=env,
            suite=suite,
            case=case_variant,
            qt_executable=qt_path,
            request_meta={"method": "GET", "url": qcurl_url, "headers": {}, "body": b""},
            response_meta=resp_meta,
            download_count=1,
            case_env={
                "QCURL_LC_CASE_ID": case_id,
                "QCURL_LC_PROTO": proto,
                "QCURL_LC_REQ_ID": qcurl_req_id,
                "QCURL_LC_OBSERVE_HTTP_PORT": str(port),
            },
        )
        obs = observe_http_observed_for_id(observe_log, qcurl_req_id)
        assert obs.headers.get("x-qcurl-one") == "alpha"
        assert obs.headers.get("x-qcurl-override") == "second"
        assert obs.headers.get("x-qcurl-case") == "Camel"
        assert obs.headers.get("authorization") == "Basic"
        _assert_raw_request_header_evidence(obs)
        qcurl["payload"]["request"] = _observed_request(obs)
        qcurl["payload"]["response"]["status"] = obs.status
        qcurl["payload"]["response"]["http_version"] = proto
        qcurl["payload"]["response"]["headers"] = dict(obs.response_headers)
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
                    "baseline_req_id": baseline_req_id,
                    "qcurl_req_id": qcurl_req_id,
                    "observe_http_port": port,
                },
            )
        raise
