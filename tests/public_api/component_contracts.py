"""Reusable install/export consumer contracts for opt-in QCurl components."""

from __future__ import annotations

from argparse import Namespace
from pathlib import Path
from typing import Callable
import shutil
import subprocess

from tests.public_api.consumer_contracts import configure_and_build


RunCommand = Callable[..., subprocess.CompletedProcess[str]]
FailFunc = Callable[[str], int]


def read_manifest(path: Path) -> list[str]:
    """Read a one-header-per-line component manifest."""

    return [line.strip() for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]


def check_opt_in_headers(
    *,
    manifest: Path,
    default_stage_dir: Path,
    component_stage_dir: Path,
    component_label: str,
    fail_func: FailFunc,
) -> int:
    """Verify component headers are absent from default Core and present in opt-in stage."""

    default_include_dir = default_stage_dir / "include" / "qcurl"
    component_include_dir = component_stage_dir / "include" / "qcurl"

    errors: list[str] = []
    for header in read_manifest(manifest):
        if (default_include_dir / header).exists():
            errors.append(f"{header}: leaked into default Core stage")
        if not (component_include_dir / header).exists():
            errors.append(f"{header}: missing from {component_label} stage")

    if errors:
        return fail_func("\n".join(errors))

    print(f"[public_api] {component_label} install contract passed")
    return 0


def build_default_core_negative_consumer(
    *,
    cmake: str,
    default_stage_dir: Path,
    negative_source_dir: Path,
    negative_build_dir: Path,
    config: str,
    component_label: str,
    run_command: RunCommand,
    fail_func: FailFunc,
) -> int | None:
    """Verify a negative consumer cannot build against the default Core stage."""

    if negative_build_dir.exists():
        shutil.rmtree(negative_build_dir)

    configure = [
        cmake,
        "-S",
        str(negative_source_dir),
        "-B",
        str(negative_build_dir),
        f"-DQCURL_STAGE_PREFIX={default_stage_dir}",
    ]
    try:
        run_command(configure)
    except RuntimeError as exc:
        return fail_func(f"{component_label} negative consumer configure failed unexpectedly: {exc}")

    build = [cmake, "--build", str(negative_build_dir)]
    if config:
        build.extend(["--config", config])

    proc = run_command(build, expect_success=False)
    if proc.returncode == 0:
        return fail_func(f"{component_label} negative consumer unexpectedly built successfully")
    return None


def run_opt_in_consumer_smoke(
    *,
    cmake: str,
    component_stage_dir: Path,
    default_stage_dir: Path,
    positive_source_dir: Path,
    positive_build_dir: Path,
    negative_source_dir: Path,
    negative_build_dir: Path,
    config: str,
    component_label: str,
    run_command: RunCommand,
    fail_func: FailFunc,
) -> int:
    """Verify opt-in positive consumer and default Core negative consumer."""

    try:
        configure_and_build(
            positive_source_dir,
            positive_build_dir,
            component_stage_dir,
            cmake,
            config,
            run_command,
        )
    except RuntimeError as exc:
        return fail_func(f"{component_label} positive consumer smoke failed: {exc}")

    negative_error = build_default_core_negative_consumer(
        cmake=cmake,
        default_stage_dir=default_stage_dir,
        negative_source_dir=negative_source_dir,
        negative_build_dir=negative_build_dir,
        config=config,
        component_label=component_label,
        run_command=run_command,
        fail_func=fail_func,
    )
    if negative_error is not None:
        return negative_error

    print(f"[public_api] {component_label} consumer smoke passed")
    return 0
