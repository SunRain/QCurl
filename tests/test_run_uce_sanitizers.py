from __future__ import annotations

import json
import os
import sys
from functools import partial
from pathlib import Path

import pytest

from scripts import run_uce_sanitizers as sanitizer
from scripts import uce_tsan
from scripts.run_uce_sanitizers import cmake_configure_command
from scripts.run_uce_sanitizers import sanitizer_build_command
from scripts.run_uce_sanitizers import sanitizer_profiles
from scripts.run_uce_sanitizers import sanitizer_subject_environment
from scripts.uce_tsan import control_succeeded
from scripts.uce_tsan import parse_qt_test_functions
from scripts.uce_tsan import scheduler_function_succeeded
from scripts.uce_tsan import tsan_scheduler_commands
from scripts.uce_tsan import tsan_subject_regex
from scripts.uce_tsan import validate_loaded_qt
from scripts.run_uce_sanitizers import websocket_enabled


def test_cmake_configure_command_enables_consistency_for_asan(tmp_path: Path) -> None:
    source_dir = tmp_path / "src"
    build_dir = tmp_path / "build"
    profile = sanitizer_profiles()["asan-ubsan-lsan"]

    command = cmake_configure_command(source_dir, build_dir, profile)

    assert f"-DQCURL_BUILD_LIBCURL_CONSISTENCY=ON" in command
    assert any(
        item.startswith("-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined,leak") for item in command
    )


def test_cmake_configure_command_requires_clang_for_tsan(tmp_path: Path) -> None:
    command = cmake_configure_command(
        tmp_path / "src", tmp_path / "build", sanitizer_profiles()["tsan"]
    )

    assert "-DCMAKE_C_COMPILER=clang" in command
    assert "-DCMAKE_CXX_COMPILER=clang++" in command


def test_tsan_subject_regex_is_stable() -> None:
    pattern = tsan_subject_regex()

    assert "tst_QCNetworkReply" in pattern
    assert "tst_QCNetworkScheduler" not in pattern
    assert "tst_QCNetworkConnectionPool" in pattern
    assert "tst_QCWebSocket" in pattern
    assert "tst_QCWebSocketPool" in pattern


def test_tsan_scheduler_commands_split_each_qttest_function(tmp_path: Path) -> None:
    functions = parse_qt_test_functions("testFirst()\ntestSecond()\n")

    commands = tsan_scheduler_commands(tmp_path, functions)

    binary = str(tmp_path / "tests" / "tst_QCNetworkScheduler")
    assert functions == ["testFirst", "testSecond"]
    assert commands == [[binary, "testFirst"], [binary, "testSecond"]]


def test_asan_build_command_builds_the_complete_nightly_suite(tmp_path: Path) -> None:
    command = sanitizer_build_command(
        tmp_path / "build",
        sanitizer_profiles()["asan-ubsan-lsan"],
        websocket_available=True,
        nproc=4,
    )

    assert command == ["cmake", "--build", str(tmp_path / "build"), "-j4"]


def test_tsan_build_command_remains_focused(tmp_path: Path) -> None:
    command = sanitizer_build_command(
        tmp_path / "build",
        sanitizer_profiles()["tsan"],
        websocket_available=True,
        nproc=3,
    )

    assert "--target" in command
    assert "tst_QCNetworkScheduler" in command
    assert "tst_QCWebSocketPool" in command
    assert set(uce_tsan.tsan_subject_names()).issubset(command)
    assert "qcurl_qt_tsan_control" in command
    assert command[-1] == "-j3"


def test_websocket_enabled_reads_configured_feature(tmp_path: Path) -> None:
    config_dir = tmp_path / "src"
    config_dir.mkdir()
    config_path = config_dir / "QCurlConfig.h"
    config_path.write_text("/* #undef QCURL_WEBSOCKET_SUPPORT */\n", encoding="utf-8")
    assert not websocket_enabled(tmp_path)

    config_path.write_text("#define QCURL_WEBSOCKET_SUPPORT\n", encoding="utf-8")
    assert websocket_enabled(tmp_path)


def test_tsan_subject_environment_preserves_options_and_scopes_qttest_noise(
    tmp_path: Path,
) -> None:
    environment = sanitizer_subject_environment(
        tmp_path,
        sanitizer_profiles()["tsan"],
        base_environment={"TSAN_OPTIONS": "history_size=7"},
    )

    options = environment["TSAN_OPTIONS"].split(":")
    assert "history_size=7" in options
    assert f"suppressions={tmp_path / 'tests' / 'qcurl' / 'qt_test_tsan.supp'}" in options
    assert "print_suppressions=1" in options
    assert "report_thread_leaks=0" in options
    assert "halt_on_error=1" in options
    assert "exitcode=66" in options


def test_tsan_subject_environment_uses_addr2line_when_available(tmp_path: Path) -> None:
    addr2line = tmp_path / "addr2line"
    addr2line.write_text("#!/bin/sh\n", encoding="utf-8")
    addr2line.chmod(0o755)

    environment = sanitizer_subject_environment(
        tmp_path,
        sanitizer_profiles()["tsan"],
        base_environment={"PATH": str(tmp_path)},
    )

    options = environment["TSAN_OPTIONS"].split(":")
    assert f"external_symbolizer_path={addr2line}" in options
    assert "allow_addr2line=1" in options


def test_asan_subject_environment_uses_addr2line_when_available(tmp_path: Path) -> None:
    addr2line = tmp_path / "addr2line"
    addr2line.write_text("#!/bin/sh\n", encoding="utf-8")
    addr2line.chmod(0o755)

    environment = sanitizer_subject_environment(
        tmp_path,
        sanitizer_profiles()["asan-ubsan-lsan"],
        base_environment={"PATH": str(tmp_path), "ASAN_OPTIONS": "detect_leaks=1"},
    )

    options = environment["ASAN_OPTIONS"].split(":")
    assert "detect_leaks=1" in options
    assert f"external_symbolizer_path={addr2line}" in options
    assert "allow_addr2line=1" in options


def test_qttest_tsan_suppression_allows_only_logger_shutdown_races() -> None:
    suppression_path = Path(__file__).resolve().parents[1] / "tests" / "qcurl" / "qt_test_tsan.supp"
    rules = [
        line.strip()
        for line in suppression_path.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]

    assert rules == [
        "race:QPlainTestLogger::stopLogging",
        "race:QAbstractTestLogger::outputString",
    ]


@pytest.mark.parametrize(
    "mode,value", [("std-mutex", 20000), ("qt-mutex", 20000), ("qt-wait", 42), ("qt-queued", 42)]
)
def test_positive_controls_require_exact_result_and_no_report(mode: str, value: int) -> None:
    output = f"CONTROL PASS {mode} value={value} Qt=6.11.2\n"
    assert control_succeeded(mode, 0, output)
    assert not control_succeeded(mode, 66, output)
    assert not control_succeeded(mode, 0, output + "WARNING: ThreadSanitizer: data race\n")
    assert not control_succeeded(mode, 0, output.replace(f"value={value}", "value=-1"))


def test_deliberate_race_requires_detector_exit_and_expected_source() -> None:
    report = (
        "WARNING: ThreadSanitizer: data race\n" "#0 deliberateRaceWrite qt_tsan_controls.cpp:106\n"
    )
    assert control_succeeded("deliberate-race", 66, report)
    for code, output in [
        (0, report),
        (66, ""),
        (-11, report),
        (124, report),
        (66, report.replace("deliberateRaceWrite", "unrelated")),
        (66, report + "FATAL: ThreadSanitizer: startup failed"),
    ]:
        assert not control_succeeded("deliberate-race", code, output)


@pytest.mark.parametrize("output", ["", "testFirst()\ntestFirst()\n", "WARNING: tool failure\n"])
def test_scheduler_discovery_rejects_empty_duplicate_or_invalid_output(output: str) -> None:
    assert parse_qt_test_functions(output) == []


def test_scheduler_requires_target_pass_and_zero_skips() -> None:
    output = (
        "PASS   : tst_QCNetworkScheduler::testFirst()\n"
        "Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted, 1ms\n"
    )
    assert scheduler_function_succeeded("testFirst", output)
    assert scheduler_function_succeeded(
        "testFirst",
        output + "ThreadSanitizer: Matched 1 suppressions:\n1 race:QPlainTestLogger::stopLogging\n",
    )
    assert not scheduler_function_succeeded("testOther", output)
    assert not scheduler_function_succeeded("testFirst", output.replace("0 skipped", "1 skipped"))
    assert not scheduler_function_succeeded(
        "testFirst", output.replace("0 blacklisted", "1 blacklisted")
    )
    assert not scheduler_function_succeeded("testFirst", "Totals: 2 passed, 0 failed, 0 skipped\n")


def test_loaded_qt_must_match_explicit_instrumented_prefix(tmp_path: Path) -> None:
    prefix = tmp_path / "qt"
    core = prefix / "lib" / "libQt6Core.so.6"
    output = f"libQt6Core.so.6 => {core} (0x1234)\n"
    assert validate_loaded_qt(output, prefix, {"Core"}) == {"Core": str(core)}
    for invalid in [output.replace(str(prefix), "/usr"), "", output + "libssl.so => not found\n"]:
        with pytest.raises(ValueError):
            validate_loaded_qt(invalid, prefix, {"Core"})


@pytest.mark.parametrize(
    "vendor,qt_vendor",
    [("", ""), ("Ubuntu ", ""), ("Debian ", ""), ("Custom Vendor ", "Custom Vendor ")],
)
def test_control_compilers_accept_matching_versions_with_vendor_prefixes(
    vendor: str, qt_vendor: str
) -> None:
    output = (
        f"CONTROL COMPILER {vendor}Clang 22.1.8 (package build)\n"
        f"CONTROL QT_BUILD Qt 6.11.2 (by {qt_vendor}Clang 22.1.8)\n"
    )
    uce_tsan.validate_control_compilers(output, f"{vendor}clang version 22.1.8\n")


@pytest.mark.parametrize(
    "field,replacement",
    [
        ("compiler", "gcc version 22.1.8\n"),
        ("compiler", "clang version unknown\n"),
        ("compiler", "clang version 22.1.9\n"),
        ("program", "CONTROL COMPILER GCC 22.1.8\n"),
        ("program", ""),
        ("program", "CONTROL COMPILER Clang 22.1.9\n"),
        ("qt", "CONTROL QT_BUILD Qt 6.11.2 (by GCC 22.1.8)\n"),
        ("qt", "CONTROL QT_BUILD Qt 6.11.2 (by Clang)\n"),
        ("qt", "CONTROL QT_BUILD Qt 6.11.2 (by Clang 22.1.9)\n"),
    ],
)
def test_control_compilers_reject_missing_non_clang_or_mismatched_versions(
    field: str, replacement: str
) -> None:
    fields = {
        "compiler": "clang version 22.1.8\n",
        "program": "CONTROL COMPILER Clang 22.1.8\n",
        "qt": "CONTROL QT_BUILD Qt 6.11.2 (by Clang 22.1.8)\n",
    }
    fields[field] = replacement
    with pytest.raises(ValueError):
        uce_tsan.validate_control_compilers(fields["program"] + fields["qt"], fields["compiler"])


def test_tsan_cannot_disable_reports_or_ignore_dependency_accesses(tmp_path: Path) -> None:
    environment = sanitizer_subject_environment(
        tmp_path,
        sanitizer_profiles()["tsan"],
        base_environment={
            "TSAN_OPTIONS": "report_bugs=0:exitcode=0:ignore_noninstrumented_modules=1"
        },
    )
    options = environment["TSAN_OPTIONS"].split(":")
    assert "report_bugs=1" in options
    assert "exitcode=66" in options
    assert "ignore_noninstrumented_modules=0" in options
    with pytest.raises(ValueError, match="TSAN_OPTIONS"):
        sanitizer_subject_environment(
            tmp_path,
            sanitizer_profiles()["tsan"],
            base_environment={"TSAN_OPTIONS": "ignore_sync=1"},
        )


@pytest.mark.parametrize("code", [0, 7])
def test_command_records_actual_exit(tmp_path: Path, code: int) -> None:
    records = []
    log = tmp_path / "command.log"
    command = [sys.executable, "-c", f"print('actual-output'); raise SystemExit({code})"]
    assert sanitizer.run_command(command, cwd=tmp_path, log_path=log, records=records) == code
    assert log.read_text(encoding="utf-8") == "actual-output\n"
    assert records[0]["process_returncode"] == code
    assert not records[0]["timed_out"]


def test_command_timeout_cannot_pass(tmp_path: Path) -> None:
    records = []
    command = [sys.executable, "-c", "import time; time.sleep(30)"]
    result = sanitizer.run_command(
        command, cwd=tmp_path, log_path=tmp_path / "timeout.log", timeout=0.1, records=records
    )
    assert result == 124
    assert records[0]["timed_out"]
    assert records[0]["process_returncode"] == -9


def test_command_start_failure_is_archived(tmp_path: Path) -> None:
    records = []
    result = sanitizer.run_command(
        [str(tmp_path / "missing")],
        cwd=tmp_path,
        log_path=tmp_path / "missing.log",
        records=records,
    )
    assert result == 127
    assert records[0]["process_returncode"] is None
    assert "FileNotFoundError" in (tmp_path / "missing.log").read_text(encoding="utf-8")


def test_zero_product_tests_fail_before_execution(tmp_path: Path) -> None:
    commands = []

    def run(command, *, log_path, **kwargs):
        commands.append(command)
        log_path.write_text('{"tests": []}', encoding="utf-8")
        return 0

    with pytest.raises(ValueError, match="集合为空"):
        uce_tsan.run_tsan_representatives(tmp_path, tmp_path, tmp_path, {}, ["required"], run)
    assert len(commands) == 1


@pytest.mark.parametrize("scenario", ["passed", "disabled", "skipped", "missing", "failed"])
def test_representatives_require_actual_ctest_execution(tmp_path: Path, scenario: str) -> None:
    repo_root = Path(__file__).resolve().parents[1]
    first = "print('fixture words Skipped Disabled are not CTest results')"
    exitcode = {"skipped": 77, "failed": 1}.get(scenario, 0)
    second = f"raise SystemExit({exitcode})"
    entries = [f"add_test(required_first {json.dumps(sys.executable)} -c {json.dumps(first)})"]
    if scenario != "missing":
        entries.append(
            f"add_test(required_second {json.dumps(sys.executable)} -c {json.dumps(second)})"
        )
    if scenario == "disabled":
        entries.append("set_tests_properties(required_second PROPERTIES DISABLED TRUE)")
    if scenario == "skipped":
        entries.append("set_tests_properties(required_second PROPERTIES SKIP_RETURN_CODE 77)")
    (tmp_path / "CTestTestfile.cmake").write_text("\n".join(entries) + "\n", encoding="utf-8")
    records = []
    run = partial(sanitizer.run_command, cwd=repo_root, records=records)
    arguments = (
        repo_root,
        tmp_path,
        tmp_path,
        {**os.environ, "LC_ALL": "C.UTF-8"},
        ["required_first", "required_second"],
        run,
    )
    if scenario in {"disabled", "skipped", "missing"}:
        expected_error = "缺项" if scenario == "missing" else "required_second"
        with pytest.raises(ValueError, match=expected_error):
            uce_tsan.run_tsan_representatives(*arguments)
        # CTest 的真实成功退出码必须保留；失败来自目标未实际通过，而非伪造进程状态。
        assert records[-1]["returncode"] == records[-1]["process_returncode"] == 0
    else:
        code, commands = uce_tsan.run_tsan_representatives(*arguments)
        assert code == records[-1]["returncode"] == (8 if scenario == "failed" else 0)
        assert len(commands) == 1


@pytest.mark.parametrize(
    "output",
    [
        "",
        "1: 1/1 Test #1: required ........ Passed 0.01 sec\n",
        "1/1 Test #1: required ........ Passed 0.01 sec\n" * 2,
        "1/2 Test #1: required ........ Passed 0.01 sec\n"
        "2/2 Test #1: required ........***Skipped 0.00 sec\n",
    ],
)
def test_representatives_reject_missing_or_duplicate_ctest_results(
    tmp_path: Path, output: str
) -> None:
    def run(command, *, log_path, **kwargs):
        content = (
            '{"tests": [{"name": "required"}]}'
            if "--show-only=json-v1" in command
            else output
        )
        log_path.write_text(content, encoding="utf-8")
        return 0

    with pytest.raises(ValueError, match="required"):
        uce_tsan.run_tsan_representatives(tmp_path, tmp_path, tmp_path, {}, ["required"], run)


@pytest.mark.parametrize(
    "discovery,output",
    [
        ("", ""),
        (
            "testFirst()\n",
            "SKIP   : tst_QCNetworkScheduler::testFirst()\n"
            "Totals: 2 passed, 0 failed, 1 skipped, 0 blacklisted\n",
        ),
        (
            "testFirst()\n",
            "PASS   : tst_QCNetworkScheduler::testOther()\n"
            "Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted\n",
        ),
    ],
)
def test_scheduler_gate_rejects_empty_skipped_or_missing_function(
    tmp_path: Path,
    discovery: str,
    output: str,
) -> None:
    def run(command, *, log_path, **kwargs):
        log_path.write_text(discovery if command[-1] == "-functions" else output, encoding="utf-8")
        return 0

    code, commands = uce_tsan.run_tsan_scheduler_functions(tmp_path, tmp_path, {}, run)
    assert code != 0
    assert len(commands) == (1 if discovery else 0)


@pytest.mark.parametrize("returncode", [0, 66, -11, 124, 127])
def test_scheduler_gate_preserves_process_status_with_suppression_statistics(
    tmp_path: Path, returncode: int
) -> None:
    output = (
        "PASS   : tst_QCNetworkScheduler::testFirst()\n"
        "Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted, 1ms\n"
        "ThreadSanitizer: Matched 1 suppressions:\n1 race:QPlainTestLogger::stopLogging\n"
    )

    def run(command, *, log_path, **kwargs):
        discovery = command[-1] == "-functions"
        log_path.write_text("testFirst()\n" if discovery else output, encoding="utf-8")
        return 0 if discovery else returncode

    code, commands = uce_tsan.run_tsan_scheduler_functions(tmp_path, tmp_path, {}, run)
    assert code == returncode
    assert len(commands) == 1


def test_uninstrumented_qt_never_starts_controls_or_products(tmp_path: Path) -> None:
    (tmp_path / "CMakeCache.txt").write_text(
        "QT_FEATURE_sanitize_thread:INTERNAL=OFF\n", encoding="utf-8"
    )

    def run(*args, **kwargs):
        pytest.fail("环境校验失败后不应执行对照或产品")

    result = uce_tsan.run_tsan_subjects(
        tmp_path, tmp_path, tmp_path, {}, websocket_available=False, run=run
    )
    assert result["returncode"] != 0
    assert result["stage"] == "environment"
    assert result["commands"] == []


@pytest.mark.parametrize("changed", [False, True])
def test_source_identity_drift_rejects_otherwise_successful_run(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    changed: bool,
) -> None:
    fingerprints = iter(
        [
            {"available": True, "combined": "before"},
            {"available": True, "combined": "after" if changed else "before"},
        ]
    )
    monkeypatch.setattr(
        sanitizer, "capture_candidate_fingerprint", lambda *a, **k: next(fingerprints)
    )

    def execute(repo_root, args, report):
        report.update(configure_returncode=0, build_returncode=0, subject_returncode=0)

    monkeypatch.setattr(sanitizer, "_execute_profile", execute)
    code = sanitizer.main(
        [
            "--profile",
            "tsan",
            "--build-dir",
            str(tmp_path / "build"),
            "--output-dir",
            str(tmp_path / "evidence"),
        ]
    )
    report = json.loads((tmp_path / "evidence" / "report.json").read_text(encoding="utf-8"))
    assert code == (3 if changed else 0)
    assert report["candidate_unchanged"] is not changed
    assert report["result"] == ("fail" if changed else "pass")
