"""
P2: Range boundary cross proof through downloadFileResumable().
"""

from __future__ import annotations

import hashlib
import os
import uuid
from pathlib import Path

import pytest

from tests.libcurl_consistency.pytest_support.artifacts import (
    apply_error_namespaces,
    build_request_semantic,
    build_response_summary,
    write_json,
)
from tests.libcurl_consistency.pytest_support.baseline import run_libtest_case
from tests.libcurl_consistency.pytest_support.compare import assert_artifacts_match
from tests.libcurl_consistency.pytest_support.observed import observe_http_observed_for_id
from tests.libcurl_consistency.pytest_support.qcurl_runner import require_qcurl_qttest, run_qt_test
from tests.libcurl_consistency.pytest_support.service_logs import collect_service_logs_for_case, should_collect_service_logs


_FULL_BODY = bytes((ord("a") + (i % 26)) for i in range(32))


def _append_req_id(url: str, req_id: str) -> str:
    sep = "&" if "?" in url else "?"
    return f"{url}{sep}id={req_id}"


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


@pytest.mark.parametrize(
    "case_id,scenario,existing_size,expected_status,expected_final,expect_error",
    [
        ("p2_range_n_dash_success", "n_dash", 8, 206, _FULL_BODY, False),
        ("p2_range_complete_416", "complete_416", 32, 416, _FULL_BODY, False),
        ("p2_range_mismatch_start", "mismatch_start", 8, 206, _FULL_BODY[:8], True),
    ],
)
def test_p2_range_boundaries(case_id, scenario, existing_size, expected_status, expected_final, expect_error, env, lc_observe_http, tmp_path):
    qt_path = require_qcurl_qttest()

    collect_logs = should_collect_service_logs()
    port = int(lc_observe_http["port"])
    observe_log = Path(str(lc_observe_http["log_file"]))

    suite = "p2_range_boundaries"
    proto = "http/1.1"
    case_variant = f"lc_{case_id}_http_1.1"

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_{case_id}"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    url = f"http://localhost:{port}/range_boundary?scenario={scenario}"
    baseline_url = _append_req_id(url, baseline_req_id)
    qcurl_url = _append_req_id(url, qcurl_req_id)
    range_header = f"bytes={existing_size}-"
    qcurl_path = tmp_path / f"{case_id}.data"
    qcurl_path.write_bytes(_FULL_BODY[:existing_size])
    resp_meta = {"status": expected_status, "http_version": proto, "headers": {}, "body": None}

    try:
        observe_log.write_text("", encoding="utf-8")
        baseline = run_libtest_case(
            env=env,
            suite=suite,
            case=case_variant,
            client_name="cli_lc_http",
            args=["-V", proto, "--header", f"Range: {range_header}", baseline_url],
            request_meta={"method": "GET", "url": baseline_url, "headers": {"range": range_header}, "body": b""},
            response_meta=resp_meta,
            download_count=1,
            allowed_exit_codes={0},
        )
        obs = observe_http_observed_for_id(observe_log, baseline_req_id)
        assert obs.headers.get("range") == range_header
        assert obs.status == expected_status
        if expected_status == 206:
            assert "content-range" in obs.response_headers
        baseline["payload"]["request"] = build_request_semantic(obs.method, obs.url, dict(obs.headers), b"")
        baseline["payload"]["response"] = build_response_summary(
            status=obs.status,
            http_version=proto,
            headers=dict(obs.response_headers),
            body=expected_final,
        )
        baseline["payload"]["range"] = {
            "scenario": scenario,
            "existing_size": existing_size,
            "final_size": len(expected_final),
            "final_sha256": _sha(expected_final),
        }
        if expect_error:
            apply_error_namespaces(
                baseline["payload"],
                kind="range_mismatch",
                http_status=obs.status,
                curlcode=0,
                http_code=obs.status,
            )
        write_json(baseline["path"], baseline["payload"])

        observe_log.write_text("", encoding="utf-8")
        qcurl = run_qt_test(
            env=env,
            suite=suite,
            case=case_variant,
            qt_executable=qt_path,
            request_meta={"method": "GET", "url": qcurl_url, "headers": {"range": range_header}, "body": b""},
            response_meta=resp_meta,
            case_env={
                "QCURL_LC_CASE_ID": case_id,
                "QCURL_LC_PROTO": proto,
                "QCURL_LC_REQ_ID": qcurl_req_id,
                "QCURL_LC_TARGET_URL": qcurl_url,
                "QCURL_LC_RESUME_PATH": str(qcurl_path),
            },
        )
        obs = observe_http_observed_for_id(observe_log, qcurl_req_id)
        assert obs.headers.get("range") == range_header
        assert obs.status == expected_status
        final_bytes = qcurl_path.read_bytes()
        assert final_bytes == expected_final
        qcurl["payload"]["request"] = build_request_semantic(obs.method, obs.url, dict(obs.headers), b"")
        qcurl["payload"]["response"] = build_response_summary(
            status=obs.status,
            http_version=proto,
            headers=dict(obs.response_headers),
            body=final_bytes,
        )
        qcurl["payload"]["range"] = {
            "scenario": scenario,
            "existing_size": existing_size,
            "final_size": len(final_bytes),
            "final_sha256": _sha(final_bytes),
        }
        if expect_error:
            apply_error_namespaces(
                qcurl["payload"],
                kind="range_mismatch",
                http_status=obs.status,
                curlcode=0,
                http_code=obs.status,
            )
        write_json(qcurl["path"], qcurl["payload"])

        assert_artifacts_match(baseline["path"], qcurl["path"])
    except Exception:
        if collect_logs:
            collect_service_logs_for_case(
                env,
                suite=suite,
                case=case_variant,
                logs={"observe_http_log": observe_log},
                meta={
                    "case_id": case_id,
                    "baseline_req_id": baseline_req_id,
                    "qcurl_req_id": qcurl_req_id,
                    "observe_http_port": port,
                },
            )
        raise
