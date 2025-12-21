"""
P1：HTTP proxy 可观测一致性（本仓库自建 proxy + baseline 可执行）。

目标：
- 对齐“代理侧可观测语义”：method/target + Proxy-* 头（Proxy-Authorization）
- 覆盖 CONNECT 隧道：HTTPS over proxy 的 CONNECT 目标一致

基线：repo 内置 `qcurl_lc_http_baseline`（client_name=cli_lc_http）
QCurl：tst_LibcurlConsistency（通过 QCURL_LC_CASE_ID 执行相同场景）
"""

from __future__ import annotations

import os
import uuid
from pathlib import Path
from typing import Dict, List

import pytest

from tests.libcurl_consistency.pytest_support.artifacts import build_request_semantic, write_json
from tests.libcurl_consistency.pytest_support.baseline import run_libtest_case
from tests.libcurl_consistency.pytest_support.case_defs import P1_PROXY_CASES
from tests.libcurl_consistency.pytest_support.compare import assert_artifacts_match
from tests.libcurl_consistency.pytest_support.observed import httpd_observed_for_id, proxy_observed_for_log
from tests.libcurl_consistency.pytest_support.qcurl_runner import run_qt_test
from tests.libcurl_consistency.pytest_support.service_logs import collect_service_logs_for_case, should_collect_service_logs


def _append_req_id(url: str, req_id: str) -> str:
    sep = "&" if "?" in url else "?"
    return f"{url}{sep}id={req_id}"


def _fmt_args(template: List[str], defaults: Dict, env) -> List[str]:
    ctx = defaults.copy()
    ctx.update({
        "http_port": env.http_port,
        "https_port": env.https_port,
        "ws_port": env.ws_port,
    })
    return [str(x).format(**ctx) for x in template]


@pytest.mark.parametrize("case_id", sorted(P1_PROXY_CASES.keys()))
def test_p1_proxy_basic_auth(case_id, env, lc_services, lc_logs, lc_http_proxy, tmp_path):
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("QCURL_QTTEST 未设置或可执行不存在")

    collect_logs = should_collect_service_logs()
    case = P1_PROXY_CASES[case_id]

    proxy_port = int(lc_http_proxy["port"])
    proxy_log = Path(str(lc_http_proxy["log_file"]))
    proxy_user = str(lc_http_proxy["username"])
    proxy_pass = str(lc_http_proxy["password"])
    proxy_url = f"http://localhost:{proxy_port}"

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_{case_id}"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    suite = str(case["suite"])
    proto = "http/1.1" if case_id == "proxy_http_basic_auth" else "h2"
    case_variant = f"{case['case']}_{proto.replace('/', '_')}"

    base_defaults = dict(case["defaults"])
    base_defaults.update({
        "proxy_url": proxy_url,
        "proxy_user": proxy_user,
        "proxy_pass": proxy_pass,
    })
    base_target_url = str(base_defaults["url"]).format(
        http_port=env.http_port,
        https_port=env.https_port,
    )
    baseline_url = _append_req_id(base_target_url, baseline_req_id)
    base_defaults["url"] = baseline_url

    args = _fmt_args(case["args_template"], base_defaults, env)
    req_meta = {"method": "GET", "url": baseline_url, "headers": {}, "body": b""}
    resp_meta = {"status": 200, "http_version": proto, "headers": {}, "body": None}

    access_log = Path(lc_logs["httpd_access_log"])

    try:
        proxy_log.write_text("", encoding="utf-8")
        baseline = run_libtest_case(
            env=env,
            suite=suite,
            case=case_variant,
            client_name=str(case["client"]),
            args=args,
            request_meta=req_meta,
            response_meta=resp_meta,
            download_count=case.get("baseline_download_count"),
        )
        proxy_method = "GET" if case_id == "proxy_http_basic_auth" else "CONNECT"
        proxy_obs = proxy_observed_for_log(proxy_log, method=proxy_method)
        origin_obs = httpd_observed_for_id(access_log, baseline_req_id, require_range=False)

        if case_id == "proxy_http_basic_auth":
            baseline["payload"]["requests"] = [
                build_request_semantic(proxy_obs.method, proxy_obs.url, proxy_obs.headers, b""),
            ]
        else:
            baseline["payload"]["requests"] = [
                build_request_semantic(proxy_obs.method, proxy_obs.url, proxy_obs.headers, b""),
                build_request_semantic(origin_obs.method, origin_obs.url, origin_obs.headers, b""),
            ]
        baseline["payload"]["response"]["status"] = origin_obs.status
        baseline["payload"]["response"]["http_version"] = origin_obs.http_version
        write_json(baseline["path"], baseline["payload"])

        proxy_log.write_text("", encoding="utf-8")
        qcurl_defaults = dict(case["defaults"])
        qcurl_target_url = str(qcurl_defaults["url"]).format(
            http_port=env.http_port,
            https_port=env.https_port,
        )
        qcurl_url = _append_req_id(qcurl_target_url, qcurl_req_id)

        case_env = {
            "QCURL_LC_CASE_ID": case_id,
            "QCURL_LC_PROTO": proto,
            "QCURL_LC_HTTP_PORT": str(env.http_port),
            "QCURL_LC_HTTPS_PORT": str(env.https_port),
            "QCURL_LC_WS_PORT": str(env.ws_port),
            "QCURL_LC_COUNT": "1",
            "QCURL_LC_DOCNAME": "",
            "QCURL_LC_UPLOAD_SIZE": "0",
            "QCURL_LC_ABORT_OFFSET": "0",
            "QCURL_LC_FILE_SIZE": "0",
            "QCURL_LC_REQ_ID": qcurl_req_id,
            "QCURL_LC_PROXY_PORT": str(proxy_port),
            "QCURL_LC_PROXY_USER": proxy_user,
            "QCURL_LC_PROXY_PASS": proxy_pass,
            "QCURL_LC_PROXY_TARGET_URL": qcurl_url,
        }
        qcurl = run_qt_test(
            env=env,
            suite=suite,
            case=case_variant,
            qt_executable=qt_path,
            args=[],
            request_meta={"method": "GET", "url": qcurl_url, "headers": {}, "body": b""},
            response_meta=resp_meta,
            download_count=case.get("qcurl_download_count"),
            case_env=case_env,
        )

        proxy_obs = proxy_observed_for_log(proxy_log, method=proxy_method)
        origin_obs = httpd_observed_for_id(access_log, qcurl_req_id, require_range=False)

        if case_id == "proxy_http_basic_auth":
            qcurl["payload"]["requests"] = [
                build_request_semantic(proxy_obs.method, proxy_obs.url, proxy_obs.headers, b""),
            ]
        else:
            qcurl["payload"]["requests"] = [
                build_request_semantic(proxy_obs.method, proxy_obs.url, proxy_obs.headers, b""),
                build_request_semantic(origin_obs.method, origin_obs.url, origin_obs.headers, b""),
            ]
        qcurl["payload"]["response"]["status"] = origin_obs.status
        qcurl["payload"]["response"]["http_version"] = origin_obs.http_version
        write_json(qcurl["path"], qcurl["payload"])

        assert_artifacts_match(baseline["path"], qcurl["path"])
    except Exception:
        if collect_logs:
            collect_service_logs_for_case(
                env,
                suite=suite,
                case=case_variant,
                logs=lc_logs,
                meta={
                    "case_id": case_id,
                    "case_variant": case_variant,
                    "proto": proto,
                    "baseline_req_id": baseline_req_id,
                    "qcurl_req_id": qcurl_req_id,
                    "proxy_port": proxy_port,
                },
            )
        raise

