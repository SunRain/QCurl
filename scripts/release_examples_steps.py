"""构造示例与基准程序的 release gate 步骤。"""

from __future__ import annotations

import argparse
from pathlib import Path

if __package__:
    from .release_gate_model import GateStep, GateTier
else:
    from release_gate_model import GateStep, GateTier


def examples_benchmarks_steps(
    args: argparse.Namespace,
    test_build_dir: Path,
) -> list[GateStep]:
    """返回独立配置并构建 examples/benchmarks 的两个直接命令。"""

    gate_build_dir = test_build_dir / "examples_benchmarks_gate"
    return [
        GateStep(
            "examples_benchmarks_configure",
            GateTier.STRICT,
            [
                args.cmake,
                "-S",
                ".",
                "-B",
                str(gate_build_dir),
                "-DCMAKE_BUILD_TYPE=Release",
                "-DBUILD_TESTING=OFF",
                "-DBUILD_EXAMPLES=ON",
                "-DBUILD_BENCHMARKS=ON",
            ],
            "configure an isolated examples and benchmarks build tree",
            "test-shared-gcc",
            (),
        ),
        GateStep(
            "examples_benchmarks_build",
            GateTier.STRICT,
            [
                args.cmake,
                "--build",
                str(gate_build_dir),
                "--parallel",
                str(args.jobs),
            ],
            "verify all examples and benchmarks build successfully",
            "test-shared-gcc",
            (),
        ),
    ]
