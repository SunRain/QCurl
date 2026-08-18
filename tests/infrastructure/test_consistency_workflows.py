from __future__ import annotations

from pathlib import Path

import pytest
import yaml


_WORKFLOW_JOBS = (
    ("basic_no_problem_gate.yml", "basic_no_problem", "build", "scripts/run_basic_no_problem_gate.py"),
    ("libcurl_consistency_ext_gate.yml", "libcurl_consistency_gate", "build", "tests/libcurl_consistency/run_gate.py"),
    ("release_delivery_http3_gate.yml", "release_debian12", "build", "tests/libcurl_consistency/run_gate.py"),
    ("release_delivery_http3_gate.yml", "release_arch_snapshot", "build", "tests/libcurl_consistency/run_gate.py"),
    ("pr_fast_gate.yml", "pr_fast_gate", "build", "scripts/run_uce_gate.py"),
    ("uce_nightly.yml", "uce_nightly", "build-nightly", "scripts/run_uce_gate.py"),
    ("uce_soak.yml", "uce_soak", "build-soak", "scripts/run_uce_gate.py"),
)


def _job(workflow_name: str, job_name: str) -> dict[str, object]:
    workflow_path = Path(".github/workflows") / workflow_name
    workflow = yaml.safe_load(workflow_path.read_text(encoding="utf-8"))
    return workflow["jobs"][job_name]


def _run_blocks(job: dict[str, object]) -> list[str]:
    return [str(step["run"]) for step in job["steps"] if "run" in step]


@pytest.mark.parametrize(
    ("workflow_name", "job_name", "build_dir", "gate_entry"),
    _WORKFLOW_JOBS,
)
def test_consistency_workflow_has_complete_evidence_route(
    workflow_name: str,
    job_name: str,
    build_dir: str,
    gate_entry: str,
) -> None:
    job = _job(workflow_name, job_name)
    checkout = next(step for step in job["steps"] if step.get("uses") == "actions/checkout@v4")
    run_blocks = _run_blocks(job)

    assert checkout["with"]["submodules"] == "recursive"
    assert any("tests/libcurl_consistency/requirements.lock.txt" in block for block in run_blocks)
    assert any(
        "cmake -S ." in block and "-DQCURL_BUILD_LIBCURL_CONSISTENCY=ON" in block
        for block in run_blocks
    )
    assert any(
        f"ctest --test-dir {build_dir} -R '^qcurl_libcurl_consistency_infrastructure$'"
        in block
        for block in run_blocks
    )
    assert any(gate_entry in block for block in run_blocks)


def test_consistency_workflow_wrappers_delegate_to_supported_gate() -> None:
    basic_runner = Path("scripts/run_basic_no_problem_gate.py").read_text(encoding="utf-8")
    uce_runner = Path("scripts/uce_gate/execute.py").read_text(encoding="utf-8")

    assert '"libcurl_consistency" / "run_gate.py"' in basic_runner
    assert '"libcurl_consistency" / "run_gate.py"' in uce_runner
