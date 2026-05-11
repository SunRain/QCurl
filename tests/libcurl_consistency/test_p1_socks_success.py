"""
P1: SOCKS5 successful CONNECT observable consistency.
"""

from __future__ import annotations

import json
import os
import uuid
from pathlib import Path

import pytest

from tests.libcurl_consistency.pytest_support.artifacts import build_request_semantic, write_json
from tests.libcurl_consistency.pytest_support.baseline import run_libtest_case
from tests.libcurl_consistency.pytest_support.compare import assert_artifacts_match
from tests.libcurl_consistency.pytest_support.observed import observe_http_observed_for_id
from tests.libcurl_consistency.pytest_support.qcurl_runner import require_qcurl_qttest, run_qt_test
from tests.libcurl_consistency.pytest_support.service_logs import collect_service_logs_for_case, should_collect_service_logs


def _append_req_id(url: str, req_id: str) -> str:
    sep = "&" if "?" in url else "?"
    return f"{url}{sep}id={req_id}"


def _last_socks_entry(path: Path) -> dict:
    entries = []
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if raw.strip():
            entries.append(json.loads(raw))
    if not entries:
        raise AssertionError(f"SOCKS log empty: {path}")
    entry = dict(entries[-1])
    return {
        "version": int(entry.get("version") or 5),
        "cmd": int(entry.get("cmd") or 0),
        "atyp": int(entry.get("atyp") or 0),
        "dst": str(entry.get("dst") or ""),
        "dst_port": int(entry.get("dst_port") or 0),
        "rep": int(entry.get("rep") or 0),
    }


@pytest.mark.parametrize(
    "mode,proxy_type,target_host,expected_atyp",
    [
        ("socks5", "socks5", "127.0.0.1", 0x01),
        ("socks5h", "socks5h", "localhost", 0x03),
    ],
)
def test_p1_socks_success_http_1_1(mode, proxy_type, target_host, expected_atyp, env, lc_logs, lc_observe_http, lc_socks5_success_proxy):
    qt_path = require_qcurl_qttest()

    collect_logs = should_collect_service_logs()
    port = int(lc_observe_http["port"])
    observe_log = Path(str(lc_observe_http["log_file"]))
    proxy_port = int(lc_socks5_success_proxy["port"])
    proxy_log = Path(str(lc_socks5_success_proxy["log_file"]))

    suite = "p1_socks_success"
    proto = "http/1.1"
    case_variant = f"lc_{mode}_success_http_1.1"
    case_id = f"p1_{mode}_success"

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_{mode}_success"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    base_url = f"http://{target_host}:{port}/empty_200"
    baseline_url = _append_req_id(base_url, baseline_req_id)
    qcurl_url = _append_req_id(base_url, qcurl_req_id)
    proxy = f"127.0.0.1:{proxy_port}"
    resp_meta = {"status": 200, "http_version": proto, "headers": {}, "body": None}

    try:
        observe_log.write_text("", encoding="utf-8")
        proxy_log.write_text("", encoding="utf-8")
        baseline = run_libtest_case(
            env=env,
            suite=suite,
            case=case_variant,
            client_name="cli_lc_http",
            args=["-V", proto, "--proxy", proxy, "--proxy-type", proxy_type, baseline_url],
            request_meta={"method": "GET", "url": baseline_url, "headers": {}, "body": b""},
            response_meta=resp_meta,
            download_count=1,
        )
        obs = observe_http_observed_for_id(observe_log, baseline_req_id)
        socks = _last_socks_entry(proxy_log)
        assert socks["atyp"] == expected_atyp
        assert socks["rep"] == 0
        baseline["payload"]["request"] = build_request_semantic(obs.method, obs.url, dict(obs.headers), b"")
        baseline["payload"]["response"]["status"] = obs.status
        baseline["payload"]["response"]["http_version"] = proto
        baseline["payload"]["response"]["headers"] = dict(obs.response_headers)
        baseline["payload"]["socks"] = socks
        write_json(baseline["path"], baseline["payload"])

        observe_log.write_text("", encoding="utf-8")
        proxy_log.write_text("", encoding="utf-8")
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
                "QCURL_LC_TARGET_URL": qcurl_url,
                "QCURL_LC_SOCKS5_PORT": str(proxy_port),
            },
        )
        obs = observe_http_observed_for_id(observe_log, qcurl_req_id)
        socks = _last_socks_entry(proxy_log)
        assert socks["atyp"] == expected_atyp
        assert socks["rep"] == 0
        qcurl["payload"]["request"] = build_request_semantic(obs.method, obs.url, dict(obs.headers), b"")
        qcurl["payload"]["response"]["status"] = obs.status
        qcurl["payload"]["response"]["http_version"] = proto
        qcurl["payload"]["response"]["headers"] = dict(obs.response_headers)
        qcurl["payload"]["socks"] = socks
        write_json(qcurl["path"], qcurl["payload"])

        assert_artifacts_match(baseline["path"], qcurl["path"])
    except Exception:
        if collect_logs:
            logs = dict(lc_logs)
            logs["observe_http_log"] = observe_log
            logs["socks5_success_proxy_log"] = proxy_log
            collect_service_logs_for_case(
                env,
                suite=suite,
                case=case_variant,
                logs=logs,
                meta={
                    "case_id": case_id,
                    "baseline_req_id": baseline_req_id,
                    "qcurl_req_id": qcurl_req_id,
                    "proxy_port": proxy_port,
                    "observe_http_port": port,
                },
            )
        raise
