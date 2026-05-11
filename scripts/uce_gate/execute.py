"""Reusable UCE gate execution steps."""

from __future__ import annotations

from pathlib import Path
from typing import Any
import os

from scripts.uce.manifest import add_artifact
from scripts.uce.manifest import add_contract
from scripts.uce.manifest import add_policy_violation
from scripts.uce.manifest import add_result
from scripts.uce_gate.planner import GateSpec
from scripts.uce_gate.runtime import GateResult
from scripts.uce_gate.runtime import record_gate_result
from scripts.uce_gate.runtime import run_gate


def run_offline_ctest_gate(
    repo_root: Path,
    build_dir: Path,
    evidence_dir: Path,
    manifest: dict[str, Any],
) -> list[GateResult]:
    """Run the default offline QtTest gate."""

    meta_dir = evidence_dir / "meta"
    logs_dir = evidence_dir / "logs"
    offline_list = run_gate(
        "ctest_list_offline",
        ["ctest", "-N", "--no-tests=error", "-L", "offline"],
        meta_dir / "ctest_list_offline.txt",
        cwd=build_dir,
    )
    offline_gate = run_gate(
        "ctest_strict_offline",
        [
            "python3",
            str(repo_root / "scripts" / "ctest_strict.py"),
            "--build-dir",
            str(build_dir),
            "--label-regex",
            "offline",
            "--max-skips",
            "0",
        ],
        logs_dir / "ctest_strict_offline.log",
        cwd=repo_root,
    )
    for result in (offline_list, offline_gate):
        record_gate_result(manifest, result)

    add_artifact(manifest, artifact_id="ctest_offline_list", path="meta/ctest_list_offline.txt", kind="report", required=True)
    add_artifact(manifest, artifact_id="ctest_offline_log", path="logs/ctest_strict_offline.log", kind="log", required=True)
    add_contract(
        manifest,
        contract_id="qtest_offline@v1",
        provider="ctest_strict",
        result="pass" if offline_gate.returncode == 0 else "fail",
        required=True,
        report_artifact="ctest_offline_log",
    )
    if offline_gate.returncode != 0:
        add_policy_violation(manifest, "gate_offline_failed")
    return [offline_list, offline_gate]


def run_libcurl_consistency_gates(
    repo_root: Path,
    build_dir: Path,
    evidence_dir: Path,
    manifest: dict[str, Any],
    tier_plan: list[GateSpec],
    httpbin_env: dict[str, str],
) -> list[GateResult]:
    """Run libcurl-consistency gates from the tier plan."""

    results: list[GateResult] = []
    logs_dir = evidence_dir / "logs"
    reports_dir = evidence_dir / "libcurl_consistency" / "reports"
    reports_dir.mkdir(parents=True, exist_ok=True)

    for gate_spec in tier_plan:
        if gate_spec.kind != "libcurl_consistency":
            continue
        suite = gate_spec.selector
        gate_result = run_gate(
            gate_spec.gate_id,
            [
                "python3",
                str(repo_root / "tests" / "libcurl_consistency" / "run_gate.py"),
                "--suite",
                suite,
                "--build",
                "--qcurl-build",
                str(build_dir),
                "--reports-dir",
                str(reports_dir),
            ],
            logs_dir / f"{gate_spec.gate_id}.log",
            cwd=repo_root,
            env=(os.environ.copy() | httpbin_env) if httpbin_env else None,
        )
        results.append(gate_result)
        record_gate_result(manifest, gate_result)
        _register_libcurl_gate_artifacts(manifest, suite, gate_spec, gate_result)
    return results


def _register_libcurl_gate_artifacts(
    manifest: dict[str, Any],
    suite: str,
    gate_spec: GateSpec,
    gate_result: GateResult,
) -> None:
    add_artifact(
        manifest,
        artifact_id=f"{suite}_gate_log",
        path=f"logs/{gate_spec.gate_id}.log",
        kind="log",
        required=True,
    )
    add_artifact(
        manifest,
        artifact_id=f"{suite}_gate_report",
        path=f"libcurl_consistency/reports/gate_{suite}.json",
        kind="report",
        required=True,
        media_type="application/json",
    )
    add_artifact(
        manifest,
        artifact_id=f"{suite}_junit_report",
        path=f"libcurl_consistency/reports/junit_{suite}.xml",
        kind="report",
        required=True,
        media_type="application/xml",
    )
    add_contract(
        manifest,
        contract_id=f"libcurl_consistency_{suite}@v1",
        provider="libcurl_consistency",
        result="pass" if gate_result.returncode == 0 else "fail",
        required=True,
        report_artifact=f"{suite}_gate_report",
    )
    if gate_result.returncode != 0:
        add_policy_violation(manifest, gate_spec.policy_code)


def run_netproof_gate(
    repo_root: Path,
    build_dir: Path,
    evidence_dir: Path,
    manifest: dict[str, Any],
) -> GateResult:
    """Run the netproof strace gate and register its contract."""

    logs_dir = evidence_dir / "logs"
    netproof_report_path = evidence_dir / "netproof" / "strace_report.json"
    netproof_trace_dir = evidence_dir / "netproof" / "trace"
    result = run_gate(
        "netproof_strace_gate",
        [
            "python3",
            str(repo_root / "scripts" / "netproof_strace_gate.py"),
            "--build-dir",
            str(build_dir),
            "--report",
            str(netproof_report_path),
            "--trace-dir",
            str(netproof_trace_dir),
        ],
        logs_dir / "netproof_strace_gate.log",
        cwd=repo_root,
    )
    record_gate_result(manifest, result)
    return result


def register_netproof_contract(
    manifest: dict[str, Any],
    *,
    result: GateResult,
    netproof_report: dict[str, Any],
) -> None:
    """Register netproof artifacts, result, contract, and policy violations."""

    add_artifact(
        manifest,
        artifact_id="netproof_strace_report",
        path="netproof/strace_report.json",
        kind="report",
        required=True,
        media_type="application/json",
    )
    add_artifact(
        manifest,
        artifact_id="netproof_trace_dir",
        path="netproof/trace",
        kind="evidence",
        required=True,
    )
    add_artifact(
        manifest,
        artifact_id="netproof_strace_log",
        path="logs/netproof_strace_gate.log",
        kind="log",
        required=True,
    )
    add_contract(
        manifest,
        contract_id="netproof@v1",
        provider="netproof_strace_gate",
        result="pass" if result.returncode == 0 else "fail",
        required=True,
        report_artifact="netproof_strace_report",
        evidence_artifacts=["netproof_trace_dir", "netproof_capabilities"],
        violations=list(netproof_report.get("policy_violations") or []),
        notes=["subject=ctest_strict offline under strace trace=network"],
    )
    for code in netproof_report.get("policy_violations", []):
        add_policy_violation(manifest, code)


def register_redaction_result(
    manifest: dict[str, Any],
    *,
    redaction_report: dict[str, Any],
    redaction_report_path: Path,
) -> None:
    """Register redaction report artifacts, result, and contract."""

    passed = not redaction_report["violations"] and not redaction_report["missing_roots"]
    add_artifact(
        manifest,
        artifact_id="redaction_report",
        path="reports/redaction_gate.json",
        kind="report",
        required=True,
        media_type="application/json",
    )
    add_result(
        manifest,
        result_id="redaction_gate",
        kind="validator",
        result="pass" if passed else "fail",
        log_file=str(redaction_report_path),
    )
    add_contract(
        manifest,
        contract_id="redaction@v1",
        provider="redaction_gate",
        result="pass" if passed else "fail",
        required=True,
        report_artifact="redaction_report",
        notes=["scan missing 或 violation 均按 fail-closed 处理"],
    )
    if not passed:
        add_policy_violation(manifest, "redaction")
