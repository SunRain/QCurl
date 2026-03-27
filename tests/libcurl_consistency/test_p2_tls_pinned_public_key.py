"""
P2：TLS pinned public key 语义一致性。

复用本地测试证书，并在运行时计算 `sha256//...` pinned key。
"""

from __future__ import annotations

import base64
import os
import re
import shutil
import subprocess
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


def _tls_boundary(*, proto: str) -> dict[str, object]:
    return {
        "scheme": "https",
        "http_version": proto,
        "proxy_mode": "direct",
        "alpn": proto,
        "sni": "localhost",
        "client_cert": False,
        "pinning": "sha256_public_key",
        "verify_peer": True,
        "verify_host": True,
        "ca_cert": True,
    }


def _boundary_key(boundary: dict[str, object]) -> str:
    return ";".join(f"{key}={boundary[key]}" for key in sorted(boundary))


def _make_ctbp_payload(*, proto: str, mode: str) -> dict[str, object]:
    boundary = _tls_boundary(proto=proto)
    payload: dict[str, object] = {
        "schema": "qcurl-lc/ctbp@v1",
        "kind": "tls_boundary",
        "boundary": boundary,
        "boundary_key": _boundary_key(boundary),
        "expected_result": "pass" if mode == "match" else "tls_error",
    }
    if mode != "match":
        payload["expected_error_kind"] = "tls"
    return payload


def _pinned_pubkey_sha256_from_cert(cert_path: Path) -> str:
    if not cert_path.exists():
        raise FileNotFoundError(f"cert not found: {cert_path}")
    openssl = shutil.which("openssl")  # type: ignore[name-defined]
    if not openssl:
        raise RuntimeError("openssl not found")

    # openssl x509 -pubkey -noout | openssl pkey -pubin -outform DER | openssl dgst -sha256 -binary | base64
    p1 = subprocess.run(
        [openssl, "x509", "-in", str(cert_path), "-pubkey", "-noout"],
        capture_output=True,
        check=False,
    )
    if p1.returncode != 0:
        raise RuntimeError(f"openssl x509 failed: {p1.stderr.decode(errors='replace')}")

    p2 = subprocess.run(
        [openssl, "pkey", "-pubin", "-outform", "DER"],
        input=p1.stdout,
        capture_output=True,
        check=False,
    )
    if p2.returncode != 0:
        raise RuntimeError(f"openssl pkey failed: {p2.stderr.decode(errors='replace')}")

    p3 = subprocess.run(
        [openssl, "dgst", "-sha256", "-binary"],
        input=p2.stdout,
        capture_output=True,
        check=False,
    )
    if p3.returncode != 0:
        raise RuntimeError(f"openssl dgst failed: {p3.stderr.decode(errors='replace')}")

    b64 = base64.b64encode(p3.stdout).decode("ascii").strip()
    return f"sha256//{b64}"


def _mutate_pinned(pinned: str) -> str:
    if not pinned.startswith("sha256//"):
        return "sha256//AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="
    b64 = pinned[len("sha256//") :]
    if not b64:
        return "sha256//AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="
    last = "A" if b64[-1] != "A" else "B"
    return "sha256//" + b64[:-1] + last


@pytest.mark.parametrize("mode", ["match", "mismatch"])
def test_p2_tls_pinned_public_key(mode: str, env, lc_logs, lc_observe_https, tmp_path):
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("当前环境未提供 QCURL_QTTEST 可执行文件，跳过该用例")

    collect_logs = should_collect_service_logs()
    port = int(lc_observe_https["port"])
    ca_cert = str(lc_observe_https["ca_cert"])
    cert = Path(str(lc_observe_https["cert"]))

    try:
        pinned = _pinned_pubkey_sha256_from_cert(cert)
    except Exception as e:
        pytest.skip(f"pinned public key 计算失败: {e}")

    if mode == "mismatch":
        pinned = _mutate_pinned(pinned)

    suite = "p2_tls"
    proto = "http/1.1"
    case_variant = f"lc_tls_pinned_public_key_{mode}"

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_tls_pinned_{mode}"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    url = f"https://localhost:{port}/cookie"
    req_meta = {"method": "GET", "url": url, "headers": {}, "body": b""}
    if mode == "match":
        resp_meta = {"status": 200, "http_version": proto, "headers": {}, "body": b"cookie-ok\n"}
    else:
        resp_meta = {"status": 0, "http_version": "tls", "headers": {}, "body": None}

    try:
        baseline_args = ["-V", proto, "--secure", "--cainfo", ca_cert, "--pinned-public-key", pinned, url]
        baseline = run_libtest_case(
            env=env,
            suite=suite,
            case=case_variant,
            client_name="cli_lc_http",
            args=baseline_args,
            request_meta=req_meta,
            response_meta=resp_meta,
            allowed_exit_codes={0, 6} if mode == "match" else {0, 6, 7},
        )
        if int(baseline["payload"].get("exit_code") or 0) == 6:
            pytest.skip("当前 libcurl 不支持 CURLOPT_PINNEDPUBLICKEY，跳过 pinned public key 用例")
        if mode != "match":
            curlcode = _parse_curlcode(baseline["payload"].get("stderr"))
            assert curlcode == 90, f"unexpected curlcode: {curlcode}"
            apply_error_namespaces(baseline["payload"], kind="tls", http_status=0)
        baseline["payload"]["ctbp"] = _make_ctbp_payload(proto=proto, mode=mode)
        write_json(baseline["path"], baseline["payload"])

        qcurl_env = {
            "QCURL_LC_CASE_ID": "p2_tls_pinned_public_key_match" if mode == "match" else "p2_tls_pinned_public_key_mismatch",
            "QCURL_LC_PROTO": proto,
            "QCURL_LC_REQ_ID": qcurl_req_id,
            "QCURL_LC_OBSERVE_HTTPS_PORT": str(port),
            "QCURL_LC_CA_CERT_PATH": ca_cert,
            "QCURL_LC_PINNED_PUBLIC_KEY": pinned,
        }
        qcurl = run_qt_test(
            env=env,
            suite=suite,
            case=case_variant,
            qt_executable=qt_path,
            args=[],
            request_meta=req_meta,
            response_meta=resp_meta,
            case_env=qcurl_env,
        )
        if mode != "match":
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
                    "case_id": "p2_tls_pinned_public_key",
                    "mode": mode,
                    "baseline_req_id": baseline_req_id,
                    "qcurl_req_id": qcurl_req_id,
                    "observe_https_port": port,
                },
            )
        raise
