"""Report helpers for the libcurl consistency gate."""

from __future__ import annotations

from pathlib import Path
from typing import Any
import re
import xml.etree.ElementTree as ET


def create_initial_report(
    cfg: Any,
    gate_env: dict[str, str],
    *,
    require_http3_raw: str,
    require_http3_enabled: bool,
) -> dict[str, object]:
    """Create the stable JSON report skeleton for a gate run."""

    return {
        "suite": cfg.suite,
        "with_ext": cfg.with_ext,
        "build": cfg.build,
        "repo_root": str(cfg.repo_root),
        "qcurl_build_dir": str(cfg.qcurl_build_dir),
        "curl_build_dir": str(cfg.curl_build_dir),
        "capability_manifest_path": str(cfg.capability_manifest),
        "junit_xml": str(cfg.junit_xml),
        "json_report": str(cfg.json_report),
        "run_id": str(getattr(cfg, "run_id", "")),
        "run_dir": str(getattr(cfg, "run_dir", None) or cfg.json_report.parent),
        "artifacts_dir": str(getattr(cfg, "artifacts_dir", None) or ""),
        "qt_timeout_s": cfg.qt_timeout_s,
        "commands": [],
        "pytest_files": [],
        "env": {
            "QCURL_QTTEST": str(gate_env.get("QCURL_QTTEST") or ""),
            "QCURL_LC_COLLECT_LOGS": str(gate_env.get("QCURL_LC_COLLECT_LOGS") or "0"),
            "QCURL_LC_QTTEST_TIMEOUT": str(gate_env.get("QCURL_LC_QTTEST_TIMEOUT") or cfg.qt_timeout_s),
            "QCURL_LC_EXT": "1" if str(gate_env.get("QCURL_LC_EXT") or "0") == "1" else "0",
            "QCURL_LC_EXPECT100_REPEAT": str(gate_env.get("QCURL_LC_EXPECT100_REPEAT") or ""),
            "QCURL_LC_CAPABILITY_MANIFEST": str(gate_env.get("QCURL_LC_CAPABILITY_MANIFEST") or ""),
            "QCURL_REQUIRE_HTTP3": require_http3_raw if require_http3_raw else "0",
            "CURL_BUILD_DIR": str(gate_env.get("CURL_BUILD_DIR") or ""),
            "CURL": str(gate_env.get("CURL") or ""),
            "CURLINFO": str(gate_env.get("CURLINFO") or ""),
        },
        "warnings": [],
        "preflight_http3_required": {
            "enabled": require_http3_enabled,
            "have_h3_server": None,
            "have_h3_curl": None,
            "violations": [],
        },
    }


def redact_text(text: str) -> str:
    """Redact sensitive headers before gate text is persisted."""

    if not text:
        return ""

    patterns: list[tuple[re.Pattern[str], str]] = [
        (re.compile(r"(?im)^(\s*authorization:\s*)([^\r\n]+)$"), r"\1<REDACTED>"),
        (re.compile(r"(?im)^(\s*proxy-authorization:\s*)([^\r\n]+)$"), r"\1<REDACTED>"),
        (re.compile(r"(?im)^(\s*cookie:\s*)([^\r\n]+)$"), r"\1<REDACTED>"),
        (re.compile(r"(?im)^(\s*set-cookie:\s*)([^\r\n]+)$"), r"\1<REDACTED>"),
        (re.compile(r"(?i)(\"authorization\"\s*:\s*\")([^\"]+)(\")"), r"\1<REDACTED>\3"),
        (re.compile(r"(?i)(\"proxy-authorization\"\s*:\s*\")([^\"]+)(\")"), r"\1<REDACTED>\3"),
        (re.compile(r"(?i)(\"cookie\"\s*:\s*\")([^\"]+)(\")"), r"\1<REDACTED>\3"),
        (re.compile(r"(?i)(\"set-cookie\"\s*:\s*\")([^\"]+)(\")"), r"\1<REDACTED>\3"),
    ]
    out = text
    for rx, repl in patterns:
        out = rx.sub(repl, out)
    return out


def artifacts_dir(repo_root: Path) -> Path:
    """Return the libcurl artifact directory used by the consistency gate."""

    return repo_root / "curl" / "tests" / "http" / "gen" / "artifacts"


def redaction_scan_roots(
    repo_root: Path,
    reports_dir: Path,
    artifacts_root: Path | None = None,
) -> list[Path]:
    """返回本轮报告和工件目录，避免扫描共享的历史生成目录。"""

    roots: list[Path] = []
    if reports_dir.exists():
        roots.append(reports_dir)
    if artifacts_root and artifacts_root.exists() and artifacts_root != reports_dir:
        roots.append(artifacts_root)
    return roots


def parse_junit_counts(junit_xml: Path) -> dict[str, object]:
    """Parse pytest JUnit counts and report parse/missing errors as data."""

    if not junit_xml.exists():
        return {
            "exists": False,
            "tests": 0,
            "failures": 0,
            "errors": 0,
            "skipped": 0,
            "parse_error": f"junit xml not found: {junit_xml}",
        }
    try:
        root = ET.parse(junit_xml).getroot()
    except Exception as exc:
        return {
            "exists": True,
            "tests": 0,
            "failures": 0,
            "errors": 0,
            "skipped": 0,
            "parse_error": f"failed to parse junit xml: {exc}",
        }

    suites = []
    if root.tag == "testsuites":
        suites = list(root.findall("testsuite"))
    elif root.tag == "testsuite":
        suites = [root]
    else:
        suites = list(root.findall(".//testsuite"))

    def attr_int(node: ET.Element, key: str) -> int:
        raw = (node.attrib.get(key) or "").strip()
        try:
            return int(raw) if raw else 0
        except ValueError:
            return 0

    totals = {"tests": 0, "failures": 0, "errors": 0, "skipped": 0}
    for suite in suites:
        for key in totals:
            totals[key] += attr_int(suite, key)

    return {"exists": True, **totals}


def policy_violations_from_report(report: dict[str, object]) -> list[str]:
    """Derive gate policy violation codes from postflight report data."""

    policy_violations: list[str] = []
    junit_counts = report.get("junit_counts") or {}
    if isinstance(junit_counts, dict):
        if junit_counts.get("parse_error"):
            policy_violations.append("junit_parse_error")
        if int(junit_counts.get("tests") or 0) <= 0:
            policy_violations.append("no_tests_executed")
        if int(junit_counts.get("skipped") or 0) > 0:
            policy_violations.append("skipped_tests")
    if (report.get("postflight_artifacts_schema_check") or {}).get("violations"):
        policy_violations.append("artifacts_schema")
    if (report.get("postflight_redaction_scan") or {}).get("violations"):
        policy_violations.append("redaction")
    execution_contract = report.get("execution_contract") or {}
    if isinstance(execution_contract, dict) and execution_contract.get("violations"):
        policy_violations.append("execution_contract")
    evidence_integrity = report.get("evidence_integrity") or {}
    if isinstance(evidence_integrity, dict) and evidence_integrity.get("errors"):
        policy_violations.append("evidence_integrity")
    preflight_http3 = report.get("preflight_http3_required") or {}
    if (
        isinstance(preflight_http3, dict)
        and preflight_http3.get("enabled")
        and preflight_http3.get("violations")
    ):
        policy_violations.append("http3_required")
    return policy_violations
