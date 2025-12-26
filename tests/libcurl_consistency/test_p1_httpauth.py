"""
P1：HTTPAUTH（挑战/协商）与 UNRESTRICTED_AUTH（跨 host/port 重定向）一致性。

覆盖缺口：
- QCurl 暴露请求级 HTTPAUTH 后，需要在“可观测数据层面”对齐 libcurl 行为：
  - ANY/ANYSAFE 的 401 挑战与二次请求（Authorization 头）
  - followLocation + UNRESTRICTED_AUTH 的跨 host/port 凭据发送策略

服务端：repo 内置 http_observe_server.py（/auth/*、/redir_abs、/abs_target）
基线：repo 内置 qcurl_lc_http_baseline（cli_lc_http）
QCurl：tst_LibcurlConsistency（新增 p1_httpauth_* / p1_unrestricted_auth_* 分支）
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
from tests.libcurl_consistency.pytest_support.artifacts import write_json


def _append_req_id(url: str, req_id: str) -> str:
    sep = "&" if "?" in url else "?"
    return f"{url}{sep}id={req_id}"


def _normalize_req_headers(headers: dict) -> dict:
    out = dict(headers or {})
    auth = out.get("authorization") or ""
    if isinstance(auth, str) and auth.lower().startswith("digest "):
        # Digest 头中包含随机 cnonce 等字段，跨进程不稳定；仅比较 scheme 即可验证 ANYSAFE 行为
        out["authorization"] = "Digest"
    return out


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


def test_p1_httpauth_any_basic(env, lc_observe_http):
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("QCURL_QTTEST 未设置或可执行不存在")

    collect_logs = should_collect_service_logs()
    port = int(lc_observe_http["port"])
    observe_log = Path(str(lc_observe_http["log_file"]))

    suite = "p1_httpauth"
    proto = "http/1.1"
    case_variant = "lc_httpauth_any_basic_http_1.1"

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_httpauth_any_basic"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    base_url = f"http://localhost:{port}/auth/basic"
    baseline_url = _append_req_id(base_url, baseline_req_id)
    qcurl_url = _append_req_id(base_url, qcurl_req_id)

    expected_requests = 2

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
                "--user",
                "user",
                "--pass",
                "passwd",
                "--httpauth",
                "any",
                baseline_url,
            ],
            request_meta={"method": "GET", "url": baseline_url, "headers": {}, "body": b""},
            response_meta={"status": 200, "http_version": proto, "headers": {}, "body": None},
            download_count=1,
        )

        obs_list = observe_http_observed_list_for_id(observe_log, baseline_req_id, expected_count=expected_requests)
        baseline["payload"]["requests"] = [{
            "method": obs.method,
            "url": obs.url,
            "headers": _normalize_req_headers(obs.headers),
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
        baseline["payload"]["request"]["headers"] = _normalize_req_headers(obs_list[0].headers)
        baseline["payload"]["response"]["status"] = obs_list[-1].status
        baseline["payload"]["response"]["http_version"] = proto
        baseline["payload"]["response"]["headers"] = dict(obs_list[-1].response_headers)
        write_json(baseline["path"], baseline["payload"])

        observe_log.write_text("", encoding="utf-8")
        qcurl = run_qt_test(
            env=env,
            suite=suite,
            case=case_variant,
            qt_executable=qt_path,
            args=[],
            request_meta={"method": "GET", "url": qcurl_url, "headers": {}, "body": b""},
            response_meta={"status": 200, "http_version": proto, "headers": {}, "body": None},
            download_count=1,
            case_env={
                "QCURL_LC_CASE_ID": "p1_httpauth_any_basic",
                "QCURL_LC_PROTO": proto,
                "QCURL_LC_REQ_ID": qcurl_req_id,
                "QCURL_LC_TARGET_URL": base_url,
                "QCURL_LC_AUTH_USER": "user",
                "QCURL_LC_AUTH_PASS": "passwd",
            },
        )

        obs_list = observe_http_observed_list_for_id(observe_log, qcurl_req_id, expected_count=expected_requests)
        qcurl["payload"]["requests"] = [{
            "method": obs.method,
            "url": obs.url,
            "headers": _normalize_req_headers(obs.headers),
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
        qcurl["payload"]["request"]["headers"] = _normalize_req_headers(obs_list[0].headers)
        qcurl["payload"]["response"]["status"] = obs_list[-1].status
        qcurl["payload"]["response"]["http_version"] = proto
        qcurl["payload"]["response"]["headers"] = dict(obs_list[-1].response_headers)
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
                    "case_id": "p1_httpauth_any_basic",
                    "proto": proto,
                    "baseline_req_id": baseline_req_id,
                    "qcurl_req_id": qcurl_req_id,
                    "observe_port": port,
                },
            )
        raise


def test_p1_httpauth_anysafe_digest(env, lc_observe_http):
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("QCURL_QTTEST 未设置或可执行不存在")

    collect_logs = should_collect_service_logs()
    port = int(lc_observe_http["port"])
    observe_log = Path(str(lc_observe_http["log_file"]))

    suite = "p1_httpauth"
    proto = "http/1.1"
    case_variant = "lc_httpauth_anysafe_digest_http_1.1"

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_httpauth_anysafe_digest"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    base_url = f"http://localhost:{port}/auth/digest"
    baseline_url = _append_req_id(base_url, baseline_req_id)
    qcurl_url = _append_req_id(base_url, qcurl_req_id)

    expected_requests = 2

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
                "--user",
                "user",
                "--pass",
                "passwd",
                "--httpauth",
                "anysafe",
                baseline_url,
            ],
            request_meta={"method": "GET", "url": baseline_url, "headers": {}, "body": b""},
            response_meta={"status": 200, "http_version": proto, "headers": {}, "body": None},
            download_count=1,
        )

        obs_list = observe_http_observed_list_for_id(observe_log, baseline_req_id, expected_count=expected_requests)
        baseline["payload"]["requests"] = [{
            "method": obs.method,
            "url": obs.url,
            "headers": _normalize_req_headers(obs.headers),
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
        baseline["payload"]["request"]["headers"] = _normalize_req_headers(obs_list[0].headers)
        baseline["payload"]["response"]["status"] = obs_list[-1].status
        baseline["payload"]["response"]["http_version"] = proto
        baseline["payload"]["response"]["headers"] = dict(obs_list[-1].response_headers)
        write_json(baseline["path"], baseline["payload"])

        observe_log.write_text("", encoding="utf-8")
        qcurl = run_qt_test(
            env=env,
            suite=suite,
            case=case_variant,
            qt_executable=qt_path,
            args=[],
            request_meta={"method": "GET", "url": qcurl_url, "headers": {}, "body": b""},
            response_meta={"status": 200, "http_version": proto, "headers": {}, "body": None},
            download_count=1,
            case_env={
                "QCURL_LC_CASE_ID": "p1_httpauth_anysafe_digest",
                "QCURL_LC_PROTO": proto,
                "QCURL_LC_REQ_ID": qcurl_req_id,
                "QCURL_LC_TARGET_URL": base_url,
                "QCURL_LC_AUTH_USER": "user",
                "QCURL_LC_AUTH_PASS": "passwd",
            },
        )

        obs_list = observe_http_observed_list_for_id(observe_log, qcurl_req_id, expected_count=expected_requests)
        qcurl["payload"]["requests"] = [{
            "method": obs.method,
            "url": obs.url,
            "headers": _normalize_req_headers(obs.headers),
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
        qcurl["payload"]["request"]["headers"] = _normalize_req_headers(obs_list[0].headers)
        qcurl["payload"]["response"]["status"] = obs_list[-1].status
        qcurl["payload"]["response"]["http_version"] = proto
        qcurl["payload"]["response"]["headers"] = dict(obs_list[-1].response_headers)
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
                    "case_id": "p1_httpauth_anysafe_digest",
                    "proto": proto,
                    "baseline_req_id": baseline_req_id,
                    "qcurl_req_id": qcurl_req_id,
                    "observe_port": port,
                },
            )
        raise


@pytest.mark.parametrize("unrestricted", [False, True])
def test_p1_unrestricted_auth_redirect(unrestricted: bool, env, lc_observe_http_pair):
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("QCURL_QTTEST 未设置或可执行不存在")

    collect_logs = should_collect_service_logs()
    a = lc_observe_http_pair["a"]
    b = lc_observe_http_pair["b"]
    port_a = int(a["port"])
    port_b = int(b["port"])
    log_a = Path(str(a["log_file"]))
    log_b = Path(str(b["log_file"]))

    suite = "p1_httpauth"
    proto = "http/1.1"
    case_variant = f"lc_unrestricted_auth_redirect_{'on' if unrestricted else 'off'}_http_1.1"
    case_id = f"p1_unrestricted_auth_redirect_{'on' if unrestricted else 'off'}"

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_unrestricted_{'1' if unrestricted else '0'}"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    base_url = f"http://localhost:{port_a}/redir_abs?to_port={port_b}"
    baseline_url = _append_req_id(base_url, baseline_req_id)
    qcurl_url = _append_req_id(base_url, qcurl_req_id)

    try:
        log_a.write_text("", encoding="utf-8")
        log_b.write_text("", encoding="utf-8")
        baseline_args = [
            "-V",
            proto,
            "--follow",
            "--user",
            "user",
            "--pass",
            "passwd",
            "--httpauth",
            "basic",
        ]
        if unrestricted:
            baseline_args.append("--unrestricted-auth")
        baseline_args.append(baseline_url)

        baseline = run_libtest_case(
            env=env,
            suite=suite,
            case=case_variant,
            client_name="cli_lc_http",
            args=baseline_args,
            request_meta={"method": "GET", "url": baseline_url, "headers": {}, "body": b""},
            response_meta={"status": 200, "http_version": proto, "headers": {}, "body": None},
            download_count=1,
        )

        obs_a = observe_http_observed_list_for_id(log_a, baseline_req_id, expected_count=1)
        obs_b = observe_http_observed_list_for_id(log_b, baseline_req_id, expected_count=1)
        obs_list = [obs_a[0], obs_b[0]]

        if unrestricted:
            assert "authorization" in obs_b[0].headers
        else:
            assert "authorization" not in obs_b[0].headers

        baseline["payload"]["requests"] = [{
            "method": obs.method,
            "url": obs.url,
            "headers": _normalize_req_headers(obs.headers),
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
        baseline["payload"]["request"]["headers"] = _normalize_req_headers(obs_list[0].headers)
        baseline["payload"]["response"]["status"] = obs_list[-1].status
        baseline["payload"]["response"]["http_version"] = proto
        baseline["payload"]["response"]["headers"] = dict(obs_list[-1].response_headers)
        write_json(baseline["path"], baseline["payload"])

        log_a.write_text("", encoding="utf-8")
        log_b.write_text("", encoding="utf-8")
        qcurl = run_qt_test(
            env=env,
            suite=suite,
            case=case_variant,
            qt_executable=qt_path,
            args=[],
            request_meta={"method": "GET", "url": qcurl_url, "headers": {}, "body": b""},
            response_meta={"status": 200, "http_version": proto, "headers": {}, "body": None},
            download_count=1,
            case_env={
                "QCURL_LC_CASE_ID": case_id,
                "QCURL_LC_PROTO": proto,
                "QCURL_LC_REQ_ID": qcurl_req_id,
                "QCURL_LC_TARGET_URL": base_url,
                "QCURL_LC_AUTH_USER": "user",
                "QCURL_LC_AUTH_PASS": "passwd",
            },
        )

        obs_a = observe_http_observed_list_for_id(log_a, qcurl_req_id, expected_count=1)
        obs_b = observe_http_observed_list_for_id(log_b, qcurl_req_id, expected_count=1)
        obs_list = [obs_a[0], obs_b[0]]

        qcurl["payload"]["requests"] = [{
            "method": obs.method,
            "url": obs.url,
            "headers": _normalize_req_headers(obs.headers),
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
        qcurl["payload"]["request"]["headers"] = _normalize_req_headers(obs_list[0].headers)
        qcurl["payload"]["response"]["status"] = obs_list[-1].status
        qcurl["payload"]["response"]["http_version"] = proto
        qcurl["payload"]["response"]["headers"] = dict(obs_list[-1].response_headers)
        write_json(qcurl["path"], qcurl["payload"])

        assert_artifacts_match(baseline["path"], qcurl["path"])
    except Exception:
        if collect_logs:
            collect_service_logs_for_case(
                env,
                suite=suite,
                case=case_variant,
                logs={"observe_http_a_log": log_a, "observe_http_b_log": log_b},
                meta={
                    "case_id": case_id,
                    "proto": proto,
                    "baseline_req_id": baseline_req_id,
                    "qcurl_req_id": qcurl_req_id,
                    "observe_port_a": port_a,
                    "observe_port_b": port_b,
                    "unrestricted": unrestricted,
                },
            )
        raise

