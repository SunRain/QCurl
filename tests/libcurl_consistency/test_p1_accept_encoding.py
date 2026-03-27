"""
P1：自动内容解码交付一致性。

启用自动解压时，QCurl 与 libcurl baseline 都应发送 `Accept-Encoding`，
并交付解压后的 body 字节。
"""

from __future__ import annotations

import os
import uuid
from pathlib import Path

import pytest

from tests.libcurl_consistency.pytest_support.artifacts import sha256_bytes, write_json
from tests.libcurl_consistency.pytest_support.baseline import run_libtest_case
from tests.libcurl_consistency.pytest_support.compare import assert_artifacts_match
from tests.libcurl_consistency.pytest_support.observed import observe_http_observed_list_for_id
from tests.libcurl_consistency.pytest_support.qcurl_runner import run_qt_test
from tests.libcurl_consistency.pytest_support.service_logs import collect_service_logs_for_case, should_collect_service_logs


_REPO_ROOT = Path(__file__).resolve().parents[2]


def _has_accept_encoding_api() -> bool:
    header = _REPO_ROOT / "src" / "QCNetworkRequest.h"
    try:
        text = header.read_text(encoding="utf-8", errors="replace")
    except Exception:
        return False
    return "setAcceptedEncodings" in text and "setAutoDecompressionEnabled" in text


if not _has_accept_encoding_api():
    pytest.skip("当前构建未提供 Accept-Encoding API，跳过该用例", allow_module_level=True)


def _append_req_id(url: str, req_id: str) -> str:
    sep = "&" if "?" in url else "?"
    return f"{url}{sep}id={req_id}"


def _hes_accept_encoding_payload(headers: dict, response_headers: dict, body_len: int, body_sha: str) -> dict:
    return {
        "kind": "accept_encoding",
        "request_accept_encoding": str(headers.get("accept-encoding") or ""),
        "response_content_encoding": str(response_headers.get("content-encoding") or ""),
        "body_len": int(body_len),
        "body_sha256": body_sha,
    }


def test_p1_accept_encoding_gzip_http_1_1(env, lc_observe_http):
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("当前环境未提供 QCURL_QTTEST 可执行文件，跳过该用例")

    collect_logs = should_collect_service_logs()
    port = int(lc_observe_http["port"])
    observe_log = Path(str(lc_observe_http["log_file"]))

    suite = "p1_accept_encoding"
    proto = "http/1.1"
    case_variant = "p1_accept_encoding_gzip_http_1.1"
    case_id = "p1_accept_encoding_gzip"

    expected_body = b"accept-encoding-ok\n"
    expected_len = len(expected_body)
    expected_sha = sha256_bytes(expected_body)

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_accept_encoding_gzip"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    url = f"http://localhost:{port}/enc"
    baseline_url = _append_req_id(url, baseline_req_id)
    qcurl_url = _append_req_id(url, qcurl_req_id)

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
                "--accept-encoding",
                "gzip",
                baseline_url,
            ],
            request_meta={"method": "GET", "url": baseline_url, "headers": {}, "body": b""},
            response_meta={"status": 200, "http_version": proto, "headers": {}, "body": None},
            download_count=1,
        )
        obs = observe_http_observed_list_for_id(observe_log, baseline_req_id, expected_count=1)[0]
        assert "accept-encoding" in obs.headers
        assert "gzip" in str(obs.headers.get("accept-encoding") or "").lower()
        assert str(obs.response_headers.get("content-encoding") or "").lower() == "gzip"

        assert int(baseline["payload"]["response"]["body_len"]) == expected_len
        assert str(baseline["payload"]["response"]["body_sha256"]) == expected_sha

        baseline["payload"]["request"]["method"] = obs.method
        baseline["payload"]["request"]["url"] = obs.url
        baseline["payload"]["request"]["headers"] = dict(obs.headers)
        baseline["payload"]["response"]["status"] = obs.status
        baseline["payload"]["response"]["http_version"] = proto
        baseline["payload"]["response"]["headers"] = dict(obs.response_headers)
        baseline["payload"]["hes"] = _hes_accept_encoding_payload(
            dict(obs.headers),
            dict(obs.response_headers),
            expected_len,
            expected_sha,
        )
        write_json(baseline["path"], baseline["payload"])

        observe_log.write_text("", encoding="utf-8")
        qcurl = run_qt_test(
            env=env,
            suite=suite,
            case=case_variant,
            qt_executable=qt_path,
            args=[],
            request_meta={"method": "GET", "url": qcurl_url, "headers": {}, "body": b""},
            response_meta={"status": 200, "http_version": proto, "headers": {}, "body": None},
            download_count=1,
            case_env={
                "QCURL_LC_CASE_ID": case_id,
                "QCURL_LC_PROTO": proto,
                "QCURL_LC_REQ_ID": qcurl_req_id,
                "QCURL_LC_OBSERVE_HTTP_PORT": str(port),
            },
        )
        obs = observe_http_observed_list_for_id(observe_log, qcurl_req_id, expected_count=1)[0]
        assert "accept-encoding" in obs.headers
        assert "gzip" in str(obs.headers.get("accept-encoding") or "").lower()
        assert str(obs.response_headers.get("content-encoding") or "").lower() == "gzip"

        assert int(qcurl["payload"]["response"]["body_len"]) == expected_len
        assert str(qcurl["payload"]["response"]["body_sha256"]) == expected_sha

        qcurl["payload"]["request"]["method"] = obs.method
        qcurl["payload"]["request"]["url"] = obs.url
        qcurl["payload"]["request"]["headers"] = dict(obs.headers)
        qcurl["payload"]["response"]["status"] = obs.status
        qcurl["payload"]["response"]["http_version"] = proto
        qcurl["payload"]["response"]["headers"] = dict(obs.response_headers)
        qcurl["payload"]["hes"] = _hes_accept_encoding_payload(
            dict(obs.headers),
            dict(obs.response_headers),
            expected_len,
            expected_sha,
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
                    "proto": proto,
                    "baseline_req_id": baseline_req_id,
                    "qcurl_req_id": qcurl_req_id,
                    "observe_port": port,
                },
            )
        raise
