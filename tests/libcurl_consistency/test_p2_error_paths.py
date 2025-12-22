"""
P2：错误路径一致性（LC-32：连接拒绝 / proxy 407 / URL malformat）。

目的：
- 仅基于可观测输出，对齐 QCurl 与 libcurl baseline 的错误语义：
  - curlcode / http_code（baseline：stderr；QCurl：基于映射与断言）
  - QCurl NetworkError 映射：ConnectionRefused / InvalidRequest / HTTP 407
  - 响应 body 落盘结果（空文件）一致

说明：
- “连接拒绝/URL 非法”属于“服务端未收到请求”的场景，因此不依赖服务端观测日志；
  request 语义摘要按输入 URL 构造（与 README 约定一致）。
- “proxy 407”以代理服务端观测为主（method/target/Proxy-* 头），并比较错误归一化字段。
"""

from __future__ import annotations

import os
import re
import uuid
from pathlib import Path
from urllib.parse import parse_qs, urlencode, urlsplit, urlunsplit

import pytest

from tests.libcurl_consistency.pytest_support.artifacts import build_request_semantic, write_json
from tests.libcurl_consistency.pytest_support.baseline import run_libtest_case
from tests.libcurl_consistency.pytest_support.compare import assert_artifacts_match
from tests.libcurl_consistency.pytest_support.qcurl_runner import run_qt_test
from tests.libcurl_consistency.pytest_support.service_logs import collect_service_logs_for_case, should_collect_service_logs


_CURLINFO_RE = re.compile(r"curlcode=(\d+)\s+http_code=(\d+)")


def _parse_curlcode_http_code(stderr_lines: list[str]) -> tuple[int, int]:
    for line in stderr_lines:
        m = _CURLINFO_RE.search(line)
        if m:
            return int(m.group(1)), int(m.group(2))
    return -1, -1


def _strip_query_id_keep_origin(url: str) -> str:
    parts = urlsplit(url)
    q = parse_qs(parts.query, keep_blank_values=True)
    q.pop("id", None)
    query = urlencode(q, doseq=True)
    return urlunsplit((parts.scheme, parts.netloc, parts.path, query, parts.fragment))


def _append_req_id(url: str, req_id: str) -> str:
    sep = "&" if "?" in url else "?"
    return f"{url}{sep}id={req_id}"


def _error(kind: str, *, http_status: int, curlcode: int, http_code: int) -> dict:
    return {
        "kind": kind,
        "http_status": int(http_status),
        "curlcode": int(curlcode),
        "http_code": int(http_code),
    }


def _proxy_observed_for_id_any(proxy_log: Path, req_id: str) -> tuple[str, str, dict]:
    if not proxy_log.exists():
        raise AssertionError(f"proxy log 不存在: {proxy_log}")
    for raw in proxy_log.read_text(encoding="utf-8", errors="replace").splitlines():
        if not raw.strip():
            continue
        try:
            import json

            e = json.loads(raw)
        except Exception:
            continue
        if (e.get("id") or "") != req_id:
            continue
        method = str(e.get("method") or "").upper()
        target = str(e.get("target") or "")
        headers_in = e.get("headers") or {}
        headers = {str(k).lower(): str(v) for k, v in headers_in.items()}
        headers_allowlist: dict = {}
        for name in ("proxy-authorization", "proxy-connection"):
            v = headers.get(name)
            if v:
                headers_allowlist[name] = v
        url = target
        if method != "CONNECT":
            url = _strip_query_id_keep_origin(target)
        return method, url, headers_allowlist
    raise AssertionError(f"proxy log 无匹配记录：id={req_id}, file={proxy_log}")


def test_p2_error_connect_refused(env, lc_logs, free_tcp_port, tmp_path):
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("QCURL_QTTEST 未设置或可执行不存在")

    collect_logs = should_collect_service_logs()
    suite = "p2_error_paths"
    proto = "http/1.1"
    case_variant = "p2_error_refused_http_1.1"

    url = f"http://localhost:{int(free_tcp_port)}/"
    resp_meta = {"status": 0, "http_version": proto, "headers": {}, "body": None}

    try:
        baseline = run_libtest_case(
            env=env,
            suite=suite,
            case=case_variant,
            client_name="cli_lc_http",
            args=[
                "-V",
                proto,
                "--connect-timeout-ms",
                "200",
                url,
            ],
            request_meta={"method": "GET", "url": url, "headers": {}, "body": b""},
            response_meta=resp_meta,
            download_count=1,
            allowed_exit_codes={0, 7},
        )
        curlcode, http_code = _parse_curlcode_http_code(list(baseline["payload"].get("stderr") or []))
        if curlcode < 0:
            raise AssertionError("baseline stderr 未包含 curlcode/http_code")
        baseline["payload"]["error"] = _error("connect", http_status=0, curlcode=curlcode, http_code=http_code)
        write_json(baseline["path"], baseline["payload"])

        qcurl = run_qt_test(
            env=env,
            suite=suite,
            case=case_variant,
            qt_executable=qt_path,
            args=[],
            request_meta={"method": "GET", "url": url, "headers": {}, "body": b""},
            response_meta=resp_meta,
            download_count=1,
            case_env={
                "QCURL_LC_CASE_ID": "p2_error_refused",
                "QCURL_LC_PROTO": proto,
                "QCURL_LC_TARGET_URL": url,
            },
        )
        # QCurl 对该错误的可观测输出为 NetworkError::ConnectionRefused（映射自 CURLE_COULDNT_CONNECT=7）
        qcurl["payload"]["error"] = _error("connect", http_status=0, curlcode=7, http_code=0)
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
                    "case_id": "p2_error_refused",
                    "case_variant": case_variant,
                    "proto": proto,
                    "target_url": url,
                },
            )
        raise


def test_p2_error_url_malformat(env, lc_logs, tmp_path):
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("QCURL_QTTEST 未设置或可执行不存在")

    collect_logs = should_collect_service_logs()
    suite = "p2_error_paths"
    proto = "http/1.1"
    case_variant = "p2_error_malformat_http_1.1"

    url = "http://"
    resp_meta = {"status": 0, "http_version": proto, "headers": {}, "body": None}

    try:
        baseline = run_libtest_case(
            env=env,
            suite=suite,
            case=case_variant,
            client_name="cli_lc_http",
            args=[
                "-V",
                proto,
                url,
            ],
            request_meta={"method": "GET", "url": url, "headers": {}, "body": b""},
            response_meta=resp_meta,
            download_count=1,
            allowed_exit_codes={0, 7},
        )
        curlcode, http_code = _parse_curlcode_http_code(list(baseline["payload"].get("stderr") or []))
        if curlcode < 0:
            raise AssertionError("baseline stderr 未包含 curlcode/http_code")
        baseline["payload"]["error"] = _error("url", http_status=0, curlcode=curlcode, http_code=http_code)
        write_json(baseline["path"], baseline["payload"])

        qcurl = run_qt_test(
            env=env,
            suite=suite,
            case=case_variant,
            qt_executable=qt_path,
            args=[],
            request_meta={"method": "GET", "url": url, "headers": {}, "body": b""},
            response_meta=resp_meta,
            download_count=1,
            case_env={
                "QCURL_LC_CASE_ID": "p2_error_malformat",
                "QCURL_LC_PROTO": proto,
                "QCURL_LC_TARGET_URL": url,
            },
        )
        # QCurl 对该错误的可观测输出为 NetworkError::InvalidRequest（映射自 CURLE_URL_MALFORMAT=3）
        qcurl["payload"]["error"] = _error("url", http_status=0, curlcode=3, http_code=0)
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
                    "case_id": "p2_error_malformat",
                    "case_variant": case_variant,
                    "proto": proto,
                    "target_url": url,
                },
            )
        raise


def test_p2_error_proxy_407(env, lc_logs, lc_http_proxy, tmp_path):
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("QCURL_QTTEST 未设置或可执行不存在")

    collect_logs = should_collect_service_logs()
    suite = "p2_error_paths"
    proto = "http/1.1"
    case_variant = "p2_error_proxy_407_http_1.1"

    proxy_port = int(lc_http_proxy["port"])
    proxy_log = Path(str(lc_http_proxy["log_file"]))
    proxy_url = f"http://localhost:{proxy_port}"

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_proxy_407"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    target = _append_req_id(f"http://localhost:{int(env.http_port)}/", baseline_req_id)
    qcurl_target = _append_req_id(f"http://localhost:{int(env.http_port)}/", qcurl_req_id)

    resp_meta = {"status": 407, "http_version": proto, "headers": {}, "body": None}

    try:
        proxy_log.write_text("", encoding="utf-8")
        baseline = run_libtest_case(
            env=env,
            suite=suite,
            case=case_variant,
            client_name="cli_lc_http",
            args=[
                "-V",
                proto,
                "--proxy",
                proxy_url,
                target,
            ],
            request_meta={"method": "GET", "url": target, "headers": {}, "body": b""},
            response_meta=resp_meta,
            download_count=1,
        )
        curlcode, http_code = _parse_curlcode_http_code(list(baseline["payload"].get("stderr") or []))
        if curlcode < 0:
            raise AssertionError("baseline stderr 未包含 curlcode/http_code")
        method, url_no_id, hdrs = _proxy_observed_for_id_any(proxy_log, baseline_req_id)
        baseline["payload"]["requests"] = [build_request_semantic(method, url_no_id, hdrs, b"")]
        baseline["payload"]["response"]["status"] = 407
        baseline["payload"]["response"]["http_version"] = proto
        baseline["payload"]["error"] = _error("http", http_status=407, curlcode=curlcode, http_code=http_code)
        write_json(baseline["path"], baseline["payload"])

        proxy_log.write_text("", encoding="utf-8")
        qcurl = run_qt_test(
            env=env,
            suite=suite,
            case=case_variant,
            qt_executable=qt_path,
            args=[],
            request_meta={"method": "GET", "url": qcurl_target, "headers": {}, "body": b""},
            response_meta=resp_meta,
            download_count=1,
            case_env={
                "QCURL_LC_CASE_ID": "p2_error_proxy_407",
                "QCURL_LC_PROTO": proto,
                "QCURL_LC_PROXY_PORT": str(proxy_port),
                "QCURL_LC_PROXY_TARGET_URL": qcurl_target,
            },
        )
        method, url_no_id, hdrs = _proxy_observed_for_id_any(proxy_log, qcurl_req_id)
        qcurl["payload"]["requests"] = [build_request_semantic(method, url_no_id, hdrs, b"")]
        qcurl["payload"]["response"]["status"] = 407
        qcurl["payload"]["response"]["http_version"] = proto
        qcurl["payload"]["error"] = _error("http", http_status=407, curlcode=0, http_code=407)
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
                    "case_id": "p2_error_proxy_407",
                    "case_variant": case_variant,
                    "proto": proto,
                    "proxy_port": proxy_port,
                    "baseline_req_id": baseline_req_id,
                    "qcurl_req_id": qcurl_req_id,
                },
            )
        raise
