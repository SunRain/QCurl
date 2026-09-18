"""Build and run installed-only consumers with the producer compiler profile."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys
from typing import Callable

RunCommand = Callable[..., subprocess.CompletedProcess[str]]


def configure_consumer(
    source_dir: Path,
    build_dir: Path,
    stage_dir: Path,
    cmake: str,
    consumer_cache: Path,
    run_command: RunCommand,
) -> None:
    """Configure an installed consumer with an explicit producer initial cache."""

    if not consumer_cache.is_file():
        raise RuntimeError(f"missing producer consumer cache: {consumer_cache}")
    run_command(
        [
            cmake,
            "-S",
            str(source_dir),
            "-B",
            str(build_dir),
            "-C",
            str(consumer_cache),
            f"-DQCURL_STAGE_PREFIX={stage_dir}",
        ]
    )


def build_consumer(
    build_dir: Path,
    cmake: str,
    config: str,
    run_command: RunCommand,
    *,
    expect_success: bool = True,
) -> subprocess.CompletedProcess[str]:
    """Rebuild every consumer translation unit and preserve verbose command evidence."""

    command = [cmake, "--build", str(build_dir), "--clean-first", "--verbose"]
    if config:
        command.extend(["--config", config])
    return run_command(command, expect_success=expect_success)


def configure_and_build(
    source_dir: Path,
    build_dir: Path,
    stage_dir: Path,
    cmake: str,
    config: str,
    run_command: RunCommand,
    consumer_cache: Path,
) -> None:
    """Configure and build a consumer without removing its existing build directory."""

    configure_consumer(
        source_dir, build_dir, stage_dir, cmake, consumer_cache, run_command
    )
    build_consumer(build_dir, cmake, config, run_command)


def run_consumer(
    build_dir: Path, executable_name: str, config: str, run_command: RunCommand
) -> None:
    """Execute the built fixture and record sanitizer runtime settings without overrides."""

    executable = build_dir / executable_name
    if sys.platform == "win32":
        executable = executable.with_suffix(".exe")
    if config and (build_dir / config / executable.name).is_file():
        executable = build_dir / config / executable.name
    for variable in ("ASAN_OPTIONS", "UBSAN_OPTIONS", "LSAN_OPTIONS"):
        print(
            f"[public_api] {variable}={os.environ.get(variable, '<unset>')}", flush=True
        )
    run_command([str(executable)])
