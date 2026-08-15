from __future__ import annotations

from pathlib import Path

from scripts.run_uce_sanitizers import cmake_configure_command
from scripts.run_uce_sanitizers import sanitizer_build_command
from scripts.run_uce_sanitizers import sanitizer_profiles
from scripts.run_uce_sanitizers import sanitizer_subject_environment
from scripts.run_uce_sanitizers import parse_qt_test_functions
from scripts.run_uce_sanitizers import tsan_scheduler_commands
from scripts.run_uce_sanitizers import tsan_subject_regex
from scripts.run_uce_sanitizers import websocket_enabled


def test_cmake_configure_command_enables_consistency_for_asan(tmp_path: Path) -> None:
    source_dir = tmp_path / "src"
    build_dir = tmp_path / "build"
    profile = sanitizer_profiles()["asan-ubsan-lsan"]

    command = cmake_configure_command(source_dir, build_dir, profile)

    assert f"-DQCURL_BUILD_LIBCURL_CONSISTENCY=ON" in command
    assert any(item.startswith("-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined,leak") for item in command)


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
    assert all(not option.startswith("halt_on_error=") for option in options)


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
    suppression_path = (
        Path(__file__).resolve().parents[1] / "tests" / "qcurl" / "qt_test_tsan.supp"
    )
    rules = [
        line.strip()
        for line in suppression_path.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]

    assert rules == [
        "race:QPlainTestLogger::stopLogging",
        "race:QAbstractTestLogger::outputString",
    ]
