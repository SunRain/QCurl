from __future__ import annotations

from pathlib import Path
import subprocess

import pytest

from scripts import netproof_strace_gate
from scripts.netproof_strace_gate import build_subject_environment
from scripts.netproof_strace_gate import default_subject_command
from scripts.netproof_strace_gate import find_network_activity_syscalls
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


def test_network_activity_ignores_socket_capability_probe() -> None:
    trace_text = """
12345 socket(AF_INET6, SOCK_DGRAM, IPPROTO_IP) = 4
12345 close(4) = 0
"""

    assert find_network_syscalls(trace_text) == [
        "12345 socket(AF_INET6, SOCK_DGRAM, IPPROTO_IP) = 4",
    ]
    assert find_network_activity_syscalls(trace_text) == []


def test_network_activity_rejects_reachability_and_listener_operations() -> None:
    trace_text = """
12345 socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) = 4
12345 bind(4, {sa_family=AF_INET, sin_port=htons(0)}, 16) = 0
12345 listen(4, 16) = 0
12345 connect(4, {sa_family=AF_INET, sin_port=htons(443)}, 16) = -1 EINPROGRESS
12345 sendto(4, "x", 1, 0, NULL, 0) = 1
"""

    assert find_network_activity_syscalls(trace_text) == [
        "12345 bind(4, {sa_family=AF_INET, sin_port=htons(0)}, 16) = 0",
        "12345 listen(4, 16) = 0",
        "12345 connect(4, {sa_family=AF_INET, sin_port=htons(443)}, 16) = -1 EINPROGRESS",
        '12345 sendto(4, "x", 1, 0, NULL, 0) = 1',
    ]


def test_network_activity_ignores_unix_ipc_but_keeps_inet_fd_activity() -> None:
    trace_text = """
12345 connect(3<UNIX-STREAM:[1]>, {sa_family=AF_UNIX, sun_path=\"/run/systemd/userdb/io.systemd.Machine\"}, 41) = 0
12345 sendto(3<UNIX-STREAM:[1->2]>, \"userdb\", 6, 0, NULL, 0) = 6
12345 connect(4<TCPv6:[3]>, {sa_family=AF_INET6, sin6_port=htons(443)}, 28) = -1 ECONNREFUSED
12345 sendto(4<TCPv6:[3]>, \"x\", 1, 0, NULL, 0) = 1
"""

    assert find_network_activity_syscalls(trace_text) == [
        "12345 connect(4<TCPv6:[3]>, {sa_family=AF_INET6, sin6_port=htons(443)}, 28) = -1 ECONNREFUSED",
        '12345 sendto(4<TCPv6:[3]>, "x", 1, 0, NULL, 0) = 1',
    ]


def test_network_activity_ignores_netlink_kernel_ipc() -> None:
    trace_text = """
12345 bind(3<NETLINK:[1]>, {sa_family=AF_NETLINK, nl_pid=0, nl_groups=0}, 12) = 0
12345 sendto(3<NETLINK:[1]>, "x", 1, 0, NULL, 0) = 1
12345 connect(4<TCPv6:[3]>, {sa_family=AF_INET6, sin6_port=htons(443)}, 28) = -1 ECONNREFUSED
"""

    assert find_network_activity_syscalls(trace_text) == [
        "12345 connect(4<TCPv6:[3]>, {sa_family=AF_INET6, sin6_port=htons(443)}, 28) = -1 ECONNREFUSED",
    ]


@pytest.mark.parametrize("descriptor", ["9<TCP:[1->2]>", "9<UDPv6:[1]>", "9", "-1"])
def test_network_activity_rejects_inherited_duplicated_and_unknown_sockets(descriptor: str) -> None:
    """继承、dup 后或类型未知的 fd 不依赖同一 trace 中出现 socket()。"""

    line = f'sendto({descriptor}, "x", 1, 0, NULL, 0) = 1'
    assert find_network_activity_syscalls(line) == [line]


def test_network_activity_uses_current_fd_type_after_reuse() -> None:
    trace_text = '''
socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) = 4<TCP:[1]>
sendto(4<UNIX-STREAM:[2->3]>, "local", 5, 0, NULL, 0) = 5
sendto(4<UDP:[4]>, "network", 7, 0, NULL, 0) = 7
'''
    assert find_network_activity_syscalls(trace_text) == [
        'sendto(4<UDP:[4]>, "network", 7, 0, NULL, 0) = 7',
    ]


def test_payload_cannot_disguise_unknown_socket_as_unix_ipc() -> None:
    line = 'sendto(9, "AF_UNIX sendto(3<UNIX-STREAM:[1]>,", 33, 0, NULL, 0) = 33'
    assert find_network_activity_syscalls(line) == [line]


def test_traced_subject_requests_socket_type_annotations(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    commands: list[list[str]] = []

    def fake_run(command: list[str], **_: object) -> subprocess.CompletedProcess[str]:
        commands.append(command)
        return subprocess.CompletedProcess(command, 0, "")

    monkeypatch.setattr(netproof_strace_gate.subprocess, "run", fake_run)
    netproof_strace_gate._run_traced_subject("strace", ["subject"], tmp_path, tmp_path / "trace")
    assert commands == [["strace", "-ff", "-yy", "-e", "trace=network", "-o", str(tmp_path / "trace"), "subject"]]


def test_subject_environment_disables_lsan_under_ptrace() -> None:
    base = {
        "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
        "LSAN_OPTIONS": "detect_leaks=1:leak_check_at_exit=1:exitcode=23",
        "UBSAN_OPTIONS": "halt_on_error=1",
    }
    environment = build_subject_environment(base)

    assert environment["ASAN_OPTIONS"] == "halt_on_error=1:detect_leaks=0"
    assert environment["LSAN_OPTIONS"] == "leak_check_at_exit=1:exitcode=23:detect_leaks=0"
    assert environment["UBSAN_OPTIONS"] == "halt_on_error=1"
    assert base["ASAN_OPTIONS"] == "detect_leaks=1:halt_on_error=1"
    assert base["LSAN_OPTIONS"] == "detect_leaks=1:leak_check_at_exit=1:exitcode=23"


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
