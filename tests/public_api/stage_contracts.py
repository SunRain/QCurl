"""Staging/build checks for QCurl public API gates."""

from __future__ import annotations

from pathlib import Path
from typing import Callable
import shutil
import subprocess


RunCommand = Callable[..., subprocess.CompletedProcess[str]]
FailFunc = Callable[[str], int]


def read_manifest(path: Path) -> list[str]:
    """Read a one-header-per-line manifest file."""

    return [line.strip() for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]


def install_stage(args, *, run_command: RunCommand, fail_func: FailFunc) -> int:
    """Install the current build into a clean staging prefix."""

    if args.stage_dir.exists():
        shutil.rmtree(args.stage_dir)
    args.stage_dir.mkdir(parents=True, exist_ok=True)

    components = args.components or ["Development", "Runtime", "BundledRuntime"]
    try:
        for component in components:
            command = [
                args.cmake,
                "--install",
                str(args.build_dir),
                "--prefix",
                str(args.stage_dir),
                "--component",
                component,
            ]
            if args.config:
                command.extend(["--config", args.config])
            run_command(command)
    except RuntimeError as exc:
        return fail_func(str(exc))

    print(f"[public_api] staged install at {args.stage_dir}")
    return 0


def build_target(args, *, run_command: RunCommand, fail_func: FailFunc) -> int:
    """Build a single target in the configured build tree."""

    command = [args.cmake, "--build", str(args.build_dir), "--target", args.target]
    if args.config:
        command.extend(["--config", args.config])

    try:
        run_command(command)
    except RuntimeError as exc:
        return fail_func(str(exc))

    print(f"[public_api] built target {args.target}")
    return 0


def check_installed_headers(args, *, fail_func: FailFunc) -> int:
    """Verify staged public headers exactly match the manifest plus QCurlConfig.h."""

    include_dir = args.stage_dir / "include" / "qcurl"
    if not include_dir.is_dir():
        return fail_func(f"missing include directory: {include_dir}")

    expected = set(read_manifest(args.manifest))
    expected.add(Path(args.generated_header).name)
    actual = {path.name for path in include_dir.iterdir() if path.is_file()}

    missing = sorted(expected - actual)
    extra = sorted(actual - expected)
    if missing or extra:
        details = []
        if missing:
            details.append(f"missing={missing}")
        if extra:
            details.append(f"extra={extra}")
        return fail_func("installed header set mismatch: " + ", ".join(details))

    print("[public_api] installed header set matches manifest")
    return 0
