"""
扩展用例：校验 `reported_meta.json` 的对外字段合同。
仅在 `QCURL_LC_EXT=1` 时启用。
"""

from __future__ import annotations

import json
import os
import uuid
from pathlib import Path

import pytest

from tests.libcurl_consistency.pytest_support.observed import observe_http_observed_for_id
from tests.libcurl_consistency.pytest_support.qcurl_runner import run_qt_test


if os.environ.get("QCURL_LC_EXT", "").strip() != "1":
    pytest.skip("该扩展用例仅在 QCURL_LC_EXT=1 时启用", allow_module_level=True)


def _append_req_id(url: str, req_id: str) -> str:
    sep = "&" if "?" in url else "?"
    return f"{url}{sep}id={req_id}"


@pytest.mark.parametrize("status_code", [200, 418])
def test_ext_api_reported_status(status_code, env, lc_observe_http):
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("当前环境未提供 QCURL_QTTEST 可执行文件，跳过该用例")

    observe_port = int(lc_observe_http["port"])
    observe_log = Path(str(lc_observe_http["log_file"]))

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_ext_api_reported_status_{status_code}"
    qcurl_req_id = f"{trace_base}__qcurl"

    url = _append_req_id(f"http://localhost:{observe_port}/status/{status_code}", qcurl_req_id)
    req_meta = {"method": "GET", "url": url, "headers": {}, "body": b""}
    resp_meta = {"status": status_code, "http_version": "http/1.1", "headers": {}, "body": None}

    case_variant = f"reported_status_{status_code}"
    qcurl = run_qt_test(
        env=env,
        suite="ext_api_reported_status",
        case=case_variant,
        qt_executable=qt_path,
        args=[],
        request_meta=req_meta,
        response_meta=resp_meta,
        download_count=None,
        case_env={
            "QCURL_LC_CASE_ID": "ext_api_reported_status",
            "QCURL_LC_PROTO": "http/1.1",
            "QCURL_LC_OBSERVE_HTTP_PORT": str(observe_port),
            "QCURL_LC_STATUS_CODE": str(status_code),
            "QCURL_LC_REQ_ID": qcurl_req_id,
        },
    )

    run_dir = qcurl["path"].parent / "qcurl_run"
    meta_path = run_dir / "reported_meta.json"
    if not meta_path.exists():
        raise AssertionError(f"reported_meta.json 未生成：{meta_path}")
    meta = json.loads(meta_path.read_text(encoding="utf-8", errors="replace"))

    obs = observe_http_observed_for_id(observe_log, qcurl_req_id)
    assert int(meta.get("httpStatusCode") or 0) == int(obs.status)
