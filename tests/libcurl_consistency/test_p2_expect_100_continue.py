"""
P2：`Expect: 100-continue` 一致性。

比较 417 响应后的重试次数、Expect 头可见性和最终响应字节。
"""

from __future__ import annotations

import os
import uuid
from pathlib import Path

import pytest

from tests.libcurl_consistency.pytest_support.artifacts import build_request_semantic, write_json
from tests.libcurl_consistency.pytest_support.baseline import run_libtest_case
from tests.libcurl_consistency.pytest_support.compare import assert_artifacts_match
from tests.libcurl_consistency.pytest_support.observed import observe_http_observed_list_for_id
from tests.libcurl_consistency.pytest_support.qcurl_runner import run_qt_test
from tests.libcurl_consistency.pytest_support.service_logs import collect_service_logs_for_case, should_collect_service_logs


def _append_req_id(url: str, req_id: str) -> str:
    sep = "&" if "?" in url else "?"
    return f"{url}{sep}id={req_id}"


def _qt_test_binary_or_skip() -> Path:
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("当前环境未提供 QCURL_QTTEST 可执行文件，跳过该用例")
    return qt_path


def _env_int(name: str, default: int) -> int:
    raw = (os.environ.get(name) or "").strip()
    if not raw:
        return int(default)
    try:
        return int(raw)
    except ValueError:
        return int(default)


def _hes_expect100_payload(obs_list, proto: str, final_body_len: int, final_body_sha: str) -> dict:
    return {
        "kind": "expect_100_continue",
        "http_version": proto,
        "request_count": len(obs_list),
        "statuses": [int(obs.status) for obs in obs_list],
        "first_expect_header": str(obs_list[0].headers.get("expect") or ""),
        "second_expect_present": "expect" in obs_list[1].headers,
        "final_body_len": int(final_body_len),
        "final_body_sha256": final_body_sha,
    }


def _run_expect100_417_retry_once(
    *,
    env,
    lc_observe_http,
    qt_executable: Path,
    suite: str,
    proto: str,
    case_id: str,
    case_variant: str,
    upload_size: int,
    expect100_timeout_ms: int,
    baseline_args_extra: list[str],
) -> dict[str, str]:
    port = int(lc_observe_http["port"])
    observe_log = Path(str(lc_observe_http["log_file"]))

    body = b"x" * upload_size
    trace_base = f"lc_{uuid.uuid4().hex[:8]}_expect100"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"
    meta = {
        "baseline_req_id": baseline_req_id,
        "qcurl_req_id": qcurl_req_id,
        "observe_port": str(port),
        "observe_log": str(observe_log),
    }

    url = f"http://localhost:{port}/expect_417"
    baseline_url = _append_req_id(url, baseline_req_id)
    resp_meta = {"status": 200, "http_version": proto, "headers": {}, "body": None}

    observe_log.write_text("", encoding="utf-8")
    baseline = run_libtest_case(
        env=env,
        suite=suite,
        case=case_variant,
        client_name="cli_lc_http",
        args=[
            "-V",
            proto,
            "--method",
            "PUT",
            *baseline_args_extra,
            "--data-size",
            str(upload_size),
            "--expect100-timeout-ms",
            str(expect100_timeout_ms),
            baseline_url,
        ],
        request_meta={"method": "PUT", "url": baseline_url, "headers": {}, "body": body},
        response_meta=resp_meta,
        download_count=1,
    )

    obs_list = observe_http_observed_list_for_id(observe_log, baseline_req_id, expected_count=2)
    assert [o.method for o in obs_list] == ["PUT", "PUT"]
    assert [int(o.status) for o in obs_list] == [417, 200]
    assert "100-continue" in str(obs_list[0].headers.get("expect") or "").lower()
    assert "expect" not in obs_list[1].headers

    baseline["payload"]["requests"] = [
        build_request_semantic(obs.method, obs.url, obs.headers, b"" if "expect" in obs.headers else body)
        for obs in obs_list
    ]
    baseline["payload"]["responses"] = [
        {
            "status": int(obs_list[0].status),
            "http_version": proto,
            "headers": dict(obs_list[0].response_headers),
            "body_len": 0,
            "body_sha256": "",
        },
        {
            "status": int(obs_list[1].status),
            "http_version": proto,
            "headers": dict(obs_list[1].response_headers),
            "body_len": int(baseline["payload"]["response"].get("body_len") or 0),
            "body_sha256": str(baseline["payload"]["response"].get("body_sha256") or ""),
        },
    ]
    baseline["payload"]["request"]["method"] = obs_list[0].method
    baseline["payload"]["request"]["url"] = obs_list[0].url
    baseline["payload"]["request"]["headers"] = obs_list[0].headers
    baseline["payload"]["response"]["status"] = obs_list[-1].status
    baseline["payload"]["response"]["http_version"] = proto
    baseline["payload"]["response"]["headers"] = obs_list[-1].response_headers
    assert int(baseline["payload"]["response"].get("body_len") or 0) == upload_size
    baseline["payload"]["hes"] = _hes_expect100_payload(
        obs_list,
        proto,
        int(baseline["payload"]["response"].get("body_len") or 0),
        str(baseline["payload"]["response"].get("body_sha256") or ""),
    )
    write_json(baseline["path"], baseline["payload"])

    observe_log.write_text("", encoding="utf-8")
    qcurl_url = _append_req_id(url, qcurl_req_id)
    qcurl = run_qt_test(
        env=env,
        suite=suite,
        case=case_variant,
        qt_executable=qt_executable,
        args=[],
        request_meta={"method": "PUT", "url": qcurl_url, "headers": {}, "body": body},
        response_meta=resp_meta,
        download_count=1,
        case_env={
            "QCURL_LC_CASE_ID": case_id,
            "QCURL_LC_PROTO": proto,
            "QCURL_LC_REQ_ID": qcurl_req_id,
            "QCURL_LC_UPLOAD_SIZE": str(upload_size),
            "QCURL_LC_EXPECT100_TIMEOUT_MS": str(expect100_timeout_ms),
            "QCURL_LC_OBSERVE_HTTP_PORT": str(port),
        },
    )

    obs_list = observe_http_observed_list_for_id(observe_log, qcurl_req_id, expected_count=2)
    assert [o.method for o in obs_list] == ["PUT", "PUT"]
    assert [int(o.status) for o in obs_list] == [417, 200]
    assert "100-continue" in str(obs_list[0].headers.get("expect") or "").lower()
    assert "expect" not in obs_list[1].headers

    qcurl["payload"]["requests"] = [
        build_request_semantic(obs.method, obs.url, obs.headers, b"" if "expect" in obs.headers else body)
        for obs in obs_list
    ]
    qcurl["payload"]["responses"] = [
        {
            "status": int(obs_list[0].status),
            "http_version": proto,
            "headers": dict(obs_list[0].response_headers),
            "body_len": 0,
            "body_sha256": "",
        },
        {
            "status": int(obs_list[1].status),
            "http_version": proto,
            "headers": dict(obs_list[1].response_headers),
            "body_len": int(qcurl["payload"]["response"].get("body_len") or 0),
            "body_sha256": str(qcurl["payload"]["response"].get("body_sha256") or ""),
        },
    ]
    qcurl["payload"]["request"]["method"] = obs_list[0].method
    qcurl["payload"]["request"]["url"] = obs_list[0].url
    qcurl["payload"]["request"]["headers"] = obs_list[0].headers
    qcurl["payload"]["response"]["status"] = obs_list[-1].status
    qcurl["payload"]["response"]["http_version"] = proto
    qcurl["payload"]["response"]["headers"] = obs_list[-1].response_headers
    assert int(qcurl["payload"]["response"].get("body_len") or 0) == upload_size
    qcurl["payload"]["hes"] = _hes_expect100_payload(
        obs_list,
        proto,
        int(qcurl["payload"]["response"].get("body_len") or 0),
        str(qcurl["payload"]["response"].get("body_sha256") or ""),
    )
    write_json(qcurl["path"], qcurl["payload"])

    assert_artifacts_match(baseline["path"], qcurl["path"])
    return meta


def test_p2_expect_100_continue_417_retry(env, lc_observe_http):
    qt_path = _qt_test_binary_or_skip()
    collect_logs = should_collect_service_logs()
    suite = "p2_expect"
    proto = "http/1.1"
    case_id = "p2_expect_100_continue"
    case_variant = "lc_expect_100_continue_http_1.1"

    expect100_timeout_ms = 10_000
    upload_size = 1053700
    meta: dict[str, str] = {}
    try:
        meta = _run_expect100_417_retry_once(
            env=env,
            lc_observe_http=lc_observe_http,
            qt_executable=qt_path,
            suite=suite,
            proto=proto,
            case_id=case_id,
            case_variant=case_variant,
            upload_size=upload_size,
            expect100_timeout_ms=expect100_timeout_ms,
            baseline_args_extra=["--stream-body", "--seekable-body"],
        )
    except Exception:
        if collect_logs:
            collect_service_logs_for_case(
                env,
                suite=suite,
                case=case_variant,
                logs={"observe_http_log": Path(str(lc_observe_http["log_file"]))},
                meta={
                    "case_id": case_id,
                    "case_variant": case_variant,
                    "proto": proto,
                    "upload_size": upload_size,
                    **meta,
                },
            )
        raise


def test_p2_expect_100_continue_417_retry_repeat_inprocess(env, lc_observe_http):
    """
    进程内重复执行同一用例，用于快速收敛 flaky（避免 shell 层循环多次启动 pytest/httpd）。

    默认关闭：需要设置 QCURL_LC_EXPECT100_REPEAT>=2 才会执行。
    """
    qt_path = _qt_test_binary_or_skip()

    repeat = _env_int("QCURL_LC_EXPECT100_REPEAT", 1)
    if repeat <= 1:
        pytest.skip("QCURL_LC_EXPECT100_REPEAT<=1（设置为 N>=2 以启用进程内重复跑）")

    collect_logs = should_collect_service_logs()
    suite = "p2_expect"
    proto = "http/1.1"
    case_id = "p2_expect_100_continue"
    case_variant = "lc_expect_100_continue_http_1.1"

    expect100_timeout_ms = 10_000
    upload_size = 1053700

    for i in range(repeat):
        meta: dict[str, str] = {}
        try:
            meta = _run_expect100_417_retry_once(
                env=env,
                lc_observe_http=lc_observe_http,
                qt_executable=qt_path,
                suite=suite,
                proto=proto,
                case_id=case_id,
                case_variant=case_variant,
                upload_size=upload_size,
                expect100_timeout_ms=expect100_timeout_ms,
                baseline_args_extra=["--stream-body", "--seekable-body"],
            )
        except Exception:
            if collect_logs:
                collect_service_logs_for_case(
                    env,
                    suite=suite,
                    case=case_variant,
                    logs={"observe_http_log": Path(str(lc_observe_http["log_file"]))},
                    meta={
                        "case_id": case_id,
                        "case_variant": case_variant,
                        "proto": proto,
                        "upload_size": upload_size,
                        "repeat_total": repeat,
                        "repeat_iteration": i + 1,
                        **meta,
                    },
                )
            raise


def test_p2_expect_100_continue_417_retry_inline_body(env, lc_observe_http):
    """
    覆盖 PUT + inline-body（CURLOPT_POSTFIELDS 语义）路径的 Expect/417/重试行为。

    说明：
    - 原用例对齐上游 test357，走 seekable upload（READFUNCTION/SEEKFUNCTION）
    - 本用例补齐 inline bytes 路径，避免“只测 uploadDevice 导致的覆盖空洞”
    """
    qt_path = _qt_test_binary_or_skip()
    collect_logs = should_collect_service_logs()
    suite = "p2_expect"
    proto = "http/1.1"
    case_id = "p2_expect_100_continue_inline_body"
    case_variant = "lc_expect_100_continue_inline_body_http_1.1"

    expect100_timeout_ms = 10_000
    upload_size = 1053700
    meta: dict[str, str] = {}
    try:
        meta = _run_expect100_417_retry_once(
            env=env,
            lc_observe_http=lc_observe_http,
            qt_executable=qt_path,
            suite=suite,
            proto=proto,
            case_id=case_id,
            case_variant=case_variant,
            upload_size=upload_size,
            expect100_timeout_ms=expect100_timeout_ms,
            baseline_args_extra=[],
        )
    except Exception:
        if collect_logs:
            collect_service_logs_for_case(
                env,
                suite=suite,
                case=case_variant,
                logs={"observe_http_log": Path(str(lc_observe_http["log_file"]))},
                meta={
                    "case_id": case_id,
                    "case_variant": case_variant,
                    "proto": proto,
                    "upload_size": upload_size,
                    **meta,
                },
            )
        raise
