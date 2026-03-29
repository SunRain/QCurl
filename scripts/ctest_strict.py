#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ctest 严格门禁（解决：QtTest 的 QSKIP 在 ctest 下会被计为 Passed）。

背景：
- QtTest 的 QSKIP 会让进程返回码为 0，ctest 会将该 test 计为 Passed；
  这会造成“全绿≠已覆盖”的误读（尤其在依赖外部服务/环境的用例中）。

策略：
- 运行 ctest（强制开启 -V 以获取 QtTest Totals 行）。
- 解析输出中的 `Totals: ... skipped`，统计跳过数量与对应的 test 名称。
- 当 skipped_total > max_skips 时，返回非 0 作为门禁失败。

用法示例：
- 默认（build/，仅跑离线门禁标签）：python3 scripts/ctest_strict.py
- 指定构建目录：python3 scripts/ctest_strict.py --build-dir build-debug
- 允许跳过阈值：python3 scripts/ctest_strict.py --max-skips 2
- 仅跑环境依赖用例：python3 scripts/ctest_strict.py --label-regex env
- 传递 ctest 参数：python3 scripts/ctest_strict.py -- -R '^tst_QCNetwork.*' -j 8

环境变量：
- QCURL_CTEST_MAX_SKIPS：默认的 max-skips（未设置则为 0）
- QCURL_CTEST_LABEL_REGEX：默认的标签过滤正则（未设置则为 offline；置空表示不过滤）
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path


_START_RE = re.compile(r"^\s*Start\s+\d+:\s+(?P<name>.+?)\s*$")
_TOTALS_RE = re.compile(
    r"^\s*(?:\d+:\s*)?Totals:\s*(?P<passed>\d+)\s+passed,\s*(?P<failed>\d+)\s+failed,\s*(?P<skipped>\d+)\s+skipped\b"
)


def _has_verbose_flag(args: list[str]) -> bool:
    for a in args:
        if a in ("-V", "-VV", "--verbose", "--extra-verbose"):
            return True
        if a.startswith("-V") and a != "-":
            return True
    return False

def _has_label_filter(args: list[str]) -> bool:
    for i, a in enumerate(args):
        if a in ("-L", "--label-regex", "-LE", "--label-exclude"):
            return True
        if a.startswith("-L") and a not in ("-L", "-LE"):
            return True
        if a.startswith("--label-regex=") or a.startswith("--label-exclude="):
            return True
        if a.startswith("-LE") and a not in ("-LE",):
            return True
        # 防御：形如 "-Lfoo" / "-LEbar" 的紧凑写法
        if a.startswith("-L") and len(a) > 2:
            return True
    return False

def _has_no_tests_flag(args: list[str]) -> bool:
    for a in args:
        if a == "--no-tests":
            return True
        if a.startswith("--no-tests="):
            return True
    return False

def _has_timeout_flag(args: list[str]) -> bool:
    for i, a in enumerate(args):
        if a == "--timeout":
            return True
        if a.startswith("--timeout="):
            return True
        # 兼容 "--timeout 60" 这种两段写法
        if a == "--timeout" and i + 1 < len(args):
            return True
    return False


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Run ctest and fail when QtTest skipped exceeds threshold.")
    parser.add_argument(
        "--build-dir",
        default=os.environ.get("QCURL_BUILD_DIR", "build"),
        help="CMake build directory (default: build/ or $QCURL_BUILD_DIR).",
    )
    parser.add_argument(
        "--ctest",
        default=os.environ.get("CTEST", "ctest"),
        help="ctest executable name/path (default: ctest or $CTEST).",
    )
    parser.add_argument(
        "--max-skips",
        type=int,
        default=int(os.environ.get("QCURL_CTEST_MAX_SKIPS", "0")),
        help="Max allowed QtTest skipped count (default: $QCURL_CTEST_MAX_SKIPS or 0).",
    )
    parser.add_argument(
        "--label-regex",
        default=os.environ.get("QCURL_CTEST_LABEL_REGEX", "offline"),
        help="ctest label regex filter (passed as `-L`). Default: $QCURL_CTEST_LABEL_REGEX or 'offline'. "
             "Set to empty string to disable label filtering.",
    )
    parser.add_argument(
        "--ctest-timeout",
        type=int,
        default=int(os.environ.get("QCURL_CTEST_TIMEOUT", "300")),
        help="Default ctest per-test timeout in seconds (passed as `--timeout`). "
             "Set to 0 to disable. Default: $QCURL_CTEST_TIMEOUT or 300.",
    )
    parser.add_argument(
        "ctest_args",
        nargs=argparse.REMAINDER,
        help="Arguments passed to ctest (prefix with '--' to separate).",
    )
    args = parser.parse_args(argv)

    build_dir = Path(args.build_dir)
    if not build_dir.exists():
        sys.stderr.write(f"[ctest_strict] build dir not found: {build_dir}\n")
        return 2

    ctest_args = list(args.ctest_args or [])
    if ctest_args and ctest_args[0] == "--":
        ctest_args = ctest_args[1:]
    if not _has_verbose_flag(ctest_args):
        ctest_args.append("-V")
    if not _has_no_tests_flag(ctest_args):
        ctest_args.append("--no-tests=error")
    if args.label_regex and (not _has_label_filter(ctest_args)):
        ctest_args += ["-L", str(args.label_regex)]
    if int(args.ctest_timeout) > 0 and (not _has_timeout_flag(ctest_args)):
        ctest_args += ["--timeout", str(int(args.ctest_timeout))]

    cmd = [args.ctest, *ctest_args]
    sys.stderr.write(
        "[ctest_strict] evidence gate active: this wrapper enforces skip=fail; "
        "bare ctest output alone is not sufficient evidence.\n"
    )
    sys.stderr.write(
        f"[ctest_strict] build_dir={build_dir} label_regex={args.label_regex or '<none>'} "
        f"max_skips={args.max_skips}\n"
    )
    run_env = os.environ.copy()
    # 取证式门禁默认抑制 debug/info 噪声，避免日志爆炸掩盖关键信息；如需详细日志可显式设置 QT_LOGGING_RULES。
    run_env.setdefault(
        "QT_LOGGING_RULES",
        os.environ.get("QCURL_CTEST_QT_LOGGING_RULES", "*.debug=false;*.info=false"),
    )
    proc = subprocess.run(
        cmd,
        cwd=str(build_dir),
        env=run_env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    out = proc.stdout or ""
    sys.stdout.write(out)

    skipped_total = 0
    skipped_by_test: dict[str, int] = {}
    current_test: str | None = None

    for line in out.splitlines():
        m = _START_RE.match(line)
        if m:
            current_test = m.group("name").strip()
            continue
        m = _TOTALS_RE.match(line)
        if m:
            skipped = int(m.group("skipped"))
            if skipped > 0:
                name = current_test or "<unknown>"
                skipped_by_test[name] = skipped_by_test.get(name, 0) + skipped
                skipped_total += skipped

    # ctest 自身失败优先返回（避免掩盖真实失败）
    if proc.returncode != 0:
        sys.stderr.write(f"[ctest_strict] ctest failed: returncode={proc.returncode}\n")
        return int(proc.returncode)

    if skipped_total > int(args.max_skips):
        sys.stderr.write(
            f"[ctest_strict] QtTest skipped too many: skipped_total={skipped_total} > max_skips={args.max_skips}\n"
        )
        for name, count in sorted(skipped_by_test.items(), key=lambda x: (-x[1], x[0])):
            sys.stderr.write(f"[ctest_strict] skipped: {name}: {count}\n")
        sys.stderr.write(
            "[ctest_strict] use label selection to exclude non-target evidence groups; "
            "do not treat raw ctest pass output as proof when skips are present.\n"
        )
        return 3

    sys.stderr.write(
        f"[ctest_strict] ok: skipped_total={skipped_total}, max_skips={args.max_skips}\n"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
