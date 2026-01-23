"""
LC-45（P2/PR gate）：连接/并发上限弱门禁。

约束：
- 仅做“配置链路 + 不崩溃/不死锁”的弱断言（避免 CI CPU/时钟不稳定导致 flaky）
- 不对“并发确实被限制到精确阀值”做强断言；强验证留给 bench/soak

实现：
- 复用 Qt Test case `multi_limits_smoke`（会显式设置 multi limits 并执行并发下载）
- 仅在 PR gate（QCURL_LC_EXT=1）启用，避免默认 gate 扩容
"""

from __future__ import annotations

import os
import uuid
from pathlib import Path

import pytest

from tests.libcurl_consistency.pytest_support.qcurl_runner import run_qt_test


if os.environ.get("QCURL_LC_EXT", "").strip() != "1":
    pytest.skip("connection limits smoke is only enabled for PR gate (QCURL_LC_EXT=1)", allow_module_level=True)


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
