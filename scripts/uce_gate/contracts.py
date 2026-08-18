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
from tests.uce.timeline.parser import merge_timeline_parse_errors
from tests.uce.timeline.validate import validate_timelines


def _collect_timeline_evidence(
    build_dir: Path,
    timeline_dir: Path,
    run_id: str,
    artifact_roots: list[Path],
) -> tuple[Path, Path, dict[str, Any], dict[str, Any]]:
    qt_artifacts_root = build_dir / "test-artifacts"
    current_run_roots = [
        qt_artifacts_root / "dci" / run_id,
        qt_artifacts_root / "bp" / run_id,
    ]
    lc_collection = _merge_timeline_collections(
        "libcurl_consistency",
        artifact_roots,
        [collect_from_lc(root) for root in artifact_roots],
    )
    qt_collection = _merge_timeline_collections(
        "qt",
        artifact_roots,
        [
            collect_from_qt(
                root,
                dci_evidence_roots=current_run_roots if index == 0 else [],
            )
            for index, root in enumerate(artifact_roots)
        ],
    )

    lc_timeline_path = timeline_dir / "libcurl_consistency.timeline.jsonl"
    qt_timeline_path = timeline_dir / "qt.timeline.jsonl"
    write_timeline_jsonl(lc_timeline_path, lc_collection["events"])
    write_timeline_jsonl(qt_timeline_path, qt_collection["events"])
    return lc_timeline_path, qt_timeline_path, lc_collection, qt_collection


def _merge_timeline_collections(
    provider: str,
    artifact_roots: list[Path],
    collections: list[dict[str, Any]],
) -> dict[str, Any]:
    """Merge per-suite collectors without scanning any global artifact root."""

    scoped_events: list[dict[str, Any]] = []
    for root, collection in zip(artifact_roots, collections):
        run_id = root.parent.name
        for event in collection.get("events", []):
            scoped_event = dict(event)
            stream_id = str(scoped_event.get("stream_id") or "")
            if stream_id:
                scoped_event["stream_id"] = f"{run_id}:{stream_id}"
            scoped_events.append(scoped_event)

    return {
        "provider": provider,
        "artifact_roots": [str(root) for root in artifact_roots],
        "stream_count": sum(int(item.get("stream_count") or 0) for item in collections),
        "event_count": sum(int(item.get("event_count") or 0) for item in collections),
        "source_files": [
            str(source)
            for item in collections
            for source in item.get("source_files", [])
        ],
        "missing_roots": [
            str(root)
            for item in collections
            for root in item.get("missing_roots", [])
        ],
        "errors": [error for item in collections for error in item.get("errors", [])],
        "events": scoped_events,
    }


def _register_timeline_result(
    manifest: dict[str, Any],
    report: dict[str, Any],
    report_path: Path,
    required_providers: set[str],
    tier: str,
) -> None:
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


def run_timeline_contract(
    repo_root: Path,
    build_dir: Path,
    evidence_dir: Path,
    manifest: dict[str, Any],
    *,
    tier: str,
    run_id: str,
    artifact_roots: list[Path],
) -> list[str]:
    """Collect and validate timeline evidence."""

    timeline_dir = evidence_dir / "timeline"
    safe_mkdir(timeline_dir)

    contract_src = repo_root / "tests" / "uce" / "contracts" / "timeline@v1.yaml"
    contract_dst = timeline_dir / "timeline@v1.yaml"
    shutil.copy2(contract_src, contract_dst)

    lc_timeline_path, qt_timeline_path, lc_collection, qt_collection = _collect_timeline_evidence(
        build_dir,
        timeline_dir,
        run_id,
        artifact_roots,
    )

    required_providers = timeline_required_providers(tier)
    report = validate_timelines(contract_dst, [lc_timeline_path, qt_timeline_path], required_providers)
    merge_timeline_parse_errors(
        report,
        list(lc_collection.get("errors", [])) + list(qt_collection.get("errors", [])),
    )
    report["collections"] = {
        "libcurl_consistency": {key: value for key, value in lc_collection.items() if key != "events"},
        "qt": {key: value for key, value in qt_collection.items() if key != "events"},
    }
    report_path = timeline_dir / "report.json"
    write_json(report_path, report)

    _register_timeline_result(manifest, report, report_path, required_providers, tier)
    return list(report["policy_violations"])


def _register_ctbp_result(
    manifest: dict[str, Any],
    report: dict[str, Any],
    report_path: Path,
) -> None:
    required_runners = ctbp_required_runners()
    required_kinds = ctbp_required_kinds()
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
            "required_runners": sorted(required_runners),
            "required_kinds": sorted(required_kinds),
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
            f"required runners: {', '.join(sorted(required_runners))}",
            f"required kinds: {', '.join(sorted(required_kinds))}",
        ],
    )


def run_ctbp_contract(
    repo_root: Path,
    evidence_dir: Path,
    manifest: dict[str, Any],
    *,
    artifact_roots: list[Path],
) -> list[str]:
    """Collect and validate CTBP evidence."""

    ctbp_dir = evidence_dir / "ctbp"
    safe_mkdir(ctbp_dir)

    contract_src = repo_root / "tests" / "uce" / "contracts" / "ctbp@v1.yaml"
    contract_dst = ctbp_dir / "ctbp@v1.yaml"
    shutil.copy2(contract_src, contract_dst)

    report = validate_ctbp(
        contract_dst,
        artifact_roots,
        ctbp_required_runners(),
        ctbp_required_kinds(),
    )

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
    _register_ctbp_result(manifest, report, report_path)
    return list(report["policy_violations"])


def run_hes_contract(
    repo_root: Path,
    evidence_dir: Path,
    manifest: dict[str, Any],
    *,
    tier: str,
    artifact_roots: list[Path],
) -> list[str]:
    """Collect and validate HES evidence."""

    hes_dir = evidence_dir / "hes"
    safe_mkdir(hes_dir)

    contract_src = repo_root / "tests" / "uce" / "contracts" / "hes@v1.yaml"
    contract_dst = hes_dir / "hes@v1.yaml"
    shutil.copy2(contract_src, contract_dst)

    report = validate_hes(contract_dst, artifact_roots, tier)
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
