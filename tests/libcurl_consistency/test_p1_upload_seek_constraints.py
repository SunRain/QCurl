"""
P1：带 body 请求的可重发约束。

seekable body 应允许重发；non-seekable body 应显式失败，不得静默降级。
"""

from __future__ import annotations

import os
import re
import uuid
from pathlib import Path

import pytest

from tests.libcurl_consistency.pytest_support.baseline import run_libtest_case
from tests.libcurl_consistency.pytest_support.compare import assert_artifacts_match
from tests.libcurl_consistency.pytest_support.observed import observe_http_observed_list_for_id
from tests.libcurl_consistency.pytest_support.qcurl_runner import run_qt_test
from tests.libcurl_consistency.pytest_support.service_logs import collect_service_logs_for_case, should_collect_service_logs
from tests.libcurl_consistency.pytest_support.artifacts import apply_error_namespaces, sha256_bytes, write_json


_REPO_ROOT = Path(__file__).resolve().parents[2]
_CURLINFO_RE = re.compile(r"curlcode=(\d+)\s+http_code=(\d+)")


def _has_m2_upload_device_api() -> bool:
    """
    以源码为准的能力门控：
    - 当前构建未提供 uploadDevice API 时跳过本模块
    - 构建提供该 API 后自动启用，无需额外环境开关
    """
    header = _REPO_ROOT / "src" / "QCNetworkRequest.h"
    try:
        text = header.read_text(encoding="utf-8", errors="replace")
    except Exception:
        return False
    return "setUploadDevice" in text


if not _has_m2_upload_device_api():
    pytest.skip("当前构建未提供 uploadDevice API，跳过 seek/重发约束一致性用例", allow_module_level=True)


def _normalize_req_headers(headers: dict) -> dict:
    out: dict = {}
    for name in ("host", "content-length", "authorization", "expect"):
        v = headers.get(name)
        if v:
            out[name] = v
    return out


def _response_sha_empty() -> str:
    return sha256_bytes(b"")


def _parse_curlcode_http_code(stderr_lines: list[str]) -> tuple[int, int]:
    for line in stderr_lines or []:
        m = _CURLINFO_RE.search(str(line))
        if m:
            return int(m.group(1)), int(m.group(2))
    return -1, -1


@pytest.mark.parametrize("seekable", [False, True])
@pytest.mark.parametrize("method", ["POST", "PUT"])
def test_p1_stream_body_redirect_307_replay(seekable: bool, method: str, env, lc_observe_http):
    """
    307 redirect（保持 method/body）会触发 body 重发：
    - seekable：应成功到达 /method 并回显 body
    - non-seekable：应明确失败（无法重发）
    """
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("当前环境未提供 QCURL_QTTEST 可执行文件，跳过该用例")

    collect_logs = should_collect_service_logs()
    port = int(lc_observe_http["port"])
    observe_log = Path(str(lc_observe_http["log_file"]))

    suite = "p1_upload_seek"
    proto = "http/1.1"
    upload_size = 4096
    flavor = "seekable" if seekable else "nonseekable"
    case_variant = f"lc_stream_body_redirect_307_{method.lower()}_{flavor}_http_1.1"
    case_id = f"p1_stream_body_redirect_307_{method.lower()}_{flavor}"

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_redir307_{method.lower()}_{'s' if seekable else 'n'}"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    base_url = f"http://localhost:{port}/redir_307"
    baseline_url = f"{base_url}?id={baseline_req_id}"
    qcurl_url = f"{base_url}?id={qcurl_req_id}"

    body = b"x" * upload_size
    expect_success = bool(seekable)
    expected_count = 2 if expect_success else 1  # /redir_307 -> /method

    try:
        observe_log.write_text("", encoding="utf-8")
        baseline_args = [
            "-V",
            proto,
            "--method",
            method,
            "--data-size",
            str(upload_size),
            "--stream-body",
            "--follow",
        ]
        if seekable:
            baseline_args.append("--seekable-body")
        baseline_args.append(baseline_url)

        baseline = run_libtest_case(
            env=env,
            suite=suite,
            case=case_variant,
            client_name="cli_lc_http",
            args=baseline_args,
            request_meta={"method": method, "url": baseline_url, "headers": {}, "body": body},
            response_meta={"status": 200 if expect_success else 0, "http_version": proto, "headers": {}, "body": None},
            download_count=1,
            allowed_exit_codes={0, 7},
        )
        obs_base = observe_http_observed_list_for_id(observe_log, baseline_req_id, expected_count=expected_count)
        baseline["payload"]["requests"] = [{
            "method": obs.method,
            "url": obs.url,
            "headers": _normalize_req_headers(obs.headers),
            "body_len": len(body),
            "body_sha256": sha256_bytes(body),
        } for obs in obs_base]
        baseline["payload"]["responses"] = []
        for idx, obs in enumerate(obs_base):
            if idx < len(obs_base) - 1:
                baseline["payload"]["responses"].append({
                    "status": obs.status,
                    "http_version": proto,
                    "headers": dict(obs.response_headers),
                    "body_len": 0,
                    "body_sha256": _response_sha_empty(),
                })
            else:
                baseline["payload"]["responses"].append({
                    **baseline["payload"]["response"],
                    "status": obs.status if expect_success else 0,
                    "http_version": proto,
                    "headers": dict(obs.response_headers),
                })
        baseline["payload"]["request"]["method"] = obs_base[0].method
        baseline["payload"]["request"]["url"] = obs_base[0].url
        baseline["payload"]["request"]["headers"] = _normalize_req_headers(obs_base[0].headers)
        baseline["payload"]["response"]["status"] = obs_base[-1].status if expect_success else 0
        baseline["payload"]["response"]["http_version"] = proto
        baseline["payload"]["response"]["headers"] = dict(obs_base[-1].response_headers) if expect_success else {}
        write_json(baseline["path"], baseline["payload"])

        observe_log.write_text("", encoding="utf-8")
        qcurl = run_qt_test(
            env=env,
            suite=suite,
            case=case_variant,
            qt_executable=qt_path,
            args=[],
            request_meta={"method": method, "url": qcurl_url, "headers": {}, "body": body},
            response_meta={"status": 200 if expect_success else 0, "http_version": proto, "headers": {}, "body": None},
            download_count=1,
            case_env={
                "QCURL_LC_CASE_ID": case_id,
                "QCURL_LC_PROTO": proto,
                "QCURL_LC_OBSERVE_HTTP_PORT": str(port),
                "QCURL_LC_REQ_ID": qcurl_req_id,
            },
        )
        obs_q = observe_http_observed_list_for_id(observe_log, qcurl_req_id, expected_count=expected_count)
        qcurl["payload"]["requests"] = [{
            "method": obs.method,
            "url": obs.url,
            "headers": _normalize_req_headers(obs.headers),
            "body_len": len(body),
            "body_sha256": sha256_bytes(body),
        } for obs in obs_q]
        qcurl["payload"]["responses"] = []
        for idx, obs in enumerate(obs_q):
            if idx < len(obs_q) - 1:
                qcurl["payload"]["responses"].append({
                    "status": obs.status,
                    "http_version": proto,
                    "headers": dict(obs.response_headers),
                    "body_len": 0,
                    "body_sha256": _response_sha_empty(),
                })
            else:
                qcurl["payload"]["responses"].append({
                    **qcurl["payload"]["response"],
                    "status": obs.status if expect_success else 0,
                    "http_version": proto,
                    "headers": dict(obs.response_headers),
                })
        qcurl["payload"]["request"]["method"] = obs_q[0].method
        qcurl["payload"]["request"]["url"] = obs_q[0].url
        qcurl["payload"]["request"]["headers"] = _normalize_req_headers(obs_q[0].headers)
        qcurl["payload"]["response"]["status"] = obs_q[-1].status if expect_success else 0
        qcurl["payload"]["response"]["http_version"] = proto
        qcurl["payload"]["response"]["headers"] = dict(obs_q[-1].response_headers) if expect_success else {}
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
                    "method": method,
                    "seekable": seekable,
                },
            )
        raise


@pytest.mark.parametrize("seekable", [False, True])
def test_p1_stream_body_httpauth_anysafe_digest_replay(seekable: bool, env, lc_observe_http):
    """
    401 challenge（Digest/AnySafe）会触发同一请求体重发：
    - seekable：应通过 challenge 并成功回显 body
    - non-seekable：应明确失败（无法重发）
    """
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("当前环境未提供 QCURL_QTTEST 可执行文件，跳过该用例")

    collect_logs = should_collect_service_logs()
    port = int(lc_observe_http["port"])
    observe_log = Path(str(lc_observe_http["log_file"]))

    suite = "p1_upload_seek"
    proto = "http/1.1"
    upload_size = 4096
    flavor = "seekable" if seekable else "nonseekable"
    case_variant = f"lc_stream_body_httpauth_anysafe_digest_post_{flavor}_http_1.1"
    case_id = f"p1_stream_body_httpauth_anysafe_digest_post_{flavor}"

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_authdigest_post_{'s' if seekable else 'n'}"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    base_url = f"http://localhost:{port}/auth/digest"
    baseline_url = f"{base_url}?id={baseline_req_id}"
    qcurl_url = f"{base_url}?id={qcurl_req_id}"

    body = b"x" * upload_size
    expect_success = bool(seekable)
    expected_count = 2 if expect_success else 1  # 401 -> 200（或 non-seekable 失败止于首次）
    replay_error = {"kind": "body_replay_not_possible", "http_status": 401}

    try:
        observe_log.write_text("", encoding="utf-8")
        resp_status = 200 if expect_success else 401
        baseline_args = [
            "-V",
            proto,
            "--method",
            "POST",
            "--data-size",
            str(upload_size),
            "--stream-body",
            "--user",
            "user",
            "--pass",
            "passwd",
            "--httpauth",
            "anysafe",
        ]
        if seekable:
            baseline_args.append("--seekable-body")
        baseline_args.append(baseline_url)

        baseline = run_libtest_case(
            env=env,
            suite=suite,
            case=case_variant,
            client_name="cli_lc_http",
            args=baseline_args,
            request_meta={"method": "POST", "url": baseline_url, "headers": {}, "body": body},
            response_meta={"status": resp_status, "http_version": proto, "headers": {}, "body": None},
            download_count=1,
            allowed_exit_codes={0, 7},
        )
        curlcode, http_code = _parse_curlcode_http_code(list(baseline["payload"].get("stderr") or []))
        if curlcode < 0:
            raise AssertionError("baseline stderr 未包含 curlcode/http_code")
        if expect_success:
            if curlcode != 0 or http_code != 200:
                raise AssertionError(f"baseline unexpected curlcode/http_code: {curlcode}/{http_code} (want 0/200)")
        else:
            # libcurl 在无法 rewind body 时返回 CURLE_SEND_FAIL_REWIND（=65）
            if curlcode != 65 or http_code != 401:
                raise AssertionError(f"baseline unexpected curlcode/http_code: {curlcode}/{http_code} (want 65/401)")

        obs_base = observe_http_observed_list_for_id(observe_log, baseline_req_id, expected_count=expected_count)
        if expect_success:
            assert len(obs_base) == 2
            assert obs_base[0].status == 401
            assert "authorization" not in obs_base[0].headers
            assert str(obs_base[0].response_headers.get("www-authenticate") or "").startswith("Digest")
            assert obs_base[1].status == 200
            assert str(obs_base[1].headers.get("authorization") or "") == "Digest"
        else:
            assert len(obs_base) == 1
            assert obs_base[0].status == 401
            assert "authorization" not in obs_base[0].headers
            assert str(obs_base[0].response_headers.get("www-authenticate") or "").startswith("Digest")

        baseline["payload"]["requests"] = [{
            "method": obs.method,
            "url": obs.url,
            "headers": _normalize_req_headers(obs.headers),
            "body_len": len(body),
            "body_sha256": sha256_bytes(body),
        } for obs in obs_base]
        baseline["payload"]["responses"] = []
        for idx, obs in enumerate(obs_base):
            if idx < len(obs_base) - 1:
                baseline["payload"]["responses"].append({
                    "status": obs.status,
                    "http_version": proto,
                    "headers": dict(obs.response_headers),
                    "body_len": 0,
                    "body_sha256": _response_sha_empty(),
                })
            else:
                baseline["payload"]["responses"].append({
                    **baseline["payload"]["response"],
                    "status": obs.status,
                    "http_version": proto,
                    "headers": dict(obs.response_headers),
                })
        baseline["payload"]["request"]["method"] = obs_base[0].method
        baseline["payload"]["request"]["url"] = obs_base[0].url
        baseline["payload"]["request"]["headers"] = _normalize_req_headers(obs_base[0].headers)
        baseline["payload"]["response"]["status"] = obs_base[-1].status
        baseline["payload"]["response"]["http_version"] = proto
        baseline["payload"]["response"]["headers"] = dict(obs_base[-1].response_headers)
        if not expect_success:
            apply_error_namespaces(
                baseline["payload"],
                kind=replay_error["kind"],
                http_status=int(replay_error["http_status"]),
            )
        write_json(baseline["path"], baseline["payload"])

        observe_log.write_text("", encoding="utf-8")
        qcurl = run_qt_test(
            env=env,
            suite=suite,
            case=case_variant,
            qt_executable=qt_path,
            args=[],
            request_meta={"method": "POST", "url": qcurl_url, "headers": {}, "body": body},
            response_meta={"status": resp_status, "http_version": proto, "headers": {}, "body": None},
            download_count=1,
            case_env={
                "QCURL_LC_CASE_ID": case_id,
                "QCURL_LC_PROTO": proto,
                "QCURL_LC_OBSERVE_HTTP_PORT": str(port),
                "QCURL_LC_REQ_ID": qcurl_req_id,
                "QCURL_LC_AUTH_USER": "user",
                "QCURL_LC_AUTH_PASS": "passwd",
            },
        )
        obs_q = observe_http_observed_list_for_id(observe_log, qcurl_req_id, expected_count=expected_count)
        if expect_success:
            assert len(obs_q) == 2
            assert obs_q[0].status == 401
            assert "authorization" not in obs_q[0].headers
            assert str(obs_q[0].response_headers.get("www-authenticate") or "").startswith("Digest")
            assert obs_q[1].status == 200
            assert str(obs_q[1].headers.get("authorization") or "") == "Digest"
        else:
            assert len(obs_q) == 1
            assert obs_q[0].status == 401
            assert "authorization" not in obs_q[0].headers
            assert str(obs_q[0].response_headers.get("www-authenticate") or "").startswith("Digest")

        qcurl["payload"]["requests"] = [{
            "method": obs.method,
            "url": obs.url,
            "headers": _normalize_req_headers(obs.headers),
            "body_len": len(body),
            "body_sha256": sha256_bytes(body),
        } for obs in obs_q]
        qcurl["payload"]["responses"] = []
        for idx, obs in enumerate(obs_q):
            if idx < len(obs_q) - 1:
                qcurl["payload"]["responses"].append({
                    "status": obs.status,
                    "http_version": proto,
                    "headers": dict(obs.response_headers),
                    "body_len": 0,
                    "body_sha256": _response_sha_empty(),
                })
            else:
                qcurl["payload"]["responses"].append({
                    **qcurl["payload"]["response"],
                    "status": obs.status,
                    "http_version": proto,
                    "headers": dict(obs.response_headers),
                })
        qcurl["payload"]["request"]["method"] = obs_q[0].method
        qcurl["payload"]["request"]["url"] = obs_q[0].url
        qcurl["payload"]["request"]["headers"] = _normalize_req_headers(obs_q[0].headers)
        qcurl["payload"]["response"]["status"] = obs_q[-1].status
        qcurl["payload"]["response"]["http_version"] = proto
        qcurl["payload"]["response"]["headers"] = dict(obs_q[-1].response_headers)
        if not expect_success:
            apply_error_namespaces(
                qcurl["payload"],
                kind=replay_error["kind"],
                http_status=int(replay_error["http_status"]),
            )
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
                    "seekable": seekable,
                },
            )
        raise
