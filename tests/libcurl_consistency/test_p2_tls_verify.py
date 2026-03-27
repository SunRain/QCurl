"""
P2：TLS 校验成功/失败路径一致性。

比较 `verifyPeer`、`verifyHost` 和 `caCertPath` 相关的可观测结果。
"""

from __future__ import annotations

import os
import re
import uuid
from pathlib import Path

import pytest

from tests.libcurl_consistency.pytest_support.artifacts import apply_error_namespaces, write_json
from tests.libcurl_consistency.pytest_support.baseline import run_libtest_case
from tests.libcurl_consistency.pytest_support.compare import assert_artifacts_match
from tests.libcurl_consistency.pytest_support.qcurl_runner import run_qt_test
from tests.libcurl_consistency.pytest_support.service_logs import collect_service_logs_for_case, should_collect_service_logs


_CURLCODE_RE = re.compile(r"curlcode=(\d+)")


def _parse_curlcode(stderr_lines) -> int:
    for line in stderr_lines or []:
        m = _CURLCODE_RE.search(str(line))
        if m:
            return int(m.group(1))
    return -1


def _tls_boundary(*, proto: str, ca_cert: bool) -> dict[str, object]:
    return {
        "scheme": "https",
        "http_version": proto,
        "proxy_mode": "direct",
        "alpn": proto,
        "sni": "localhost",
        "client_cert": False,
        "pinning": "none",
        "verify_peer": True,
        "verify_host": True,
        "ca_cert": ca_cert,
    }


def _boundary_key(boundary: dict[str, object]) -> str:
    return ";".join(f"{key}={boundary[key]}" for key in sorted(boundary))


def _make_ctbp_payload(*, proto: str, mode: str) -> dict[str, object]:
    boundary = _tls_boundary(proto=proto, ca_cert=(mode == "success_with_ca"))
    payload: dict[str, object] = {
        "schema": "qcurl-lc/ctbp@v1",
        "kind": "tls_boundary",
        "boundary": boundary,
        "boundary_key": _boundary_key(boundary),
        "expected_result": "pass" if mode == "success_with_ca" else "tls_error",
    }
    if mode != "success_with_ca":
        payload["expected_error_kind"] = "tls"
    return payload


@pytest.mark.parametrize("mode", ["success_with_ca", "fail_no_ca"])
def test_p2_tls_verify(mode: str, env, lc_logs, lc_observe_https, tmp_path):
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("当前环境未提供 QCURL_QTTEST 可执行文件，跳过该用例")

    collect_logs = should_collect_service_logs()
    port = int(lc_observe_https["port"])
    ca_cert = str(lc_observe_https["ca_cert"])

    suite = "p2_tls"
    proto = "http/1.1"
    case_variant = f"lc_tls_verify_{mode}"

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_tls_{mode}"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    # 握手失败时服务端不会收到 HTTP 请求，因此这里不追加 id，也不依赖服务端日志。
    url = f"https://localhost:{port}/cookie"

    req_meta = {"method": "GET", "url": url, "headers": {}, "body": b""}
    if mode == "success_with_ca":
        resp_meta = {"status": 200, "http_version": proto, "headers": {}, "body": None}
    else:
        resp_meta = {"status": 0, "http_version": "tls", "headers": {}, "body": None}

    try:
        baseline_args = ["-V", proto, "--secure"]
        if mode == "success_with_ca":
            baseline_args.extend(["--cainfo", ca_cert])
        baseline_args.append(url)

        baseline = run_libtest_case(
            env=env,
            suite=suite,
            case=case_variant,
            client_name="cli_lc_http",
            args=baseline_args,
            request_meta=req_meta,
            response_meta=resp_meta,
            download_count=1 if mode == "success_with_ca" else None,
            allowed_exit_codes={0, 7} if mode != "success_with_ca" else None,
        )

        if mode != "success_with_ca":
            curlcode = _parse_curlcode(baseline["payload"].get("stderr"))
            assert curlcode == 60, f"unexpected curlcode: {curlcode}"
            apply_error_namespaces(baseline["payload"], kind="tls", http_status=0)
        baseline["payload"]["ctbp"] = _make_ctbp_payload(proto=proto, mode=mode)
        write_json(baseline["path"], baseline["payload"])

        qcurl_env = {
            "QCURL_LC_CASE_ID": "p2_tls_verify_success" if mode == "success_with_ca" else "p2_tls_verify_fail_no_ca",
            "QCURL_LC_PROTO": proto,
            "QCURL_LC_REQ_ID": qcurl_req_id,
            "QCURL_LC_OBSERVE_HTTPS_PORT": str(port),
            "QCURL_LC_CA_CERT_PATH": ca_cert,
        }

        qcurl = run_qt_test(
            env=env,
            suite=suite,
            case=case_variant,
            qt_executable=qt_path,
            args=[],
            request_meta={"method": "GET", "url": url, "headers": {}, "body": b""},
            response_meta=resp_meta,
            download_count=1 if mode == "success_with_ca" else None,
            case_env=qcurl_env,
        )

        if mode != "success_with_ca":
            apply_error_namespaces(qcurl["payload"], kind="tls", http_status=0)
        qcurl["payload"]["ctbp"] = _make_ctbp_payload(proto=proto, mode=mode)
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
                    "case_id": "p2_tls_verify",
                    "case_variant": case_variant,
                    "mode": mode,
                    "baseline_req_id": baseline_req_id,
                    "qcurl_req_id": qcurl_req_id,
                    "observe_https_port": port,
                },
            )
        raise
