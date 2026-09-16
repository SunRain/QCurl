from __future__ import annotations

from fnmatch import fnmatchcase
from pathlib import Path
import shlex

import pytest
import yaml


_WORKFLOW_JOBS = (
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
    """验证 UCE runner 委派给 libcurl_consistency provider。

    注：basic_no_problem_gate 已进入由 UCE nightly 承接的条件性 hard-breaking 迁移；
    当前候选 fresh acceptance 证据闭合前，不把删除视为已完成。
    """
    uce_runner = Path("scripts/uce_gate/execute.py").read_text(encoding="utf-8")

    assert '"libcurl_consistency" / "run_gate.py"' in uce_runner


@pytest.mark.parametrize(("workflow_name", "job_name", "build_dir", "gate_entry"), _WORKFLOW_JOBS[3:])
def test_uce_upload_always_follows_fail_closed_archive_validation(
    workflow_name: str, job_name: str, build_dir: str, gate_entry: str,
) -> None:
    """预检与上传均始终执行；诊断上传不能隐藏任何必需工件或 gate 失败。"""

    steps = _job(workflow_name, job_name)["steps"]
    checks = [step for step in steps if "scripts/verify_uce_archive.py" in step.get("run", "")]
    assert len(checks) == 1
    check = checks[0]
    assert check.get("if") == "always()"
    assert not check.get("continue-on-error", False)
    command = shlex.split(check["run"])
    assert command[command.index("--evidence-root") + 1] == f"{build_dir}/evidence/uce"
    assert command[command.index("--run-id") + 1] == "${{ env.UCE_RUN_ID }}"
    assert "--require-pass" in command
    upload = next(step for step in steps if step.get("uses") == "actions/upload-artifact@v4"
                  and "manifest.json" in step["with"]["path"])
    assert upload.get("if") == "always()"
    assert not upload.get("continue-on-error", False)
    assert upload["with"]["if-no-files-found"] == "error"
    root = f"{build_dir}/evidence/uce/${{{{ env.UCE_RUN_ID }}}}"
    assert set(upload["with"]["path"].splitlines()) == {
        f"{root}/manifest.json", f"{root}/policy_violations.json",
        f"{root}.tar.gz", f"{root}.archive-envelope.json",
    }
    gate = next(step for step in steps if gate_entry in step.get("run", ""))
    assert steps.index(gate) < steps.index(check) < steps.index(upload)


def test_nightly_preserves_full_acceptance_trigger_scope() -> None:
    """旧 acceptance 的分支、手动触发和路径范围均由 nightly 承接。"""

    workflow = yaml.load(
        Path(".github/workflows/uce_nightly.yml").read_text(encoding="utf-8"), Loader=yaml.BaseLoader,
    )
    triggers = workflow["on"]
    assert "workflow_dispatch" in triggers
    assert set(triggers["push"]["branches"]) == {"master", "main", "develop"}
    paths = triggers["push"]["paths"]
    assert {"CMakeLists.txt", "src/**", "tests/**", "scripts/**", "docs/**"} <= set(paths)
    assert not any(".helloagents" in pattern for pattern in paths)
    for workflow_name in ("basic_no_problem_gate.yml", "uce_nightly.yml", "uce_soak.yml", "pr_fast_gate.yml"):
        assert any(fnmatchcase(f".github/workflows/{workflow_name}", pattern) for pattern in paths)


@pytest.mark.parametrize("event", ("push", "pull_request"))
def test_policy_dictionary_workflow_tracks_versioned_inputs(event: str) -> None:
    """字典的正式文件变更必须触发校验，本地方案路径不参与 CI。"""
    workflow = yaml.load(
        Path(".github/workflows/policy_violations_dictionary.yml").read_text(encoding="utf-8"),
        Loader=yaml.BaseLoader,
    )
    paths = workflow["on"][event]["paths"]
    for suffix in ("json", "md"):
        relative = f"docs/dev/uce/policy_violations_dictionary.{suffix}"
        assert any(fnmatchcase(relative, pattern) for pattern in paths)
    assert not any(".helloagents" in pattern for pattern in paths)
