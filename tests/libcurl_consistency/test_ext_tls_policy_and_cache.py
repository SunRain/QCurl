"""
扩展用例：覆盖 TLS policy 与 HSTS/Alt-Svc cache 持久化的 smoke contract。
落盘路径使用临时目录隔离，并在用例结束后清理。
"""

from __future__ import annotations

import os
import tempfile
import uuid
from pathlib import Path

import pytest

from tests.libcurl_consistency.pytest_support.qcurl_runner import run_qt_test


if os.environ.get("QCURL_LC_EXT", "").strip() != "1":
    pytest.skip("该扩展用例仅在 QCURL_LC_EXT=1 时启用", allow_module_level=True)


def test_ext_tls_policy_and_cache_persistence(env, lc_httpd_cache_headers):
    qt_path = Path(os.environ["QCURL_QTTEST"])

    repo_root = Path(__file__).resolve().parents[2]
    ca_cert = repo_root / "curl" / "tests" / "http" / "gen" / "ca" / "ca.pem"
    if not ca_cert.exists():
        pytest.skip("当前环境未生成 curl testenv ca.pem，跳过该用例")

    req_id = uuid.uuid4().hex[:8]
    url = f"https://localhost:{env.https_port}/lc_cache_headers?id={req_id}"

    with tempfile.TemporaryDirectory(prefix="qcurl_lc_cache_") as tmp:
        tmp_dir = Path(tmp)
        hsts_path = tmp_dir / "hsts.txt"
        altsvc_path = tmp_dir / "altsvc.txt"

        case_env = {
            "QCURL_LC_CASE_ID": "ext_tls_policy_and_cache",
            "QCURL_LC_PROTO": "h2",
            "QCURL_LC_HTTPS_PORT": str(env.https_port),
            "QCURL_LC_COUNT": "1",
            "QCURL_LC_DOCNAME": "",
            "QCURL_LC_UPLOAD_SIZE": "0",
            "QCURL_LC_ABORT_OFFSET": "0",
            "QCURL_LC_FILE_SIZE": "0",
            "QCURL_LC_REQ_ID": req_id,
            "QCURL_LC_CA_CERT_PATH": str(ca_cert),
            "QCURL_LC_HSTS_PATH": str(hsts_path),
            "QCURL_LC_ALTSVC_PATH": str(altsvc_path),
        }

        run_qt_test(
            env=env,
            suite="ext_tls_cache",
            case="lc_ext_tls_policy_and_cache_h2",
            qt_executable=qt_path,
            args=[],
            request_meta={"method": "GET", "url": url, "headers": {}, "body": b""},
            response_meta={"status": 200, "http_version": "h2", "headers": {}, "body": b"cache-headers-ok\n"},
            download_files=None,
            download_count=None,
            case_env=case_env,
        )
