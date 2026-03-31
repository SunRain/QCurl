#!/usr/bin/env python3
"""Validate QCurl release contracts.

Checks:
1. PROJECT_VERSION major must match QCurl SOVERSION.
2. The curl submodule checkout must match the currently intended pin
   (either HEAD's gitlink or the working-tree-updated gitlink).
3. Print the bundled curl version from curlver.h for diagnostics.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys


def read_text(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8")


def parse_project_version(source_root: pathlib.Path) -> tuple[int, int, int]:
    content = read_text(source_root / "CMakeLists.txt")
    match = re.search(r"project\s*\(\s*QCurl\s+VERSION\s+(\d+)\.(\d+)\.(\d+)", content)
    if not match:
        raise ValueError("无法从 CMakeLists.txt 解析 PROJECT_VERSION")
    return tuple(int(part) for part in match.groups())


def parse_soversion(source_root: pathlib.Path) -> int:
    content = read_text(source_root / "src" / "CMakeLists.txt")
    match = re.search(r"\bSOVERSION\s+(\d+)\b", content)
    if not match:
        raise ValueError("无法从 src/CMakeLists.txt 解析 SOVERSION")
    return int(match.group(1))


def run_git(args: list[str], source_root: pathlib.Path) -> str:
    result = subprocess.run(
        ["git", *args],
        cwd=source_root,
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def current_curl_pin(source_root: pathlib.Path) -> str:
    return run_git(["-C", "curl", "rev-parse", "HEAD"], source_root)


def desired_curl_pin(source_root: pathlib.Path) -> str:
    diff_output = run_git(["diff", "--submodule=short", "--", "curl"], source_root)
    if diff_output:
        for line in reversed(diff_output.splitlines()):
            match = re.match(r"^\+Subproject commit ([0-9a-f]{40})$", line.strip())
            if match:
                return match.group(1)
        raise ValueError("检测到 curl gitlink diff，但无法解析目标 commit")

    ls_tree = run_git(["ls-tree", "HEAD", "curl"], source_root)
    match = re.search(r"\b([0-9a-f]{40})\s+curl$", ls_tree)
    if not match:
        raise ValueError("无法从 git ls-tree HEAD curl 解析 gitlink commit")
    return match.group(1)


def parse_bundled_curl_version(source_root: pathlib.Path) -> str:
    content = read_text(source_root / "curl" / "include" / "curl" / "curlver.h")
    match = re.search(r'#define\s+LIBCURL_VERSION\s+"([^"]+)"', content)
    if not match:
        raise ValueError("无法从 curlver.h 解析 LIBCURL_VERSION")
    return match.group(1)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--source-root",
        type=pathlib.Path,
        default=pathlib.Path(__file__).resolve().parents[1],
        help="QCurl source root",
    )
    args = parser.parse_args()
    source_root = args.source_root.resolve()

    project_version = parse_project_version(source_root)
    soversion = parse_soversion(source_root)
    current_pin = current_curl_pin(source_root)
    desired_pin = desired_curl_pin(source_root)
    curl_version = parse_bundled_curl_version(source_root)

    errors: list[str] = []
    if project_version[0] != soversion:
        errors.append(
            "PROJECT_VERSION major 与 SOVERSION 不一致："
            f"{project_version[0]} != {soversion}"
        )

    if current_pin != desired_pin:
        errors.append(
            "curl submodule checkout 与当前 gitlink/pin 不一致："
            f"{current_pin[:12]} != {desired_pin[:12]}"
        )

    print(
        "QCurl release contract:"
        f" version={project_version[0]}.{project_version[1]}.{project_version[2]},"
        f" soversion={soversion},"
        f" curl_pin={current_pin[:12]},"
        f" curl_version={curl_version}"
    )

    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
