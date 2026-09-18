#!/usr/bin/env python3
"""构建 sanitizer 候选，执行现有门禁并归档原始命令与源码身份。"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import signal
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime
from datetime import timezone
from functools import partial
from pathlib import Path
from typing import Any

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from scripts import uce_tsan
from scripts.uce_gate.candidate import capture_candidate_fingerprint


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


def cmake_configure_command(
    source_dir: Path, build_dir: Path, profile: SanitizerProfile
) -> list[str]:
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
        command.extend(["-DCMAKE_C_COMPILER=clang", "-DCMAKE_CXX_COMPILER=clang++"])
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
            *uce_tsan.tsan_subject_names(websocket_available),
            "qcurl_qt_tsan_control",
            "tst_QCNetworkScheduler",
            "tst_QCBlockingNetworkClient",
            "tst_QCBlockingRequestConfig",
            "tst_QCNetworkMockHandler",
            "tst_QCNetworkDiagnostics",
        ]
        command.extend(["--target", *targets])
    if nproc > 0:
        command.append(f"-j{nproc}")
    return command


def _set_sanitizer_option(options: str, name: str, value: str) -> str:
    retained = [item for item in options.split(":") if item and not item.startswith(f"{name}=")]
    retained.append(f"{name}={value}")
    return ":".join(retained)


def _strict_tsan_options(repo_root: Path, options: str) -> str:
    # 外部选项只能调节已知诊断参数；所有影响报告和检测覆盖的开关固定。
    forced = {
        "halt_on_error": "1",
        "exitcode": "66",
        "report_bugs": "1",
        "report_atomic_races": "1",
        "report_signal_unsafe": "1",
        "report_destroy_locked": "1",
        "detect_deadlocks": "1",
        "ignore_noninstrumented_modules": "0",
        "ignore_interceptors_accesses": "0",
        "suppress_equal_stacks": "0",
        "suppress_equal_addresses": "0",
        "symbolize": "1",
        "print_suppressions": "1",
        "suppressions": str(repo_root / "tests" / "qcurl" / "qt_test_tsan.supp"),
        # 仅沿用 QtTest 逐函数 watchdog 的既有退出处理，不用于独立 Qt 对照。
        "report_thread_leaks": "0",
    }
    allowed = {"history_size", "verbosity", "external_symbolizer_path", "allow_addr2line"}
    for item in filter(None, options.split(":")):
        name, separator, _ = item.partition("=")
        if not separator or name not in forced.keys() | allowed:
            raise ValueError(f"不支持的 TSAN_OPTIONS 项: {name}")
    for name, value in forced.items():
        options = _set_sanitizer_option(options, name, value)
    return options


def sanitizer_subject_environment(
    repo_root: Path,
    profile: SanitizerProfile,
    *,
    base_environment: dict[str, str] | None = None,
) -> dict[str, str]:
    """构造严格检测环境，外部选项不能禁用泄漏检测或失败退出。"""
    environment = dict(os.environ if base_environment is None else base_environment)
    if profile.name not in {"asan-ubsan-lsan", "tsan"}:
        return environment
    option_name = "TSAN_OPTIONS" if profile.name == "tsan" else "ASAN_OPTIONS"
    options = environment.get(option_name, "")
    if profile.name == "tsan":
        options = _strict_tsan_options(repo_root, options)
        environment["LC_ALL"] = "C.UTF-8"
    else:
        for name, value in {
            "detect_leaks": "1", "leak_check_at_exit": "1",
            "halt_on_error": "1", "exitcode": "1",
        }.items():
            options = _set_sanitizer_option(options, name, value)
        for variable, required in {
            "LSAN_OPTIONS": {"detect_leaks": "1", "leak_check_at_exit": "1", "exitcode": "23"},
            "UBSAN_OPTIONS": {"halt_on_error": "1", "exitcode": "1", "print_stacktrace": "1"},
        }.items():
            effective = environment.get(variable, "")
            for name, value in required.items():
                effective = _set_sanitizer_option(effective, name, value)
            environment[variable] = effective
    addr2line = shutil.which("addr2line", path=environment.get("PATH"))
    if addr2line:
        # LLVM 22 symbolizer 在并发 QtTest 退出时可能崩溃，保留可定位的替代符号解析。
        options = _set_sanitizer_option(options, "external_symbolizer_path", addr2line)
        options = _set_sanitizer_option(options, "allow_addr2line", "1")
    environment[option_name] = options
    return environment


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def run_command(
    command: list[str],
    *,
    cwd: Path,
    log_path: Path,
    env: dict[str, str] | None = None,
    timeout: int = 3600,
    records: list[dict] | None = None,
) -> int:
    """保存原始输出与真实进程状态；超时终止整个子进程组，不留后台检测。"""
    process_code = None
    timed_out = False
    try:
        with subprocess.Popen(
            command,
            cwd=cwd,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        ) as process:
            try:
                output, _ = process.communicate(timeout=timeout)
            except subprocess.TimeoutExpired:
                timed_out = True
                os.killpg(process.pid, signal.SIGKILL)
                output, _ = process.communicate()
            process_code = process.returncode
        code = 124 if timed_out else int(process_code)
        if timed_out:
            output += f"\n检测命令超时（{timeout} 秒），进程组已终止。\n"
    except OSError as error:
        output, code = f"{type(error).__name__}: {error}\n", 127
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_text(output or "", encoding="utf-8")
    if records is not None:
        records.append(
            {
                "command": command,
                "returncode": code,
                "process_returncode": process_code,
                "timed_out": timed_out,
                "log": str(log_path),
                "tsan_options": (env or {}).get("TSAN_OPTIONS", ""),
            }
        )
    return code


def parse_arguments(argv: list[str] | None) -> argparse.Namespace:
    """保持 sanitizer 入口的既有命令行参数。"""
    parser = argparse.ArgumentParser(description="Run QCurl sanitizer builds and archive reports.")
    parser.add_argument("--profile", choices=tuple(sanitizer_profiles()), required=True)
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--run-id", default="")
    parser.add_argument("--nproc", default=os.environ.get("QCURL_SANITIZER_NPROC", "0"))
    return parser.parse_args(argv)


def _execute_subjects(repo_root: Path, profile: SanitizerProfile, report: dict, run) -> None:
    build_dir, output_dir = Path(report["build_dir"]), Path(report["output_dir"])
    environment = sanitizer_subject_environment(repo_root, profile)
    report["sanitizer_options"] = {
        name: environment[name]
        for name in ("ASAN_OPTIONS", "TSAN_OPTIONS", "UBSAN_OPTIONS", "LSAN_OPTIONS")
        if name in environment
    }
    if profile.name == "tsan":
        result = uce_tsan.run_tsan_subjects(
            repo_root,
            build_dir,
            output_dir,
            environment,
            websocket_available=websocket_enabled(build_dir),
            run=run,
        )
        report["tsan"] = result
        report["subject_returncode"] = result["returncode"]
        report["subject_commands"] = result["commands"]
    else:
        command = [
            sys.executable,
            str(repo_root / "scripts" / "run_uce_gate.py"),
            "--tier",
            "nightly",
            "--build-dir",
            str(build_dir),
            "--run-id",
            f"{report['run_id']}-{profile.name}",
            "--evidence-root",
            str(output_dir / "uce"),
        ]
        report["subject_commands"] = [command]
        report["subject_returncode"] = run(
            command, log_path=output_dir / "logs" / "subject.log", env=environment, timeout=14400
        )
    report["subject_command"] = next(iter(report["subject_commands"]), [])


def _execute_profile(repo_root: Path, args: argparse.Namespace, report: dict) -> None:
    profile = sanitizer_profiles()[args.profile]
    build_dir, output_dir = Path(report["build_dir"]), Path(report["output_dir"])
    run = partial(run_command, cwd=repo_root, records=report["command_results"])
    report["configure_command"] = cmake_configure_command(repo_root, build_dir, profile)
    report["configure_returncode"] = run(
        report["configure_command"], log_path=output_dir / "logs" / "configure.log"
    )
    if report["configure_returncode"] != 0:
        return
    report["build_command"] = sanitizer_build_command(
        build_dir,
        profile,
        websocket_available=websocket_enabled(build_dir),
        nproc=int(args.nproc) if str(args.nproc).isdigit() else 0,
    )
    report["build_returncode"] = run(
        report["build_command"], log_path=output_dir / "logs" / "build.log"
    )
    if report["build_returncode"] == 0:
        _execute_subjects(repo_root, profile, report, run)


def _candidate_after(repo_root: Path, output_dir: Path, report: dict) -> None:
    try:
        report["candidate_after"] = capture_candidate_fingerprint(
            repo_root, excluded_paths=(output_dir,)
        )
        before = report.get("candidate_before", {})
        report["candidate_unchanged"] = (
            bool(before.get("available"))
            and before.get("combined") == report["candidate_after"]["combined"]
        )
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        report["candidate_error"] = str(error)
        report["candidate_unchanged"] = False


def _finish_report(report: dict) -> int:
    violations = []
    if report.get("configure_returncode") not in {None, 0}:
        violations.append("sanitizer_config_failed")
    if report.get("build_returncode") not in {None, 0}:
        violations.append("sanitizer_build_failed")
    incomplete = (
        report.get("configure_returncode") == 0
        and report.get("build_returncode") == 0
        and report.get("subject_returncode") is None
    )
    if (
        report.get("subject_returncode") not in {None, 0}
        or incomplete
        or report.get("execution_error")
        or not report.get("candidate_unchanged")
    ):
        violations.append("sanitizer_subject_failed")
    report["policy_violations"] = violations
    report["result"] = "pass" if not violations else "fail"
    write_json(Path(report["output_dir"]) / "report.json", report)
    print(f"sanitizer {report['profile']}: {report['result']}; policy_violations={violations}")
    return 0 if not violations else 3


def main(argv: list[str] | None = None) -> int:
    """围绕同一候选执行构建和检测，源码漂移不能借用此前的通过结果。"""
    args = parse_arguments(argv)
    repo_root = Path(__file__).resolve().parent.parent
    build_dir = (repo_root / args.build_dir).resolve()
    output_dir = (repo_root / args.output_dir).resolve()
    report = {
        "generated_at_utc": utc_now_iso(),
        "schema": "qcurl-uce/sanitizer-report@v1",
        "profile": args.profile,
        "build_dir": str(build_dir),
        "output_dir": str(output_dir),
        "run_id": args.run_id.strip() or datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ"),
        "configure_command": [],
        "build_command": [],
        "subject_command": [],
        "subject_commands": [],
        "command_results": [],
        "configure_returncode": None,
        "build_returncode": None,
        "subject_returncode": None,
    }
    try:
        report["candidate_before"] = capture_candidate_fingerprint(
            repo_root, excluded_paths=(output_dir,)
        )
        _execute_profile(repo_root, args, report)
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        report["execution_error"] = str(error)
    _candidate_after(repo_root, output_dir, report)
    return _finish_report(report)


if __name__ == "__main__":
    raise SystemExit(main())
