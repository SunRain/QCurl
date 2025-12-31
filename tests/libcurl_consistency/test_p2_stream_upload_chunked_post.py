"""
P2：POST 流式上传 sizeBytes 未知（chunked，HTTP/1.1）。

目标：
- baseline（libcurl easy）与 QCurl 在“unknown size + chunked”路径下的可观测语义一致：
  - 服务端观测到 Transfer-Encoding: chunked
  - 回显 body 字节一致（len/hash 对齐）

说明：
- 仅覆盖 HTTP/1.1（与 task_autorun.md 3.9 对齐）。
"""

from __future__ import annotations

import os
import uuid
from pathlib import Path

import pytest

from tests.libcurl_consistency.pytest_support.baseline import run_libtest_case
from tests.libcurl_consistency.pytest_support.compare import assert_artifacts_match
from tests.libcurl_consistency.pytest_support.observed import observe_http_observed_list_for_id
from tests.libcurl_consistency.pytest_support.qcurl_runner import run_qt_test
from tests.libcurl_consistency.pytest_support.service_logs import collect_service_logs_for_case, should_collect_service_logs
from tests.libcurl_consistency.pytest_support.artifacts import sha256_bytes, write_json


_REPO_ROOT = Path(__file__).resolve().parents[2]


def _has_chunked_post_upload_api() -> bool:
    header = _REPO_ROOT / "src" / "QCNetworkRequest.h"
    try:
        text = header.read_text(encoding="utf-8", errors="replace")
    except Exception:
        return False
    return "setUploadDevice" in text and "setAllowChunkedUploadForPost" in text


if not _has_chunked_post_upload_api():
    pytest.skip("chunked unknown-size upload API 未落地，跳过该用例", allow_module_level=True)


def _normalize_req_headers(headers: dict) -> dict:
    out: dict = {}
    for name in ("host", "content-length", "transfer-encoding", "expect"):
        v = headers.get(name)
        if v:
            out[name] = v
    return out


def _assert_chunked(headers: dict) -> None:
    te = str(headers.get("transfer-encoding") or "")
    assert "chunked" in te.lower(), f"expected transfer-encoding chunked, got: {te!r}"


def test_p2_stream_body_post_unknown_size_chunked_http_1_1(env, lc_observe_http):
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("QCURL_QTTEST 未设置或可执行不存在")

    collect_logs = should_collect_service_logs()
    port = int(lc_observe_http["port"])
    observe_log = Path(str(lc_observe_http["log_file"]))

    suite = "p2_chunked_upload"
    proto = "http/1.1"
    upload_size = 4096

    case_variant = "lc_stream_body_post_chunked_unknown_size_http_1.1"
    case_id = "p2_stream_body_post_chunked_unknown_size"

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_post_chunked_unknown"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    base_url = f"http://localhost:{port}/method"
    baseline_url = f"{base_url}?id={baseline_req_id}"
    qcurl_url = f"{base_url}?id={qcurl_req_id}"

    body = b"x" * upload_size

    try:
        observe_log.write_text("", encoding="utf-8")
        baseline_args = [
            "-V",
            proto,
            "--method",
            "POST",
            "--data-size",
            str(upload_size),
            "--stream-body",
            "--unknown-size",
            baseline_url,
        ]
        baseline = run_libtest_case(
            env=env,
            suite=suite,
            case=case_variant,
            client_name="cli_lc_http",
            args=baseline_args,
            request_meta={"method": "POST", "url": baseline_url, "headers": {}, "body": body},
            response_meta={"status": 200, "http_version": proto, "headers": {}, "body": None},
            download_count=1,
            allowed_exit_codes={0},
        )
        obs_base = observe_http_observed_list_for_id(observe_log, baseline_req_id, expected_count=1)
        _assert_chunked(obs_base[0].headers)
        baseline["payload"]["requests"] = [{
            "method": obs_base[0].method,
            "url": obs_base[0].url,
            "headers": _normalize_req_headers(obs_base[0].headers),
            "body_len": len(body),
            "body_sha256": sha256_bytes(body),
        }]
        baseline["payload"]["responses"] = [{
            **baseline["payload"]["response"],
            "status": obs_base[0].status,
            "http_version": proto,
            "headers": dict(obs_base[0].response_headers),
        }]
        baseline["payload"]["request"]["method"] = obs_base[0].method
        baseline["payload"]["request"]["url"] = obs_base[0].url
        baseline["payload"]["request"]["headers"] = _normalize_req_headers(obs_base[0].headers)
        baseline["payload"]["response"]["status"] = obs_base[0].status
        baseline["payload"]["response"]["http_version"] = proto
        baseline["payload"]["response"]["headers"] = dict(obs_base[0].response_headers)
        write_json(baseline["path"], baseline["payload"])

        observe_log.write_text("", encoding="utf-8")
        qcurl = run_qt_test(
            env=env,
            suite=suite,
            case=case_variant,
            qt_executable=qt_path,
            args=[],
            request_meta={"method": "POST", "url": qcurl_url, "headers": {}, "body": body},
            response_meta={"status": 200, "http_version": proto, "headers": {}, "body": None},
            download_count=1,
            case_env={
                "QCURL_LC_CASE_ID": case_id,
                "QCURL_LC_PROTO": proto,
                "QCURL_LC_OBSERVE_HTTP_PORT": str(port),
                "QCURL_LC_REQ_ID": qcurl_req_id,
            },
        )
        obs_q = observe_http_observed_list_for_id(observe_log, qcurl_req_id, expected_count=1)
        _assert_chunked(obs_q[0].headers)
        qcurl["payload"]["requests"] = [{
            "method": obs_q[0].method,
            "url": obs_q[0].url,
            "headers": _normalize_req_headers(obs_q[0].headers),
            "body_len": len(body),
            "body_sha256": sha256_bytes(body),
        }]
        qcurl["payload"]["responses"] = [{
            **qcurl["payload"]["response"],
            "status": obs_q[0].status,
            "http_version": proto,
            "headers": dict(obs_q[0].response_headers),
        }]
        qcurl["payload"]["request"]["method"] = obs_q[0].method
        qcurl["payload"]["request"]["url"] = obs_q[0].url
        qcurl["payload"]["request"]["headers"] = _normalize_req_headers(obs_q[0].headers)
        qcurl["payload"]["response"]["status"] = obs_q[0].status
        qcurl["payload"]["response"]["http_version"] = proto
        qcurl["payload"]["response"]["headers"] = dict(obs_q[0].response_headers)
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
                    "upload_size": upload_size,
                },
            )
        raise

