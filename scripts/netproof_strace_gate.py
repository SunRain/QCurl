#!/usr/bin/env python3
"""Run offline gate subject under strace and prove whether network syscalls occur."""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from datetime import datetime
from datetime import timezone
from pathlib import Path
from typing import Any
from typing import Mapping


_SOCKET_MARKERS = (
    "socket(AF_INET",
    "socket(AF_INET6",
)

_NETWORK_ACTIVITY_MARKERS = (
    "bind(",
    "connect(",
    "listen(",
    "sendto(",
    "sendmsg(",
    "sendmmsg(",
    "recvfrom(",
    "recvmsg(",
    "recvmmsg(",
    "accept(",
    "accept4(",
)

_NETWORK_MARKERS = (*_SOCKET_MARKERS, *_NETWORK_ACTIVITY_MARKERS)
_LOCAL_IPC_FD_RE = re.compile(
    r"^(?:\d+\s+)?\w+\(\d+<(?:UNIX(?:-[A-Z]+)?|NETLINK):"
)


def utc_now_iso() -> str:
    """Return current UTC timestamp."""

    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def default_subject_command(repo_root: Path, build_dir: Path) -> list[str]:
    """Return the default offline gate subject command."""

    return [
        "python3",
        str(repo_root / "scripts" / "ctest_strict.py"),
        "--build-dir",
        str(build_dir),
        "--label-regex",
        "offline",
        "--max-skips",
        "0",
    ]


def _find_matching_syscalls(trace_text: str, markers: tuple[str, ...]) -> list[str]:
    """Extract syscall lines containing one of the supplied markers."""

    hits: list[str] = []
    for raw in trace_text.splitlines():
        line = raw.strip()
        if not line:
            continue
        if any(marker in line for marker in markers):
            hits.append(line)
    return hits


def find_network_syscalls(trace_text: str) -> list[str]:
    """Extract all observed INET socket and network-activity syscalls."""

    return _find_matching_syscalls(trace_text, _NETWORK_MARKERS)


def find_network_activity_syscalls(trace_text: str) -> list[str]:
    """仅排除 strace 明确标为本地 IPC 的 UNIX/NETLINK fd，未知类型仍阻断。

    -yy 从内核解析调用时的 fd 类型，不依赖同一 trace 中先出现 socket()，因此覆盖
    继承、dup、跨线程和 fd 复用。只信任调用的首个 fd 标注，不读取载荷中的类型文字。
    """

    return [
        line for line in _find_matching_syscalls(trace_text, _NETWORK_ACTIVITY_MARKERS)
        if not _LOCAL_IPC_FD_RE.match(line)
    ]


def _set_sanitizer_option(options: str, key: str, value: str) -> str:
    parts = [part for part in options.split(":") if part and part.partition("=")[0] != key]
    parts.append(f"{key}={value}")
    return ":".join(parts)


def build_subject_environment(base_environment: Mapping[str, str] | None = None) -> dict[str, str]:
    """Disable leak detection for the ptraced subject while preserving sanitizer hard-fail options."""

    environment = dict(base_environment) if base_environment is not None else os.environ.copy()
    environment["ASAN_OPTIONS"] = _set_sanitizer_option(
        environment.get("ASAN_OPTIONS", ""),
        "detect_leaks",
        "0",
    )
    return environment


def write_json(path: Path, payload: dict[str, Any]) -> None:
    """Write JSON payload to disk."""

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def _parse_args(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run offline gate under strace and detect network syscalls.")
    parser.add_argument("--build-dir", required=True, help="CMake build directory.")
    parser.add_argument("--report", required=True, help="Report JSON path.")
    parser.add_argument("--trace-dir", required=True, help="Directory for raw strace outputs.")
    parser.add_argument(
        "--command",
        nargs=argparse.REMAINDER,
        help="Optional subject command; defaults to ctest_strict offline.",
    )
    return parser.parse_args(argv)


def _resolve_build_dir(repo_root: Path, build_dir_arg: str) -> Path:
    build_dir = Path(build_dir_arg)
    return build_dir if build_dir.is_absolute() else (repo_root / build_dir).resolve()


def _create_report(subject_command: list[str], trace_dir: Path) -> dict[str, Any]:
    return {
        "generated_at_utc": utc_now_iso(),
        "schema": "qcurl-uce/netproof-strace-report@v1",
        "subject_command": subject_command,
        "trace_dir": str(trace_dir),
        "trace_files": [],
        "network_syscalls": [],
        "network_activity_syscalls": [],
        "socket_creation_syscalls": [],
        "ptrace_sanitizer_mode": {
            "leak_detection": "disabled",
            "reason": "LeakSanitizer is incompatible with ptrace; the sanitizer gate covers leaks separately.",
        },
        "policy_violations": [],
    }


def _run_traced_subject(
    strace_bin: str,
    subject_command: list[str],
    repo_root: Path,
    trace_prefix: Path,
) -> subprocess.CompletedProcess[str]:
    command = [
        strace_bin,
        "-ff",
        "-yy",
        "-e",
        "trace=network",
        "-o",
        str(trace_prefix),
        *subject_command,
    ]
    return subprocess.run(
        command,
        cwd=str(repo_root),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
        env=build_subject_environment(),
    )


def _collect_trace_evidence(
    trace_dir: Path,
) -> tuple[list[Path], list[str], list[str], list[str]]:
    trace_files = sorted(trace_dir.glob("trace*"))
    network_hits: list[str] = []
    network_activity_hits: list[str] = []
    socket_creation_hits: list[str] = []
    for path in trace_files:
        trace_text = path.read_text(encoding="utf-8", errors="replace")
        network_hits.extend(find_network_syscalls(trace_text))
        network_activity_hits.extend(find_network_activity_syscalls(trace_text))
        socket_creation_hits.extend(_find_matching_syscalls(trace_text, _SOCKET_MARKERS))
    return trace_files, network_hits, network_activity_hits, socket_creation_hits


def main(argv: list[str] | None = None) -> int:
    """CLI entry point."""

    args = _parse_args(argv)

    repo_root = Path(__file__).resolve().parent.parent
    build_dir = _resolve_build_dir(repo_root, args.build_dir)
    report_path = Path(args.report)
    trace_dir = Path(args.trace_dir)
    trace_dir.mkdir(parents=True, exist_ok=True)

    strace_bin = shutil.which("strace")
    subject_command = list(args.command) if args.command else default_subject_command(repo_root, build_dir)
    report = _create_report(subject_command, trace_dir)

    if not strace_bin:
        report["result"] = "fail"
        report["policy_violations"].append("netproof_strace_missing")
        write_json(report_path, report)
        return 3

    completed = _run_traced_subject(strace_bin, subject_command, repo_root, trace_dir / "trace")

    subject_log = trace_dir / "subject.log"
    subject_log.write_text(completed.stdout or "", encoding="utf-8")
    report["subject_log"] = str(subject_log)
    report["subject_returncode"] = int(completed.returncode)

    trace_files, network_hits, network_activity_hits, socket_creation_hits = _collect_trace_evidence(
        trace_dir
    )
    report["trace_files"] = [str(path) for path in trace_files]
    report["network_syscalls"] = network_hits[:200]
    report["network_activity_syscalls"] = network_activity_hits[:200]
    report["socket_creation_syscalls"] = socket_creation_hits[:200]

    if completed.returncode != 0:
        report["policy_violations"].append("netproof_subject_failed")
    if network_activity_hits:
        report["policy_violations"].append("netproof_network_syscall_detected")

    report["result"] = "pass" if not report["policy_violations"] else "fail"
    write_json(report_path, report)
    return 0 if report["result"] == "pass" else 3


if __name__ == "__main__":
    raise SystemExit(main())
