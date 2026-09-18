"""Regression coverage for explicit installed-consumer compiler profiles."""

from __future__ import annotations

from argparse import Namespace
from pathlib import Path
import subprocess

import pytest

from tests.public_api import run_public_api_checks as checks
from tests.public_api.consumer_build import configure_and_build, run_consumer

PUBLIC_API = Path(__file__).resolve().parent


@pytest.fixture
def consumer_cache(tmp_path: Path) -> Path:
    """Generate a real CMake initial cache from producer variables."""

    producer = tmp_path / "producer.cmake"
    producer.write_text(
        'set(CMAKE_CXX_COMPILER "/toolchain/compiler with spaces")\n'
        "set(CMAKE_BUILD_TYPE Debug)\n"
        'set(CMAKE_CXX_FLAGS "-fsanitize=address,undefined,leak -fno-omit-frame-pointer")\n'
        'set(CMAKE_EXE_LINKER_FLAGS "-fsanitize=address,undefined,leak")\n'
        'set(CMAKE_CXX_FLAGS_DEBUG "-g -DQUOTED=\\"a b\\"")\n'
        'set(CMAKE_EXE_LINKER_FLAGS_DEBUG "-Wl,--as-needed")\n'
        f'include("{PUBLIC_API / "consumer_profile.cmake"}")\n',
        encoding="utf-8",
    )
    subprocess.run(
        ["cmake", "-P", str(producer)], cwd=tmp_path, check=True, capture_output=True
    )
    return tmp_path / "generated/consumer-profile.cmake"


def test_producer_profile_preserves_compiler_compile_and_executable_link_flags(
    tmp_path: Path, consumer_cache: Path
) -> None:
    """CMake consumes the producer profile unchanged, including config-specific flags."""

    probe = tmp_path / "probe.cmake"
    probe.write_text(
        'if(NOT CMAKE_CXX_COMPILER STREQUAL "/toolchain/compiler with spaces")\n'
        '  message(FATAL_ERROR "compiler was not propagated")\n'
        "endif()\n"
        'if(NOT CMAKE_CXX_FLAGS STREQUAL "-fsanitize=address,undefined,leak -fno-omit-frame-pointer")\n'
        '  message(FATAL_ERROR "compile instrumentation missing")\n'
        "endif()\n"
        'if(NOT CMAKE_EXE_LINKER_FLAGS STREQUAL "-fsanitize=address,undefined,leak")\n'
        '  message(FATAL_ERROR "executable runtime missing")\n'
        "endif()\n"
        'if(NOT CMAKE_CXX_FLAGS_DEBUG STREQUAL "-g -DQUOTED=\\"a b\\"")\n'
        '  message(FATAL_ERROR "config compile flags changed")\n'
        "endif()\n"
        'if(NOT CMAKE_EXE_LINKER_FLAGS_DEBUG STREQUAL "-Wl,--as-needed")\n'
        '  message(FATAL_ERROR "config linker flags changed")\n'
        "endif()\n",
        encoding="utf-8",
    )
    subprocess.run(
        ["cmake", "-C", str(consumer_cache), "-P", str(probe)],
        cwd=tmp_path,
        check=True,
        capture_output=True,
    )


def _consumer_args(tmp_path: Path, consumer_cache: Path, fixture: str) -> Namespace:
    return Namespace(
        cmake="cmake",
        config="Debug",
        consumer_cache=consumer_cache,
        stage_dir=tmp_path / "package-stage",
        default_stage_dir=tmp_path / "core-stage",
        blocking_stage_dir=tmp_path / "package-stage",
        test_support_stage_dir=tmp_path / "package-stage",
        other_extras_stage_dir=tmp_path / "package-stage",
        positive_source_dir=PUBLIC_API / fixture,
        positive_build_dir=tmp_path / "positive",
        negative_source_dir=tmp_path / "negative-source",
        negative_build_dir=tmp_path / "negative",
        source_dir=PUBLIC_API / fixture,
        build_dir=tmp_path / "positive",
    )


@pytest.mark.parametrize(
    ("function", "fixture", "executable", "configure_count"),
    [
        (checks.consumer_smoke, "consumer_smoke", "qcurl_public_api_consumer_smoke", 2),
        (
            checks.metatype_consumer_smoke,
            "consumer_metatype_smoke",
            "qcurl_public_api_consumer_metatype_smoke",
            1,
        ),
        (
            checks.blocking_extras_consumer_smoke,
            "consumer_blocking_extras_smoke",
            "qcurl_public_api_consumer_blocking_extras_smoke",
            2,
        ),
        (
            checks.test_support_consumer_smoke,
            "consumer_test_support_smoke",
            "qcurl_public_api_consumer_test_support_smoke",
            2,
        ),
        (
            checks.other_extras_consumer_smoke,
            "consumer_other_extras_smoke",
            "qcurl_public_api_consumer_other_extras_smoke",
            2,
        ),
        (
            checks.hard_break_negative_consumer,
            "consumer_magic_hard_break_negative_diagnostics_defaults",
            None,
            1,
        ),
    ],
)
def test_every_consumer_uses_producer_cache_and_positive_fixtures_run(
    tmp_path: Path,
    consumer_cache: Path,
    monkeypatch,
    function,
    fixture,
    executable,
    configure_count,
) -> None:
    """Core, component and hard-break consumers share one explicit build profile."""

    commands = []

    def record(command, *, expect_success=True):
        commands.append(command)
        return subprocess.CompletedProcess(command, 0 if expect_success else 1, "", "")

    monkeypatch.setattr(checks, "run", record)
    args = _consumer_args(tmp_path, consumer_cache, fixture)
    assert function(args) == 0
    configure = [command for command in commands if "-S" in command]
    assert len(configure) == configure_count
    assert all(
        command[command.index("-C") + 1] == str(consumer_cache) for command in configure
    )
    builds = [command for command in commands if "--build" in command]
    assert len(builds) == configure_count
    assert all(
        "--verbose" in command and "--clean-first" in command for command in builds
    )
    executions = [command for command in commands if len(command) == 1]
    assert executions == (
        [[str(tmp_path / "positive" / executable)]] if executable else []
    )


@pytest.mark.parametrize("failure_phase", ["build", "run"])
def test_missing_sanitizer_runtime_cannot_report_consumer_success(
    tmp_path: Path, consumer_cache: Path, monkeypatch, capsys, failure_phase: str
) -> None:
    """A runtime link/load failure stops before the consumer success message."""

    def fail_runtime(command, *, expect_success=True):
        if (failure_phase == "build" and "--build" in command) or (
            failure_phase == "run" and len(command) == 1
        ):
            raise RuntimeError("undefined reference to __asan_report_load8")
        return subprocess.CompletedProcess(command, 0, "", "")

    monkeypatch.setattr(checks, "run", fail_runtime)
    args = _consumer_args(tmp_path, consumer_cache, "consumer_smoke")
    assert checks.consumer_smoke(args) == 1
    captured = capsys.readouterr()
    assert "__asan_report_load8" in captured.err
    assert "consumer smoke passed" not in captured.out


def test_missing_producer_cache_fails_before_configure(tmp_path: Path) -> None:
    """Missing instrumentation input cannot silently use ambient compiler settings."""

    with pytest.raises(RuntimeError, match="missing producer consumer cache"):
        configure_and_build(
            tmp_path,
            tmp_path / "build",
            tmp_path,
            "cmake",
            "",
            None,
            tmp_path / "missing",
        )


def test_runtime_environment_is_recorded_without_disabling_leak_detection(
    tmp_path: Path, monkeypatch, capsys
) -> None:
    """Consumer execution inherits and exposes active ASan/LSan settings."""

    monkeypatch.setenv("ASAN_OPTIONS", "detect_leaks=1:halt_on_error=1")
    monkeypatch.setenv("LSAN_OPTIONS", "exitcode=23")
    monkeypatch.setenv("UBSAN_OPTIONS", "halt_on_error=1")
    run_consumer(tmp_path, "consumer", "", lambda command: None)
    output = capsys.readouterr().out
    assert "ASAN_OPTIONS=detect_leaks=1:halt_on_error=1" in output
    assert "LSAN_OPTIONS=exitcode=23" in output
    assert "UBSAN_OPTIONS=halt_on_error=1" in output


def test_successful_build_commands_and_output_are_retained(monkeypatch, capsys) -> None:
    """CTest logs must contain actual compiler/linker lines even after success."""

    monkeypatch.setattr(
        checks.subprocess,
        "run",
        lambda *args, **kwargs: subprocess.CompletedProcess(
            args[0], 0, "clang++ -fsanitize=address,undefined,leak -o consumer\n", ""
        ),
    )
    checks.run(["cmake", "--build", "consumer build", "--verbose"])
    output = capsys.readouterr().out
    assert "cmake --build 'consumer build' --verbose" in output
    assert "clang++ -fsanitize=address,undefined,leak -o consumer" in output
    assert "exit code: 0" in output
