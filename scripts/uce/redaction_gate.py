#!/usr/bin/env python3
"""Scan UCE evidence roots for unredacted sensitive data."""

from __future__ import annotations

import argparse
import json
import re
from datetime import datetime
from datetime import timezone
from pathlib import Path
from typing import Any


TEXT_SUFFIXES = {".env", ".json", ".jsonl", ".log", ".md", ".txt", ".xml", ".yaml", ".yml"}

RULES: tuple[dict[str, Any], ...] = (
    {
        "id": "auth_basic_unredacted",
        "desc": "Authorization: Basic 明文（value 不得落盘）",
        "patterns": (
            re.compile(br'(?i)"authorization"\s*:\s*"basic\s+'),
            re.compile(br"(?im)^\s*authorization:\s*basic\s+"),
        ),
    },
    {
        "id": "auth_bearer_unredacted",
        "desc": "Authorization: Bearer 明文（token 不得落盘）",
        "patterns": (
            re.compile(br'(?i)"authorization"\s*:\s*"bearer\s+'),
            re.compile(br"(?im)^\s*authorization:\s*bearer\s+"),
        ),
    },
    {
        "id": "auth_digest_unredacted",
        "desc": "Authorization: Digest 明文（参数不应落盘）",
        "patterns": (
            re.compile(br'(?i)"authorization"\s*:\s*"digest\s+'),
            re.compile(br"(?im)^\s*authorization:\s*digest\s+"),
        ),
    },
    {
        "id": "proxy_auth_basic_unredacted",
        "desc": "Proxy-Authorization: Basic 明文（value 不得落盘）",
        "patterns": (
            re.compile(br'(?i)"proxy-authorization"\s*:\s*"basic\s+'),
            re.compile(br"(?im)^\s*proxy-authorization:\s*basic\s+"),
        ),
    },
    {
        "id": "cookie_header_unredacted",
        "desc": "Cookie 请求头明文（value 不得落盘）",
        "patterns": (
            re.compile(br'(?i)"cookie"\s*:\s*"[^"\r\n]*='),
            re.compile(br"(?im)^\s*cookie:\s*[^\r\n]*="),
        ),
    },
    {
        "id": "set_cookie_unredacted",
        "desc": "Set-Cookie 响应头明文（value 不得落盘）",
        "patterns": (
            re.compile(br'(?i)"set-cookie"\s*:\s*"[^"\r\n]*='),
            re.compile(br"(?im)^\s*set-cookie:\s*[^\r\n]*="),
        ),
    },
)


def _utc_now_iso() -> str:
    """Return the current UTC time in ISO-8601 format."""

    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def _should_scan(path: Path) -> bool:
    """Return whether a file should be scanned as text evidence."""

    return path.name in {"stderr", "stdout"} or path.suffix.lower() in TEXT_SUFFIXES


def scan_paths(scan_roots: list[Path]) -> dict[str, Any]:
    """Scan all provided roots and return a structured report."""

    missing_roots = [str(root) for root in scan_roots if not root.exists()]
    scan_files: list[Path] = []
    for root in scan_roots:
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if path.is_file() and _should_scan(path):
                scan_files.append(path)

    violations: list[dict[str, Any]] = []
    for path in sorted(scan_files):
        try:
            data = path.read_bytes()
        except OSError:
            continue
        for line_no, line in enumerate(data.splitlines(), 1):
            for rule in RULES:
                for pattern in rule["patterns"]:
                    if pattern.search(line):
                        violations.append(
                            {
                                "rule": rule["id"],
                                "file": str(path),
                                "line": line_no,
                            }
                        )
                        break

    return {
        "generated_at_utc": _utc_now_iso(),
        "scan_roots": [str(root) for root in scan_roots],
        "missing_roots": missing_roots,
        "scanned_files": len(scan_files),
        "rules": [{"id": rule["id"], "desc": rule["desc"]} for rule in RULES],
        "violations": violations,
    }


def main(argv: list[str] | None = None) -> int:
    """CLI entry point."""

    parser = argparse.ArgumentParser(description="Scan evidence roots for unredacted sensitive headers.")
    parser.add_argument(
        "--root",
        action="append",
        dest="roots",
        required=True,
        help="Root path to scan. Can be passed multiple times.",
    )
    parser.add_argument(
        "--report",
        required=True,
        help="JSON report output path.",
    )
    args = parser.parse_args(argv)

    roots = [Path(root) for root in args.roots]
    report = scan_paths(roots)
    report_path = Path(args.report)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    return 0 if not report["violations"] and not report["missing_roots"] else 3


if __name__ == "__main__":
    raise SystemExit(main())
