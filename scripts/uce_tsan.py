"""现有 sanitizer 入口使用的 TSan 校准、依赖核对和严格用例执行。"""

from __future__ import annotations

import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Callable

from scripts.ctest_strict import ctest_targets_without_unique_pass


def tsan_subject_names(websocket_available: bool = True) -> list[str]:
    """返回必须实际执行的代表集合；Scheduler 另按函数隔离执行。"""
    names = [
        "tst_QCNetworkReply",
        "tst_QCNetworkConnectionPool",
        "tst_QCNetworkActorThreadModel",
        "tst_QCNetworkPoolContract",
        "tst_QCurlRuntime",
        "tst_QCNetworkNativeDiagnostics",
        "tst_QCNetworkCompletionContract",
    ]
    if websocket_available:
        names.extend(["tst_QCWebSocket", "tst_QCWebSocketPool"])
    return names


def tsan_subject_regex(websocket_available: bool = True) -> str:
    return "^(" + "|".join(tsan_subject_names(websocket_available)) + ")$"


def parse_qt_test_functions(output: str) -> list[str]:
    """只接受 QtTest 的非空、无重复函数列表，工具诊断不能成为测试名。"""
    lines = [line.strip() for line in output.splitlines() if line.strip()]
    if not lines or any(not re.fullmatch(r"[A-Za-z_]\w*\(\)", line) for line in lines):
        return []
    return [line[:-2] for line in lines] if len(set(lines)) == len(lines) else []


def tsan_scheduler_commands(build_dir: Path, functions: list[str]) -> list[list[str]]:
    return [[str(build_dir / "tests" / "tst_QCNetworkScheduler"), name] for name in functions]


def control_succeeded(mode: str, returncode: int, output: str) -> bool:
    """负对照必须定位故意竞争；加载失败、崩溃或没有报告均不算检测成功。"""
    if mode == "deliberate-race":
        return (
            returncode == 66
            and output.count("WARNING: ThreadSanitizer: data race") == 1
            and output.count("WARNING: ThreadSanitizer:") == 1
            and "deliberateRaceWrite" in output
            and re.search(r"qt_tsan_controls\.cpp:\d+", output) is not None
            and not re.search(
                r"FATAL:|DEADLYSIGNAL|CHECK failed|invalid path|unrecognized flag", output
            )
        )
    expected = {"std-mutex": 20000, "qt-mutex": 20000, "qt-wait": 42, "qt-queued": 42}
    return (
        mode in expected
        and returncode == 0
        and re.search(
            rf"^CONTROL PASS {re.escape(mode)} value={expected[mode]} Qt=\d", output, re.M
        )
        is not None
        and not re.search(r"Sanitizer:|FATAL:|CHECK failed|invalid path|unrecognized flag", output)
    )


def scheduler_function_succeeded(function: str, output: str) -> bool:
    """退出 0 还必须包含目标函数的 PASS 与零 skip/blacklist 的完整统计。"""
    totals = re.findall(
        r"^Totals: (\d+) passed, (\d+) failed, (\d+) skipped, (\d+) blacklisted", output, re.M
    )
    return (
        len(totals) == 1
        and int(totals[0][0]) >= 3
        and all(int(value) == 0 for value in totals[0][1:])
        and re.search(rf"^PASS\s+: tst_QCNetworkScheduler::{re.escape(function)}\(", output, re.M)
        is not None
    )


def validate_control_compilers(output: str, compiler_output: str) -> None:
    """核对 Clang 及数字版本，不把厂商展示前缀当作编译器身份。"""
    version = re.search(r"\bclang version (\d+\.\d+\.\d+)\b", compiler_output)
    if not version:
        raise ValueError("无法确认 TSan Clang 版本")
    expected = re.escape(version[1])
    if not re.search(
        rf"^CONTROL COMPILER [^\r\n]*\bClang {expected}\b", output, re.M
    ) or not re.search(
        rf"^CONTROL QT_BUILD [^\r\n]*\bby [^\r\n]*\bClang {expected}\b", output, re.M
    ):
        raise ValueError("TSan 程序、Qt 与当前 Clang 编译器版本不匹配")


def validate_loaded_qt(output: str, prefix: Path, required: set[str]) -> dict[str, str]:
    """核对加载器解析的全部 Qt 库，拒绝系统 Qt 混入或缺失依赖。"""
    libraries = dict(re.findall(r"libQt6(\w+)\.so[^\s]*\s+=>\s+(/\S+)", output))
    if "not found" in output or not required.issubset(libraries):
        raise ValueError("TSan 必需的 Qt 库未加载")
    if any(
        not Path(path).resolve().is_relative_to(prefix.resolve()) for path in libraries.values()
    ):
        raise ValueError("TSan 加载的 Qt 不属于显式指定的检测安装前缀")
    return libraries


def _read_cache(build_dir: Path) -> dict[str, str]:
    lines = (build_dir / "CMakeCache.txt").read_text(encoding="utf-8").splitlines()
    return {
        match[1]: match[2]
        for line in lines
        if (match := re.match(r"^([^/#][^:]*):[^=]+=(.*)$", line))
    }


def _inspect_binary(
    binary: Path, prefix: Path, logs: Path, env: dict[str, str], run: Callable
) -> dict[str, str]:
    link_log = logs / f"ldd-{binary.name}.log"
    if run(["ldd", str(binary)], log_path=link_log, env=env, timeout=60) != 0:
        raise ValueError(f"无法解析检测程序依赖: {binary.name}")
    output = link_log.read_text(encoding="utf-8")
    required = {"Core"} if binary.name == "qcurl_qt_tsan_control" else {"Core", "Network", "Test"}
    libraries = validate_loaded_qt(output, prefix, required)
    if binary.name == "qcurl_qt_tsan_control" and ("libQCurl" in output or "libQt6Test" in output):
        raise ValueError("独立对照不得链接 QCurl 或 QtTest")
    elf_log = logs / f"elf-{binary.name}.log"
    if run(["readelf", "-h", str(binary)], log_path=elf_log, env=env, timeout=60) != 0:
        raise ValueError(f"无法核对 ELF: {binary.name}")
    if not re.search(r"Type:\s+DYN\b", elf_log.read_text(encoding="utf-8")):
        raise ValueError(f"TSan 程序必须是 PIE: {binary.name}")
    return libraries


def inspect_tsan_environment(
    build_dir: Path, logs: Path, env: dict[str, str], names: list[str], run: Callable
) -> dict:
    """归档当前构建配置、编译器及加载库身份，不把单独插桩应用视为校准。"""
    cache = _read_cache(build_dir)
    if cache.get("QT_FEATURE_sanitize_thread") != "ON" or not cache.get("Qt6_DIR"):
        raise ValueError("请先以 Qt6_DIR 配置同版本、启用 -sanitize thread 的独立 Qt")
    prefix = Path(cache["Qt6_DIR"]).resolve().parents[2]
    compiler = cache.get("CMAKE_CXX_COMPILER", "")
    if "clang" not in Path(compiler).name:
        raise ValueError("TSan 需要与检测 Qt 匹配的 Clang")
    identity = {
        "qt_prefix": str(prefix),
        "cache": {
            key: value
            for key, value in cache.items()
            if key.startswith(
                (
                    "CMAKE_CXX_",
                    "CMAKE_C_",
                    "CMAKE_EXE_LINKER_FLAGS",
                    "CMAKE_SHARED_LINKER_FLAGS",
                    "Qt6",
                    "CURL_LIBRARY",
                    "CURL_INCLUDE",
                )
            )
            or key in {"CMAKE_BUILD_TYPE", "QT_FEATURE_sanitize_thread"}
        },
    }
    libraries = {}
    for name in ["qcurl_qt_tsan_control", *names, "tst_QCNetworkScheduler"]:
        libraries.update(_inspect_binary(build_dir / "tests" / name, prefix, logs, env, run))
    identity["qt_libraries"] = {}
    for name, path in libraries.items():
        with Path(path).open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        identity["qt_libraries"][name] = {"path": path, "sha256": digest}
    for name, command in [
        ("compiler", [compiler, "--version"]),
        ("cmake", ["cmake", "--version"]),
        ("curl", ["curl", "--version"]),
        ("qt", [str(prefix / "bin" / "qmake"), "-query"]),
    ]:
        log = logs / f"environment-{name}.log"
        if run(command, log_path=log, env=env, timeout=60) != 0:
            raise ValueError(f"无法记录检测依赖身份: {name}")
        identity[name] = log.read_text(encoding="utf-8")
    return identity


def run_tsan_controls(
    build_dir: Path, logs: Path, env: dict[str, str], run: Callable
) -> list[dict]:
    """正确与故意竞争场景都不使用 QtTest 抑制；保留各自真实退出码。"""
    control_env = dict(env)
    options = [
        item
        for item in env["TSAN_OPTIONS"].split(":")
        if not item.startswith(("suppressions=", "report_thread_leaks="))
    ]
    control_env["TSAN_OPTIONS"] = ":".join([*options, "report_thread_leaks=1"])
    results = []
    for mode in ["std-mutex", "qt-mutex", "qt-wait", "qt-queued", "deliberate-race"]:
        command = [str(build_dir / "tests" / "qcurl_qt_tsan_control"), mode]
        log = logs / f"control-{mode}.log"
        code = run(command, log_path=log, env=control_env, timeout=30)
        passed = control_succeeded(mode, code, log.read_text(encoding="utf-8"))
        results.append({"mode": mode, "returncode": code, "passed": passed})
    return results


def run_tsan_scheduler_functions(
    build_dir: Path, logs: Path, env: dict[str, str], run: Callable
) -> tuple[int, list[list[str]]]:
    """逐函数隔离 Scheduler；空集合、缺少目标 PASS 或 skip 均失败。"""
    log = logs / "scheduler-functions.log"
    code = run(
        [str(build_dir / "tests" / "tst_QCNetworkScheduler"), "-functions"],
        log_path=log,
        env=env,
        timeout=60,
    )
    functions = parse_qt_test_functions(log.read_text(encoding="utf-8")) if code == 0 else []
    if not functions:
        return code or 3, []
    commands = tsan_scheduler_commands(build_dir, functions)
    for function, command in zip(functions, commands):
        log = logs / f"subject-scheduler-{function}.log"
        result = run(command, log_path=log, env=env, timeout=300)
        if result == 0 and not scheduler_function_succeeded(
            function, log.read_text(encoding="utf-8")
        ):
            result = 3
        if result != 0 and code == 0:
            code = result
    return code, commands


def run_tsan_representatives(
    repo_root: Path,
    build_dir: Path,
    logs: Path,
    env: dict[str, str],
    names: list[str],
    run: Callable,
) -> tuple[int, list[list[str]]]:
    """核对注册集合与实际通过的目标，复用既有 QtTest 零 skip 门禁。"""
    regex = "^(" + "|".join(names) + ")$"
    discovery = ["ctest", "--test-dir", str(build_dir), "--show-only=json-v1", "-R", regex]
    log = logs / "subject-discovery.log"
    code = run(discovery, log_path=log, env=env, timeout=60)
    if code != 0:
        return code, []
    actual = [test["name"] for test in json.loads(log.read_text(encoding="utf-8"))["tests"]]
    if not names or sorted(actual) != sorted(names):
        raise ValueError("TSan 代表用例集合为空、缺项或有重复")
    command = [
        sys.executable,
        str(repo_root / "scripts" / "ctest_strict.py"),
        "--build-dir",
        str(build_dir),
        "--max-skips",
        "0",
        "--label-regex",
        "",
        "--",
        "--output-on-failure",
        "-R",
        regex,
    ]
    code = run(command, log_path=logs / "subject.log", env=env, timeout=3600)
    if code == 0:
        incomplete = ctest_targets_without_unique_pass(
            (logs / "subject.log").read_text(encoding="utf-8"),
            names,
        )
        if incomplete:
            raise ValueError("TSan 必需目标缺少唯一的 Passed 执行结果: " + ", ".join(incomplete))
    return code, [command]


def run_tsan_subjects(
    repo_root: Path,
    build_dir: Path,
    output_dir: Path,
    env: dict[str, str],
    *,
    websocket_available: bool,
    run: Callable,
) -> dict:
    """在同一证据目录完成环境核对、正负校准和产品回归。"""
    logs = output_dir / "logs"
    result = {"returncode": 3, "commands": [], "stage": "environment"}
    names = tsan_subject_names(websocket_available)
    try:
        result["environment"] = inspect_tsan_environment(build_dir, logs, env, names, run)
        result["stage"] = "controls"
        result["controls"] = run_tsan_controls(build_dir, logs, env, run)
        if not all(control["passed"] for control in result["controls"]):
            result["error"] = "TSan 正负对照未全部满足预期；产品用例未启动"
            return result
        validate_control_compilers(
            (logs / "control-std-mutex.log").read_text(encoding="utf-8"),
            result["environment"]["compiler"],
        )
        result["stage"] = "subjects"
        code, commands = run_tsan_representatives(repo_root, build_dir, logs, env, names, run)
        scheduler_code, scheduler_commands = run_tsan_scheduler_functions(build_dir, logs, env, run)
        result["returncode"] = code or scheduler_code
        result["commands"] = [*commands, *scheduler_commands]
    except (OSError, ValueError, KeyError) as error:
        result["error"] = str(error)
    return result
