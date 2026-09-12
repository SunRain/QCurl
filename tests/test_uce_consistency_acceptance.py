from __future__ import annotations

import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET

import pytest

from scripts.uce_gate import execute
from scripts.uce_gate import runtime
from scripts.uce_gate.evidence import create_gate_manifest
from scripts.uce_gate.evidence import prepare_evidence_layout
from scripts.uce_gate.evidence import resolve_evidence_layout
from scripts.uce_gate.evidence import write_manifest_and_policy_report
from scripts.uce_gate.finalize import write_validated_state
from scripts.uce_gate.planner import build_tier_plan
from tests.libcurl_consistency.pytest_support.gate_report import parse_junit_counts
from tests.libcurl_consistency.pytest_support.gate_report import policy_violations_from_report


def _write_provider_reports(command: list[str], returncode: int, omitted: str) -> Path:
    assert command[command.index("--suite") + 1] == "p0"
    assert "--build" in command
    run_id = command[command.index("--run-id") + 1]
    run_dir = Path(command[command.index("--reports-dir") + 1]) / "runs" / run_id
    run_dir.mkdir(parents=True)
    if omitted != "p0_gate_report":
        (run_dir / "gate_p0.json").write_text(
            json.dumps({"run_id": run_id, "gate_returncode": returncode}), encoding="utf-8"
        )
    if omitted != "p0_junit_report":
        root = ET.Element("testsuite", tests="1", failures="0", errors="0", skipped="0")
        ET.SubElement(root, "testcase", classname="acceptance", name="provider_probe")
        ET.ElementTree(root).write(run_dir / "junit_p0.xml", encoding="utf-8")
    return run_dir


def _run_p0_boundary(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    *,
    provider_returncode: int = 0,
    omitted: str = "",
) -> tuple[dict[str, object], list[runtime.GateResult], list[str]]:
    repo_root = Path(__file__).resolve().parents[1]
    layout = resolve_evidence_layout(
        repo_root, tmp_path / "build", run_id="p0-boundary", evidence_root_arg=str(tmp_path / "evidence")
    )
    prepare_evidence_layout(layout)
    manifest = create_gate_manifest(
        repo_root=repo_root,
        build_dir=tmp_path / "build",
        layout=layout,
        tier="pr",
        run_id="p0-boundary",
        environment={},
    )
    (layout.meta_dir / "versions.txt").write_text("fixture versions\n", encoding="utf-8")
    write_manifest_and_policy_report(layout=layout, manifest=manifest, tier="pr")

    def capture(command: list[str], **_: object) -> subprocess.CompletedProcess[str]:
        _write_provider_reports(command, provider_returncode, omitted)
        return subprocess.CompletedProcess(command, provider_returncode, stdout="provider fixture\n")

    monkeypatch.setattr(runtime, "run_capture", capture)
    plan = [step for step in build_tier_plan("pr") if step.gate_id == "libcurl_consistency_p0"]
    results, _ = execute.run_libcurl_consistency_gates(
        repo_root, tmp_path / "build", layout.evidence_dir, manifest, plan, {}, uce_run_id="p0-boundary"
    )
    missing = write_validated_state(layout, manifest, "pr")
    return manifest, results, missing


@pytest.mark.parametrize("returncode", [1, 3, 5])
def test_p0_provider_nonzero_is_promoted_to_uce_failure(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, returncode: int
) -> None:
    manifest, results, missing = _run_p0_boundary(
        tmp_path, monkeypatch, provider_returncode=returncode
    )

    assert missing == []
    assert results[0].returncode == returncode
    assert manifest["policy_violations"] == ["gate_libcurl_consistency_p0_failed"]
    assert manifest["contracts"]["libcurl_consistency_p0@v1"]["result"] == "fail"
    assert manifest["result"] == "fail"


@pytest.mark.parametrize("omitted", ["p0_gate_report", "p0_junit_report"])
def test_p0_success_without_each_required_report_is_rejected(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, omitted: str
) -> None:
    manifest, results, missing = _run_p0_boundary(tmp_path, monkeypatch, omitted=omitted)

    assert results[0].returncode == 0
    assert missing == [omitted]
    assert manifest["artifacts"][omitted]["required"] is True
    assert manifest["policy_violations"] == ["artifact_required_missing"]
    assert manifest["result"] == "fail"


def test_p0_success_requires_the_current_run_reports(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    manifest, results, missing = _run_p0_boundary(tmp_path, monkeypatch)

    assert results[0].returncode == 0
    assert missing == []
    assert manifest["policy_violations"] == []
    assert manifest["result"] == "pass"
    for artifact_id in ("p0_gate_report", "p0_junit_report"):
        path = manifest["artifacts"][artifact_id]["path"]
        assert "/runs/uce-p0-boundary-p0/" in path


@pytest.mark.parametrize(
    ("tests", "skipped", "expected"),
    [(0, 0, ["no_tests_executed"]), (1, 1, ["skipped_tests"]), (1, 0, [])],
)
def test_p0_junit_skip_and_empty_counts_are_policy_failures(
    tmp_path: Path, tests: int, skipped: int, expected: list[str]
) -> None:
    path = tmp_path / "junit_p0.xml"
    root = ET.Element(
        "testsuite", tests=str(tests), failures="0", errors="0", skipped=str(skipped)
    )
    ET.ElementTree(root).write(path, encoding="utf-8")

    counts = parse_junit_counts(path)

    assert policy_violations_from_report({"junit_counts": counts}) == expected
