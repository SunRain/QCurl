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
from scripts.uce_gate.dci_contract import run_dci_seed_suite
from scripts.uce_gate.evidence import EvidenceLayout
from scripts.uce_gate.evidence import create_gate_manifest
from scripts.uce_gate.evidence import load_json_if_exists
from scripts.uce_gate.evidence import prepare_evidence_layout
from scripts.uce_gate.evidence import register_candidate_fingerprint
from scripts.uce_gate.evidence import resolve_evidence_layout
from scripts.uce_gate.execute import register_netproof_contract
from scripts.uce_gate.execute import run_libcurl_consistency_gates
from scripts.uce_gate.execute import run_netproof_gate
from scripts.uce_gate.execute import run_offline_ctest_gate
from scripts.uce_gate.finalize import finalize_gate_evidence
from scripts.uce_gate.finalize import record_gate_exception
from scripts.uce_gate.finalize import save_failure_evidence
from scripts.uce_gate.httpbin import run_httpbin_gate
from scripts.uce_gate.httpbin import stop_httpbin_gate
from scripts.uce_gate.planner import build_tier_plan
from scripts.uce_gate.qt_contracts import run_bp_contract
from scripts.uce_gate.runtime import GateResult
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
    run_id: str,
) -> tuple[list[GateResult], dict[str, str], list[Path]]:
    results = run_offline_ctest_gate(repo_root, build_dir, layout.evidence_dir, manifest)
    httpbin_env: dict[str, str] = {}
    tier_plan = build_tier_plan(tier)

    if any(item.requires_httpbin for item in tier_plan):
        httpbin_env, env_results, env_violations = run_httpbin_gate(
            repo_root,
            build_dir,
            layout.evidence_dir,
            manifest,
            stop_after_gate=False,
        )
        results.extend(env_results)
        for code in env_violations:
            add_policy_violation(manifest, code)

    consistency_results, artifact_roots = run_libcurl_consistency_gates(
        repo_root,
        build_dir,
        layout.evidence_dir,
        manifest,
        tier_plan,
        httpbin_env,
        uce_run_id=run_id,
    )
    results.extend(consistency_results)
    return results, httpbin_env, artifact_roots


def _run_nightly_gates(
    *,
    repo_root: Path,
    build_dir: Path,
    layout: EvidenceLayout,
    manifest: dict[str, Any],
    tier: str,
    run_id: str,
    runtime_env: dict[str, str],
) -> list[GateResult]:
    if tier not in {"nightly", "soak"}:
        return []

    results: list[GateResult] = []
    dci_results, dci_violations = run_dci_seed_suite(
        repo_root,
        build_dir,
        layout.evidence_dir,
        manifest,
        tier=tier,
        run_id=run_id,
        runtime_env=runtime_env,
    )
    bp_results, bp_violations = run_bp_contract(
        repo_root,
        build_dir,
        layout.evidence_dir,
        manifest,
        tier=tier,
        run_id=run_id,
        runtime_env=runtime_env,
    )
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


def _stop_httpbin_safely(
    repo_root: Path, layout: EvidenceLayout, manifest: dict[str, Any], httpbin_env: dict[str, str],
) -> None:
    try:
        stop_result = stop_httpbin_gate(repo_root, layout.evidence_dir, manifest, httpbin_env)
        if stop_result.returncode != 0:
            add_policy_violation(manifest, "env_preflight_httpbin_stop_failed")
    except Exception as exc:
        add_policy_violation(manifest, "env_preflight_httpbin_stop_failed")
        record_gate_exception(layout, manifest, exc)


def _run_gate_workload(
    *,
    repo_root: Path,
    build_dir: Path,
    layout: EvidenceLayout,
    manifest: dict[str, Any],
    tier: str,
    run_id: str,
) -> list[Path]:
    needs_httpbin = any(item.requires_httpbin for item in build_tier_plan(tier))
    httpbin_env: dict[str, str] = {}
    artifact_roots: list[Path] = []
    try:
        _, httpbin_env, artifact_roots = _run_required_gates(
            repo_root=repo_root,
            build_dir=build_dir,
            layout=layout,
            manifest=manifest,
            tier=tier,
            run_id=run_id,
        )
        _run_nightly_gates(
            repo_root=repo_root,
            build_dir=build_dir,
            layout=layout,
            manifest=manifest,
            tier=tier,
            run_id=run_id,
            runtime_env=httpbin_env,
        )
    except Exception as exc:
        record_gate_exception(layout, manifest, exc)
    finally:
        if needs_httpbin:
            _stop_httpbin_safely(repo_root, layout, manifest, httpbin_env)
    return artifact_roots


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
    try:
        prepare_evidence_layout(layout)
    except OSError as exc:
        print(f"[uce_gate] 无法创建本次证据目录，未启动门禁: {exc}", file=sys.stderr)
        return 3
    manifest = create_gate_manifest(
        repo_root=repo_root,
        build_dir=build_dir,
        layout=layout,
        tier=tier,
        run_id=run_id,
        environment={"platform": platform.platform(), "python": sys.version},
    )

    artifact_roots: list[Path] = []
    try:
        _write_versions_and_capabilities(repo_root=repo_root, layout=layout, manifest=manifest, tier=tier)
        register_candidate_fingerprint(repo_root=repo_root, layout=layout, manifest=manifest)
        artifact_roots = _run_gate_workload(
            repo_root=repo_root,
            build_dir=build_dir,
            layout=layout,
            manifest=manifest,
            tier=tier,
            run_id=run_id,
        )
    except Exception as exc:
        record_gate_exception(layout, manifest, exc)

    try:
        finalize_gate_evidence(
            repo_root=repo_root,
            build_dir=build_dir,
            layout=layout,
            manifest=manifest,
            tier=tier,
            run_id=run_id,
            artifact_roots=artifact_roots,
        )
    except Exception as exc:
        record_gate_exception(layout, manifest, exc)
        save_failure_evidence(layout, manifest, tier)

    return 0 if manifest["result"] == "pass" else 3
