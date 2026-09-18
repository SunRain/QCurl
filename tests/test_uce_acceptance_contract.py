from __future__ import annotations

import json
from pathlib import Path

import pytest
import yaml

from scripts.uce_gate import orchestrator
from scripts.uce_gate import evidence
from scripts.uce_gate import ctest_gates
from scripts.uce_gate import httpbin
from scripts.uce_gate.evidence import create_gate_manifest
from scripts.uce_gate.evidence import prepare_evidence_layout
from scripts.uce_gate.evidence import resolve_evidence_layout
from scripts.uce_gate.runtime import GateResult
from scripts.uce_gate.finalize import write_validated_state


def _workflow(path: str) -> dict[str, object]:
    payload = yaml.safe_load(Path(path).read_text(encoding="utf-8"))
    if True in payload and "on" not in payload:
        payload["on"] = payload.pop(True)
    return payload


def _run_blocks(job: dict[str, object]) -> list[str]:
    return [str(step["run"]) for step in job["steps"] if "run" in step]


def test_uce_nightly_preserves_acceptance_push_and_manual_triggers() -> None:
    workflow = _workflow(".github/workflows/uce_nightly.yml")
    triggers = workflow["on"]

    assert "push" in triggers
    assert triggers["push"]["branches"] == ["master", "main", "develop"]
    assert "workflow_dispatch" in triggers


def test_uce_nightly_uploads_archive_envelope() -> None:
    workflow = _workflow(".github/workflows/uce_nightly.yml")
    job = workflow["jobs"]["uce_nightly"]
    upload = next(
        step for step in job["steps"] if step.get("name") == "Upload UCE nightly evidence [required]"
    )
    paths = str(upload["with"]["path"])

    assert "archive-envelope.json" in paths


def test_uce_nightly_keeps_asan_required_without_tsan_prerequisites() -> None:
    workflow = _workflow(".github/workflows/uce_nightly.yml")
    job = workflow["jobs"]["uce_nightly"]
    run_blocks = _run_blocks(job)
    sanitizer_runs = [block for block in run_blocks if "run_uce_sanitizers.py" in block]

    assert len(sanitizer_runs) == 1
    assert "--profile asan-ubsan-lsan" in sanitizer_runs[0]
    assert all("tsan" not in block.lower() for block in run_blocks)
    sanitizer = next(step for step in job["steps"] if step.get("name") == "Run ASan/UBSan/LSan bundle")
    assert sanitizer.get("continue-on-error", False) is False
    upload = next(step for step in job["steps"] if step.get("name") == "Upload sanitizer evidence [required]")
    assert str(upload["with"]["path"]).strip() == "build/evidence/uce-sanitizers/asan/"
    assert upload["with"]["if-no-files-found"] == "error"


def test_uce_workload_exception_is_structured_and_archived(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    build_dir = tmp_path / "build"
    build_dir.mkdir()

    def raise_workload(**_: object) -> list[Path]:
        raise RuntimeError("synthetic workload failure")

    monkeypatch.setattr(orchestrator, "_run_gate_workload", raise_workload)

    rc = orchestrator.run_uce_gate(
        repo_root=Path(__file__).resolve().parents[1],
        build_dir=build_dir,
        tier="pr",
        run_id="exception-run",
        evidence_root_arg=str(tmp_path / "evidence"),
    )

    evidence_dir = tmp_path / "evidence" / "exception-run"
    manifest = json.loads((evidence_dir / "manifest.json").read_text(encoding="utf-8"))
    assert rc == 3
    assert manifest["result"] == "fail"
    assert "gate_exception" in manifest["policy_violations"]
    assert manifest["exception"]["type"] == "RuntimeError"
    assert (evidence_dir / "logs" / "gate_exception.log").exists()
    assert manifest["candidate_fingerprint"]["head"]
    assert (evidence_dir / "meta" / "candidate_fingerprint.json").exists()
    assert (tmp_path / "evidence" / "exception-run.tar.gz").exists()


@pytest.mark.parametrize(
    ("gate_id", "policy_code"),
    [
        ("public_api_slow", "gate_public_api_slow_failed"),
        ("capability", "gate_capability_failed"),
    ],
)
def test_acceptance_label_failure_is_fail_closed(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    gate_id: str,
    policy_code: str,
) -> None:
    build_dir = tmp_path / "build"
    evidence_dir = tmp_path / "evidence"
    build_dir.mkdir()
    evidence_dir.mkdir()
    manifest = {
        "policy_violations": [],
        "results": [],
        "artifacts": {},
        "contracts": {},
    }

    def fake_run_gate(
        current_id: str,
        command: list[str],
        log_path: Path,
        *,
        cwd: Path,
        env: dict[str, str] | None = None,
    ) -> GateResult:
        del command, cwd, env
        log_path.parent.mkdir(parents=True, exist_ok=True)
        log_path.write_text("failure\n", encoding="utf-8")
        return GateResult(current_id, [], 1 if current_id == gate_id else 0, 0.001, log_path)

    monkeypatch.setattr(ctest_gates, "run_gate", fake_run_gate)
    runner = ctest_gates.run_public_api_slow_gate if gate_id == "public_api_slow" else ctest_gates.run_capability_gate

    runner(Path(__file__).resolve().parents[1], build_dir, evidence_dir, manifest)

    assert policy_code in manifest["policy_violations"]
    assert manifest["contracts"][f"qtest_{gate_id}@v1"]["result"] == "fail"


@pytest.mark.parametrize(
    ("gate_id", "policy_code"),
    [
        ("public_api_slow", "gate_public_api_slow_failed"),
        ("capability", "gate_capability_failed"),
    ],
)
def test_acceptance_label_listing_failure_is_fail_closed(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    gate_id: str,
    policy_code: str,
) -> None:
    build_dir = tmp_path / "build"
    evidence_dir = tmp_path / "evidence"
    build_dir.mkdir()
    evidence_dir.mkdir()
    manifest = {"policy_violations": [], "results": [], "artifacts": {}, "contracts": {}}

    def fake_run_gate(
        current_id: str,
        command: list[str],
        log_path: Path,
        *,
        cwd: Path,
        env: dict[str, str] | None = None,
    ) -> GateResult:
        del command, cwd, env
        log_path.parent.mkdir(parents=True, exist_ok=True)
        log_path.write_text("listing failed\n", encoding="utf-8")
        return GateResult(current_id, [], 1 if current_id == f"{gate_id}_list" else 0, 0.001, log_path)

    monkeypatch.setattr(ctest_gates, "run_gate", fake_run_gate)
    runner = ctest_gates.run_public_api_slow_gate if gate_id == "public_api_slow" else ctest_gates.run_capability_gate

    runner(Path(__file__).resolve().parents[1], build_dir, evidence_dir, manifest)

    assert policy_code in manifest["policy_violations"]
    assert manifest["contracts"][f"qtest_{gate_id}@v1"]["result"] == "fail"


def test_httpbin_start_failure_is_fail_closed(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    repo_root = Path(__file__).resolve().parents[1]
    evidence_dir = tmp_path / "evidence"
    evidence_dir.mkdir()
    manifest = {"policy_violations": [], "results": [], "artifacts": {}, "contracts": {}}

    def fake_start(_repo_root: Path, env_file: Path, log_path: Path, _manifest: dict[str, object]) -> GateResult:
        del _repo_root, _manifest
        log_path.parent.mkdir(parents=True, exist_ok=True)
        log_path.write_text("docker unavailable\n", encoding="utf-8")
        return GateResult("httpbin_start", [], 1, 0.001, log_path)

    def fake_stop(_repo_root: Path, _container: str, log_path: Path, _manifest: dict[str, object]) -> GateResult:
        del _repo_root, _container, _manifest
        log_path.parent.mkdir(parents=True, exist_ok=True)
        log_path.write_text("stopped\n", encoding="utf-8")
        return GateResult("httpbin_stop", [], 0, 0.001, log_path)

    monkeypatch.setattr(httpbin, "_start_httpbin", fake_start)
    monkeypatch.setattr(httpbin, "_stop_httpbin", fake_stop)

    env_values, _results, violations = httpbin.run_httpbin_gate(
        repo_root,
        tmp_path / "build",
        evidence_dir,
        manifest,
    )

    assert env_values == {}
    assert "env_preflight_httpbin_start_failed" in violations
    assert "env_preflight_httpbin_env_missing" in violations
    assert "env_preflight_httpbin_url_missing" in violations
    assert (evidence_dir / "httpbin" / "httpbin_unavailable.txt").exists()


def test_httpbin_env_parse_failure_is_fail_closed(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    repo_root = Path(__file__).resolve().parents[1]
    evidence_dir = tmp_path / "evidence"
    evidence_dir.mkdir()
    manifest = {"policy_violations": [], "results": [], "artifacts": {}, "contracts": {}}

    def fake_start(_repo_root: Path, env_file: Path, log_path: Path, _manifest: dict[str, object]) -> GateResult:
        del _repo_root, _manifest
        env_file.parent.mkdir(parents=True, exist_ok=True)
        env_file.write_text('export QCURL_HTTPBIN_URL="http://127.0.0.1:1\n', encoding="utf-8")
        log_path.parent.mkdir(parents=True, exist_ok=True)
        log_path.write_text("started\n", encoding="utf-8")
        return GateResult("httpbin_start", [], 0, 0.001, log_path)

    def fake_stop(_repo_root: Path, _container: str, log_path: Path, _manifest: dict[str, object]) -> GateResult:
        del _repo_root, _container, _manifest
        log_path.parent.mkdir(parents=True, exist_ok=True)
        log_path.write_text("stopped\n", encoding="utf-8")
        return GateResult("httpbin_stop", [], 0, 0.001, log_path)

    monkeypatch.setattr(httpbin, "_start_httpbin", fake_start)
    monkeypatch.setattr(httpbin, "_stop_httpbin", fake_stop)

    _env_values, _results, violations = httpbin.run_httpbin_gate(
        repo_root,
        tmp_path / "build",
        evidence_dir,
        manifest,
    )

    assert "env_preflight_httpbin_env_parse_error" in violations
    assert "env_preflight_httpbin_url_missing" in violations
    assert (evidence_dir / "httpbin" / "httpbin_env_parse_error.txt").exists()


def test_required_artifact_deleted_before_validation_is_fail_closed(tmp_path: Path) -> None:
    repo_root = Path(__file__).resolve().parents[1]
    layout = resolve_evidence_layout(repo_root, tmp_path / "build", run_id="artifact-delete", evidence_root_arg=str(tmp_path / "evidence"))
    prepare_evidence_layout(layout)
    manifest = create_gate_manifest(
        repo_root=repo_root,
        build_dir=tmp_path / "build",
        layout=layout,
        tier="pr",
        run_id="artifact-delete",
        environment={},
    )
    required = layout.evidence_dir / "required.txt"
    required.write_text("present\n", encoding="utf-8")
    from scripts.uce.manifest import add_artifact

    add_artifact(manifest, artifact_id="required_probe", path="required.txt", kind="report", required=True)
    required.unlink()

    missing = write_validated_state(layout, manifest, "pr")

    assert "required_probe" in missing
    assert "artifact_required_missing" in manifest["policy_violations"]
    assert manifest["result"] == "fail"


def test_packaging_failure_retains_required_archive_signal(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    repo_root = Path(__file__).resolve().parents[1]
    layout = resolve_evidence_layout(repo_root, tmp_path / "build", run_id="pack-fail", evidence_root_arg=str(tmp_path / "evidence"))
    prepare_evidence_layout(layout)
    manifest = create_gate_manifest(
        repo_root=repo_root,
        build_dir=tmp_path / "build",
        layout=layout,
        tier="pr",
        run_id="pack-fail",
        environment={},
    )

    def fail_packaging(*_: object, **__: object) -> None:
        raise OSError("synthetic tar failure")

    monkeypatch.setattr(evidence, "tar_gz_dir", fail_packaging)
    evidence.package_evidence_bundle(layout, manifest)
    write_validated_state(layout, manifest, "pr")

    assert "packaging_tar_gz_failed" in manifest["policy_violations"]
    assert "artifact_required_missing" in manifest["policy_violations"]
    assert "archive_bundle" not in manifest["artifacts"]


def test_missing_archive_source_is_structured(tmp_path: Path) -> None:
    repo_root = Path(__file__).resolve().parents[1]
    layout = resolve_evidence_layout(repo_root, tmp_path / "build", run_id="missing-archive", evidence_root_arg=str(tmp_path / "evidence"))
    prepare_evidence_layout(layout)
    manifest = create_gate_manifest(
        repo_root=repo_root,
        build_dir=tmp_path / "build",
        layout=layout,
        tier="pr",
        run_id="missing-archive",
        environment={},
    )

    evidence.write_archive_envelope(layout, manifest)

    assert "archive_envelope_source_missing" in manifest["policy_violations"]
    assert manifest["packaging"]["envelope_error"]


def test_archive_envelope_read_failure_is_structured(tmp_path: Path) -> None:
    repo_root = Path(__file__).resolve().parents[1]
    layout = resolve_evidence_layout(repo_root, tmp_path / "build", run_id="read-fail", evidence_root_arg=str(tmp_path / "evidence"))
    prepare_evidence_layout(layout)
    manifest = create_gate_manifest(
        repo_root=repo_root,
        build_dir=tmp_path / "build",
        layout=layout,
        tier="pr",
        run_id="read-fail",
        environment={},
    )
    layout.tar_path.mkdir(parents=True)

    evidence.write_archive_envelope(layout, manifest)

    assert "archive_envelope_read_failed" in manifest["policy_violations"]


def test_archive_envelope_write_failure_is_structured(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    repo_root = Path(__file__).resolve().parents[1]
    layout = resolve_evidence_layout(repo_root, tmp_path / "build", run_id="write-fail", evidence_root_arg=str(tmp_path / "evidence"))
    prepare_evidence_layout(layout)
    manifest = create_gate_manifest(
        repo_root=repo_root,
        build_dir=tmp_path / "build",
        layout=layout,
        tier="pr",
        run_id="write-fail",
        environment={},
    )
    layout.tar_path.write_bytes(b"tar\n")

    def fail_write(*_: object, **__: object) -> None:
        raise OSError("envelope disk failure")

    monkeypatch.setattr(evidence, "write_json", fail_write)
    evidence.write_archive_envelope(layout, manifest)

    assert "archive_envelope_write_failed" in manifest["policy_violations"]
