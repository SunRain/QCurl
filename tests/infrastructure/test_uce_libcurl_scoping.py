from __future__ import annotations

from pathlib import Path

from scripts.uce_gate import contracts as uce_contracts
from scripts.uce_gate import execute as uce_execute
from scripts.uce_gate.planner import GateSpec
from scripts.uce_gate.runtime import GateResult


def _manifest() -> dict[str, object]:
    return {
        "results": [],
        "artifacts": {},
        "contracts": {},
        "policy_violations": [],
    }


def test_uce_registers_run_scoped_consistency_reports(tmp_path: Path, monkeypatch) -> None:
    repo_root = tmp_path / "repo"
    build_dir = tmp_path / "build"
    evidence_dir = tmp_path / "evidence"
    manifest = _manifest()
    commands: list[list[str]] = []

    def fake_run_gate(
        gate_id: str,
        command: list[str],
        log_path: Path,
        *,
        cwd: Path,
        env: dict[str, str] | None = None,
    ) -> GateResult:
        del cwd, env
        commands.append(command)
        return GateResult(gate_id, command, 0, 0.01, log_path)

    monkeypatch.setattr(uce_execute, "run_gate", fake_run_gate)
    plan = [GateSpec("libcurl_consistency_p0", "libcurl_consistency", "p0", "p0_failed")]

    results, artifact_roots = uce_execute.run_libcurl_consistency_gates(
        repo_root,
        build_dir,
        evidence_dir,
        manifest,
        plan,
        {},
        uce_run_id="current-run",
    )

    run_id = "uce-current-run-p0"
    assert len(results) == 1
    assert commands[0][commands[0].index("--run-id") + 1] == run_id
    assert artifact_roots == [
        evidence_dir / "libcurl_consistency" / "reports" / "runs" / run_id / "artifacts"
    ]
    assert manifest["artifacts"]["p0_gate_report"]["path"] == (
        f"libcurl_consistency/reports/runs/{run_id}/gate_p0.json"
    )
    assert manifest["artifacts"]["p0_junit_report"]["path"] == (
        f"libcurl_consistency/reports/runs/{run_id}/junit_p0.xml"
    )


def test_uce_contracts_receive_only_current_consistency_artifact_roots(
    tmp_path: Path,
    monkeypatch,
) -> None:
    repo_root = tmp_path / "repo"
    evidence_dir = tmp_path / "evidence"
    current_roots = [
        evidence_dir / "libcurl_consistency" / "reports" / "runs" / "current-p0" / "artifacts",
        evidence_dir / "libcurl_consistency" / "reports" / "runs" / "current-p1" / "artifacts",
    ]
    captured: dict[str, list[Path]] = {}

    ctbp_contract = repo_root / "tests" / "uce" / "contracts" / "ctbp@v1.yaml"
    hes_contract = repo_root / "tests" / "uce" / "contracts" / "hes@v1.yaml"
    ctbp_contract.parent.mkdir(parents=True)
    ctbp_contract.write_text("{}\n", encoding="utf-8")
    hes_contract.write_text("{}\n", encoding="utf-8")

    def validate_ctbp(_contract, artifact_roots, _runners, _kinds):
        captured["ctbp"] = artifact_roots
        return {
            "policy_violations": [],
            "entries": [],
            "summary": {
                "entry_count": 0,
                "failed_entries": 0,
                "scanned_artifacts": 0,
                "missing_roots": [],
            },
        }

    def validate_hes(_contract, artifact_roots, _tier):
        captured["hes"] = artifact_roots
        return {
            "policy_violations": [],
            "required_kinds": [],
            "required_runners": [],
            "entries": [],
        }

    monkeypatch.setattr(uce_contracts, "validate_ctbp", validate_ctbp)
    monkeypatch.setattr(uce_contracts, "validate_hes", validate_hes)

    assert (
        uce_contracts.run_ctbp_contract(
            repo_root,
            evidence_dir,
            _manifest(),
            artifact_roots=current_roots,
        )
        == []
    )
    assert (
        uce_contracts.run_hes_contract(
            repo_root,
            evidence_dir,
            _manifest(),
            tier="nightly",
            artifact_roots=current_roots,
        )
        == []
    )

    assert captured == {"ctbp": current_roots, "hes": current_roots}


def test_timeline_merge_scopes_stream_identity_by_consistency_run(tmp_path: Path) -> None:
    roots = [
        tmp_path / "runs" / "uce-current-p0" / "artifacts",
        tmp_path / "runs" / "uce-current-p1" / "artifacts",
    ]
    duplicate_event = {
        "provider": "qt",
        "stream_id": "same-case:qcurl",
        "case_id": "same-case",
        "seq": 1,
        "event": "request_headers",
    }

    merged = uce_contracts._merge_timeline_collections(
        "qt",
        roots,
        [
            {"events": [duplicate_event]},
            {"events": [duplicate_event]},
        ],
    )

    stream_ids = [event["stream_id"] for event in merged["events"]]
    assert stream_ids == [
        "uce-current-p0:same-case:qcurl",
        "uce-current-p1:same-case:qcurl",
    ]
