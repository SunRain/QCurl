"""P2：mTLS、origin TLS 最低版本与密码套件成对证据。"""

from __future__ import annotations

import re
import uuid
from pathlib import Path

import pytest

from tests.libcurl_consistency.pytest_support.artifacts import apply_error_namespaces, write_json
from tests.libcurl_consistency.pytest_support.baseline import run_libtest_case
from tests.libcurl_consistency.pytest_support.compare import assert_artifacts_match
from tests.libcurl_consistency.pytest_support.observed import observe_http_observed_for_id
from tests.libcurl_consistency.pytest_support.qcurl_runner import require_qcurl_qttest, run_qt_test


_CURLCODE_RE = re.compile(r"curlcode=(\d+)")
_TLS12_CIPHER = "ECDHE-RSA-AES256-GCM-SHA384"
_TLS13_CIPHER = "TLS_AES_256_GCM_SHA384"

_CASES = [
    ("mtls_success", "lc_observe_https_mtls", True),
    ("mtls_failure", "lc_observe_https_mtls", False),
    ("mtls_encrypted_success", "lc_observe_https_mtls", True),
    ("mtls_encrypted_failure", "lc_observe_https_mtls", False),
    ("minimum_tls13", "lc_observe_https_tls13", True),
    ("minimum_tls13_against_tls12", "lc_observe_https_tls12", False),
    ("cipher_tls12", "lc_observe_https_tls12", True),
    ("cipher_tls13", "lc_observe_https_tls13", True),
    ("invalid_cipher", "lc_observe_https", False),
]


def _curlcode(stderr_lines: object) -> int:
    for line in stderr_lines if isinstance(stderr_lines, list) else []:
        match = _CURLCODE_RE.search(str(line))
        if match:
            return int(match.group(1))
    return -1


def _policy_options(case_id: str, server: dict[str, object]) -> tuple[list[str], dict[str, str]]:
    baseline: list[str] = ["--secure", "--cainfo", str(server["ca_cert"])]
    qcurl = {"QCURL_LC_CA_CERT_PATH": str(server["ca_cert"])}
    if case_id == "mtls_success":
        baseline.extend(["--cert", str(server["client_cert"]), "--key", str(server["client_key"])])
        qcurl.update({
            "QCURL_LC_CLIENT_CERT_PATH": str(server["client_cert"]),
            "QCURL_LC_CLIENT_KEY_PATH": str(server["client_key"]),
        })
    elif case_id in {"mtls_encrypted_success", "mtls_encrypted_failure"}:
        password = str(server["client_key_password"])
        if case_id == "mtls_encrypted_failure":
            password = f"wrong-{password}"
        baseline.extend([
            "--cert",
            str(server["client_cert"]),
            "--key",
            str(server["client_key_encrypted"]),
            "--key-pass",
            password,
        ])
        qcurl.update({
            "QCURL_LC_CLIENT_CERT_PATH": str(server["client_cert"]),
            "QCURL_LC_CLIENT_KEY_PATH": str(server["client_key_encrypted"]),
            "QCURL_LC_CLIENT_KEY_PASSWORD": password,
        })
    elif case_id == "minimum_tls13":
        baseline.extend(["--tls-min", "tls1.3"])
        qcurl["QCURL_LC_TLS_MIN_VERSION"] = "tls1.3"
    elif case_id == "minimum_tls13_against_tls12":
        baseline.extend(["--tls-min", "tls1.3"])
        qcurl["QCURL_LC_TLS_MIN_VERSION"] = "tls1.3"
    elif case_id == "cipher_tls12":
        baseline.extend(["--tls-min", "tls1.2", "--cipher-list", _TLS12_CIPHER])
        qcurl.update({
            "QCURL_LC_TLS_MIN_VERSION": "tls1.2",
            "QCURL_LC_TLS_CIPHER_LIST": _TLS12_CIPHER,
        })
    elif case_id == "cipher_tls13":
        baseline.extend(["--tls-min", "tls1.3", "--tls13-ciphers", _TLS13_CIPHER])
        qcurl.update({
            "QCURL_LC_TLS_MIN_VERSION": "tls1.3",
            "QCURL_LC_TLS13_CIPHERS": _TLS13_CIPHER,
        })
    elif case_id == "invalid_cipher":
        baseline.extend(["--cipher-list", "QCURL-INVALID-CIPHER"])
        qcurl["QCURL_LC_TLS_CIPHER_LIST"] = "QCURL-INVALID-CIPHER"
    return baseline, qcurl


@pytest.mark.parametrize("case_id,fixture_name,expect_success", _CASES)
def test_p2_tls_policy(case_id, fixture_name, expect_success, request, env):
    server = request.getfixturevalue(fixture_name)
    qt_path = require_qcurl_qttest()
    port = int(server["port"])
    observe_log = Path(str(server["log_file"]))
    trace = f"lc_{uuid.uuid4().hex[:8]}_{case_id}"
    baseline_id = f"{trace}__baseline"
    qcurl_id = f"{trace}__qcurl"
    base_url = f"https://localhost:{port}/cookie"
    suite = "p2_tls_policy"
    case_variant = f"lc_tls_policy_{case_id}"
    response = {
        "status": 200 if expect_success else 0,
        "http_version": "http/1.1" if expect_success else "tls",
        "headers": {},
        "body": None,
    }
    baseline_options, qcurl_options = _policy_options(case_id, server)

    baseline = run_libtest_case(
        env=env,
        suite=suite,
        case=case_variant,
        client_name="cli_lc_http",
        args=["-V", "http/1.1", *baseline_options, f"{base_url}?id={baseline_id}"],
        request_meta={"method": "GET", "url": base_url, "headers": {}, "body": b""},
        response_meta=response,
        download_count=1 if expect_success else None,
        allowed_exit_codes={0} if expect_success else {6, 7, 35, 58},
    )
    if not expect_success:
        assert _curlcode(baseline["payload"].get("stderr")) > 0
        apply_error_namespaces(baseline["payload"], kind="tls", http_status=0)

    qcurl_env = {
        "QCURL_LC_CASE_ID": "p2_tls_policy_success" if expect_success else "p2_tls_policy_failure",
        "QCURL_LC_PROTO": "http/1.1",
        "QCURL_LC_REQ_ID": qcurl_id,
        "QCURL_LC_OBSERVE_HTTPS_PORT": str(port),
        **qcurl_options,
    }
    if case_id.startswith("mtls_"):
        qcurl_env["QCURL_LC_CASE_ID"] = (
            "p2_tls_mtls_success" if expect_success else "p2_tls_mtls_failure"
        )
    qcurl = run_qt_test(
        env=env,
        suite=suite,
        case=case_variant,
        qt_executable=qt_path,
        request_meta={"method": "GET", "url": base_url, "headers": {}, "body": b""},
        response_meta=response,
        download_count=1 if expect_success else None,
        case_env=qcurl_env,
    )
    if not expect_success:
        apply_error_namespaces(qcurl["payload"], kind="tls", http_status=0)

    if expect_success:
        baseline_observed = observe_http_observed_for_id(observe_log, baseline_id)
        qcurl_observed = observe_http_observed_for_id(observe_log, qcurl_id)
        baseline["payload"]["transport"] = {"tls": baseline_observed.tls}
        qcurl["payload"]["transport"] = {"tls": qcurl_observed.tls}
        if case_id == "mtls_success":
            assert baseline_observed.tls.get("client_cert") is True
            assert qcurl_observed.tls.get("client_cert") is True
        if case_id == "cipher_tls12":
            assert baseline_observed.tls.get("version") == "TLSv1.2"
            assert baseline_observed.tls.get("cipher") == _TLS12_CIPHER
        if case_id in {"minimum_tls13", "cipher_tls13"}:
            assert baseline_observed.tls.get("version") == "TLSv1.3"
        if case_id == "cipher_tls13":
            assert baseline_observed.tls.get("cipher") == _TLS13_CIPHER

    write_json(baseline["path"], baseline["payload"])
    write_json(qcurl["path"], qcurl["payload"])
    assert_artifacts_match(baseline["path"], qcurl["path"])
