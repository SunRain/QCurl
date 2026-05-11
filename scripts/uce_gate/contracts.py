"""Contract validators used by scripts/run_uce_gate.py."""

from __future__ import annotations

from pathlib import Path
from typing import Any
import shutil

from scripts.uce.manifest import add_artifact
from scripts.uce.manifest import add_contract
from scripts.uce.manifest import add_result
from scripts.uce_gate.planner import ctbp_required_kinds
from scripts.uce_gate.planner import ctbp_required_runners
from scripts.uce_gate.planner import timeline_required_providers
from scripts.uce_gate.runtime import safe_mkdir
from scripts.uce_gate.runtime import utc_now_iso
from scripts.uce_gate.runtime import write_json
from tests.uce.ctbp.validate import validate_ctbp
from tests.uce.hes.validate import validate_hes
from tests.uce.timeline.collect_from_lc import collect_from_lc
from tests.uce.timeline.collect_from_qt import collect_from_qt
from tests.uce.timeline.common import write_jsonl as write_timeline_jsonl
from tests.uce.timeline.validate import validate_timelines


def run_timeline_contract(
    repo_root: Path,
    build_dir: Path,
    evidence_dir: Path,
    manifest: dict[str, Any],
    *,
    tier: str,
) -> list[str]:
    """Collect and validate timeline evidence."""

    timeline_dir = evidence_dir / "timeline"
    safe_mkdir(timeline_dir)

    contract_src = repo_root / "tests" / "uce" / "contracts" / "timeline@v1.yaml"
    contract_dst = timeline_dir / "timeline@v1.yaml"
    shutil.copy2(contract_src, contract_dst)

    lc_artifacts_root = repo_root / "curl" / "tests" / "http" / "gen" / "artifacts"
    qt_artifacts_root = build_dir / "test-artifacts"
    lc_collection = collect_from_lc(lc_artifacts_root)
    qt_collection = collect_from_qt(lc_artifacts_root, qt_artifacts_root)

    lc_timeline_path = timeline_dir / "libcurl_consistency.timeline.jsonl"
    qt_timeline_path = timeline_dir / "qt.timeline.jsonl"
    write_timeline_jsonl(lc_timeline_path, lc_collection["events"])
    write_timeline_jsonl(qt_timeline_path, qt_collection["events"])

    required_providers = timeline_required_providers(tier)
    report = validate_timelines(contract_dst, [lc_timeline_path, qt_timeline_path], required_providers)
    report["collections"] = {
        "libcurl_consistency": {key: value for key, value in lc_collection.items() if key != "events"},
        "qt": {key: value for key, value in qt_collection.items() if key != "events"},
    }
    report_path = timeline_dir / "report.json"
    write_json(report_path, report)

    add_artifact(manifest, artifact_id="timeline_contract", path="timeline/timeline@v1.yaml", kind="contract", required=True, media_type="application/yaml")
    add_artifact(manifest, artifact_id="timeline_report", path="timeline/report.json", kind="report", required=True, media_type="application/json")
    add_artifact(manifest, artifact_id="timeline_qt_jsonl", path="timeline/qt.timeline.jsonl", kind="evidence", required=True, media_type="application/x-ndjson")
    add_artifact(manifest, artifact_id="timeline_lc_jsonl", path="timeline/libcurl_consistency.timeline.jsonl", kind="evidence", required=tier in {"nightly", "soak"}, media_type="application/x-ndjson")
    add_result(
        manifest,
        result_id="timeline_contract",
        kind="validator",
        result="pass" if not report["policy_violations"] else "fail",
        log_file=str(report_path),
        details={
            "required_providers": sorted(required_providers),
            "failed_streams": report["summary"]["failed_streams"],
            "provider_summary": report["provider_summary"],
        },
    )
    add_contract(
        manifest,
        contract_id="timeline@v1",
        provider="uce_timeline_validator",
        result="pass" if not report["policy_violations"] else "fail",
        required=True,
        report_artifact="timeline_report",
        evidence_artifacts=["timeline_contract", "timeline_qt_jsonl", "timeline_lc_jsonl"],
        violations=report["policy_violations"],
        notes=[f"required providers: {', '.join(sorted(required_providers))}"],
    )
    return list(report["policy_violations"])


def run_ctbp_contract(
    repo_root: Path,
    evidence_dir: Path,
    manifest: dict[str, Any],
) -> list[str]:
    """Collect and validate CTBP evidence."""

    ctbp_dir = evidence_dir / "ctbp"
    safe_mkdir(ctbp_dir)

    contract_src = repo_root / "tests" / "uce" / "contracts" / "ctbp@v1.yaml"
    contract_dst = ctbp_dir / "ctbp@v1.yaml"
    shutil.copy2(contract_src, contract_dst)

    lc_artifacts_root = repo_root / "curl" / "tests" / "http" / "gen" / "artifacts"
    report = validate_ctbp(contract_dst, [lc_artifacts_root], ctbp_required_runners(), ctbp_required_kinds())

    evidence_path = ctbp_dir / "evidence.json"
    write_json(
        evidence_path,
        {
            "generated_at_utc": utc_now_iso(),
            "entries": report["entries"],
            "scanned_artifacts": report["summary"]["scanned_artifacts"],
            "missing_roots": report["summary"]["missing_roots"],
        },
    )

    report_path = ctbp_dir / "report.json"
    write_json(report_path, report)
    add_artifact(manifest, artifact_id="ctbp_contract", path="ctbp/ctbp@v1.yaml", kind="contract", required=True, media_type="application/yaml")
    add_artifact(manifest, artifact_id="ctbp_evidence", path="ctbp/evidence.json", kind="evidence", required=True, media_type="application/json")
    add_artifact(manifest, artifact_id="ctbp_report", path="ctbp/report.json", kind="report", required=True, media_type="application/json")
    add_result(
        manifest,
        result_id="ctbp_contract",
        kind="validator",
        result="pass" if not report["policy_violations"] else "fail",
        log_file=str(report_path),
        details={
            "required_runners": sorted(ctbp_required_runners()),
            "required_kinds": sorted(ctbp_required_kinds()),
            "entry_count": report["summary"]["entry_count"],
            "failed_entries": report["summary"]["failed_entries"],
        },
    )
    add_contract(
        manifest,
        contract_id="ctbp@v1",
        provider="uce_ctbp_validator",
        result="pass" if not report["policy_violations"] else "fail",
        required=True,
        report_artifact="ctbp_report",
        evidence_artifacts=["ctbp_contract", "ctbp_evidence"],
        violations=report["policy_violations"],
        notes=[
            f"required runners: {', '.join(sorted(ctbp_required_runners()))}",
            f"required kinds: {', '.join(sorted(ctbp_required_kinds()))}",
        ],
    )
    return list(report["policy_violations"])


def run_hes_contract(
    repo_root: Path,
    evidence_dir: Path,
    manifest: dict[str, Any],
    *,
    tier: str,
) -> list[str]:
    """Collect and validate HES evidence."""

    hes_dir = evidence_dir / "hes"
    safe_mkdir(hes_dir)

    contract_src = repo_root / "tests" / "uce" / "contracts" / "hes@v1.yaml"
    contract_dst = hes_dir / "hes@v1.yaml"
    shutil.copy2(contract_src, contract_dst)

    artifacts_root = repo_root / "curl" / "tests" / "http" / "gen" / "artifacts"
    report = validate_hes(contract_dst, [artifacts_root], tier)
    report_path = hes_dir / "report.json"
    write_json(report_path, report)

    add_artifact(manifest, artifact_id="hes_contract", path="hes/hes@v1.yaml", kind="contract", required=True, media_type="application/yaml")
    add_artifact(manifest, artifact_id="hes_report", path="hes/report.json", kind="report", required=True, media_type="application/json")
    add_result(
        manifest,
        result_id="hes_contract",
        kind="validator",
        result="pass" if not report["policy_violations"] else "fail",
        log_file=str(report_path),
        details={
            "required_kinds": report["required_kinds"],
            "required_runners": report["required_runners"],
            "entry_count": len(report["entries"]),
        },
    )
    add_contract(
        manifest,
        contract_id="hes@v1",
        provider="uce_hes_validator",
        result="pass" if not report["policy_violations"] else "fail",
        required=True,
        report_artifact="hes_report",
        evidence_artifacts=["hes_contract"],
        violations=report["policy_violations"],
        notes=[f"tier={tier}"],
    )
    return list(report["policy_violations"])
