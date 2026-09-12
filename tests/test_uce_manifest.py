from __future__ import annotations

import json
import tarfile
from pathlib import Path

import pytest

from scripts.uce.manifest import add_artifact
from scripts.uce.manifest import add_contract
from scripts.uce.manifest import add_policy_violation
from scripts.uce.manifest import add_result
from scripts.uce.manifest import create_manifest
from scripts.uce.manifest import set_capability
from scripts.uce.manifest import write_manifest


def test_create_manifest_starts_with_required_sections() -> None:
    manifest = create_manifest(
        gate_id="uce",
        tier="pr",
        run_id="run-1",
        repo_root="/repo",
        build_dir="/repo/build",
        evidence_dir="/repo/build/evidence/uce/run-1",
        tar_gz="/repo/build/evidence/uce/run-1.tar.gz",
        environment={"platform": "Linux"},
    )

    assert manifest["schema_version"] == 1
    assert manifest["result"] == "fail"
    assert manifest["policy_violations"] == []
    assert manifest["results"] == []
    assert manifest["artifacts"] == {}
    assert manifest["contracts"] == {}


def test_manifest_helpers_upsert_sections_and_dedupe_policy_codes(tmp_path: Path) -> None:
    manifest = create_manifest(
        gate_id="uce",
        tier="nightly",
        run_id="run-2",
        repo_root="/repo",
        build_dir="/repo/build",
        evidence_dir="/repo/build/evidence/uce/run-2",
        tar_gz="/repo/build/evidence/uce/run-2.tar.gz",
    )

    add_result(manifest, result_id="tlc", kind="validator", result="pass", returncode=0)
    add_artifact(
        manifest,
        artifact_id="tlc_report",
        path="reports/tlc.json",
        kind="report",
        required=True,
        media_type="application/json",
    )
    add_contract(
        manifest,
        contract_id="timeline@v1",
        provider="tlc",
        result="pass",
        required=True,
        report_artifact="tlc_report",
    )
    add_policy_violation(manifest, "contract_missing")
    add_policy_violation(manifest, "contract_missing")
    set_capability(manifest, "netproof", {"schema_version": 1})

    assert manifest["results"][0]["id"] == "tlc"
    assert manifest["artifacts"]["tlc_report"]["required"] is True
    assert manifest["contracts"]["timeline@v1"]["provider"] == "tlc"
    assert manifest["policy_violations"] == ["contract_missing"]
    assert manifest["capabilities"]["netproof"]["schema_version"] == 1

    output = tmp_path / "manifest.json"
    write_manifest(output, manifest)

    assert output.exists()
    assert '"timeline@v1"' in output.read_text(encoding="utf-8")


def test_packaged_manifest_matches_on_disk_manifest(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch,
) -> None:
    """执行真实终结器，验证两份元数据逐字节一致且不依赖同秒写入。"""

    from scripts.uce_gate import evidence, finalize
    from scripts.uce_gate.evidence import create_gate_manifest
    from scripts.uce_gate.evidence import prepare_evidence_layout
    from scripts.uce_gate.evidence import resolve_evidence_layout

    timestamps = iter(f"timestamp-{i}" for i in range(20))
    monkeypatch.setattr(evidence, "utc_now_iso", lambda: next(timestamps))
    monkeypatch.setattr(finalize, "_run_contract_validators", lambda **_: None)

    repo_root = Path(__file__).resolve().parents[1]
    layout = resolve_evidence_layout(repo_root, tmp_path / "build", run_id="run-pack", evidence_root_arg="")
    prepare_evidence_layout(layout)
    manifest = create_gate_manifest(
        repo_root=repo_root,
        build_dir=tmp_path / "build",
        layout=layout,
        tier="pr",
        run_id="run-pack",
        environment={},
    )
    (layout.meta_dir / "versions.txt").write_text("test versions\n", encoding="utf-8")
    add_result(manifest, result_id="workload", kind="gate", result="pass", returncode=0)
    finalize.finalize_gate_evidence(
        repo_root=repo_root, build_dir=tmp_path / "build", layout=layout, manifest=manifest,
        tier="pr", run_id="run-pack", artifact_roots=[],
    )

    on_disk = json.loads(layout.manifest_path.read_text(encoding="utf-8"))
    with tarfile.open(layout.tar_path) as archive:
        for name in ("manifest.json", "policy_violations.json"):
            assert archive.extractfile(f"run-pack/{name}").read() == (layout.evidence_dir / name).read_bytes()
        assert not any("archive-envelope" in name for name in archive.getnames())

    assert on_disk["result"] == "pass"
    assert "archive_bundle" not in on_disk["artifacts"]

    envelope = json.loads(layout.archive_envelope_path.read_text(encoding="utf-8"))
    assert envelope["archive"]["byte_count"] == layout.tar_path.stat().st_size


def test_candidate_fingerprint_contains_separate_index_and_worktree_identity() -> None:
    from scripts.uce_gate.candidate import capture_candidate_fingerprint

    fingerprint = capture_candidate_fingerprint(Path(__file__).resolve().parents[1])

    assert fingerprint["schemaVersion"] == 3
    assert len(fingerprint["head"]) == 40
    assert fingerprint["available"] is True
    assert fingerprint["index"]["entries"]
    assert len(fingerprint["index"]["sha256"]) == 64
    assert fingerprint["authority"]["files"]
    assert len(fingerprint["unstaged"]["sha256"]) == 64
    assert len(fingerprint["untracked"]["sha256"]) == 64
    assert fingerprint["combined"].startswith("sha256:")
