from __future__ import annotations

from pathlib import Path

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
