"""
P2：连接/并发上限 smoke。

只验证配置链路和“不崩溃/不死锁”，不对精确阀值做强断言。
"""

from __future__ import annotations

import os
import uuid
from pathlib import Path

import pytest

from tests.libcurl_consistency.pytest_support.qcurl_runner import run_qt_test


if os.environ.get("QCURL_LC_EXT", "").strip() != "1":
    pytest.skip("该 smoke 用例仅在 QCURL_LC_EXT=1 时启用", allow_module_level=True)


def test_p2_connection_limits_smoke(env):
    qt_path = Path(os.environ["QCURL_QTTEST"])

    req_id = uuid.uuid4().hex[:8]
    url = f"https://localhost:{env.https_port}/path/24020001?id={req_id}"

    case_env = {
        "QCURL_LC_CASE_ID": "multi_limits_smoke",
        "QCURL_LC_PROTO": "h2",
        "QCURL_LC_HTTPS_PORT": str(env.https_port),
        "QCURL_LC_COUNT": "4",
        "QCURL_LC_DOCNAME": "path/2402",
        "QCURL_LC_UPLOAD_SIZE": "0",
        "QCURL_LC_ABORT_OFFSET": "0",
        "QCURL_LC_FILE_SIZE": "0",
        "QCURL_LC_REQ_ID": req_id,
    }

    run_qt_test(
        env=env,
        suite="p2_connection_limits",
        case="lc_p2_connection_limits_smoke_h2",
        qt_executable=qt_path,
        args=[],
        request_meta={"method": "GET", "url": url, "headers": {}, "body": b""},
        response_meta={"status": 200, "http_version": "h2", "headers": {}, "body": None},
        download_files=None,
        download_count=4,
        case_env=case_env,
    )
