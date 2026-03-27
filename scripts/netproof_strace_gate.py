#!/usr/bin/env python3
"""Run offline gate subject under strace and prove whether network syscalls occur."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from datetime import datetime
from datetime import timezone
from pathlib import Path
from typing import Any


_NETWORK_MARKERS = (
    "socket(AF_INET",
    "socket(AF_INET6",
    "connect(",
    "sendto(",
    "sendmsg(",
    "recvfrom(",
    "recvmsg(",
    "accept(",
    "accept4(",
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


def find_network_syscalls(trace_text: str) -> list[str]:
    """Extract network-related syscall lines from a strace log."""

    hits: list[str] = []
    for raw in trace_text.splitlines():
        line = raw.strip()
        if not line:
            continue
        if any(marker in line for marker in _NETWORK_MARKERS):
            hits.append(line)
    return hits


def write_json(path: Path, payload: dict[str, Any]) -> None:
    """Write JSON payload to disk."""

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    """CLI entry point."""

    parser = argparse.ArgumentParser(description="Run offline gate under strace and detect network syscalls.")
    parser.add_argument("--build-dir", required=True, help="CMake build directory.")
    parser.add_argument("--report", required=True, help="Report JSON path.")
    parser.add_argument("--trace-dir", required=True, help="Directory for raw strace outputs.")
    parser.add_argument(
        "--command",
        nargs=argparse.REMAINDER,
        help="Optional subject command; defaults to ctest_strict offline.",
    )
    args = parser.parse_args(argv)

    repo_root = Path(__file__).resolve().parent.parent
    build_dir = Path(args.build_dir)
    if not build_dir.is_absolute():
        build_dir = (repo_root / build_dir).resolve()

    report_path = Path(args.report)
    trace_dir = Path(args.trace_dir)
    trace_dir.mkdir(parents=True, exist_ok=True)

    strace_bin = shutil.which("strace")
    subject_command = list(args.command) if args.command else default_subject_command(repo_root, build_dir)
    trace_prefix = trace_dir / "trace"

    report: dict[str, Any] = {
        "generated_at_utc": utc_now_iso(),
        "schema": "qcurl-uce/netproof-strace-report@v1",
        "subject_command": subject_command,
        "trace_dir": str(trace_dir),
        "trace_files": [],
        "network_syscalls": [],
        "policy_violations": [],
    }

    if not strace_bin:
        report["result"] = "fail"
        report["policy_violations"].append("netproof_strace_missing")
        write_json(report_path, report)
        return 3

    command = [
        strace_bin,
        "-ff",
        "-e",
        "trace=network",
        "-o",
        str(trace_prefix),
        *subject_command,
    ]
    completed = subprocess.run(
        command,
        cwd=str(repo_root),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )

    subject_log = trace_dir / "subject.log"
    subject_log.write_text(completed.stdout or "", encoding="utf-8")
    report["subject_log"] = str(subject_log)
    report["subject_returncode"] = int(completed.returncode)

    trace_files = sorted(trace_dir.glob("trace*"))
    report["trace_files"] = [str(path) for path in trace_files]
    network_hits: list[str] = []
    for path in trace_files:
        network_hits.extend(find_network_syscalls(path.read_text(encoding="utf-8", errors="replace")))
    report["network_syscalls"] = network_hits[:200]

    if completed.returncode != 0:
        report["policy_violations"].append("netproof_subject_failed")
    if network_hits:
        report["policy_violations"].append("netproof_network_syscall_detected")

    report["result"] = "pass" if not report["policy_violations"] else "fail"
    write_json(report_path, report)
    return 0 if report["result"] == "pass" else 3


if __name__ == "__main__":
    raise SystemExit(main())
