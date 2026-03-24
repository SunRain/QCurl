"""
P1：原始响应头与重复头交付一致性。

比较动态头过滤后的 header 行集合、大小写和顺序。
"""

from __future__ import annotations

import hashlib
import json
import os
import uuid
from pathlib import Path

import pytest

from tests.libcurl_consistency.pytest_support.artifacts import (
    artifacts_root,
    build_request_semantic,
    ensure_case_dir,
    write_json,
)
from tests.libcurl_consistency.pytest_support.baseline import run_libtest_case
from tests.libcurl_consistency.pytest_support.compare import assert_artifacts_match
from tests.libcurl_consistency.pytest_support.observed import observe_http_observed_for_id
from tests.libcurl_consistency.pytest_support.qcurl_runner import run_qt_test
from tests.libcurl_consistency.pytest_support.service_logs import collect_service_logs_for_case, should_collect_service_logs


def _append_req_id(url: str, req_id: str) -> str:
    sep = "&" if "?" in url else "?"
    return f"{url}{sep}id={req_id}"


def _normalize_raw_header_lines(raw: bytes) -> list[str]:
    # headers 使用 latin-1 解码，尽量保持逐字节可比性，避免 utf-8 解码失败影响结果。
    text = raw.decode("iso-8859-1", errors="replace")
    out: list[str] = []
    for line in text.splitlines():
        line = line.rstrip("\r\n")
        if not line:
            continue
        low = line.lower()
        if low.startswith("date:") or low.startswith("server:"):
            continue
        out.append(line)
    return out


def _raw_header_fields(lines: list[str]) -> dict:
    blob = "\n".join(lines).encode("utf-8")
    return {
        "headers_raw_lines": lines,
        "headers_raw_len": int(len(blob)),
        "headers_raw_sha256": hashlib.sha256(blob).hexdigest(),
    }


def _assert_server_headers_shape(lines: list[str]) -> None:
    # 该断言用于避免“对比器或用例失效却仍然通过”的情况，例如意外未采集到 headers。
    set_cookie = [l for l in lines if l.lower().startswith("set-cookie:")]
    x_dupe = [l for l in lines if l.lower().startswith("x-dupe:")]
    assert len(set_cookie) == 2, f"Set-Cookie 行数异常: {set_cookie}"
    assert len(x_dupe) == 2, f"X-Dupe 行数异常: {x_dupe}"
    assert "X-Case: A" in lines, "缺少 X-Case: A"
    assert "x-case: b" in lines, "缺少 x-case: b"

def _parse_curl_easy_header_stdout(lines: list[str]) -> dict[str, object]:
    """
    解析 curl/tests/libtest/lib1940 的 stdout（curl_easy_header 输出），提取可比较的 header 值。

    输出格式：
    - 单值：`<Name> == <Value>`
    - 多值：`- <Name> == <Value> (i/n)`
    """
    single: dict[str, str] = {}
    multi: dict[str, list[str]] = {}

    for raw in lines:
        s = (raw or "").rstrip("\r\n")
        if "==" not in s:
            continue
        s = s.strip()
        if not s:
            continue

        if s.startswith("- "):
            rest = s[2:]
            if " == " not in rest:
                continue
            name, tail = rest.split(" == ", 1)
            name = name.strip()
            value = tail.rstrip()
            if " (" in value and value.endswith(")"):
                value = value.rsplit(" (", 1)[0]
            multi.setdefault(name, []).append(value)
        else:
            rest = s.lstrip()
            if " == " not in rest:
                continue
            name, value = rest.split(" == ", 1)
            single[name.strip()] = value.rstrip()

    out: dict[str, object] = {}
    out.update(single)
    for k, v in multi.items():
        out[k] = v
    return out


def test_p1_resp_headers_raw(env, lc_logs, lc_observe_http, tmp_path):
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("当前环境未提供 QCURL_QTTEST 可执行文件，跳过该用例")

    collect_logs = should_collect_service_logs()
    port = int(lc_observe_http["port"])
    observe_log = Path(str(lc_observe_http["log_file"]))

    suite = "p1_resp_headers"
    proto = "http/1.1"
    case_variant = "p1_resp_headers_http_1.1"

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_resp_headers"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    base_url = f"http://localhost:{port}/resp_headers"
    baseline_url = _append_req_id(base_url, baseline_req_id)
    qcurl_url = _append_req_id(base_url, qcurl_req_id)

    case_dir = ensure_case_dir(artifacts_root(env), suite=suite, case=case_variant)
    baseline_header_file = case_dir / "baseline_response_headers.data"
    baseline_header_file.write_bytes(b"")

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
                "--header-out",
                str(baseline_header_file),
                baseline_url,
            ],
            request_meta={"method": "GET", "url": baseline_url, "headers": {}, "body": b""},
            response_meta=resp_meta,
            download_count=1,
        )
        obs = observe_http_observed_for_id(observe_log, baseline_req_id)
        baseline["payload"]["request"] = build_request_semantic(obs.method, obs.url, obs.headers, b"")
        baseline["payload"]["response"]["status"] = obs.status
        baseline["payload"]["response"]["http_version"] = proto
        baseline["payload"]["response"]["headers"] = obs.response_headers
        raw_lines = _normalize_raw_header_lines(baseline_header_file.read_bytes())
        _assert_server_headers_shape(raw_lines)
        baseline["payload"]["response"].update(_raw_header_fields(raw_lines))
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
                "QCURL_LC_CASE_ID": "p1_resp_headers",
                "QCURL_LC_PROTO": proto,
                "QCURL_LC_REQ_ID": qcurl_req_id,
                "QCURL_LC_OBSERVE_HTTP_PORT": str(port),
            },
        )
        obs = observe_http_observed_for_id(observe_log, qcurl_req_id)
        qcurl["payload"]["request"] = build_request_semantic(obs.method, obs.url, obs.headers, b"")
        qcurl["payload"]["response"]["status"] = obs.status
        qcurl["payload"]["response"]["http_version"] = proto
        qcurl["payload"]["response"]["headers"] = obs.response_headers

        qcurl_header_file = qcurl["path"].parent / "qcurl_run" / "response_headers_0.data"
        raw_lines = _normalize_raw_header_lines(qcurl_header_file.read_bytes())
        _assert_server_headers_shape(raw_lines)
        qcurl["payload"]["response"].update(_raw_header_fields(raw_lines))
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
                    "case_id": "p1_resp_headers",
                    "case_variant": case_variant,
                    "proto": proto,
                    "baseline_req_id": baseline_req_id,
                    "qcurl_req_id": qcurl_req_id,
                    "observe_http_port": port,
                },
            )
        raise


def test_p1_resp_headers_unfold_1940(env, lc_logs, lc_observe_http, tmp_path):
    """
    P1：响应头 unfold 一致性（curl test1940 语义来源：折叠行 + TAB 的 curl_easy_header 可观测值）。

    基线：lib1940（curl_easy_header 输出到 stdout）
    QCurl：Qt Test（resp_headers_unfold_1940，落盘 headers_unfolded_1940.json）
    """
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("当前环境未提供 QCURL_QTTEST 可执行文件，跳过该用例")

    collect_logs = should_collect_service_logs()
    port = int(lc_observe_http["port"])
    observe_log = Path(str(lc_observe_http["log_file"]))

    suite = "p1_resp_headers"
    proto = "http/1.1"
    case_variant = "p1_resp_headers_unfold_1940_http_1.1"

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_resp_headers_unfold_1940"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    base_url = f"http://localhost:{port}/resp_headers?scenario=1940"
    baseline_url = _append_req_id(base_url, baseline_req_id)
    qcurl_url = _append_req_id(base_url, qcurl_req_id)

    resp_meta = {"status": 200, "http_version": proto, "headers": {}, "body": None}

    try:
        observe_log.write_text("", encoding="utf-8")
        baseline = run_libtest_case(
            env=env,
            suite=suite,
            case=case_variant,
            client_name="lib1940",
            args=[baseline_url],
            request_meta={"method": "GET", "url": baseline_url, "headers": {}, "body": b""},
            response_meta=resp_meta,
        )
        obs = observe_http_observed_for_id(observe_log, baseline_req_id)
        baseline["payload"]["request"] = build_request_semantic(obs.method, obs.url, obs.headers, b"")
        baseline["payload"]["response"]["status"] = obs.status
        baseline["payload"]["response"]["http_version"] = proto
        baseline_unfolded = _parse_curl_easy_header_stdout(baseline["payload"]["stdout"])
        baseline["payload"]["headers_unfolded_1940"] = baseline_unfolded
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
            case_env={
                "QCURL_LC_CASE_ID": "resp_headers_unfold_1940",
                "QCURL_LC_PROTO": proto,
                "QCURL_LC_REQ_ID": qcurl_req_id,
                "QCURL_LC_OBSERVE_HTTP_PORT": str(port),
            },
        )
        obs = observe_http_observed_for_id(observe_log, qcurl_req_id)
        qcurl["payload"]["request"] = build_request_semantic(obs.method, obs.url, obs.headers, b"")
        qcurl["payload"]["response"]["status"] = obs.status
        qcurl["payload"]["response"]["http_version"] = proto

        qcurl_unfolded_path = qcurl["path"].parent / "qcurl_run" / "headers_unfolded_1940.json"
        qcurl_unfolded = json.loads(qcurl_unfolded_path.read_text(encoding="utf-8"))
        qcurl["payload"]["headers_unfolded_1940"] = qcurl_unfolded
        write_json(qcurl["path"], qcurl["payload"])

        qcurl_unfolded_for_compare = {
            k: v
            for k, v in qcurl_unfolded.items()
            if not (v == "" and k not in baseline_unfolded)
        }
        assert baseline_unfolded == qcurl_unfolded_for_compare
        assert_artifacts_match(baseline["path"], qcurl["path"])
    except Exception:
        if collect_logs:
            collect_service_logs_for_case(
                env,
                suite=suite,
                case=case_variant,
                logs=lc_logs,
                meta={
                    "case_id": "resp_headers_unfold_1940",
                    "case_variant": case_variant,
                    "proto": proto,
                    "baseline_req_id": baseline_req_id,
                    "qcurl_req_id": qcurl_req_id,
                    "observe_http_port": port,
                },
            )
        raise
