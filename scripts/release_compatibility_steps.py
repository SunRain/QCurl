"""将首版参考和 patch 兼容验证绑定到既有 shared producer。"""

from __future__ import annotations

import argparse
from pathlib import Path

if __package__:
    from .release_compatibility_policy import historical_references
    from .release_gate_model import GateStep, GateTier
    from .release_tree_model import tree_path
else:
    from release_compatibility_policy import historical_references
    from release_gate_model import GateStep, GateTier
    from release_tree_model import tree_path


def compatibility_steps(args: argparse.Namespace) -> list[GateStep]:
    """构造 required 的参考捕获、ABI/旧二进制及旧源码验证。"""

    if args.abi_mode == "none":
        return []
    build = tree_path(args, "release-shared")
    root = build / "evidence" / "compatibility"
    stage = build / "evidence" / "package-stage" / "shared"
    command = [args.python, "scripts/qcurl_compatibility.py"]
    producer = ["--build-dir", str(build), "--stage-prefix", str(stage)]
    steps = [
        GateStep(
            "compatibility_reference_capture",
            GateTier.FULL,
            [*command, "capture", *producer, "--output-dir", str(root / "reference")],
            "capture both libraries and four compiled consumers; validate first-release references",
            "release-shared",
            (
                "compatibility_reference",
                "compatibility_reference_validation",
                "abi_core_snapshot",
                "abi_other_extras_snapshot",
            ),
        ),
        GateStep(
            "compatibility_reference_roundtrip",
            GateTier.FULL,
            [
                *command,
                "check",
                "--reference",
                str(root / "reference"),
                *producer,
                "--output-dir",
                str(root / "roundtrip"),
            ],
            "validate retained binaries and both ABI snapshots against the identical candidate; not a patch upgrade",
            "release-shared",
            _comparison_ids("roundtrip"),
        ),
    ]
    for role, reference, _ in historical_references(args, Path(args.source_root)):
        steps.extend(_reference_steps(role, reference, command, producer, root))
    return steps


def _comparison_ids(role: str) -> tuple[str, ...]:
    return tuple(
        f"compatibility_{role}_{name}"
        for name in (
            "report",
            "core_full",
            "core_residual",
            "core_comparison",
            "other_extras_full",
            "other_extras_residual",
            "other_extras_comparison",
        )
    )


def _reference_steps(
    role: str,
    reference: Path,
    command: list[str],
    producer: list[str],
    root: Path,
) -> list[GateStep]:
    inputs = ["--reference", str(reference), *producer]
    return [
        GateStep(
            f"compatibility_{role}_check",
            GateTier.FULL,
            [
                *command,
                "check",
                *inputs,
                "--require-history",
                "--output-dir",
                str(root / f"check-{role}"),
            ],
            f"compare both ABIs and run unchanged {role} release binaries against candidate libraries",
            "release-shared",
            _comparison_ids(role),
        ),
        GateStep(
            f"compatibility_{role}_source",
            GateTier.FULL,
            [
                *command,
                "rebuild-source",
                *inputs,
                "--require-history",
                "--output-dir",
                str(root / f"source-{role}"),
            ],
            f"rebuild and execute the {role} release's four installed source consumers",
            "release-shared",
            (f"compatibility_{role}_source",),
        ),
    ]
