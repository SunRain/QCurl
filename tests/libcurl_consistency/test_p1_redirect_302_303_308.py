"""
P1: 302/303/308 redirect method and body observable consistency.
"""

from __future__ import annotations

import os
import re
import uuid
from pathlib import Path

import pytest

from tests.libcurl_consistency.pytest_support.artifacts import apply_error_namespaces, build_request_semantic, write_json
from tests.libcurl_consistency.pytest_support.baseline import run_libtest_case
from tests.libcurl_consistency.pytest_support.compare import assert_artifacts_match
from tests.libcurl_consistency.pytest_support.observed import observe_http_observed_list_for_id
from tests.libcurl_consistency.pytest_support.qcurl_runner import require_qcurl_qttest, run_qt_test
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


def _requests_from_observed(observed_list) -> list[dict]:
    return [
        {
            **build_request_semantic(obs.method, obs.url, {}, b""),
            "body_len": int(obs.body_len),
            "body_sha256": str(obs.body_sha256),
        }
        for obs in observed_list
    ]


def _responses_from_observed(observed_list, final_response, proto: str) -> list[dict]:
    out = []
    for idx, obs in enumerate(observed_list):
        is_last = idx == len(observed_list) - 1
        out.append({
            "status": int(obs.status),
            "http_version": proto,
            "headers": dict(obs.response_headers),
            "body_len": int(final_response.get("body_len") or 0) if is_last else 0,
            "body_sha256": str(final_response.get("body_sha256") or "") if is_last else "",
        })
    return out


@pytest.mark.parametrize(
    "case_id,status,method,post_redir,body_kind,expected_methods,allowed_exit_codes",
    [
        ("p1_redirect_302_post_to_get", 302, "POST", "default", "inline", ["POST", "GET"], {0}),
        ("p1_redirect_303_post_to_get", 303, "POST", "default", "inline", ["POST", "GET"], {0}),
        ("p1_redirect_308_post_seekable", 308, "POST", "all", "seekable", ["POST", "POST"], {0}),
        ("p1_redirect_308_put_seekable", 308, "PUT", "all", "seekable", ["PUT", "PUT"], {0}),
        ("p1_redirect_308_post_nonseekable", 308, "POST", "all", "nonseekable", ["POST"], {7}),
    ],
)
def test_p1_redirect_302_303_308_method_body(case_id, status, method, post_redir, body_kind, expected_methods, allowed_exit_codes, env, lc_observe_http):
    qt_path = require_qcurl_qttest()

    collect_logs = should_collect_service_logs()
    port = int(lc_observe_http["port"])
    observe_log = Path(str(lc_observe_http["log_file"]))

    suite = "p1_redirect_302_303_308"
    proto = "http/1.1"
    case_variant = f"lc_{case_id}_http_1.1"
    upload_size = 4096 if "nonseekable" in body_kind else 32
    body = b"x" * upload_size

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_{case_id}"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    url = f"http://localhost:{port}/redir_{status}"
    baseline_url = _append_req_id(url, baseline_req_id)
    qcurl_url = _append_req_id(url, qcurl_req_id)
    resp_meta = {"status": 200, "http_version": proto, "headers": {}, "body": None}

    args = ["-V", proto, "--follow", "--max-redirs", "10", "--method", method]
    if post_redir != "default":
        args.extend(["--post-redir", post_redir])
    if body_kind == "inline":
        args.extend(["--data-size", str(upload_size)])
    else:
        args.extend(["--stream-body", "--data-size", str(upload_size)])
        if body_kind == "seekable":
            args.append("--seekable-body")
    args.append(baseline_url)

    try:
        observe_log.write_text("", encoding="utf-8")
        baseline = run_libtest_case(
            env=env,
            suite=suite,
            case=case_variant,
            client_name="cli_lc_http",
            args=args,
            request_meta={"method": method, "url": baseline_url, "headers": {}, "body": body},
            response_meta=resp_meta,
            download_count=1,
            allowed_exit_codes=allowed_exit_codes,
        )
        obs_list = observe_http_observed_list_for_id(observe_log, baseline_req_id, expected_count=len(expected_methods))
        assert [o.method for o in obs_list] == expected_methods
        baseline["payload"]["requests"] = _requests_from_observed(obs_list)
        baseline["payload"]["responses"] = _responses_from_observed(obs_list, baseline["payload"]["response"], proto)
        baseline["payload"]["request"] = baseline["payload"]["requests"][0]
        baseline["payload"]["response"]["status"] = obs_list[-1].status
        baseline["payload"]["response"]["http_version"] = proto
        baseline["payload"]["response"]["headers"] = dict(obs_list[-1].response_headers)
        if body_kind == "nonseekable":
            curlcode, http_code = _parse_curlcode_http_code(list(baseline["payload"].get("stderr") or []))
            apply_error_namespaces(
                baseline["payload"],
                kind="redirect_replay",
                http_status=obs_list[-1].status,
                curlcode=curlcode,
                http_code=http_code,
            )
        write_json(baseline["path"], baseline["payload"])

        observe_log.write_text("", encoding="utf-8")
        qcurl = run_qt_test(
            env=env,
            suite=suite,
            case=case_variant,
            qt_executable=qt_path,
            request_meta={"method": method, "url": qcurl_url, "headers": {}, "body": body},
            response_meta=resp_meta,
            download_count=1,
            case_env={
                "QCURL_LC_CASE_ID": case_id,
                "QCURL_LC_PROTO": proto,
                "QCURL_LC_REQ_ID": qcurl_req_id,
                "QCURL_LC_OBSERVE_HTTP_PORT": str(port),
                "QCURL_LC_UPLOAD_SIZE": str(upload_size),
            },
        )
        obs_list = observe_http_observed_list_for_id(observe_log, qcurl_req_id, expected_count=len(expected_methods))
        assert [o.method for o in obs_list] == expected_methods
        qcurl["payload"]["requests"] = _requests_from_observed(obs_list)
        qcurl["payload"]["responses"] = _responses_from_observed(obs_list, qcurl["payload"]["response"], proto)
        qcurl["payload"]["request"] = qcurl["payload"]["requests"][0]
        qcurl["payload"]["response"]["status"] = obs_list[-1].status
        qcurl["payload"]["response"]["http_version"] = proto
        qcurl["payload"]["response"]["headers"] = dict(obs_list[-1].response_headers)
        if body_kind == "nonseekable":
            apply_error_namespaces(
                qcurl["payload"],
                kind="redirect_replay",
                http_status=obs_list[-1].status,
                curlcode=curlcode,
                http_code=http_code,
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
