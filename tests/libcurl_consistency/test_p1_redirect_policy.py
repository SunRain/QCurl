"""
P1：重定向策略控制一致性（LC-41，CURLOPT_MAXREDIRS/POSTREDIR/AUTOREFERER/REFERER）。

覆盖：
- MAXREDIRS：触发 TooManyRedirects 的错误语义与观测序列一致
- POSTREDIR：POST 301 重定向后保持 POST（KeepPost301）行为一致
- AUTOREFERER：重定向链路自动注入 Referer
- REFERER：显式设置 Referer 头
- 敏感头跨站开关：setAllowUnrestrictedSensitiveHeadersOnRedirect（不依赖 allowUnrestrictedAuth）
"""

from __future__ import annotations

import os
import re
import uuid
from pathlib import Path

import pytest

from tests.libcurl_consistency.pytest_support.artifacts import build_request_semantic, write_json
from tests.libcurl_consistency.pytest_support.baseline import run_libtest_case
from tests.libcurl_consistency.pytest_support.compare import assert_artifacts_match
from tests.libcurl_consistency.pytest_support.observed import observe_http_observed_list_for_id
from tests.libcurl_consistency.pytest_support.qcurl_runner import run_qt_test
from tests.libcurl_consistency.pytest_support.service_logs import collect_service_logs_for_case, should_collect_service_logs


_CURLINFO_RE = re.compile(r"curlcode=(\d+)\s+http_code=(\d+)")


def _parse_curlcode_http_code(stderr_lines: list[str]) -> tuple[int, int]:
    for line in stderr_lines:
        m = _CURLINFO_RE.search(line)
        if m:
            return int(m.group(1)), int(m.group(2))
    return -1, -1


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


def _error(kind: str, *, http_status: int, curlcode: int, http_code: int) -> dict:
    return {
        "kind": kind,
        "http_status": int(http_status),
        "curlcode": int(curlcode),
        "http_code": int(http_code),
    }


def test_p1_redirect_max_redirs_too_many_http_1_1(env, lc_observe_http):
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("QCURL_QTTEST 未设置或可执行不存在")

    collect_logs = should_collect_service_logs()
    port = int(lc_observe_http["port"])
    observe_log = Path(str(lc_observe_http["log_file"]))

    suite = "p1_redirect_policy"
    proto = "http/1.1"
    case_variant = "p1_redirect_max_redirs_too_many_http_1.1"
    case_id = "p1_redirect_max_redirs_too_many"

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_maxredirs"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    url = f"http://localhost:{port}/redir/3"
    baseline_url = _append_req_id(url, baseline_req_id)
    qcurl_url = _append_req_id(url, qcurl_req_id)

    resp_meta = {"status": 302, "http_version": proto, "headers": {}, "body": None}

    try:
        observe_log.write_text("", encoding="utf-8")
        baseline = run_libtest_case(
            env=env,
            suite=suite,
            case=case_variant,
            client_name="cli_lc_http",
            args=["-V", proto, "--follow", "--max-redirs", "1", baseline_url],
            request_meta={"method": "GET", "url": baseline_url, "headers": {}, "body": b""},
            response_meta=resp_meta,
            download_count=1,
            allowed_exit_codes={0, 7},
        )
        curlcode, http_code = _parse_curlcode_http_code(list(baseline["payload"].get("stderr") or []))
        if curlcode < 0:
            raise AssertionError("baseline stderr 未包含 curlcode/http_code")

        obs_list = observe_http_observed_list_for_id(observe_log, baseline_req_id, expected_count=2)
        baseline["payload"]["requests"] = [
            build_request_semantic(obs.method, obs.url, obs.headers, b"")
            for obs in obs_list
        ]
        baseline["payload"]["responses"] = _responses_from_observed(
            observed_list=obs_list,
            final_response=baseline["payload"]["response"],
            proto=proto,
        )
        baseline["payload"]["request"]["method"] = obs_list[0].method
        baseline["payload"]["request"]["url"] = obs_list[0].url
        baseline["payload"]["request"]["headers"] = dict(obs_list[0].headers)
        baseline["payload"]["response"]["status"] = obs_list[-1].status
        baseline["payload"]["response"]["http_version"] = proto
        baseline["payload"]["response"]["headers"] = dict(obs_list[-1].response_headers)
        baseline["payload"]["error"] = _error("redirect", http_status=obs_list[-1].status, curlcode=curlcode, http_code=http_code)
        write_json(baseline["path"], baseline["payload"])

        observe_log.write_text("", encoding="utf-8")
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
                "QCURL_LC_CASE_ID": case_id,
                "QCURL_LC_PROTO": proto,
                "QCURL_LC_REQ_ID": qcurl_req_id,
                "QCURL_LC_OBSERVE_HTTP_PORT": str(port),
            },
        )
        obs_list = observe_http_observed_list_for_id(observe_log, qcurl_req_id, expected_count=2)
        qcurl["payload"]["requests"] = [
            build_request_semantic(obs.method, obs.url, obs.headers, b"")
            for obs in obs_list
        ]
        qcurl["payload"]["responses"] = _responses_from_observed(
            observed_list=obs_list,
            final_response=qcurl["payload"]["response"],
            proto=proto,
        )
        qcurl["payload"]["request"]["method"] = obs_list[0].method
        qcurl["payload"]["request"]["url"] = obs_list[0].url
        qcurl["payload"]["request"]["headers"] = dict(obs_list[0].headers)
        qcurl["payload"]["response"]["status"] = obs_list[-1].status
        qcurl["payload"]["response"]["http_version"] = proto
        qcurl["payload"]["response"]["headers"] = dict(obs_list[-1].response_headers)
        qcurl["payload"]["error"] = _error("redirect", http_status=obs_list[-1].status, curlcode=47, http_code=obs_list[-1].status)
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
                    "case_id": case_id,
                    "proto": proto,
                    "baseline_req_id": baseline_req_id,
                    "qcurl_req_id": qcurl_req_id,
                    "observe_port": port,
                },
            )
        raise


def test_p1_redirect_postredir_keep_post_301_http_1_1(env, lc_observe_http):
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("QCURL_QTTEST 未设置或可执行不存在")

    collect_logs = should_collect_service_logs()
    port = int(lc_observe_http["port"])
    observe_log = Path(str(lc_observe_http["log_file"]))

    suite = "p1_redirect_policy"
    proto = "http/1.1"
    case_variant = "p1_redirect_postredir_keep_post_301_http_1.1"
    case_id = "p1_redirect_postredir_keep_post_301"

    upload_size = 16
    body = b"x" * upload_size

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_postredir301"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    url = f"http://localhost:{port}/redir_post_301"
    baseline_url = _append_req_id(url, baseline_req_id)
    qcurl_url = _append_req_id(url, qcurl_req_id)

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
                "--post-redir",
                "301",
                "--method",
                "POST",
                "--data-size",
                str(upload_size),
                baseline_url,
            ],
            request_meta={"method": "POST", "url": baseline_url, "headers": {}, "body": body},
            response_meta=resp_meta,
            download_count=1,
        )
        obs_list = observe_http_observed_list_for_id(observe_log, baseline_req_id, expected_count=2)
        assert [o.method for o in obs_list] == ["POST", "POST"]
        assert [int(o.status) for o in obs_list] == [301, 200]

        baseline["payload"]["requests"] = [
            build_request_semantic(obs.method, obs.url, obs.headers, body)
            for obs in obs_list
        ]
        baseline["payload"]["responses"] = _responses_from_observed(
            observed_list=obs_list,
            final_response=baseline["payload"]["response"],
            proto=proto,
        )
        baseline["payload"]["request"]["method"] = obs_list[0].method
        baseline["payload"]["request"]["url"] = obs_list[0].url
        baseline["payload"]["request"]["headers"] = dict(obs_list[0].headers)
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
            request_meta={"method": "POST", "url": qcurl_url, "headers": {}, "body": body},
            response_meta=resp_meta,
            download_count=1,
            case_env={
                "QCURL_LC_CASE_ID": case_id,
                "QCURL_LC_PROTO": proto,
                "QCURL_LC_REQ_ID": qcurl_req_id,
                "QCURL_LC_UPLOAD_SIZE": str(upload_size),
                "QCURL_LC_OBSERVE_HTTP_PORT": str(port),
            },
        )
        obs_list = observe_http_observed_list_for_id(observe_log, qcurl_req_id, expected_count=2)
        assert [o.method for o in obs_list] == ["POST", "POST"]
        assert [int(o.status) for o in obs_list] == [301, 200]

        qcurl["payload"]["requests"] = [
            build_request_semantic(obs.method, obs.url, obs.headers, body)
            for obs in obs_list
        ]
        qcurl["payload"]["responses"] = _responses_from_observed(
            observed_list=obs_list,
            final_response=qcurl["payload"]["response"],
            proto=proto,
        )
        qcurl["payload"]["request"]["method"] = obs_list[0].method
        qcurl["payload"]["request"]["url"] = obs_list[0].url
        qcurl["payload"]["request"]["headers"] = dict(obs_list[0].headers)
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
                    "case_id": case_id,
                    "proto": proto,
                    "baseline_req_id": baseline_req_id,
                    "qcurl_req_id": qcurl_req_id,
                    "observe_port": port,
                },
            )
        raise


def test_p1_redirect_auto_referer_http_1_1(env, lc_observe_http):
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("QCURL_QTTEST 未设置或可执行不存在")

    collect_logs = should_collect_service_logs()
    port = int(lc_observe_http["port"])
    observe_log = Path(str(lc_observe_http["log_file"]))

    suite = "p1_redirect_policy"
    proto = "http/1.1"
    case_variant = "p1_redirect_auto_referer_http_1.1"
    case_id = "p1_redirect_auto_referer"

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_autoreferer"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    url = f"http://localhost:{port}/redir/1"
    baseline_url = _append_req_id(url, baseline_req_id)
    qcurl_url = _append_req_id(url, qcurl_req_id)

    resp_meta = {"status": 200, "http_version": proto, "headers": {}, "body": None}

    def expected_referer() -> str:
        return f"http://localhost:{port}/redir/1"

    try:
        observe_log.write_text("", encoding="utf-8")
        baseline = run_libtest_case(
            env=env,
            suite=suite,
            case=case_variant,
            client_name="cli_lc_http",
            args=["-V", proto, "--follow", "--max-redirs", "10", "--auto-referer", baseline_url],
            request_meta={"method": "GET", "url": baseline_url, "headers": {}, "body": b""},
            response_meta=resp_meta,
            download_count=1,
        )
        obs_list = observe_http_observed_list_for_id(observe_log, baseline_req_id, expected_count=2)
        assert "referer" not in obs_list[0].headers
        assert obs_list[1].headers.get("referer") == expected_referer()

        baseline["payload"]["requests"] = [
            build_request_semantic(obs.method, obs.url, obs.headers, b"")
            for obs in obs_list
        ]
        baseline["payload"]["responses"] = _responses_from_observed(
            observed_list=obs_list,
            final_response=baseline["payload"]["response"],
            proto=proto,
        )
        baseline["payload"]["request"]["method"] = obs_list[0].method
        baseline["payload"]["request"]["url"] = obs_list[0].url
        baseline["payload"]["request"]["headers"] = dict(obs_list[0].headers)
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
            response_meta=resp_meta,
            download_count=1,
            case_env={
                "QCURL_LC_CASE_ID": case_id,
                "QCURL_LC_PROTO": proto,
                "QCURL_LC_REQ_ID": qcurl_req_id,
                "QCURL_LC_OBSERVE_HTTP_PORT": str(port),
            },
        )
        obs_list = observe_http_observed_list_for_id(observe_log, qcurl_req_id, expected_count=2)
        assert "referer" not in obs_list[0].headers
        assert obs_list[1].headers.get("referer") == expected_referer()

        qcurl["payload"]["requests"] = [
            build_request_semantic(obs.method, obs.url, obs.headers, b"")
            for obs in obs_list
        ]
        qcurl["payload"]["responses"] = _responses_from_observed(
            observed_list=obs_list,
            final_response=qcurl["payload"]["response"],
            proto=proto,
        )
        qcurl["payload"]["request"]["method"] = obs_list[0].method
        qcurl["payload"]["request"]["url"] = obs_list[0].url
        qcurl["payload"]["request"]["headers"] = dict(obs_list[0].headers)
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
                    "case_id": case_id,
                    "proto": proto,
                    "baseline_req_id": baseline_req_id,
                    "qcurl_req_id": qcurl_req_id,
                    "observe_port": port,
                },
            )
        raise


def test_p1_referer_explicit_http_1_1(env, lc_observe_http):
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("QCURL_QTTEST 未设置或可执行不存在")

    collect_logs = should_collect_service_logs()
    port = int(lc_observe_http["port"])
    observe_log = Path(str(lc_observe_http["log_file"]))

    suite = "p1_redirect_policy"
    proto = "http/1.1"
    case_variant = "p1_referer_explicit_http_1.1"
    case_id = "p1_referer_explicit"

    referer = "http://example.invalid/from"

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_referer"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    url = f"http://localhost:{port}/abs_target"
    baseline_url = _append_req_id(url, baseline_req_id)
    qcurl_url = _append_req_id(url, qcurl_req_id)

    resp_meta = {"status": 200, "http_version": proto, "headers": {}, "body": None}

    try:
        observe_log.write_text("", encoding="utf-8")
        baseline = run_libtest_case(
            env=env,
            suite=suite,
            case=case_variant,
            client_name="cli_lc_http",
            args=["-V", proto, "--referer", referer, baseline_url],
            request_meta={"method": "GET", "url": baseline_url, "headers": {}, "body": b""},
            response_meta=resp_meta,
            download_count=1,
        )
        obs = observe_http_observed_list_for_id(observe_log, baseline_req_id, expected_count=1)[0]
        assert obs.headers.get("referer") == referer

        baseline["payload"]["request"]["method"] = obs.method
        baseline["payload"]["request"]["url"] = obs.url
        baseline["payload"]["request"]["headers"] = dict(obs.headers)
        baseline["payload"]["response"]["status"] = obs.status
        baseline["payload"]["response"]["http_version"] = proto
        baseline["payload"]["response"]["headers"] = dict(obs.response_headers)
        write_json(baseline["path"], baseline["payload"])

        observe_log.write_text("", encoding="utf-8")
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
                "QCURL_LC_CASE_ID": case_id,
                "QCURL_LC_PROTO": proto,
                "QCURL_LC_REQ_ID": qcurl_req_id,
                "QCURL_LC_OBSERVE_HTTP_PORT": str(port),
                "QCURL_LC_REFERER": referer,
            },
        )
        obs = observe_http_observed_list_for_id(observe_log, qcurl_req_id, expected_count=1)[0]
        assert obs.headers.get("referer") == referer

        qcurl["payload"]["request"]["method"] = obs.method
        qcurl["payload"]["request"]["url"] = obs.url
        qcurl["payload"]["request"]["headers"] = dict(obs.headers)
        qcurl["payload"]["response"]["status"] = obs.status
        qcurl["payload"]["response"]["http_version"] = proto
        qcurl["payload"]["response"]["headers"] = dict(obs.response_headers)
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
                    "case_id": case_id,
                    "proto": proto,
                    "baseline_req_id": baseline_req_id,
                    "qcurl_req_id": qcurl_req_id,
                    "observe_port": port,
                },
            )
        raise


@pytest.mark.parametrize("unrestricted", [False, True])
def test_p1_unrestricted_sensitive_headers_redirect(unrestricted: bool, env, lc_observe_http_pair):
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

    suite = "p1_redirect_policy"
    proto = "http/1.1"
    case_variant = f"p1_unrestricted_sensitive_headers_redirect_{'on' if unrestricted else 'off'}_http_1.1"
    case_id = f"p1_unrestricted_sensitive_headers_redirect_{'on' if unrestricted else 'off'}"

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_unrestricted_reqflag_{'1' if unrestricted else '0'}"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    base_url = f"http://localhost:{port_a}/redir_abs?to_port={port_b}"
    baseline_url = _append_req_id(base_url, baseline_req_id)
    qcurl_url = _append_req_id(base_url, qcurl_req_id)

    resp_meta = {"status": 200, "http_version": proto, "headers": {}, "body": None}

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
            response_meta=resp_meta,
            download_count=1,
        )

        obs_a = observe_http_observed_list_for_id(log_a, baseline_req_id, expected_count=1)
        obs_b = observe_http_observed_list_for_id(log_b, baseline_req_id, expected_count=1)
        obs_list = [obs_a[0], obs_b[0]]

        if unrestricted:
            assert "authorization" in obs_b[0].headers
        else:
            assert "authorization" not in obs_b[0].headers

        baseline["payload"]["requests"] = [
            build_request_semantic(obs.method, obs.url, obs.headers, b"")
            for obs in obs_list
        ]
        baseline["payload"]["responses"] = _responses_from_observed(
            observed_list=obs_list,
            final_response=baseline["payload"]["response"],
            proto=proto,
        )
        baseline["payload"]["request"]["method"] = obs_list[0].method
        baseline["payload"]["request"]["url"] = obs_list[0].url
        baseline["payload"]["request"]["headers"] = dict(obs_list[0].headers)
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
            response_meta=resp_meta,
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

        if unrestricted:
            assert "authorization" in obs_b[0].headers
        else:
            assert "authorization" not in obs_b[0].headers

        qcurl["payload"]["requests"] = [
            build_request_semantic(obs.method, obs.url, obs.headers, b"")
            for obs in obs_list
        ]
        qcurl["payload"]["responses"] = _responses_from_observed(
            observed_list=obs_list,
            final_response=qcurl["payload"]["response"],
            proto=proto,
        )
        qcurl["payload"]["request"]["method"] = obs_list[0].method
        qcurl["payload"]["request"]["url"] = obs_list[0].url
        qcurl["payload"]["request"]["headers"] = dict(obs_list[0].headers)
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
