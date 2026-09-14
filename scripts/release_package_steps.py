"""Default package、生命周期和 sanitizer 的 release gate 步骤。"""

from __future__ import annotations

import argparse

if __package__:
    from .release_gate_model import GateStep, GateTier
    from .release_tree_model import tree_path
else:
    from release_gate_model import GateStep, GateTier
    from release_tree_model import tree_path


PACKAGE_GATE_SCRIPT = "tests/public_api/package_gate_contracts.py"
PACKAGE_GATE_MANIFEST = "tests/public_api/package_gate_manifest.json"


def shared_package_steps(args: argparse.Namespace) -> list[GateStep]:
    """构造 shared release producer 的 package 合同步骤。"""

    build_dir = tree_path(args, "release-shared")
    return [
        GateStep(
            "package_gate_contract",
            GateTier.FAST,
            [args.python, PACKAGE_GATE_SCRIPT, "validate-contract", PACKAGE_GATE_MANIFEST],
            "validate four-target delivery, lifecycle and sanitizer evidence mapping",
            "release-shared",
            (),
        ),
        GateStep(
            "shared_package_candidate",
            GateTier.FAST,
            [args.python, PACKAGE_GATE_SCRIPT, "validate-candidate", str(build_dir)],
            "reject force-disabled or capability-cropped shared release candidates",
            "release-shared",
            (),
        ),
    ]


def package_evidence_step(
    args: argparse.Namespace,
    *,
    name: str,
    tree_id: str,
    linkage: str,
) -> GateStep:
    """构造绑定单一 OFF producer tree 的安装、消费者和生命周期步骤。"""

    build_dir = tree_path(args, tree_id)
    install_artifact = (
        "shared_install_consumer_report"
        if linkage == "shared"
        else "static_install_consumer_report"
    )
    lifecycle_artifact = (
        "shared_lifecycle_report"
        if linkage == "shared"
        else "static_lifecycle_report"
    )
    return GateStep(
        name,
        GateTier.FULL,
        [
            args.python,
            "scripts/release_package_evidence.py",
            "--build-dir",
            str(build_dir),
            "--linkage",
            linkage,
            "--contract",
            PACKAGE_GATE_MANIFEST,
            "--surface-manifest",
            "tests/public_api/surface_manifest.json",
            "--cmake",
            args.cmake,
            "--jobs",
            str(args.jobs),
            "--install-report",
            str(
                build_dir
                / "evidence"
                / "package"
                / f"{linkage}-install-consumer.json"
            ),
            "--lifecycle-report",
            str(build_dir / "evidence" / "lifecycle" / f"{linkage}.xml"),
        ],
        f"run {linkage} install, consumer and lifecycle evidence from the OFF tree",
        tree_id,
        (install_artifact, lifecycle_artifact),
    )


def sanitizer_steps(args: argparse.Namespace) -> list[GateStep]:
    """构造绑定专用 sanitizer tree 的 package evidence 步骤。"""

    steps: list[GateStep] = []
    for tree_id, profile, artifact_id in (
        ("asan-ubsan-lsan", "asan-ubsan-lsan", "asan_ubsan_lsan_report"),
        ("tsan", "tsan", "tsan_report"),
    ):
        build_dir = tree_path(args, tree_id)
        evidence_root = build_dir / "evidence" / "package-sanitizers" / profile
        name = "package_asan_ubsan_lsan" if profile != "tsan" else "package_tsan"
        steps.append(
            GateStep(
                name,
                GateTier.FULL,
                [
                    args.python,
                    "scripts/run_uce_sanitizers.py",
                    "--profile",
                    profile,
                    "--build-dir",
                    str(build_dir),
                    "--output-dir",
                    str(evidence_root),
                    "--nproc",
                    str(args.jobs),
                ],
                f"run {profile} package evidence from its dedicated producer tree",
                tree_id,
                (artifact_id,),
            )
        )
    return steps
