"""UCE gate orchestration helpers."""

from __future__ import annotations

from pathlib import Path
from typing import Any
import platform
import sys

from scripts.netproof_capabilities import build_report
from scripts.uce.manifest import add_artifact
from scripts.uce.manifest import add_policy_violation
from scripts.uce.manifest import add_result
from scripts.uce.manifest import set_capability
from scripts.uce.redaction_gate import scan_paths
from scripts.uce_gate.contracts import run_ctbp_contract
from scripts.uce_gate.contracts import run_hes_contract
from scripts.uce_gate.contracts import run_timeline_contract
from scripts.uce_gate.evidence import EvidenceLayout
from scripts.uce_gate.evidence import create_gate_manifest
from scripts.uce_gate.evidence import load_json_if_exists
from scripts.uce_gate.evidence import package_evidence_bundle
from scripts.uce_gate.evidence import prepare_evidence_layout
from scripts.uce_gate.evidence import resolve_evidence_layout
from scripts.uce_gate.evidence import write_manifest_and_policy_report
from scripts.uce_gate.execute import register_netproof_contract
from scripts.uce_gate.execute import register_redaction_result
from scripts.uce_gate.execute import run_libcurl_consistency_gates
from scripts.uce_gate.execute import run_netproof_gate
from scripts.uce_gate.execute import run_offline_ctest_gate
from scripts.uce_gate.httpbin import run_httpbin_gate
from scripts.uce_gate.planner import build_tier_plan
from scripts.uce_gate.planner import validate_required_artifacts
from scripts.uce_gate.qt_contracts import run_bp_contract
from scripts.uce_gate.qt_contracts import run_dci_seed_suite
from scripts.uce_gate.runtime import GateResult
from scripts.uce_gate.runtime import best_effort_copy_glob
from scripts.uce_gate.runtime import best_effort_copytree
from scripts.uce_gate.runtime import collect_versions
from scripts.uce_gate.runtime import record_gate_result
from scripts.uce_gate.runtime import write_json
from scripts.uce_gate.runtime import write_text


def _write_versions_and_capabilities(
    *,
    repo_root: Path,
    layout: EvidenceLayout,
    manifest: dict[str, Any],
    tier: str,
) -> None:
    write_text(layout.meta_dir / "versions.txt", collect_versions(repo_root))

    capability_report = build_report(selected_tier=tier)
    capability_path = layout.netproof_dir / "capabilities.json"
    write_json(capability_path, capability_report)
    add_artifact(
        manifest,
        artifact_id="netproof_capabilities",
        path="netproof/capabilities.json",
        kind="capability",
        required=True,
        media_type="application/json",
    )
    add_result(manifest, result_id="netproof_capabilities", kind="capability", result="pass", log_file=str(capability_path))
    set_capability(manifest, "netproof", capability_report)
    if capability_report["tiers"][tier]["missing_required_providers"]:
        add_policy_violation(manifest, "capability_required_provider_missing")


def _run_required_gates(
    *,
    repo_root: Path,
    build_dir: Path,
    layout: EvidenceLayout,
    manifest: dict[str, Any],
    tier: str,
) -> tuple[list[GateResult], dict[str, str]]:
    results = run_offline_ctest_gate(repo_root, build_dir, layout.evidence_dir, manifest)
    httpbin_env: dict[str, str] = {}
    tier_plan = build_tier_plan(tier)

    if any(item.requires_httpbin for item in tier_plan):
        httpbin_env, env_results, env_violations = run_httpbin_gate(repo_root, build_dir, layout.evidence_dir, manifest)
        results.extend(env_results)
        for code in env_violations:
            add_policy_violation(manifest, code)

    results.extend(run_libcurl_consistency_gates(repo_root, build_dir, layout.evidence_dir, manifest, tier_plan, httpbin_env))
    return results, httpbin_env


def _run_nightly_gates(
    *,
    repo_root: Path,
    build_dir: Path,
    layout: EvidenceLayout,
    manifest: dict[str, Any],
    tier: str,
    run_id: str,
) -> list[GateResult]:
    if tier not in {"nightly", "soak"}:
        return []

    results: list[GateResult] = []
    dci_results, dci_violations = run_dci_seed_suite(repo_root, build_dir, layout.evidence_dir, manifest, tier=tier, run_id=run_id)
    bp_results, bp_violations = run_bp_contract(repo_root, build_dir, layout.evidence_dir, manifest, tier=tier, run_id=run_id)
    results.extend(dci_results)
    results.extend(bp_results)
    for gate_result in dci_results + bp_results:
        record_gate_result(manifest, gate_result)
    for code in dci_violations + bp_violations:
        add_policy_violation(manifest, code)

    netproof_result = run_netproof_gate(repo_root, build_dir, layout.evidence_dir, manifest)
    results.append(netproof_result)
    register_netproof_contract(
        manifest,
        result=netproof_result,
        netproof_report=load_json_if_exists(layout.netproof_dir / "strace_report.json"),
    )
    return results


def _copy_optional_evidence(repo_root: Path, build_dir: Path, layout: EvidenceLayout, manifest: dict[str, Any]) -> None:
    if best_effort_copytree(build_dir / "test-artifacts", layout.evidence_dir / "test-artifacts"):
        add_artifact(manifest, artifact_id="test_artifacts_dir", path="test-artifacts", kind="evidence", required=False)

    copied_service_logs = best_effort_copy_glob(
        repo_root / "curl" / "tests" / "http" / "gen" / "artifacts",
        "**/service_logs/**",
        layout.lc_dir / "service_logs",
    )
    if copied_service_logs:
        add_artifact(
            manifest,
            artifact_id="service_logs_dir",
            path="libcurl_consistency/service_logs",
            kind="evidence",
            required=False,
        )


def _run_contract_validators(
    *,
    repo_root: Path,
    build_dir: Path,
    layout: EvidenceLayout,
    manifest: dict[str, Any],
    tier: str,
) -> None:
    validators = run_timeline_contract(repo_root, build_dir, layout.evidence_dir, manifest, tier=tier)
    if tier in {"nightly", "soak"}:
        validators.extend(run_ctbp_contract(repo_root, layout.evidence_dir, manifest))
    validators.extend(run_hes_contract(repo_root, layout.evidence_dir, manifest, tier=tier))
    for code in validators:
        add_policy_violation(manifest, code)


def _run_redaction_gate(layout: EvidenceLayout, manifest: dict[str, Any]) -> None:
    redaction_report = scan_paths([layout.evidence_dir])
    redaction_report_path = layout.reports_dir / "redaction_gate.json"
    write_json(redaction_report_path, redaction_report)
    register_redaction_result(
        manifest,
        redaction_report=redaction_report,
        redaction_report_path=redaction_report_path,
    )


def _write_validated_state(layout: EvidenceLayout, manifest: dict[str, Any], tier: str) -> list[str]:
    missing_required = validate_required_artifacts(manifest, layout.evidence_dir)
    if missing_required:
        add_policy_violation(manifest, "artifact_required_missing")
    write_manifest_and_policy_report(
        layout=layout,
        manifest=manifest,
        tier=tier,
        missing_required_artifacts=missing_required,
    )
    return missing_required


def run_uce_gate(
    *,
    repo_root: Path,
    build_dir: Path,
    tier: str,
    run_id: str,
    evidence_root_arg: str,
) -> int:
    """Run the UCE evidence gate and return the legacy CLI status code."""

    layout = resolve_evidence_layout(repo_root, build_dir, run_id=run_id, evidence_root_arg=evidence_root_arg)
    prepare_evidence_layout(layout)
    manifest = create_gate_manifest(
        repo_root=repo_root,
        build_dir=build_dir,
        layout=layout,
        tier=tier,
        run_id=run_id,
        environment={"platform": platform.platform(), "python": sys.version},
    )

    _write_versions_and_capabilities(repo_root=repo_root, layout=layout, manifest=manifest, tier=tier)
    _run_required_gates(repo_root=repo_root, build_dir=build_dir, layout=layout, manifest=manifest, tier=tier)
    _run_nightly_gates(repo_root=repo_root, build_dir=build_dir, layout=layout, manifest=manifest, tier=tier, run_id=run_id)
    _copy_optional_evidence(repo_root, build_dir, layout, manifest)
    _run_contract_validators(repo_root=repo_root, build_dir=build_dir, layout=layout, manifest=manifest, tier=tier)
    write_manifest_and_policy_report(layout=layout, manifest=manifest, tier=tier)

    _run_redaction_gate(layout, manifest)
    _write_validated_state(layout, manifest, tier)
    package_evidence_bundle(layout, manifest)
    _write_validated_state(layout, manifest, tier)

    return 0 if manifest["result"] == "pass" else 3
