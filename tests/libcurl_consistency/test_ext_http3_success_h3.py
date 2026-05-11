"""
Ext-only HTTP/3 success observable consistency.

This file is capability-planned out of the default gate when the local
H3-capable nghttpx or bundled curl HTTP3 support is unavailable.
"""

from __future__ import annotations

import os
import uuid
from pathlib import Path

import pytest

from tests.libcurl_consistency.pytest_support.artifacts import build_request_semantic, write_json
from tests.libcurl_consistency.pytest_support.baseline import run_libtest_case
from tests.libcurl_consistency.pytest_support.capability_manifest import guard_planned_test
from tests.libcurl_consistency.pytest_support.compare import assert_artifacts_match
from tests.libcurl_consistency.pytest_support.observed import nghttpx_observed_for_id
from tests.libcurl_consistency.pytest_support.qcurl_runner import run_qt_test
from tests.libcurl_consistency.pytest_support.service_logs import collect_service_logs_for_case, should_collect_service_logs


if os.environ.get("QCURL_LC_EXT", "").strip() != "1":
    pytest.skip("HTTP/3 success coverage is ext-only", allow_module_level=True)


def _append_req_id(url: str, req_id: str) -> str:
    sep = "&" if "?" in url else "?"
    return f"{url}{sep}id={req_id}"


def test_ext_http3_success_h3(env, lc_logs):
    guard_planned_test("test_ext_http3_success_h3.py")
    if not env.have_h3():
        pytest.fail("capability planner should exclude HTTP/3 success when env.have_h3() is false")

    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("当前环境未提供 QCURL_QTTEST 可执行文件，跳过该用例")

    collect_logs = should_collect_service_logs()
    suite = "ext_http3_version_policy"
    proto = "h3"
    case_variant = "lc_http3_success_h3"
    case_id = "ext_http3_success"
    trace_base = f"lc_{uuid.uuid4().hex[:8]}_http3_success"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    baseline_url = _append_req_id(f"https://localhost:{env.https_port}/data-1m", baseline_req_id)
    qcurl_url = _append_req_id(f"https://localhost:{env.https_port}/data-1m", qcurl_req_id)
    resp_meta = {"status": 200, "http_version": proto, "headers": {}, "body": None}

    try:
        baseline = run_libtest_case(
            env=env,
            suite=suite,
            case=case_variant,
            client_name="cli_lc_http",
            args=["-V", proto, "--secure", "--cainfo", env.ca.cert_file, baseline_url],
            request_meta={"method": "GET", "url": baseline_url, "headers": {}, "body": b""},
            response_meta=resp_meta,
            download_count=1,
        )
        obs = nghttpx_observed_for_id(Path(lc_logs["nghttpx_access_log"]), baseline_req_id, require_range=False)
        assert obs.http_version == "h3"
        baseline["payload"]["request"] = build_request_semantic(obs.method, obs.url, dict(obs.headers), b"")
        baseline["payload"]["response"]["status"] = obs.status
        baseline["payload"]["response"]["http_version"] = obs.http_version
        baseline["payload"]["protocol"] = {"requested": "h3", "observed": obs.http_version}
        write_json(baseline["path"], baseline["payload"])

        qcurl = run_qt_test(
            env=env,
            suite=suite,
            case=case_variant,
            qt_executable=qt_path,
            request_meta={"method": "GET", "url": qcurl_url, "headers": {}, "body": b""},
            response_meta=resp_meta,
            download_count=1,
            case_env={
                "QCURL_LC_CASE_ID": case_id,
                "QCURL_LC_PROTO": proto,
                "QCURL_LC_REQ_ID": qcurl_req_id,
                "QCURL_LC_HTTPS_PORT": str(env.https_port),
                "QCURL_LC_DOCNAME": "data-1m",
            },
        )
        obs = nghttpx_observed_for_id(Path(lc_logs["nghttpx_access_log"]), qcurl_req_id, require_range=False)
        assert obs.http_version == "h3"
        qcurl["payload"]["request"] = build_request_semantic(obs.method, obs.url, dict(obs.headers), b"")
        qcurl["payload"]["response"]["status"] = obs.status
        qcurl["payload"]["response"]["http_version"] = obs.http_version
        qcurl["payload"]["protocol"] = {"requested": "h3", "observed": obs.http_version}
        write_json(qcurl["path"], qcurl["payload"])

        assert_artifacts_match(baseline["path"], qcurl["path"])
    except Exception:
        if collect_logs:
            collect_service_logs_for_case(
                env,
                suite=suite,
                case=case_variant,
                logs=lc_logs,
                meta={"case_id": case_id, "baseline_req_id": baseline_req_id, "qcurl_req_id": qcurl_req_id},
            )
        raise
