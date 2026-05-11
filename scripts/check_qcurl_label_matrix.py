#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Validate qcurl preflight targets expose their required CTest labels."""

from __future__ import annotations

import argparse
import re
import shlex
import sys
from pathlib import Path


_PRELIGHT_CALL = "add_qcurl_test_with_preflight"
_LABEL_MAP = {
    "REQUIRE_FRAGMENT_WS": "websocket",
    "REQUIRE_HTTP2_SUITE": "http2",
    "REQUIRE_HTTPBIN": "httpbin",
    "REQUIRE_LOCAL_PORT": "local_port",
    "REQUIRE_NODE": "node",
    "REQUIRE_PYTHON": "python",
}


def _strip_comments(block: str) -> str:
    lines = []
    for line in block.splitlines():
        lines.append(line.split("#", 1)[0])
    return "\n".join(lines)


def _extract_calls(cmake_text: str, function_name: str) -> list[str]:
    pattern = re.compile(rf"{re.escape(function_name)}\s*\(")
    calls: list[str] = []
    for match in pattern.finditer(cmake_text):
        depth = 1
        cursor = match.end()
        while cursor < len(cmake_text) and depth > 0:
            ch = cmake_text[cursor]
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
            cursor += 1
        if depth != 0:
            raise ValueError(f"unterminated {function_name}(...) block")
        calls.append(cmake_text[match.end() : cursor - 1])
    return calls


def _parse_preflight_targets(cmake_text: str) -> dict[str, set[str]]:
    targets: dict[str, set[str]] = {}
    for block in _extract_calls(cmake_text, _PRELIGHT_CALL):
        tokens = shlex.split(_strip_comments(block), comments=False, posix=True)
        if not tokens:
            continue
        target = tokens[0]
        flags = {token for token in tokens[1:] if token.startswith("REQUIRE_")}
        targets[target] = flags
    return targets


def _parse_labels(cmake_text: str) -> dict[str, str]:
    labels_by_target: dict[str, str] = {}
    for block in _extract_calls(cmake_text, "set_tests_properties"):
        tokens = shlex.split(_strip_comments(block), comments=False, posix=True)
        if not tokens:
            continue
        try:
            props_index = tokens.index("PROPERTIES")
        except ValueError:
            continue
        targets = tokens[:props_index]
        props = tokens[props_index + 1 :]
        if len(props) % 2 != 0:
            raise ValueError(f"invalid PROPERTIES clause: {block!r}")
        pairs = dict(zip(props[::2], props[1::2]))
        label_string = pairs.get("LABELS")
        if label_string is None:
            continue
        for target in targets:
            labels_by_target[target] = label_string
    return labels_by_target


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Fail when qcurl preflight targets miss required CTest labels.",
    )
    parser.add_argument(
        "--cmake",
        default="tests/qcurl/CMakeLists.txt",
        help="Path to the qcurl tests CMakeLists.txt file.",
    )
    args = parser.parse_args(argv)

    cmake_path = Path(args.cmake)
    if not cmake_path.exists():
        sys.stderr.write(f"[qcurl_label_matrix] CMake file not found: {cmake_path}\n")
        return 2

    cmake_text = cmake_path.read_text(encoding="utf-8")
    preflight_targets = _parse_preflight_targets(cmake_text)
    labels_by_target = _parse_labels(cmake_text)

    violations: list[str] = []
    for target, flags in sorted(preflight_targets.items()):
        label_string = labels_by_target.get(target, "")
        labels = {label for label in label_string.split(";") if label}
        if not labels:
            violations.append(f"{target}: missing LABELS for preflight-guarded test")
            continue
        for flag, required_label in sorted(_LABEL_MAP.items()):
            if flag in flags and required_label not in labels:
                violations.append(
                    f"{target}: {flag} requires label `{required_label}` (current: {label_string})",
                )

    if violations:
        sys.stderr.write("[qcurl_label_matrix] label/preflight mismatches detected:\n")
        for violation in violations:
            sys.stderr.write(f"  - {violation}\n")
        return 3

    sys.stderr.write(
        f"[qcurl_label_matrix] ok: {len(preflight_targets)} preflight targets expose required labels\n",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
