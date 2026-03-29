#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Validate that only `external_*` tests may override the QtTest skip contract."""

from __future__ import annotations

import argparse
import re
import shlex
import sys
from pathlib import Path


_DEFAULT_FAIL_REGEX = r"SKIP *:"
_BLOCK_RE = re.compile(
    r"set_tests_properties\((?P<targets>.*?)\s+PROPERTIES\s+(?P<props>.*?)\)",
    re.S,
)


def parse_blocks(cmake_text: str) -> tuple[dict[str, str], dict[str, str]]:
    """Return per-target LABELS and FAIL_REGULAR_EXPRESSION overrides."""
    labels_by_target: dict[str, str] = {}
    fail_regex_by_target: dict[str, str] = {}

    for match in _BLOCK_RE.finditer(cmake_text):
        targets = [token for token in match.group("targets").split() if token]
        props = shlex.split(match.group("props"))
        if len(props) % 2 != 0:
            raise ValueError(f"invalid PROPERTIES clause: {match.group(0)!r}")

        pairs = dict(zip(props[::2], props[1::2]))
        for target in targets:
            if "LABELS" in pairs:
                labels_by_target[target] = pairs["LABELS"]
            if "FAIL_REGULAR_EXPRESSION" in pairs:
                fail_regex_by_target[target] = pairs["FAIL_REGULAR_EXPRESSION"]

    return labels_by_target, fail_regex_by_target


def has_external_label(label_string: str) -> bool:
    """Return whether the LABELS string contains at least one `external_*` label."""
    return any(label.startswith("external_") for label in label_string.split(";") if label)


def main(argv: list[str]) -> int:
    """CLI entry point."""
    parser = argparse.ArgumentParser(
        description="Fail when non-external tests override FAIL_REGULAR_EXPRESSION.",
    )
    parser.add_argument(
        "--cmake",
        default="tests/qcurl/CMakeLists.txt",
        help="Path to the qcurl tests CMakeLists.txt file.",
    )
    args = parser.parse_args(argv)

    cmake_path = Path(args.cmake)
    if not cmake_path.exists():
        sys.stderr.write(f"[skip_contract_guard] CMake file not found: {cmake_path}\n")
        return 2

    labels_by_target, fail_regex_by_target = parse_blocks(cmake_path.read_text(encoding="utf-8"))
    violations: list[str] = []
    allowed: list[str] = []

    for target, fail_regex in sorted(fail_regex_by_target.items()):
        if fail_regex == _DEFAULT_FAIL_REGEX:
            continue

        labels = labels_by_target.get(target, "")
        if has_external_label(labels):
            allowed.append(f"{target} [{labels}] -> {fail_regex}")
            continue

        violations.append(
            f"{target} [{labels or '<no labels>'}] overrides FAIL_REGULAR_EXPRESSION={fail_regex!r}",
        )

    if violations:
        sys.stderr.write("[skip_contract_guard] non-external skip-contract overrides detected:\n")
        for line in violations:
            sys.stderr.write(f"  - {line}\n")
        return 3

    sys.stderr.write("[skip_contract_guard] ok: only external_* tests override skip=fail\n")
    for line in allowed:
        sys.stderr.write(f"[skip_contract_guard] allow: {line}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
