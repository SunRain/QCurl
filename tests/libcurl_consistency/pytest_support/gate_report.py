"""Report helpers for the libcurl consistency gate."""

from __future__ import annotations

from pathlib import Path
from typing import Any
import json
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


def redaction_scan_roots(repo_root: Path, reports_dir: Path) -> list[Path]:
    """Return existing roots that should be scanned for sensitive values."""

    roots: list[Path] = []
    candidate_artifacts = artifacts_dir(repo_root)
    if candidate_artifacts.exists():
        roots.append(candidate_artifacts)
    if reports_dir.exists():
        roots.append(reports_dir)
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


def postflight_artifacts_schema_check(
    *,
    repo_root: Path,
    artifacts_dir: Path,
    since_ts: float,
    expected_schema: str,
) -> dict[str, object]:
    """Validate artifacts schema and required request/response fields."""

    if not artifacts_dir.exists():
        return {
            "schema_expected": expected_schema,
            "since_ts": float(since_ts),
            "scanned_files": 0,
            "violations": [],
            "note": "artifacts dir not found",
        }

    targets: list[Path] = []
    for name in ("baseline.json", "qcurl.json"):
        for path in artifacts_dir.rglob(name):
            if not path.is_file():
                continue
            try:
                if float(path.stat().st_mtime) < (float(since_ts) - 1.0):
                    continue
            except OSError:
                continue
            targets.append(path)

    def rel(path: Path) -> str:
        try:
            return str(path.relative_to(repo_root))
        except Exception:
            return str(path)

    required_request_fields = ("method", "url", "headers", "body_len", "body_sha256")
    required_response_fields = ("status", "http_version", "headers", "body_len", "body_sha256")
    violations: list[dict[str, object]] = []

    def add_violation(path: Path, reason: str) -> None:
        violations.append({"file": rel(path), "reason": reason})

    for path in sorted(targets):
        try:
            payload = json.loads(path.read_text(encoding="utf-8", errors="replace"))
        except Exception as exc:
            add_violation(path, f"invalid json: {exc}")
            continue

        schema = payload.get("schema")
        if schema != expected_schema:
            add_violation(path, f"schema mismatch: {schema!r} != {expected_schema!r}")
            continue

        runner = payload.get("runner")
        if not isinstance(runner, str) or not runner:
            add_violation(path, "runner missing or invalid")

        req = payload.get("request")
        if not isinstance(req, dict):
            add_violation(path, "request missing or invalid")
        else:
            for key in required_request_fields:
                if key not in req:
                    add_violation(path, f"request.{key} missing")
            if "headers" in req and not isinstance(req.get("headers"), dict):
                add_violation(path, "request.headers not a dict")

        resp = payload.get("response")
        if not isinstance(resp, dict):
            add_violation(path, "response missing or invalid")
        else:
            for key in required_response_fields:
                if key not in resp:
                    add_violation(path, f"response.{key} missing")
            if "headers" in resp and not isinstance(resp.get("headers"), dict):
                add_violation(path, "response.headers not a dict")

    return {
        "schema_expected": expected_schema,
        "since_ts": float(since_ts),
        "scanned_files": len(targets),
        "violations": violations,
    }


def postflight_redaction_scan(
    repo_root: Path,
    scan_roots: list[Path],
    *,
    since_ts: float,
) -> dict[str, object]:
    """Scan gate reports/artifacts for unredacted sensitive header values."""

    rules: list[dict[str, object]] = [
        {
            "id": "auth_basic_unredacted",
            "desc": "Authorization: Basic 明文（应仅保留 scheme 或摘要）",
            "patterns": [
                re.compile(br"(?i)\"authorization\"\s*:\s*\"basic\s+"),
                re.compile(br"(?im)^\s*authorization:\s*basic\s+"),
            ],
        },
        {
            "id": "auth_bearer_unredacted",
            "desc": "Authorization: Bearer 明文（token 不得落盘）",
            "patterns": [
                re.compile(br"(?i)\"authorization\"\s*:\s*\"bearer\s+"),
                re.compile(br"(?im)^\s*authorization:\s*bearer\s+"),
            ],
        },
        {
            "id": "auth_digest_unredacted",
            "desc": "Authorization: Digest 明文（参数不应落盘）",
            "patterns": [
                re.compile(br"(?i)\"authorization\"\s*:\s*\"digest\s+"),
                re.compile(br"(?im)^\s*authorization:\s*digest\s+"),
            ],
        },
        {
            "id": "proxy_auth_basic_unredacted",
            "desc": "Proxy-Authorization: Basic 明文（应仅保留 scheme 或摘要）",
            "patterns": [
                re.compile(br"(?i)\"proxy-authorization\"\s*:\s*\"basic\s+"),
                re.compile(br"(?im)^\s*proxy-authorization:\s*basic\s+"),
            ],
        },
        {
            "id": "cookie_header_unredacted",
            "desc": "Cookie 请求头明文（value 不得落盘）",
            "patterns": [
                re.compile(br"(?i)\"cookie\"\s*:\s*\"[^\r\n\"]*="),
                re.compile(br"(?im)^\s*cookie:\s*[^\r\n]*="),
            ],
        },
        {
            "id": "set_cookie_unredacted",
            "desc": "Set-Cookie 响应头明文（value 不得落盘）",
            "patterns": [
                re.compile(br"(?i)\"set-cookie\"\s*:\s*\"[^\r\n\"]*="),
                re.compile(br"(?im)^\s*set-cookie:\s*[^\r\n]*="),
            ],
        },
    ]

    def should_scan_file(path: Path) -> bool:
        if path.name in ("stderr", "stdout"):
            return True
        return path.suffix.lower() in (".json", ".jsonl", ".xml", ".txt", ".log")

    scan_files: list[Path] = []
    for root in scan_roots:
        for path in root.rglob("*"):
            if not path.is_file():
                continue
            try:
                if float(path.stat().st_mtime) < (float(since_ts) - 1.0):
                    continue
            except OSError:
                continue
            if should_scan_file(path):
                scan_files.append(path)

    def rel(path: Path) -> str:
        try:
            return str(path.relative_to(repo_root))
        except Exception:
            return str(path)

    violations: list[dict[str, object]] = []
    max_hits = 200
    for path in sorted(scan_files):
        try:
            data = path.read_bytes()
        except OSError:
            continue
        for line_no, line in enumerate(data.splitlines(), 1):
            for rule in rules:
                for rx in rule["patterns"]:  # type: ignore[index]
                    if rx.search(line):  # type: ignore[union-attr]
                        violations.append({
                            "rule": str(rule["id"]),
                            "file": rel(path),
                            "line": int(line_no),
                        })
                        if len(violations) >= max_hits:
                            break
                if len(violations) >= max_hits:
                    break
            if len(violations) >= max_hits:
                break
        if len(violations) >= max_hits:
            break

    return {
        "scan_roots": [str(path) for path in scan_roots],
        "since_ts": float(since_ts),
        "scanned_files": len(scan_files),
        "rules": [{"id": rule["id"], "desc": rule["desc"]} for rule in rules],
        "violations": violations,
    }


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
    preflight_http3 = report.get("preflight_http3_required") or {}
    if (
        isinstance(preflight_http3, dict)
        and preflight_http3.get("enabled")
        and preflight_http3.get("violations")
    ):
        policy_violations.append("http3_required")
    return policy_violations
