#!/usr/bin/env python3
"""Configure sanitizer builds and run UCE/nightly verification commands."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
from dataclasses import dataclass
from datetime import datetime
from datetime import timezone
from pathlib import Path
from typing import Any


@dataclass(frozen=True)
class SanitizerProfile:
    name: str
    sanitizer_flags: str
    enable_libcurl_consistency: bool


def utc_now_iso() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def sanitizer_profiles() -> dict[str, SanitizerProfile]:
    return {
        "asan-ubsan-lsan": SanitizerProfile(
            name="asan-ubsan-lsan",
            sanitizer_flags="-fsanitize=address,undefined,leak -fno-omit-frame-pointer",
            enable_libcurl_consistency=True,
        ),
        "tsan": SanitizerProfile(
            name="tsan",
            sanitizer_flags="-fsanitize=thread -fno-omit-frame-pointer",
            enable_libcurl_consistency=False,
        ),
    }


def cmake_configure_command(source_dir: Path, build_dir: Path, profile: SanitizerProfile) -> list[str]:
    command = [
        "cmake",
        "-S",
        str(source_dir),
        "-B",
        str(build_dir),
        "-G",
        "Ninja",
    ]
    if profile.name == "tsan":
        command.extend(
            ["-DCMAKE_C_COMPILER=clang", "-DCMAKE_CXX_COMPILER=clang++"]
        )
    command.extend(
        [
            "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
            "-DBUILD_TESTING=ON",
            "-DBUILD_EXAMPLES=OFF",
            "-DBUILD_BENCHMARKS=OFF",
            f"-DQCURL_BUILD_LIBCURL_CONSISTENCY={'ON' if profile.enable_libcurl_consistency else 'OFF'}",
            f"-DCMAKE_C_FLAGS={profile.sanitizer_flags}",
            f"-DCMAKE_CXX_FLAGS={profile.sanitizer_flags}",
            f"-DCMAKE_EXE_LINKER_FLAGS={profile.sanitizer_flags}",
            f"-DCMAKE_SHARED_LINKER_FLAGS={profile.sanitizer_flags}",
        ]
    )
    return command


def tsan_subject_regex() -> str:
    return (
        "^(tst_QCNetworkReply|tst_QCNetworkConnectionPool|tst_QCWebSocket"
        "|tst_QCWebSocketPool)$"
    )


def parse_qt_test_functions(output: str) -> list[str]:
    """Parse the function names printed by a QtTest binary's ``-functions`` option."""

    functions = []
    for line in output.splitlines():
        function = line.strip()
        if function.endswith("()"):
            function = function[:-2]
        if function:
            functions.append(function)
    return functions


def tsan_scheduler_commands(build_dir: Path, functions: list[str]) -> list[list[str]]:
    """Build one standalone QtTest command for every Scheduler function."""

    binary = str(build_dir / "tests" / "tst_QCNetworkScheduler")
    return [[binary, function] for function in functions]


def websocket_enabled(build_dir: Path) -> bool:
    """Return whether the configured sanitizer build exposes WebSocket targets."""

    config_path = build_dir / "src" / "QCurlConfig.h"
    if not config_path.is_file():
        return False
    return any(
        line.strip() == "#define QCURL_WEBSOCKET_SUPPORT"
        for line in config_path.read_text(encoding="utf-8").splitlines()
    )


def sanitizer_build_command(
    build_dir: Path,
    profile: SanitizerProfile,
    *,
    websocket_available: bool,
    nproc: int,
) -> list[str]:
    """Build every subject that the selected sanitizer gate will execute."""

    command = ["cmake", "--build", str(build_dir)]
    if profile.name == "tsan":
        targets = [
            "tst_QCNetworkReply",
            "tst_QCBlockingNetworkClient",
            "tst_QCBlockingRequestConfig",
            "tst_QCNetworkMockHandler",
            "tst_QCNetworkDiagnostics",
            "tst_QCNetworkScheduler",
            "tst_QCNetworkConnectionPool",
        ]
        if websocket_available:
            targets.extend(["tst_QCWebSocket", "tst_QCWebSocketPool"])
        command.extend(["--target", *targets])
    if nproc > 0:
        command.append(f"-j{nproc}")
    return command


def _set_sanitizer_option(options: str, name: str, value: str) -> str:
    retained = [
        item for item in options.split(":") if item and not item.startswith(f"{name}=")
    ]
    retained.append(f"{name}={value}")
    return ":".join(retained)


def sanitizer_subject_environment(
    repo_root: Path,
    profile: SanitizerProfile,
    *,
    base_environment: dict[str, str] | None = None,
) -> dict[str, str]:
    """Return the strict runtime environment for the selected sanitizer subject."""

    environment = dict(os.environ if base_environment is None else base_environment)
    if profile.name not in {"asan-ubsan-lsan", "tsan"}:
        return environment

    option_name = "TSAN_OPTIONS" if profile.name == "tsan" else "ASAN_OPTIONS"
    options = environment.get(option_name, "")
    if profile.name == "tsan":
        suppression_path = repo_root / "tests" / "qcurl" / "qt_test_tsan.supp"
        options = _set_sanitizer_option(options, "suppressions", str(suppression_path))
        options = _set_sanitizer_option(options, "print_suppressions", "1")
        # QtTest owns a per-function watchdog that remains reported after it finishes.
        options = _set_sanitizer_option(options, "report_thread_leaks", "0")
    addr2line = shutil.which("addr2line", path=environment.get("PATH"))
    if addr2line:
        # LLVM 22 symbolizer 可能在并发 QtTest 退出时崩溃；addr2line 仍保留精确抑制与失败语义。
        options = _set_sanitizer_option(options, "external_symbolizer_path", addr2line)
        options = _set_sanitizer_option(options, "allow_addr2line", "1")
    environment[option_name] = options
    return environment


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def run_command(command: list[str], *, cwd: Path, log_path: Path, env: dict[str, str] | None = None) -> int:
    completed = subprocess.run(
        command,
        cwd=str(cwd),
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_text(completed.stdout or "", encoding="utf-8")
    return int(completed.returncode)


def discover_qt_test_functions(
    binary: Path,
    *,
    cwd: Path,
    env: dict[str, str],
    log_path: Path,
) -> tuple[int, list[str]]:
    """Discover Scheduler functions and persist the discovery output."""

    command = [str(binary), "-functions"]
    try:
        completed = subprocess.run(
            command,
            cwd=str(cwd),
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        output = completed.stdout or ""
        returncode = int(completed.returncode)
    except OSError as error:
        output = f"{type(error).__name__}: {error}\n"
        returncode = 127
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_text(output, encoding="utf-8")
    return returncode, parse_qt_test_functions(output) if returncode == 0 else []


def run_tsan_scheduler_functions(
    repo_root: Path,
    build_dir: Path,
    logs_dir: Path,
    env: dict[str, str],
) -> tuple[int, list[list[str]]]:
    """Run every Scheduler QtTest function in an independent process."""

    binary = build_dir / "tests" / "tst_QCNetworkScheduler"
    discovery_rc, functions = discover_qt_test_functions(
        binary,
        cwd=repo_root,
        env=env,
        log_path=logs_dir / "scheduler-functions.log",
    )
    commands = tsan_scheduler_commands(build_dir, functions)
    subject_rc = discovery_rc
    for function, command in zip(functions, commands):
        log_name = f"subject-scheduler-{function}.log"
        result = run_command(command, cwd=repo_root, log_path=logs_dir / log_name, env=env)
        if result != 0 and subject_rc == 0:
            subject_rc = result
    return subject_rc, commands


def run_tsan_subjects(
    repo_root: Path,
    build_dir: Path,
    output_dir: Path,
    env: dict[str, str],
) -> tuple[int, list[list[str]]]:
    """Run four representative binaries plus isolated Scheduler functions."""

    logs_dir = output_dir / "logs"
    representative_command = [
        "ctest",
        "--test-dir",
        str(build_dir),
        "--output-on-failure",
        "-R",
        tsan_subject_regex(),
    ]
    subject_rc = run_command(
        representative_command,
        cwd=repo_root,
        log_path=logs_dir / "subject.log",
        env=env,
    )
    scheduler_rc, scheduler_commands = run_tsan_scheduler_functions(
        repo_root,
        build_dir,
        logs_dir,
        env,
    )
    if subject_rc == 0:
        subject_rc = scheduler_rc
    return subject_rc, [representative_command, *scheduler_commands]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Run QCurl sanitizer builds and archive reports.")
    parser.add_argument("--profile", choices=tuple(sanitizer_profiles()), required=True)
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--run-id", default="")
    parser.add_argument("--nproc", default=os.environ.get("QCURL_SANITIZER_NPROC", "0"))
    args = parser.parse_args(argv)

    repo_root = Path(__file__).resolve().parent.parent
    profile = sanitizer_profiles()[args.profile]
    build_dir = Path(args.build_dir)
    if not build_dir.is_absolute():
        build_dir = (repo_root / build_dir).resolve()
    output_dir = Path(args.output_dir)
    if not output_dir.is_absolute():
        output_dir = (repo_root / output_dir).resolve()
    logs_dir = output_dir / "logs"
    report_path = output_dir / "report.json"
    run_id = (args.run_id or "").strip() or datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")

    configure_cmd = cmake_configure_command(repo_root, build_dir, profile)
    configure_rc = run_command(configure_cmd, cwd=repo_root, log_path=logs_dir / "configure.log")
    nproc = int(args.nproc) if str(args.nproc).isdigit() else 0
    build_cmd = sanitizer_build_command(
        build_dir,
        profile,
        websocket_available=configure_rc == 0 and websocket_enabled(build_dir),
        nproc=nproc,
    )
    build_rc = 0 if configure_rc != 0 else run_command(build_cmd, cwd=repo_root, log_path=logs_dir / "build.log")

    subject_rc = 0
    subject_cmd: list[str]
    subject_commands: list[list[str]] = []
    if configure_rc == 0 and build_rc == 0:
        subject_env = sanitizer_subject_environment(repo_root, profile)
        if args.profile == "asan-ubsan-lsan":
            subject_cmd = [
                "python3",
                str(repo_root / "scripts" / "run_uce_gate.py"),
                "--tier",
                "nightly",
                "--build-dir",
                str(build_dir),
                "--run-id",
                f"{run_id}-{profile.name}",
                "--evidence-root",
                str(output_dir / "uce"),
            ]
            subject_commands = [subject_cmd]
            subject_rc = run_command(
                subject_cmd,
                cwd=repo_root,
                log_path=logs_dir / "subject.log",
                env=subject_env,
            )
        else:
            subject_rc, subject_commands = run_tsan_subjects(
                repo_root,
                build_dir,
                output_dir,
                subject_env,
            )
            subject_cmd = subject_commands[0] if subject_commands else []
    else:
        subject_cmd = []

    policy_violations: list[str] = []
    if configure_rc != 0:
        policy_violations.append("sanitizer_config_failed")
    if build_rc != 0:
        policy_violations.append("sanitizer_build_failed")
    if configure_rc == 0 and build_rc == 0 and subject_rc != 0:
        policy_violations.append("sanitizer_subject_failed")

    report = {
        "generated_at_utc": utc_now_iso(),
        "schema": "qcurl-uce/sanitizer-report@v1",
        "profile": profile.name,
        "build_dir": str(build_dir),
        "run_id": run_id,
        "configure_command": configure_cmd,
        "build_command": build_cmd,
        "subject_command": subject_cmd,
        "subject_commands": subject_commands,
        "configure_returncode": configure_rc,
        "build_returncode": build_rc,
        "subject_returncode": subject_rc,
        "policy_violations": policy_violations,
        "result": "pass" if not policy_violations else "fail",
    }
    write_json(report_path, report)
    return 0 if not policy_violations else 3


if __name__ == "__main__":
    raise SystemExit(main())
