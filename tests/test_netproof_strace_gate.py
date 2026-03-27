from __future__ import annotations

from pathlib import Path

from scripts.netproof_strace_gate import default_subject_command
from scripts.netproof_strace_gate import find_network_syscalls


def test_find_network_syscalls_matches_inet_operations() -> None:
    trace_text = """
12345 socket(AF_UNIX, SOCK_STREAM, 0) = 3
12345 socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) = 4
12345 connect(4, {sa_family=AF_INET, sin_port=htons(443)}, 16) = 0
12345 read(4, "ok", 2) = 2
"""

    hits = find_network_syscalls(trace_text)

    assert hits == [
        "12345 socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) = 4",
        "12345 connect(4, {sa_family=AF_INET, sin_port=htons(443)}, 16) = 0",
    ]


def test_default_subject_command_targets_offline_gate(tmp_path: Path) -> None:
    repo_root = tmp_path / "repo"
    build_dir = repo_root / "build"

    command = default_subject_command(repo_root, build_dir)

    assert command == [
        "python3",
        str(repo_root / "scripts" / "ctest_strict.py"),
        "--build-dir",
        str(build_dir),
        "--label-regex",
        "offline",
        "--max-skips",
        "0",
    ]
