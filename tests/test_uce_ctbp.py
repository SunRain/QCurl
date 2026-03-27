from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from tests.uce.ctbp.validate import validate_ctbp


def _write_artifact(path: Path, *, runner: str, ctbp: dict[str, Any], response: dict[str, Any], error_kind: str = "") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload: dict[str, Any] = {
        "schema": "qcurl-lc/artifacts@v1",
        "runner": runner,
        "request": {"method": "GET", "url": "https://example.test/data", "headers": {}, "body_len": 0, "body_sha256": ""},
        "response": response,
        "ctbp": ctbp,
    }
    if error_kind:
        payload["derived"] = {"error": {"kind": error_kind}}
        payload["error"] = {"kind": error_kind, "http_status": int(response.get("status") or 0)}
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def _connection_ctbp(boundary_key: str = "scheme=http;http_version=http/1.1") -> dict[str, Any]:
    return {
        "schema": "qcurl-lc/ctbp@v1",
        "kind": "connection_reuse",
        "boundary": {
            "scheme": "http",
            "http_version": "http/1.1",
            "proxy_mode": "direct",
            "alpn": "http/1.1",
            "sni": False,
            "client_cert": False,
            "pinning": "none",
        },
        "boundary_key": boundary_key,
        "connection_group_id": "p0_connection_reuse",
        "request_count": 3,
        "unique_connections": 1,
        "expected_unique_connections": 1,
        "conn_seq": [1, 1, 1],
        "requests": [
            {"seq": 1, "conn_id": 1, "boundary_key": boundary_key},
            {"seq": 2, "conn_id": 1, "boundary_key": boundary_key},
            {"seq": 3, "conn_id": 1, "boundary_key": boundary_key},
        ],
    }


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


def test_validate_ctbp_accepts_complete_coverage(tmp_path: Path) -> None:
    artifacts_root = tmp_path / "artifacts"

    _write_artifact(
        artifacts_root / "p0_conn" / "p0_connection_reuse_keepalive_http_1.1" / "baseline.json",
        runner="baseline",
        ctbp=_connection_ctbp(),
        response={"status": 200, "http_version": "http/1.1", "headers": {}, "body_len": 0, "body_sha256": ""},
    )
    _write_artifact(
        artifacts_root / "p0_conn" / "p0_connection_reuse_keepalive_http_1.1" / "qcurl.json",
        runner="qcurl",
        ctbp=_connection_ctbp(),
        response={"status": 200, "http_version": "http/1.1", "headers": {}, "body_len": 0, "body_sha256": ""},
    )
    _write_artifact(
        artifacts_root / "p2_tls" / "lc_tls_verify_success" / "baseline.json",
        runner="baseline",
        ctbp=_tls_ctbp(expected_result="pass"),
        response={"status": 200, "http_version": "http/1.1", "headers": {}, "body_len": 0, "body_sha256": ""},
    )
    _write_artifact(
        artifacts_root / "p2_tls" / "lc_tls_verify_success" / "qcurl.json",
        runner="qcurl",
        ctbp=_tls_ctbp(expected_result="pass"),
        response={"status": 200, "http_version": "http/1.1", "headers": {}, "body_len": 0, "body_sha256": ""},
    )

    report = validate_ctbp(Path("tests/uce/contracts/ctbp@v1.yaml"), [artifacts_root])

    assert report["policy_violations"] == []
    assert report["summary"]["entry_count"] == 4
    assert report["runner_summary"]["baseline"]["kinds"] == ["connection_reuse", "tls_boundary"]
    assert report["runner_summary"]["qcurl"]["kinds"] == ["connection_reuse", "tls_boundary"]


def test_validate_ctbp_reports_missing_runner_kind_pairs(tmp_path: Path) -> None:
    artifacts_root = tmp_path / "artifacts"

    _write_artifact(
        artifacts_root / "p0_conn" / "p0_connection_reuse_keepalive_http_1.1" / "baseline.json",
        runner="baseline",
        ctbp=_connection_ctbp(),
        response={"status": 200, "http_version": "http/1.1", "headers": {}, "body_len": 0, "body_sha256": ""},
    )

    report = validate_ctbp(Path("tests/uce/contracts/ctbp@v1.yaml"), [artifacts_root])

    assert report["policy_violations"] == ["ctbp_evidence_missing"]
    missing_pairs = {(item["runner"], item["kind"]) for item in report["violations"]}
    assert ("qcurl", "connection_reuse") in missing_pairs
    assert ("baseline", "tls_boundary") in missing_pairs
    assert ("qcurl", "tls_boundary") in missing_pairs


def test_validate_ctbp_detects_contract_failures(tmp_path: Path) -> None:
    artifacts_root = tmp_path / "artifacts"

    invalid_connection = _connection_ctbp()
    invalid_connection["unique_connections"] = 2
    invalid_connection["conn_seq"] = [1, 1, 1]
    _write_artifact(
        artifacts_root / "p0_conn" / "p0_connection_reuse_keepalive_http_1.1" / "baseline.json",
        runner="baseline",
        ctbp=invalid_connection,
        response={"status": 200, "http_version": "http/1.1", "headers": {}, "body_len": 0, "body_sha256": ""},
    )
    _write_artifact(
        artifacts_root / "p0_conn" / "p0_connection_reuse_keepalive_http_1.1" / "qcurl.json",
        runner="qcurl",
        ctbp=_connection_ctbp(),
        response={"status": 200, "http_version": "http/1.1", "headers": {}, "body_len": 0, "body_sha256": ""},
    )

    tls_error = _tls_ctbp(expected_result="tls_error")
    _write_artifact(
        artifacts_root / "p2_tls" / "lc_tls_verify_fail_no_ca" / "baseline.json",
        runner="baseline",
        ctbp=tls_error,
        response={"status": 0, "http_version": "tls", "headers": {}, "body_len": 0, "body_sha256": ""},
        error_kind="tls",
    )
    _write_artifact(
        artifacts_root / "p2_tls" / "lc_tls_verify_fail_no_ca" / "qcurl.json",
        runner="qcurl",
        ctbp=tls_error,
        response={"status": 200, "http_version": "http/1.1", "headers": {}, "body_len": 0, "body_sha256": ""},
        error_kind="",
    )

    report = validate_ctbp(Path("tests/uce/contracts/ctbp@v1.yaml"), [artifacts_root])

    assert report["policy_violations"] == ["ctbp_contract_failed"]
    failed = [item for item in report["entries"] if item["result"] == "fail"]
    assert len(failed) == 2
    assert any(item["kind"] == "connection_reuse" for item in failed)
    assert any(item["kind"] == "tls_boundary" for item in failed)
