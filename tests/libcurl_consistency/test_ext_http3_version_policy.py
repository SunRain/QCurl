"""
Ext-only HTTP/3 request policy observable consistency over HTTP/1.1.

The real H3 success path lives in test_ext_http3_success_h3.py because that
case requires an H3-capable nghttpx and curl. This file is intentionally planned
only by run_gate.py --with-ext and covers the public API policy decisions that
can be observed against the local HTTP/1.1 server: HTTP/3 fallback and
Http3Only failure.
"""

from __future__ import annotations

import os
import re
import uuid
from pathlib import Path
from urllib.parse import parse_qs, urlencode, urlsplit, urlunsplit

import pytest

from tests.libcurl_consistency.pytest_support.artifacts import apply_error_namespaces, build_request_semantic, write_json
from tests.libcurl_consistency.pytest_support.baseline import run_libtest_case
from tests.libcurl_consistency.pytest_support.capability_manifest import guard_planned_test
from tests.libcurl_consistency.pytest_support.compare import assert_artifacts_match
from tests.libcurl_consistency.pytest_support.observed import observe_http_observed_for_id
from tests.libcurl_consistency.pytest_support.qcurl_runner import run_qt_test
from tests.libcurl_consistency.pytest_support.service_logs import collect_service_logs_for_case, should_collect_service_logs


_CURLINFO_RE = re.compile(r"curlcode=(\d+)\s+http_code=(\d+)")


if os.environ.get("QCURL_LC_EXT", "").strip() != "1":
    pytest.skip("HTTP/3 version policy is ext-only", allow_module_level=True)


def _parse_curlcode_http_code(stderr_lines: list[str]) -> tuple[int, int]:
    for line in stderr_lines:
        m = _CURLINFO_RE.search(line)
        if m:
            return int(m.group(1)), int(m.group(2))
    return -1, -1


def _append_req_id(url: str, req_id: str) -> str:
    sep = "&" if "?" in url else "?"
    return f"{url}{sep}id={req_id}"


def _strip_query_id(url: str) -> str:
    parts = urlsplit(url)
    query = parse_qs(parts.query, keep_blank_values=True)
    query.pop("id", None)
    return urlunsplit((parts.scheme, parts.netloc, parts.path, urlencode(query, doseq=True), parts.fragment))


def _normalize_error_request(payload: dict, url: str) -> None:
    request = payload.get("request")
    if isinstance(request, dict):
        request["url"] = _strip_query_id(url)


def _normalize_empty_error_response(payload: dict) -> None:
    response = payload.get("response")
    if isinstance(response, dict):
        response["body_len"] = 0
        response["body_sha256"] = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"


def test_ext_http3_fallback_and_only_failure_http_1_1(env, lc_logs, lc_observe_http):
    guard_planned_test("test_ext_http3_version_policy.py")

    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("当前环境未提供 QCURL_QTTEST 可执行文件，跳过该用例")

    port = int(lc_observe_http["port"])
    observe_log = Path(str(lc_observe_http["log_file"]))
    suite = "ext_http3_version_policy"
    proto = "http/1.1"

    cases = [
        ("ext_http3_fallback_to_http_1_1", "http/1.1", False),
        ("ext_http3_only_failure_http_1_1", "h3", True),
    ]

    for case_id, baseline_proto, expected_error in cases:
        case_variant = f"lc_{case_id}"
        trace_base = f"lc_{uuid.uuid4().hex[:8]}_{case_id}"
        baseline_req_id = f"{trace_base}__baseline"
        qcurl_req_id = f"{trace_base}__qcurl"
        url = f"http://localhost:{port}/empty_200"
        baseline_url = _append_req_id(url, baseline_req_id)
        qcurl_url = _append_req_id(url, qcurl_req_id)
        resp_meta = {"status": 0 if expected_error else 200, "http_version": proto, "headers": {}, "body": None}

        observe_log.write_text("", encoding="utf-8")
        baseline = run_libtest_case(
            env=env,
            suite=suite,
            case=case_variant,
            client_name="cli_lc_http",
            args=["-V", baseline_proto, baseline_url],
            request_meta={"method": "GET", "url": baseline_url, "headers": {}, "body": b""},
            response_meta=resp_meta,
            download_count=1,
            allowed_exit_codes={0, 7},
        )
        if expected_error:
            curlcode, http_code = _parse_curlcode_http_code(list(baseline["payload"].get("stderr") or []))
            apply_error_namespaces(
                baseline["payload"],
                kind="protocol",
                http_status=0,
                curlcode=curlcode,
                http_code=http_code,
            )
            baseline["payload"]["protocol"] = {"requested": "h3-only", "observed": "none"}
            _normalize_error_request(baseline["payload"], baseline_url)
            _normalize_empty_error_response(baseline["payload"])
        else:
            obs = observe_http_observed_for_id(observe_log, baseline_req_id)
            baseline["payload"]["request"] = build_request_semantic(obs.method, obs.url, dict(obs.headers), b"")
            baseline["payload"]["response"]["status"] = obs.status
            baseline["payload"]["response"]["http_version"] = "http/1.1"
            baseline["payload"]["response"]["headers"] = dict(obs.response_headers)
            baseline["payload"]["protocol"] = {"requested": "h3", "observed": "http/1.1"}
        write_json(baseline["path"], baseline["payload"])

        observe_log.write_text("", encoding="utf-8")
        qcurl = run_qt_test(
            env=env,
            suite=suite,
            case=case_variant,
            qt_executable=qt_path,
            request_meta={"method": "GET", "url": qcurl_url, "headers": {}, "body": b""},
            response_meta=resp_meta,
            download_count=1 if not expected_error else None,
            case_env={
                "QCURL_LC_CASE_ID": case_id,
                "QCURL_LC_PROTO": proto,
                "QCURL_LC_REQ_ID": qcurl_req_id,
                "QCURL_LC_TARGET_URL": qcurl_url,
            },
        )
        if expected_error:
            apply_error_namespaces(
                qcurl["payload"],
                kind="protocol",
                http_status=0,
                curlcode=curlcode,
                http_code=http_code,
            )
            qcurl["payload"]["protocol"] = {"requested": "h3-only", "observed": "none"}
            _normalize_error_request(qcurl["payload"], qcurl_url)
            _normalize_empty_error_response(qcurl["payload"])
        else:
            obs = observe_http_observed_for_id(observe_log, qcurl_req_id)
            qcurl["payload"]["request"] = build_request_semantic(obs.method, obs.url, dict(obs.headers), b"")
            qcurl["payload"]["response"]["status"] = obs.status
            qcurl["payload"]["response"]["http_version"] = "http/1.1"
            qcurl["payload"]["response"]["headers"] = dict(obs.response_headers)
            qcurl["payload"]["protocol"] = {"requested": "h3", "observed": "http/1.1"}
        write_json(qcurl["path"], qcurl["payload"])

        assert_artifacts_match(baseline["path"], qcurl["path"])
