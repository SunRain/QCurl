"""P2：HTTPS proxy 的验证、CA、最低 TLS、密码套件和 Fail 策略成对证据。"""

from __future__ import annotations

import uuid
from pathlib import Path

import pytest

from tests.libcurl_consistency.pytest_support.artifacts import apply_error_namespaces, write_json
from tests.libcurl_consistency.pytest_support.baseline import run_libtest_case
from tests.libcurl_consistency.pytest_support.compare import assert_artifacts_match
from tests.libcurl_consistency.pytest_support.observed import proxy_observed_for_log
from tests.libcurl_consistency.pytest_support.qcurl_runner import require_qcurl_qttest, run_qt_test


_TLS12_CIPHER = "ECDHE-RSA-AES256-GCM-SHA384"


@pytest.mark.parametrize(
    "mode",
    [
        "success",
        "success_no_peer",
        "success_no_host",
        "fail_no_ca",
        "fail_invalid_cipher",
        "fail_unsupported_policy",
    ],
)
def test_p2_https_proxy_tls(mode, env, lc_https_proxy, lc_observe_http):
    proxy_port = int(lc_https_proxy["port"])
    proxy_log = Path(str(lc_https_proxy["log_file"]))
    origin_port = int(lc_observe_http["port"])
    proxy_url = f"https://127.0.0.1:{proxy_port}"
    base_url = f"http://localhost:{origin_port}/cookie"
    trace = f"lc_{uuid.uuid4().hex[:8]}_proxy_tls_{mode}"
    baseline_id = f"{trace}__baseline"
    qcurl_id = f"{trace}__qcurl"
    suite = "p2_proxy_tls"
    case_variant = f"lc_https_proxy_tls_{mode}"
    expect_success = mode.startswith("success")
    verify_peer = mode != "success_no_peer"
    verify_host = mode != "success_no_host"
    response = {
        "status": 200 if expect_success else 0,
        "http_version": "http/1.1" if expect_success else "tls",
        "headers": {},
        "body": None,
    }

    baseline_args = [
        "-V",
        "http/1.1",
        "--proxy",
        proxy_url,
        "--proxy-type",
        "https",
        "--proxy-user",
        str(lc_https_proxy["username"]),
        "--proxy-pass",
        str(lc_https_proxy["password"]),
        "--proxy-secure",
        "--proxy-tls-min",
        "tls1.2",
        "--proxy-cipher-list",
        _TLS12_CIPHER,
    ]
    if not verify_peer:
        baseline_args.append("--proxy-no-verify-peer")
    if not verify_host:
        baseline_args.append("--proxy-no-verify-host")
    if mode != "fail_no_ca":
        baseline_args.extend(["--proxy-cainfo", str(lc_https_proxy["ca_cert"])])
    if mode == "fail_invalid_cipher":
        invalid_index = baseline_args.index(_TLS12_CIPHER)
        baseline_args[invalid_index] = "QCURL-INVALID-CIPHER"
    if mode == "fail_unsupported_policy":
        baseline_args.extend(["--force-unsupported-option", "CURLOPT_PROXY_SSL_CIPHER_LIST"])
    baseline_args.append(f"{base_url}?id={baseline_id}")

    proxy_log.write_text("", encoding="utf-8")
    baseline = run_libtest_case(
        env=env,
        suite=suite,
        case=case_variant,
        client_name="cli_lc_http",
        args=baseline_args,
        request_meta={"method": "GET", "url": base_url, "headers": {}, "body": b""},
        response_meta=response,
        download_count=1 if expect_success else None,
        allowed_exit_codes={0} if expect_success else {6, 7, 35, 58},
    )
    if expect_success:
        baseline_proxy = proxy_observed_for_log(proxy_log, method="GET")
    else:
        apply_error_namespaces(baseline["payload"], kind="proxy_tls", http_status=0)

    proxy_log.write_text("", encoding="utf-8")
    qcurl_env = {
        "QCURL_LC_CASE_ID": "p2_proxy_tls_success" if expect_success else "p2_proxy_tls_failure",
        "QCURL_LC_PROTO": "http/1.1",
        "QCURL_LC_PROXY_PORT": str(proxy_port),
        "QCURL_LC_PROXY_HOST": "127.0.0.1",
        "QCURL_LC_PROXY_USER": str(lc_https_proxy["username"]),
        "QCURL_LC_PROXY_PASS": str(lc_https_proxy["password"]),
        "QCURL_LC_PROXY_TARGET_URL": f"{base_url}?id={qcurl_id}",
        "QCURL_LC_PROXY_TLS_MIN_VERSION": "tls1.2",
        "QCURL_LC_PROXY_TLS_CIPHER_LIST": _TLS12_CIPHER,
        "QCURL_LC_PROXY_TLS13_CIPHERS": "",
        "QCURL_LC_PROXY_VERIFY_PEER": "1" if verify_peer else "0",
        "QCURL_LC_PROXY_VERIFY_HOST": "1" if verify_host else "0",
    }
    if mode == "fail_invalid_cipher":
        qcurl_env["QCURL_LC_PROXY_TLS_CIPHER_LIST"] = "QCURL-INVALID-CIPHER"
    if mode == "fail_unsupported_policy":
        qcurl_env["QCURL_TEST_FORCE_CAPABILITY_ERROR"] = "CURLOPT_PROXY_SSL_CIPHER_LIST"
    if mode != "fail_no_ca":
        qcurl_env["QCURL_LC_PROXY_CA_CERT_PATH"] = str(lc_https_proxy["ca_cert"])
    qcurl = run_qt_test(
        env=env,
        suite=suite,
        case=case_variant,
        qt_executable=require_qcurl_qttest(),
        request_meta={"method": "GET", "url": base_url, "headers": {}, "body": b""},
        response_meta=response,
        download_count=1 if expect_success else None,
        case_env=qcurl_env,
    )
    if expect_success:
        qcurl_proxy = proxy_observed_for_log(proxy_log, method="GET")
        assert baseline_proxy.tls.get("version") == "TLSv1.2"
        assert qcurl_proxy.tls.get("version") == "TLSv1.2"
        assert baseline_proxy.tls.get("cipher") == _TLS12_CIPHER
        assert qcurl_proxy.tls.get("cipher") == _TLS12_CIPHER
        baseline["payload"]["transport"] = {"proxy_tls": baseline_proxy.tls}
        qcurl["payload"]["transport"] = {"proxy_tls": qcurl_proxy.tls}
    else:
        apply_error_namespaces(qcurl["payload"], kind="proxy_tls", http_status=0)

    write_json(baseline["path"], baseline["payload"])
    write_json(qcurl["path"], qcurl["payload"])
    assert_artifacts_match(baseline["path"], qcurl["path"])
