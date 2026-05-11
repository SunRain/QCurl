from __future__ import annotations

from dataclasses import replace
import json
from pathlib import Path
import subprocess
import sys

import pytest

from tests.libcurl_consistency import run_gate


def _cfg(tmp_path: Path) -> run_gate.GateConfig:
    return run_gate.GateConfig(
        repo_root=tmp_path,
        qcurl_build_dir=tmp_path / "build",
        curl_build_dir=tmp_path / "build" / "curl",
        capability_manifest=tmp_path / "capabilities.json",
        suite="p0",
        build=False,
        with_ext=False,
        junit_xml=tmp_path / "reports" / "junit.xml",
        json_report=tmp_path / "reports" / "report.json",
        qt_timeout_s=90,
    )


def test_parse_junit_counts_handles_missing_file(tmp_path) -> None:
    result = run_gate._parse_junit_counts(tmp_path / "missing.xml")

    assert result["exists"] is False
    assert result["tests"] == 0


def test_parse_junit_counts_reports_parse_failure(tmp_path) -> None:
    junit = tmp_path / "junit.xml"
    junit.write_text("<testsuite>", encoding="utf-8")

    result = run_gate._parse_junit_counts(junit)

    assert result["exists"] is True
    assert "parse_error" in result


def test_parse_junit_counts_reads_testsuite_attributes(tmp_path) -> None:
    junit = tmp_path / "junit.xml"
    junit.write_text('<testsuite tests="3" failures="1" errors="0" skipped="2"/>', encoding="utf-8")

    assert run_gate._parse_junit_counts(junit) == {
        "exists": True,
        "tests": 3,
        "failures": 1,
        "errors": 0,
        "skipped": 2,
    }


def test_artifact_schema_check_finds_missing_required_fields(tmp_path) -> None:
    cfg = _cfg(tmp_path)
    artifact_dir = tmp_path / "curl" / "tests" / "http" / "gen" / "artifacts" / "p0" / "case"
    artifact_dir.mkdir(parents=True)
    bad = artifact_dir / "baseline.json"
    bad.write_text(json.dumps({"schema": "qcurl-lc/artifacts@v1"}), encoding="utf-8")

    result = run_gate._postflight_artifacts_schema_check(cfg, since_ts=0.0)

    assert result["scanned_files"] == 1
    assert result["violations"]
    assert result["violations"][0]["reason"] == "runner missing or invalid"


def test_redaction_scan_detects_sensitive_token(tmp_path) -> None:
    cfg = _cfg(tmp_path)
    report = tmp_path / "reports" / "leak.txt"
    report.parent.mkdir()
    report.write_text("Authorization: Bearer abc\n", encoding="utf-8")

    result = run_gate._postflight_redaction_scan(cfg, since_ts=0.0)

    assert result["scanned_files"] == 1
    assert result["violations"]


def test_http3_preflight_required_records_missing_toolchain(tmp_path, monkeypatch) -> None:
    cfg = _cfg(tmp_path)
    manifest: dict[str, object] = {}
    report: dict[str, object] = {
        "warnings": [],
        "preflight_http3_required": {"enabled": True, "violations": []},
    }
    gate_env: dict[str, str] = {}

    updated = run_gate._apply_http3_preflight_to_manifest(
        replace(cfg, with_ext=True),
        gate_env,
        manifest,
        report,
        require_http3_enabled=True,
    )

    preflight = report["preflight_http3_required"]
    assert "missing_h3_server" in preflight["violations"]
    assert "missing_curl_bin" in preflight["violations"]
    tests = updated["tests"]
    assert tests["test_ext_http3_success_h3.py"]["enabled"] is False


def test_preflight_required_inputs_fails_before_empty_pytest_plan(tmp_path) -> None:
    cfg = _cfg(tmp_path)
    report: dict[str, object] = {}
    gate_env = {"QCURL_QTTEST": str(tmp_path / "build" / "tests" / "missing")}

    with pytest.raises(RuntimeError, match="QCURL_QTTEST binary missing"):
        run_gate._preflight_required_inputs(cfg, gate_env, [], report)


def test_run_gate_script_help_imports_helpers_from_repo_root() -> None:
    script = Path(__file__).with_name("run_gate.py")

    proc = subprocess.run(
        [sys.executable, str(script), "--help"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )

    assert proc.returncode == 0
    assert "--suite" in proc.stdout


def test_http_observe_server_script_help_imports_helpers_from_repo_root() -> None:
    script = Path(__file__).with_name("http_observe_server.py")

    proc = subprocess.run(
        [sys.executable, str(script), "--help"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )

    assert proc.returncode == 0
    assert "observable HTTP server" in proc.stdout
