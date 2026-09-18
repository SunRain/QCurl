from __future__ import annotations

from argparse import Namespace
from pathlib import Path

import pytest

from scripts import run_release_gate
from scripts.release_gate_execution import prepare_artifact_parents
from scripts.release_gate_model import GateStep
from scripts.release_tree_model import tree_path, tree_registry
from scripts.uce_gate.evidence import EvidenceLayout
from scripts.uce_gate.evidence import prepare_evidence_layout, resolve_evidence_layout
from scripts.uce_gate.orchestrator import run_uce_gate


@pytest.fixture
def full_release_args(tmp_path: Path) -> Namespace:
    """构造真实 full/final 步骤，隔离每棵 producer 的输出目录。"""
    argv = ["--tier", "full", "--stage", "final", "--abi-mode", "none"]
    for tree_id in (
        "release-shared",
        "release-static",
        "test-shared-gcc",
        "test-shared-clang",
        "asan-ubsan-lsan",
    ):
        argv.extend([f"--{tree_id}-build-dir", str(tmp_path / tree_id)])
    return run_release_gate.build_parser().parse_args(argv)


def _step(args: Namespace, name: str) -> GateStep:
    return next(step for step in run_release_gate._selected_steps(args) if step.name == name)


def _uce_layout(args: Namespace, repo_root: Path) -> EvidenceLayout:
    return resolve_evidence_layout(
        repo_root,
        tree_path(args, "test-shared-gcc"),
        run_id="release-gate",
        evidence_root_arg="",
    )


@pytest.mark.parametrize("existing_evidence_root", [False, True])
def test_release_runner_allows_uce_to_create_fresh_run(
    tmp_path: Path, full_release_args: Namespace, existing_evidence_root: bool
) -> None:
    layout = _uce_layout(full_release_args, tmp_path)
    if existing_evidence_root:
        layout.evidence_root.mkdir(parents=True)

    prepare_artifact_parents(
        _step(full_release_args, "uce_evidence"), tree_registry(full_release_args)
    )
    prepare_evidence_layout(layout)

    assert layout.evidence_dir.is_dir()
    assert layout.logs_dir.is_dir()
    assert layout.meta_dir.is_dir()
    assert layout.reports_dir.is_dir()
    assert layout.netproof_dir.is_dir()
    assert layout.lc_dir.is_dir()
    assert not layout.manifest_path.exists()


@pytest.mark.parametrize("collision", ["empty-directory", "directory", "archive", "envelope"])
def test_release_runner_preserves_uce_existing_evidence_rejection(
    tmp_path: Path, full_release_args: Namespace, collision: str
) -> None:
    layout = _uce_layout(full_release_args, tmp_path)
    marker = None
    if collision in {"empty-directory", "directory"}:
        layout.evidence_dir.mkdir(parents=True)
        if collision == "directory":
            marker = layout.evidence_dir / "retained.log"
    else:
        layout.evidence_root.mkdir(parents=True)
        marker = layout.tar_path if collision == "archive" else layout.archive_envelope_path
    if marker is not None:
        marker.write_bytes(b"previous evidence must remain unchanged\n")
    original_paths = set(layout.evidence_root.rglob("*"))

    prepare_artifact_parents(
        _step(full_release_args, "uce_evidence"), tree_registry(full_release_args)
    )
    result = run_uce_gate(
        repo_root=tmp_path,
        build_dir=tree_path(full_release_args, "test-shared-gcc"),
        tier="nightly",
        run_id="release-gate",
        evidence_root_arg="",
    )

    assert result == 3
    assert set(layout.evidence_root.rglob("*")) == original_paths
    if marker is not None:
        assert marker.read_bytes() == b"previous evidence must remain unchanged\n"


@pytest.mark.parametrize(
    ("gate_id", "tree_id", "report_name"),
    [
        ("full_ctest", "test-shared-gcc", "full-ctest.xml"),
        ("clang_ctest", "test-shared-clang", "clang-ctest.xml"),
    ],
)
def test_release_runner_still_prepares_ctest_report_parents(
    full_release_args: Namespace, gate_id: str, tree_id: str, report_name: str
) -> None:
    report = tree_path(full_release_args, tree_id) / "evidence" / report_name

    prepare_artifact_parents(
        _step(full_release_args, gate_id), tree_registry(full_release_args)
    )

    assert report.parent.is_dir()
    assert not report.exists()
