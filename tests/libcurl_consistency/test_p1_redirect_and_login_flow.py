"""
P1：重定向（FOLLOWLOCATION）与“模拟登录态（Set-Cookie → Cookie）”一致性。

覆盖缺口：
- FOLLOWLOCATION 未覆盖：多跳 302 的请求序列与最终落点一致性
- 关键响应头可观测：Location / Set-Cookie
- 关键请求头可观测：Host / Cookie

服务端：repo 内置 http_observe_server.py（/redir/<n>、/login、/home）
基线：repo 内置 qcurl_lc_http_baseline（cli_lc_http）
QCurl：tst_LibcurlConsistency（p1_redirect_* / p1_login_cookie_flow）
"""

from __future__ import annotations

import os
import uuid
from pathlib import Path

import pytest

from tests.libcurl_consistency.pytest_support.artifacts import write_json
from tests.libcurl_consistency.pytest_support.baseline import run_libtest_case
from tests.libcurl_consistency.pytest_support.compare import assert_artifacts_match
from tests.libcurl_consistency.pytest_support.observed import observe_http_observed_list_for_id
from tests.libcurl_consistency.pytest_support.qcurl_runner import run_qt_test
from tests.libcurl_consistency.pytest_support.service_logs import collect_service_logs_for_case, should_collect_service_logs


def _append_req_id(url: str, req_id: str) -> str:
    sep = "&" if "?" in url else "?"
    return f"{url}{sep}id={req_id}"


def _responses_from_observed(*, observed_list, final_response, proto: str) -> list[dict]:
    items: list[dict] = []
    for idx, obs in enumerate(observed_list):
        is_last = (idx == len(observed_list) - 1)
        if is_last:
            items.append({
                "status": int(obs.status),
                "http_version": proto,
                "headers": dict(obs.response_headers),
                "body_len": int(final_response.get("body_len") or 0),
                "body_sha256": str(final_response.get("body_sha256") or ""),
            })
        else:
            items.append({
                "status": int(obs.status),
                "http_version": proto,
                "headers": dict(obs.response_headers),
                "body_len": 0,
                "body_sha256": "",
            })
    return items


def _order_redir_chain(observed_list):
    def key(o) -> int:
        s = str(o.url)
        if not s.startswith("/redir/"):
            return -1
        try:
            return int(s.split("/", 2)[2])
        except Exception:
            return -1
    return sorted(observed_list, key=key, reverse=True)


def _order_login_chain(observed_list):
    # 预期顺序：/login -> /home
    order = {"/login": 0, "/home": 1}
    return sorted(observed_list, key=lambda o: order.get(str(o.url).split("?", 1)[0], 99))


@pytest.mark.parametrize("follow", [False, True])
def test_p1_redirect_followlocation(follow: bool, env, lc_logs, lc_observe_http):
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("QCURL_QTTEST 未设置或可执行不存在")

    collect_logs = should_collect_service_logs()
    port = int(lc_observe_http["port"])
    observe_log = Path(str(lc_observe_http["log_file"]))

    suite = "p1_redirect"
    proto = "http/1.1"
    case_variant = f"lc_redirect_{'follow' if follow else 'nofollow'}_http_1.1"

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_redir_{'1' if follow else '0'}"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    url = f"http://localhost:{port}/redir/3"
    baseline_url = _append_req_id(url, baseline_req_id)

    req_meta = {"method": "GET", "url": baseline_url, "headers": {}, "body": b""}
    resp_meta = {"status": 200 if follow else 302, "http_version": proto, "headers": {}, "body": None}

    expected_requests = 4 if follow else 1

    try:
        observe_log.write_text("", encoding="utf-8")
        baseline_args = ["-V", proto]
        if follow:
            baseline_args.extend(["--follow", "--max-redirs", "10"])
        baseline_args.append(baseline_url)

        baseline = run_libtest_case(
            env=env,
            suite=suite,
            case=case_variant,
            client_name="cli_lc_http",
            args=baseline_args,
            request_meta=req_meta,
            response_meta=resp_meta,
            download_count=1,
        )

        obs_list = observe_http_observed_list_for_id(observe_log, baseline_req_id, expected_count=expected_requests)
        if follow:
            obs_list = _order_redir_chain(obs_list)
        baseline["payload"]["requests"] = [{
            "method": obs.method,
            "url": obs.url,
            "headers": obs.headers,
            "body_len": 0,
            "body_sha256": "",
        } for obs in obs_list]
        baseline["payload"]["responses"] = _responses_from_observed(
            observed_list=obs_list,
            final_response=baseline["payload"]["response"],
            proto=proto,
        )
        baseline["payload"]["request"]["method"] = obs_list[0].method
        baseline["payload"]["request"]["url"] = obs_list[0].url
        baseline["payload"]["request"]["headers"] = obs_list[0].headers
        baseline["payload"]["response"]["status"] = obs_list[-1].status
        baseline["payload"]["response"]["http_version"] = proto
        baseline["payload"]["response"]["headers"] = obs_list[-1].response_headers
        write_json(baseline["path"], baseline["payload"])

        observe_log.write_text("", encoding="utf-8")
        qcurl_url = _append_req_id(url, qcurl_req_id)
        qcurl = run_qt_test(
            env=env,
            suite=suite,
            case=case_variant,
            qt_executable=qt_path,
            args=[],
            request_meta={"method": "GET", "url": qcurl_url, "headers": {}, "body": b""},
            response_meta=resp_meta,
            download_count=1,
            case_env={
                "QCURL_LC_CASE_ID": "p1_redirect_follow" if follow else "p1_redirect_nofollow",
                "QCURL_LC_PROTO": proto,
                "QCURL_LC_REQ_ID": qcurl_req_id,
                "QCURL_LC_OBSERVE_HTTP_PORT": str(port),
            },
        )

        obs_list = observe_http_observed_list_for_id(observe_log, qcurl_req_id, expected_count=expected_requests)
        if follow:
            obs_list = _order_redir_chain(obs_list)
        qcurl["payload"]["requests"] = [{
            "method": obs.method,
            "url": obs.url,
            "headers": obs.headers,
            "body_len": 0,
            "body_sha256": "",
        } for obs in obs_list]
        qcurl["payload"]["responses"] = _responses_from_observed(
            observed_list=obs_list,
            final_response=qcurl["payload"]["response"],
            proto=proto,
        )
        qcurl["payload"]["request"]["method"] = obs_list[0].method
        qcurl["payload"]["request"]["url"] = obs_list[0].url
        qcurl["payload"]["request"]["headers"] = obs_list[0].headers
        qcurl["payload"]["response"]["status"] = obs_list[-1].status
        qcurl["payload"]["response"]["http_version"] = proto
        qcurl["payload"]["response"]["headers"] = obs_list[-1].response_headers
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
                    "case_id": "p1_redirect_followlocation",
                    "case_variant": case_variant,
                    "proto": proto,
                    "baseline_req_id": baseline_req_id,
                    "qcurl_req_id": qcurl_req_id,
                    "follow": follow,
                    "observe_port": port,
                },
            )
        raise


def test_p1_login_cookie_state_flow(env, lc_logs, lc_observe_http, tmp_path):
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("QCURL_QTTEST 未设置或可执行不存在")

    collect_logs = should_collect_service_logs()
    port = int(lc_observe_http["port"])
    observe_log = Path(str(lc_observe_http["log_file"]))

    suite = "p1_login"
    proto = "http/1.1"
    case_variant = "lc_login_cookie_flow_http_1.1"

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_login_cookie"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    url = f"http://localhost:{port}/login"
    baseline_url = _append_req_id(url, baseline_req_id)

    baseline_cookie = tmp_path / "baseline.cookies"
    qcurl_cookie = tmp_path / "qcurl.cookies"
    baseline_cookie.write_text("", encoding="utf-8")
    qcurl_cookie.write_text("", encoding="utf-8")

    req_meta = {"method": "GET", "url": baseline_url, "headers": {}, "body": b""}
    resp_meta = {"status": 200, "http_version": proto, "headers": {}, "body": None}

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
                "--follow",
                "--max-redirs",
                "10",
                "--cookiefile",
                str(baseline_cookie),
                "--cookiejar",
                str(baseline_cookie),
                baseline_url,
            ],
            request_meta=req_meta,
            response_meta=resp_meta,
            download_count=1,
        )

        obs_list = observe_http_observed_list_for_id(observe_log, baseline_req_id, expected_count=2)
        obs_list = _order_login_chain(obs_list)
        baseline["payload"]["requests"] = [{
            "method": obs.method,
            "url": obs.url,
            "headers": obs.headers,
            "body_len": 0,
            "body_sha256": "",
        } for obs in obs_list]
        baseline["payload"]["responses"] = _responses_from_observed(
            observed_list=obs_list,
            final_response=baseline["payload"]["response"],
            proto=proto,
        )
        baseline["payload"]["request"]["method"] = obs_list[0].method
        baseline["payload"]["request"]["url"] = obs_list[0].url
        baseline["payload"]["request"]["headers"] = obs_list[0].headers
        baseline["payload"]["response"]["status"] = obs_list[-1].status
        baseline["payload"]["response"]["http_version"] = proto
        baseline["payload"]["response"]["headers"] = obs_list[-1].response_headers
        write_json(baseline["path"], baseline["payload"])

        observe_log.write_text("", encoding="utf-8")
        qcurl_url = _append_req_id(url, qcurl_req_id)
        qcurl = run_qt_test(
            env=env,
            suite=suite,
            case=case_variant,
            qt_executable=qt_path,
            args=[],
            request_meta={"method": "GET", "url": qcurl_url, "headers": {}, "body": b""},
            response_meta=resp_meta,
            download_count=1,
            case_env={
                "QCURL_LC_CASE_ID": "p1_login_cookie_flow",
                "QCURL_LC_PROTO": proto,
                "QCURL_LC_REQ_ID": qcurl_req_id,
                "QCURL_LC_COOKIE_PATH": str(qcurl_cookie),
                "QCURL_LC_OBSERVE_HTTP_PORT": str(port),
            },
        )

        obs_list = observe_http_observed_list_for_id(observe_log, qcurl_req_id, expected_count=2)
        obs_list = _order_login_chain(obs_list)
        qcurl["payload"]["requests"] = [{
            "method": obs.method,
            "url": obs.url,
            "headers": obs.headers,
            "body_len": 0,
            "body_sha256": "",
        } for obs in obs_list]
        qcurl["payload"]["responses"] = _responses_from_observed(
            observed_list=obs_list,
            final_response=qcurl["payload"]["response"],
            proto=proto,
        )
        qcurl["payload"]["request"]["method"] = obs_list[0].method
        qcurl["payload"]["request"]["url"] = obs_list[0].url
        qcurl["payload"]["request"]["headers"] = obs_list[0].headers
        qcurl["payload"]["response"]["status"] = obs_list[-1].status
        qcurl["payload"]["response"]["http_version"] = proto
        qcurl["payload"]["response"]["headers"] = obs_list[-1].response_headers
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
                    "case_id": "p1_login_cookie_flow",
                    "case_variant": case_variant,
                    "proto": proto,
                    "baseline_req_id": baseline_req_id,
                    "qcurl_req_id": qcurl_req_id,
                    "observe_port": port,
                },
            )
        raise
