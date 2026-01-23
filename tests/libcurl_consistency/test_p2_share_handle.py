"""
LC-46（P2）：CURLOPT_SHARE（share handle）语义与死锁回归（PR gate 适配）。

目标（弱门禁 + 可观测）：
- 默认关闭：不共享 cookie（/home 返回 401）
- 显式开启 shareCookies：同一 manager 内可共享 cookie（/home 返回 200 + body）
- 并发 smoke：开启 shareCookies 后并发请求不死锁不崩溃
"""

from __future__ import annotations

import os
import uuid
from pathlib import Path
from typing import Dict, Optional

import pytest

from tests.libcurl_consistency.pytest_support.qcurl_runner import run_qt_test


def _run_case(
    *,
    env,
    qt_path: Path,
    suite: str,
    case: str,
    case_id: str,
    observe_http_port: int,
    proto: str,
    req_id: str,
    share_handle: Optional[str],
    expected_status: int,
    expected_body: bytes,
    download_count: Optional[int] = 1,
) -> Dict:
    url = f"http://localhost:{observe_http_port}/home?id={req_id}"
    case_env = {
        "QCURL_LC_CASE_ID": case_id,
        "QCURL_LC_PROTO": proto,
        "QCURL_LC_COUNT": "1",
        "QCURL_LC_DOCNAME": "",
        "QCURL_LC_UPLOAD_SIZE": "0",
        "QCURL_LC_ABORT_OFFSET": "0",
        "QCURL_LC_FILE_SIZE": "0",
        "QCURL_LC_REQ_ID": req_id,
        "QCURL_LC_OBSERVE_HTTP_PORT": str(observe_http_port),
    }
    if share_handle:
        case_env["QCURL_LC_SHARE_HANDLE"] = share_handle

    return run_qt_test(
        env=env,
        suite=suite,
        case=case,
        qt_executable=qt_path,
        args=[],
        request_meta={"method": "GET", "url": url, "headers": {}, "body": b""},
        response_meta={"status": expected_status, "http_version": proto, "headers": {}, "body": expected_body},
        download_files=None,
        download_count=download_count,
        case_env=case_env,
    )


def test_p2_share_handle_cookie_flow(env, lc_observe_http):
    qt_path = Path(os.environ["QCURL_QTTEST"])
    port = int(lc_observe_http["port"])

    _run_case(
        env=env,
        qt_path=qt_path,
        suite="p2_share_handle",
        case="lc_p2_share_handle_cookie_disabled",
        case_id="p2_share_handle_cookie_disabled",
        observe_http_port=port,
        proto="http/1.1",
        req_id=uuid.uuid4().hex[:8],
        share_handle=None,
        expected_status=401,
        expected_body=b"missing cookie\n",
    )

    _run_case(
        env=env,
        qt_path=qt_path,
        suite="p2_share_handle",
        case="lc_p2_share_handle_cookie_enabled",
        case_id="p2_share_handle_cookie_enabled",
        observe_http_port=port,
        proto="http/1.1",
        req_id=uuid.uuid4().hex[:8],
        share_handle="cookie",
        expected_status=200,
        expected_body=b"home-ok\n",
    )


def test_p2_share_handle_concurrency_smoke(env, lc_observe_http):
    qt_path = Path(os.environ["QCURL_QTTEST"])
    port = int(lc_observe_http["port"])

    _run_case(
        env=env,
        qt_path=qt_path,
        suite="p2_share_handle",
        case="lc_p2_share_handle_cookie_concurrency",
        case_id="p2_share_handle_cookie_concurrency",
        observe_http_port=port,
        proto="http/1.1",
        req_id=uuid.uuid4().hex[:8],
        share_handle="cookie",
        expected_status=200,
        expected_body=b"",
        download_count=None,
    )
