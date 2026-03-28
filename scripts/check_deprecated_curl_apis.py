#!/usr/bin/env python3
"""Fail when deprecated libcurl options/info leak into QCurl src/tests."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


_CURLOPT_DEPRECATED_RE = re.compile(r"\bCURLOPTDEPRECATED\(\s*(CURLOPT_[A-Z0-9_]+)\b")
_CURLINFO_DEPRECATED_RE = re.compile(r"\b(CURLINFO_[A-Z0-9_]+)\s+CURL_DEPRECATED\(")
_IDENTIFIER_RE = re.compile(r"\b(?:CURLOPT_[A-Z0-9_]+|CURLINFO_[A-Z0-9_]+|CURL_IGNORE_DEPRECATION)\b")
_SOURCE_SUFFIXES = {
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
    ".ipp",
    ".inl",
}


def strip_comments_and_strings(source: str) -> str:
    """Strip comments and string literals while preserving line structure."""
    result: list[str] = []
    i = 0
    length = len(source)
    state = "normal"
    quote = ""

    while i < length:
        ch = source[i]
        nxt = source[i + 1] if i + 1 < length else ""

        if state == "normal":
            if ch == "/" and nxt == "/":
                result.extend("  ")
                i += 2
                state = "line_comment"
                continue
            if ch == "/" and nxt == "*":
                result.extend("  ")
                i += 2
                state = "block_comment"
                continue
            if ch in {'"', "'"}:
                result.append(" ")
                quote = ch
                i += 1
                state = "string"
                continue
            result.append(ch)
            i += 1
            continue

        if state == "line_comment":
            if ch == "\n":
                result.append("\n")
                state = "normal"
            else:
                result.append(" ")
            i += 1
            continue

        if state == "block_comment":
            if ch == "*" and nxt == "/":
                result.extend("  ")
                i += 2
                state = "normal"
            else:
                result.append("\n" if ch == "\n" else " ")
                i += 1
            continue

        if state == "string":
            if ch == "\\" and i + 1 < length:
                result.extend("  ")
                i += 2
                continue
            if ch == quote:
                result.append(" ")
                i += 1
                state = "normal"
                continue
            result.append("\n" if ch == "\n" else " ")
            i += 1

    return "".join(result)


def parse_forbidden_tokens(curl_header: Path) -> set[str]:
    """Parse deprecated CURLOPT/CURLINFO identifiers from curl.h."""
    content = curl_header.read_text(encoding="utf-8")
    forbidden = set(_CURLOPT_DEPRECATED_RE.findall(content))
    forbidden.update(_CURLINFO_DEPRECATED_RE.findall(content))
    return forbidden


def iter_source_files(scan_roots: list[Path]) -> list[Path]:
    """Collect source files from scan roots."""
    files: list[Path] = []
    for root in scan_roots:
        for path in sorted(root.rglob("*")):
            if path.is_file() and path.suffix in _SOURCE_SUFFIXES:
                files.append(path)
    return files


def scan_sources(scan_roots: list[Path], forbidden_tokens: set[str]) -> list[str]:
    """Return file:line diagnostics for forbidden identifiers."""
    violations: list[str] = []
    all_forbidden = set(forbidden_tokens)
    all_forbidden.add("CURL_IGNORE_DEPRECATION")

    for path in iter_source_files(scan_roots):
        stripped = strip_comments_and_strings(path.read_text(encoding="utf-8"))
        for line_number, line in enumerate(stripped.splitlines(), start=1):
            matches = [token for token in _IDENTIFIER_RE.findall(line) if token in all_forbidden]
            for token in matches:
                violations.append(f"{path}:{line_number}: forbidden {token}")

    return violations


def build_parser() -> argparse.ArgumentParser:
    """Create the CLI parser."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--curl-header", type=Path, required=True)
    parser.add_argument("--scan-root", type=Path, action="append", required=True)
    return parser


def main(argv: list[str]) -> int:
    """CLI entry point."""
    parser = build_parser()
    args = parser.parse_args(argv)

    forbidden = parse_forbidden_tokens(args.curl_header)
    violations = scan_sources(args.scan_root, forbidden)
    if violations:
        print("[deprecated_curl_api_guard] forbidden identifiers found:", file=sys.stderr)
        for violation in violations:
            print(violation, file=sys.stderr)
        return 1

    scanned_files = len(iter_source_files(args.scan_root))
    print(
        f"[deprecated_curl_api_guard] ok: scanned {scanned_files} files, "
        f"tracked {len(forbidden)} deprecated curl identifiers"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
