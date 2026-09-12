from __future__ import annotations

import json
import tarfile
from pathlib import Path

import pytest

from scripts.uce_gate import evidence, finalize, orchestrator


@pytest.fixture
def run_gate(tmp_path: Path, monkeypatch: pytest.MonkeyPatch):
    def metadata(*, layout, **_):
        (layout.meta_dir / "versions.txt").write_text("test versions\n", encoding="utf-8")

    def workload(**_):
        raise RuntimeError("synthetic workload failure")

    monkeypatch.setattr(orchestrator, "_write_versions_and_capabilities", metadata)
    monkeypatch.setattr(orchestrator, "_run_gate_workload", workload)

    def run():
        return orchestrator.run_uce_gate(
            repo_root=Path(__file__).resolve().parents[1],
            build_dir=tmp_path / "build",
            tier="pr",
            run_id="failure-run",
            evidence_root_arg=str(tmp_path / "evidence"),
        )

    return run, tmp_path / "evidence" / "failure-run"


def test_exception_log_write_failure_still_persists_structured_failure(
    run_gate, monkeypatch: pytest.MonkeyPatch,
) -> None:
    run, evidence_dir = run_gate
    original_write = Path.write_text

    def write(path, *args, **kwargs):
        if path.name == "gate_exception.log":
            raise OSError("diagnostic log unavailable")
        return original_write(path, *args, **kwargs)

    monkeypatch.setattr(Path, "write_text", write)

    assert run() == 3
    manifest = json.loads((evidence_dir / "manifest.json").read_text(encoding="utf-8"))
    policy = json.loads((evidence_dir / "policy_violations.json").read_text(encoding="utf-8"))
    assert manifest["result"] == "fail"
    assert "gate_exception" in manifest["policy_violations"]
    assert manifest["exception"]["type"] == "RuntimeError"
    assert "gate_exception" in policy["policy_violations"]
    assert evidence_dir.with_suffix(".tar.gz").is_file()


@pytest.mark.parametrize("failed_name", ["policy_violations.json", "manifest.json"])
def test_metadata_write_failure_preserves_other_metadata_and_diagnostics(
    run_gate, monkeypatch: pytest.MonkeyPatch, failed_name: str,
) -> None:
    run, evidence_dir = run_gate
    original_write = Path.write_text

    def write(path, *args, **kwargs):
        if path.name == failed_name:
            raise OSError(f"{failed_name} unavailable")
        return original_write(path, *args, **kwargs)

    monkeypatch.setattr(Path, "write_text", write)

    assert run() == 3
    surviving_name = "manifest.json" if failed_name != "manifest.json" else "policy_violations.json"
    surviving = json.loads((evidence_dir / surviving_name).read_text(encoding="utf-8"))
    assert "gate_exception" in surviving["policy_violations"]
    with tarfile.open(evidence_dir.with_suffix(".tar.gz")) as archive:
        assert archive.getmember(f"failure-run/{surviving_name}")
        assert archive.getmember("failure-run/logs/gate_exception.log")


@pytest.mark.parametrize("failure", ["envelope", "tar_once", "deleted_after_tar", "changed_after_tar"])
def test_packaging_failure_archives_the_final_failure_snapshot(
    run_gate, monkeypatch: pytest.MonkeyPatch, failure: str,
) -> None:
    run, evidence_dir = run_gate
    original_write = Path.write_text
    original_tar = evidence.tar_gz_dir
    calls = 0

    def write(path, *args, **kwargs):
        if failure == "envelope" and path.name.endswith(".archive-envelope.json"):
            raise OSError("envelope unavailable")
        return original_write(path, *args, **kwargs)

    def pack(source, target):
        nonlocal calls
        calls += 1
        if failure == "tar_once" and calls == 1:
            raise OSError("archive unavailable once")
        original_tar(source, target)
        if calls == 1 and failure == "deleted_after_tar":
            (source / "meta" / "versions.txt").unlink()
        if calls == 1 and failure == "changed_after_tar":
            (source / "policy_violations.json").write_text("{}\n", encoding="utf-8")

    monkeypatch.setattr(Path, "write_text", write)
    monkeypatch.setattr(evidence, "tar_gz_dir", pack)

    assert run() == 3
    manifest = json.loads((evidence_dir / "manifest.json").read_text(encoding="utf-8"))
    code = {"envelope": "archive_envelope_write_failed", "tar_once": "packaging_tar_gz_failed",
            "deleted_after_tar": "artifact_required_missing", "changed_after_tar": "gate_exception"}[failure]
    assert code in manifest["policy_violations"]
    assert manifest["result"] == "fail"
    assert "archive_bundle" not in manifest["artifacts"]
    with tarfile.open(evidence_dir.with_suffix(".tar.gz")) as archive:
        for name in ("manifest.json", "policy_violations.json"):
            assert archive.extractfile(f"failure-run/{name}").read() == (evidence_dir / name).read_bytes()
    assert 2 <= calls <= 3


def test_failure_recovery_still_runs_redaction_gate(run_gate, monkeypatch: pytest.MonkeyPatch) -> None:
    run, evidence_dir = run_gate

    def fail(**_):
        raise RuntimeError("validator unavailable")

    monkeypatch.setattr(finalize, "_run_contract_validators", fail)
    assert run() == 3
    manifest = json.loads((evidence_dir / "manifest.json").read_text(encoding="utf-8"))
    assert (evidence_dir / "reports" / "redaction_gate.json").is_file()
    assert "redaction_report" in manifest["artifacts"]


def test_reused_run_id_is_rejected_without_touching_prior_evidence(run_gate) -> None:
    run, evidence_dir = run_gate
    assert run() == 3
    paths = [evidence_dir / "manifest.json", evidence_dir / "policy_violations.json",
             evidence_dir.with_suffix(".tar.gz"), evidence_dir.with_suffix(".archive-envelope.json")]
    before = {path: path.read_bytes() for path in paths}
    assert run() == 3
    assert before == {path: path.read_bytes() for path in paths}


def test_cleanup_exception_preserves_the_primary_workload_error(tmp_path, monkeypatch) -> None:
    layout = evidence.resolve_evidence_layout(tmp_path, tmp_path / "build", run_id="errors", evidence_root_arg="")
    evidence.prepare_evidence_layout(layout)
    manifest = evidence.create_gate_manifest(repo_root=tmp_path, build_dir=tmp_path / "build",
                                            layout=layout, tier="nightly", run_id="errors", environment={})

    def workload(**_):
        raise RuntimeError("primary workload error")

    def cleanup(*_):
        raise OSError("cleanup error")

    monkeypatch.setattr(orchestrator, "_run_required_gates", workload)
    monkeypatch.setattr(orchestrator, "stop_httpbin_gate", cleanup)
    orchestrator._run_gate_workload(repo_root=tmp_path, build_dir=tmp_path / "build", layout=layout,
                                    manifest=manifest, tier="nightly", run_id="errors")
    assert manifest["exception"]["message"] == "primary workload error"
    assert [item["type"] for item in manifest["exceptions"]] == ["RuntimeError", "OSError"]
    assert "env_preflight_httpbin_stop_failed" in manifest["policy_violations"]


def test_dual_metadata_write_failure_does_not_archive_stale_pass(run_gate, monkeypatch) -> None:
    from scripts.uce.manifest import add_result
    from scripts.uce_gate.archive_validation import validate_archive

    run, evidence_dir = run_gate
    original_write = Path.write_text
    broken = False

    def workload(*, manifest, **_):
        add_result(manifest, result_id="workload", kind="gate", result="pass")
        return []

    def fail_after_initial_metadata(*_):
        nonlocal broken
        broken = True
        raise RuntimeError("post-metadata failure")

    def write(path, *args, **kwargs):
        if broken and path.name in {"manifest.json", "policy_violations.json"}:
            raise OSError("metadata no longer writable")
        return original_write(path, *args, **kwargs)

    monkeypatch.setattr(orchestrator, "_run_gate_workload", workload)
    monkeypatch.setattr(finalize, "_run_contract_validators", lambda **_: None)
    monkeypatch.setattr(finalize, "_run_redaction_gate", fail_after_initial_metadata)
    monkeypatch.setattr(Path, "write_text", write)
    assert run() == 3
    assert not validate_archive(evidence_dir.parent, evidence_dir.name, require_pass=True)["accepted"]
    with tarfile.open(evidence_dir.with_suffix(".tar.gz")) as archive:
        assert archive.getmember("failure-run/logs/gate_exception.log")
