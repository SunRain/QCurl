from __future__ import annotations

from dataclasses import replace
import json
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace

import pytest

from tests.libcurl_consistency import run_gate
from tests.libcurl_consistency.pytest_support.qcurl_runner import run_qt_test


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


def test_http3_preflight_required_records_missing_toolchain(tmp_path) -> None:
    cfg = _cfg(tmp_path)
    gate_env: dict[str, str] = {}

    result = run_gate._evaluate_http3_preflight(
        replace(cfg, with_ext=True),
        gate_env,
        require_http3_enabled=True,
    )

    assert "missing_h3_server" in result["violations"]
    assert "missing_curl_bin" in result["violations"]
    overrides = result["planner_overrides"]
    assert overrides["test_ext_http3_success_h3.py"]["enabled"] is False


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


_QT_PASSED = (
    "PASS   : TestLibcurlConsistency::initTestCase()\n"
    "PASS   : TestLibcurlConsistency::testCase()\n"
    "PASS   : TestLibcurlConsistency::cleanupTestCase()\n"
    "Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted, 1ms\n"
)


def _run_qt_output_fixture(tmp_path, monkeypatch, output: str, returncode: int = 0):
    monkeypatch.setenv("QCURL_LC_ARTIFACTS_DIR", str(tmp_path / "artifacts"))
    monkeypatch.delenv("QCURL_LC_OUT_DIR", raising=False)
    return run_qt_test(
        env=SimpleNamespace(gen_dir=tmp_path, test_timeout=10),
        suite="runner",
        case="completed_case",
        qt_executable=Path(sys.executable),
        args=["-c", f"import sys; print({output!r}, end=''); sys.exit({returncode})"],
        request_meta={"method": "GET", "url": "http://example.test/"},
        response_meta={"status": 200, "http_version": "http/1.1", "body": b"ok"},
    )


def test_qt_runner_accepts_completed_case_with_diagnostic_statistics(tmp_path, monkeypatch) -> None:
    output = _QT_PASSED + "ThreadSanitizer: Matched 1 suppressions (pid=123):\n"

    result = _run_qt_output_fixture(tmp_path, monkeypatch, output)

    payload = json.loads(result["path"].read_text(encoding="utf-8"))
    assert payload["response"]["body_len"] == 2
    assert payload["stdout"] == output.splitlines()


@pytest.mark.parametrize(
    "output",
    [
        "",
        "testCase()\n",
        _QT_PASSED.replace("TestLibcurlConsistency::testCase()", "OtherTest::testCase()"),
        _QT_PASSED.replace("3 passed", "2 passed"),
        _QT_PASSED.replace("0 failed", "1 failed"),
        _QT_PASSED.replace("0 skipped", "1 skipped"),
        _QT_PASSED.replace("0 blacklisted", "1 blacklisted"),
        _QT_PASSED.replace(", 0 blacklisted", ""),
        _QT_PASSED + "Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted, 2ms\n",
    ],
    ids=[
        "empty", "functions-only", "wrong-target", "incomplete-run", "failure",
        "skip", "blacklist", "partial-totals", "duplicate-totals",
    ],
)
def test_qt_runner_rejects_missing_or_incomplete_execution(tmp_path, monkeypatch, output) -> None:
    with pytest.raises(RuntimeError):
        _run_qt_output_fixture(tmp_path, monkeypatch, output)

    assert not list((tmp_path / "artifacts").rglob("qcurl.json"))


def test_qt_runner_preserves_nonzero_exit_even_after_pass_output(tmp_path, monkeypatch) -> None:
    with pytest.raises(RuntimeError, match=r"Qt Test failed \(66\)"):
        _run_qt_output_fixture(tmp_path, monkeypatch, _QT_PASSED, returncode=66)
