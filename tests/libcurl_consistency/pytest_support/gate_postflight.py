"""一致性 gate 的工件 schema 与脱敏后置检查。"""

from __future__ import annotations

import json
from pathlib import Path
import re


_REQUIRED_REQUEST_FIELDS = ("method", "url", "headers", "body_len", "body_sha256")
_REQUIRED_RESPONSE_FIELDS = ("status", "http_version", "headers", "body_len", "body_sha256")
_REDACTION_RULES: tuple[tuple[str, str, tuple[re.Pattern[bytes], ...]], ...] = (
    (
        "auth_basic_unredacted",
        "Authorization: Basic 明文（应仅保留 scheme 或摘要）",
        (
            re.compile(br"(?i)\"authorization\"\s*:\s*\"basic\s+"),
            re.compile(br"(?im)^\s*authorization:\s*basic\s+"),
        ),
    ),
    (
        "auth_bearer_unredacted",
        "Authorization: Bearer 明文（token 不得落盘）",
        (
            re.compile(br"(?i)\"authorization\"\s*:\s*\"bearer\s+"),
            re.compile(br"(?im)^\s*authorization:\s*bearer\s+"),
        ),
    ),
    (
        "auth_digest_unredacted",
        "Authorization: Digest 明文（参数不应落盘）",
        (
            re.compile(br"(?i)\"authorization\"\s*:\s*\"digest\s+"),
            re.compile(br"(?im)^\s*authorization:\s*digest\s+"),
        ),
    ),
    (
        "proxy_auth_basic_unredacted",
        "Proxy-Authorization: Basic 明文（应仅保留 scheme 或摘要）",
        (
            re.compile(br"(?i)\"proxy-authorization\"\s*:\s*\"basic\s+"),
            re.compile(br"(?im)^\s*proxy-authorization:\s*basic\s+"),
        ),
    ),
    (
        "cookie_header_unredacted",
        "Cookie 请求头明文（value 不得落盘）",
        (
            re.compile(br"(?i)\"cookie\"\s*:\s*\"[^\r\n\"]*="),
            re.compile(br"(?im)^\s*cookie:\s*[^\r\n]*="),
        ),
    ),
    (
        "set_cookie_unredacted",
        "Set-Cookie 响应头明文（value 不得落盘）",
        (
            re.compile(br"(?i)\"set-cookie\"\s*:\s*\"[^\r\n\"]*="),
            re.compile(br"(?im)^\s*set-cookie:\s*[^\r\n]*="),
        ),
    ),
)


def _relative_path(path: Path, repo_root: Path) -> str:
    try:
        return str(path.relative_to(repo_root))
    except ValueError:
        return str(path)


def _artifact_targets(artifacts_dir: Path) -> list[Path]:
    targets: list[Path] = []
    for name in ("baseline.json", "qcurl.json"):
        targets.extend(path for path in artifacts_dir.rglob(name) if path.is_file())
    return sorted(targets)


def _append_section_violations(
    violations: list[dict[str, object]],
    path_text: str,
    section_name: str,
    section: object,
    required_fields: tuple[str, ...],
) -> None:
    if not isinstance(section, dict):
        violations.append({"file": path_text, "reason": f"{section_name} missing or invalid"})
        return
    for key in required_fields:
        if key not in section:
            violations.append({"file": path_text, "reason": f"{section_name}.{key} missing"})
    if "headers" in section and not isinstance(section.get("headers"), dict):
        violations.append({"file": path_text, "reason": f"{section_name}.headers not a dict"})


def _artifact_violations(path: Path, repo_root: Path, expected_schema: str) -> list[dict[str, object]]:
    path_text = _relative_path(path, repo_root)
    try:
        payload = json.loads(path.read_text(encoding="utf-8", errors="replace"))
    except Exception as exc:
        return [{"file": path_text, "reason": f"invalid json: {exc}"}]

    schema = payload.get("schema")
    if schema != expected_schema:
        return [{"file": path_text, "reason": f"schema mismatch: {schema!r} != {expected_schema!r}"}]

    violations: list[dict[str, object]] = []
    runner = payload.get("runner")
    if not isinstance(runner, str) or not runner:
        violations.append({"file": path_text, "reason": "runner missing or invalid"})
    _append_section_violations(
        violations,
        path_text,
        "request",
        payload.get("request"),
        _REQUIRED_REQUEST_FIELDS,
    )
    _append_section_violations(
        violations,
        path_text,
        "response",
        payload.get("response"),
        _REQUIRED_RESPONSE_FIELDS,
    )
    return violations


def postflight_artifacts_schema_check(
    *,
    repo_root: Path,
    artifacts_dir: Path,
    since_ts: float | None,
    expected_schema: str,
) -> dict[str, object]:
    """校验本轮工件 schema；隔离目录已保证不需要 mtime 窗口。"""

    if not artifacts_dir.exists():
        return {
            "schema_expected": expected_schema,
            "since_ts": float(since_ts),
            "scanned_files": 0,
            "violations": [],
            "note": "artifacts dir not found",
        }

    targets = _artifact_targets(artifacts_dir)
    violations = [
        violation
        for path in targets
        for violation in _artifact_violations(path, repo_root, expected_schema)
    ]
    return {
        "schema_expected": expected_schema,
        "since_ts": float(since_ts),
        "scanned_files": len(targets),
        "violations": violations,
    }


def _should_scan_file(path: Path) -> bool:
    if path.name in ("stderr", "stdout"):
        return True
    return path.suffix.lower() in (".json", ".jsonl", ".xml", ".txt", ".log")


def _scan_files(scan_roots: list[Path], since_ts: float) -> list[Path]:
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
            if _should_scan_file(path):
                scan_files.append(path)
    return sorted(scan_files)


def _scan_violations(scan_files: list[Path], repo_root: Path) -> list[dict[str, object]]:
    violations: list[dict[str, object]] = []
    max_hits = 200
    for path in scan_files:
        try:
            data = path.read_bytes()
        except OSError:
            continue
        for line_no, line in enumerate(data.splitlines(), 1):
            for rule_id, _description, patterns in _REDACTION_RULES:
                for pattern in patterns:
                    if pattern.search(line):
                        violations.append({
                            "rule": rule_id,
                            "file": _relative_path(path, repo_root),
                            "line": int(line_no),
                        })
                        if len(violations) >= max_hits:
                            return violations
    return violations


def postflight_redaction_scan(
    repo_root: Path,
    scan_roots: list[Path],
    *,
    since_ts: float,
) -> dict[str, object]:
    """扫描本轮报告和工件，阻止敏感头明文落盘。"""

    scan_files = _scan_files(scan_roots, since_ts)
    return {
        "scan_roots": [str(path) for path in scan_roots],
        "since_ts": float(since_ts),
        "scanned_files": len(scan_files),
        "rules": [
            {"id": rule_id, "desc": description}
            for rule_id, description, _patterns in _REDACTION_RULES
        ],
        "violations": _scan_violations(scan_files, repo_root),
    }
