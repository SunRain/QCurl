from __future__ import annotations

from pathlib import Path

from scripts.run_uce_sanitizers import cmake_configure_command
from scripts.run_uce_sanitizers import sanitizer_profiles
from scripts.run_uce_sanitizers import tsan_subject_regex


def test_cmake_configure_command_enables_consistency_for_asan(tmp_path: Path) -> None:
    source_dir = tmp_path / "src"
    build_dir = tmp_path / "build"
    profile = sanitizer_profiles()["asan-ubsan-lsan"]

    command = cmake_configure_command(source_dir, build_dir, profile)

    assert f"-DQCURL_BUILD_LIBCURL_CONSISTENCY=ON" in command
    assert any(item.startswith("-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined,leak") for item in command)


def test_tsan_subject_regex_is_stable() -> None:
    assert tsan_subject_regex() == "^(tst_QCNetworkReply|tst_QCNetworkScheduler|tst_QCNetworkConnectionPool)$"
