"""P1-2 负向探针：验证 legacy-only error 必须被 CTBP 校验拒绝。"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from tests.uce.ctbp.validate import validate_ctbp


def _write_artifact(path: Path, *, runner: str, ctbp: dict[str, Any], response: dict[str, Any], legacy_error_kind: str = "") -> None:
    """写入仅含 legacy payload.error 的 artifact（应被拒绝的形态）。"""
    path.parent.mkdir(parents=True, exist_ok=True)
    payload: dict[str, Any] = {
        "schema": "qcurl-lc/artifacts@v1",
        "runner": runner,
        "request": {"method": "GET", "url": "https://example.test/data", "headers": {}, "body_len": 0, "body_sha256": ""},
        "response": response,
        "ctbp": ctbp,
    }
    if legacy_error_kind:
        # 只写 legacy payload.error，不写 observed.error 和 derived.error
        payload["error"] = {"kind": legacy_error_kind, "http_status": int(response.get("status") or 0)}
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def _tls_ctbp(*, expected_result: str) -> dict[str, Any]:
    payload: dict[str, Any] = {
        "schema": "qcurl-lc/ctbp@v1",
        "kind": "tls_boundary",
        "boundary": {
            "scheme": "https",
            "http_version": "http/1.1",
            "proxy_mode": "direct",
            "alpn": "http/1.1",
            "sni": "localhost",
            "client_cert": False,
            "pinning": "none",
            "verify_peer": True,
            "verify_host": True,
            "ca_cert": expected_result == "pass",
        },
        "boundary_key": f"tls:{expected_result}",
        "expected_result": expected_result,
    }
    if expected_result == "tls_error":
        payload["expected_error_kind"] = "tls"
    return payload


def test_validate_ctbp_rejects_legacy_only_error(tmp_path: Path) -> None:
    """验证只写 legacy payload.error 的 artifact 必须产生 violation。

    这是 P1-2 修复的首个证明点：改动前为 PASS（假绿），改动后必须被断言捕获。
    """
    artifacts_root = tmp_path / "artifacts"

    # 四个 runner/kind 完整工件，但只保留顶层 error.kind=tls（legacy-only 形态）
    _write_artifact(
        artifacts_root / "p0_conn" / "p0_connection_reuse_keepalive_http_1.1" / "baseline.json",
        runner="libcurl",
        ctbp={
            "schema": "qcurl-lc/ctbp@v1",
            "kind": "connection_reuse",
            "boundary": {"scheme": "http", "http_version": "http/1.1", "proxy_mode": "direct", "alpn": "http/1.1", "sni": False, "client_cert": False, "pinning": "none"},
            "boundary_key": "scheme=http;http_version=http/1.1",
            "connection_group_id": "p0_connection_reuse",
            "request_count": 3,
            "unique_connections": 1,
            "expected_unique_connections": 1,
            "conn_seq": [1, 1, 1],
            "requests": [
                {"seq": 1, "conn_id": 1, "boundary_key": "scheme=http;http_version=http/1.1"},
                {"seq": 2, "conn_id": 1, "boundary_key": "scheme=http;http_version=http/1.1"},
                {"seq": 3, "conn_id": 1, "boundary_key": "scheme=http;http_version=http/1.1"},
            ],
        },
        response={"status": 200, "http_version": "http/1.1", "headers": {}, "body_len": 0, "body_sha256": ""},
    )
    _write_artifact(
        artifacts_root / "p0_conn" / "p0_connection_reuse_keepalive_http_1.1" / "qcurl.json",
        runner="qcurl",
        ctbp={
            "schema": "qcurl-lc/ctbp@v1",
            "kind": "connection_reuse",
            "boundary": {"scheme": "http", "http_version": "http/1.1", "proxy_mode": "direct", "alpn": "http/1.1", "sni": False, "client_cert": False, "pinning": "none"},
            "boundary_key": "scheme=http;http_version=http/1.1",
            "connection_group_id": "p0_connection_reuse",
            "request_count": 3,
            "unique_connections": 1,
            "expected_unique_connections": 1,
            "conn_seq": [1, 1, 1],
            "requests": [
                {"seq": 1, "conn_id": 1, "boundary_key": "scheme=http;http_version=http/1.1"},
                {"seq": 2, "conn_id": 1, "boundary_key": "scheme=http;http_version=http/1.1"},
                {"seq": 3, "conn_id": 1, "boundary_key": "scheme=http;http_version=http/1.1"},
            ],
        },
        response={"status": 200, "http_version": "http/1.1", "headers": {}, "body_len": 0, "body_sha256": ""},
    )

    # TLS boundary：legacy-only error.kind=tls（不写 observed/derived）
    _write_artifact(
        artifacts_root / "p2_tls" / "lc_tls_verify_fail_no_ca" / "baseline.json",
        runner="libcurl",
        ctbp=_tls_ctbp(expected_result="tls_error"),
        response={"status": 0, "http_version": "tls", "headers": {}, "body_len": 0, "body_sha256": ""},
        legacy_error_kind="tls",
    )
    _write_artifact(
        artifacts_root / "p2_tls" / "lc_tls_verify_fail_no_ca" / "qcurl.json",
        runner="qcurl",
        ctbp=_tls_ctbp(expected_result="tls_error"),
        response={"status": 0, "http_version": "tls", "headers": {}, "body_len": 0, "body_sha256": ""},
        legacy_error_kind="tls",
    )

    report = validate_ctbp(Path("tests/uce/contracts/ctbp@v1.yaml"), [artifacts_root])

    # 修复后必须产生 violation：legacy-only 形态不提供 observed/derived error
    assert report["policy_violations"] == ["ctbp_contract_failed"], f"Expected contract failure, got {report['policy_violations']}"
    failed = [item for item in report["entries"] if item["result"] == "fail"]
    assert len(failed) == 2, f"Expected 2 TLS failures (libcurl + qcurl), got {len(failed)}"
    assert all(item["kind"] == "tls_boundary" for item in failed), "All failures must be tls_boundary"
