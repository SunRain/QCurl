"""
P2：TLS 校验语义一致性（成功/失败路径）。

覆盖缺口：
- TLS 校验语义缺失：对齐 verifyPeer/verifyHost + CAINFO/caCertPath 下的可观测结果（成功/证书错误）

服务端：repo 内置 http_observe_server.py（HTTPS，复用 curl testenv 生成的 CA + localhost 证书）
基线：repo 内置 qcurl_lc_http_baseline（cli_lc_http，--secure/--cainfo）
QCurl：tst_LibcurlConsistency（p2_tls_verify_*）
"""

from __future__ import annotations

import os
import re
import uuid
from pathlib import Path

import pytest

from tests.libcurl_consistency.pytest_support.artifacts import write_json
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


@pytest.mark.parametrize("mode", ["success_with_ca", "fail_no_ca"])
def test_p2_tls_verify(mode: str, env, lc_logs, lc_observe_https, tmp_path):
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("QCURL_QTTEST 未设置或可执行不存在")

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
            baseline["payload"]["error"] = {"kind": "tls", "http_status": 0}
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
            qcurl["payload"]["error"] = {"kind": "tls", "http_status": 0}
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

