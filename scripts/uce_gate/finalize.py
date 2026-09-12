"""UCE 合同校验、异常收口和归档终态。"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

from scripts.uce.manifest import add_artifact, add_policy_violation, add_result
from scripts.uce.redaction_gate import scan_paths
from scripts.uce_gate.archive_validation import validate_archive
from scripts.uce_gate.contracts import run_ctbp_contract, run_hes_contract, run_timeline_contract
from scripts.uce_gate.evidence import EvidenceLayout, package_evidence_bundle
from scripts.uce_gate.evidence import write_manifest_and_policy_report
from scripts.uce_gate.execute import register_redaction_result
from scripts.uce_gate.planner import validate_required_artifacts
from scripts.uce_gate.runtime import best_effort_copy_glob, best_effort_copytree, write_json, write_text


def _copy_optional_evidence(repo_root: Path, build_dir: Path, layout: EvidenceLayout,
                            manifest: dict[str, Any]) -> None:
    if best_effort_copytree(build_dir / "test-artifacts", layout.evidence_dir / "test-artifacts"):
        add_artifact(manifest, artifact_id="test_artifacts_dir", path="test-artifacts",
                     kind="evidence", required=False)

    copied_service_logs = best_effort_copy_glob(
        repo_root / "curl" / "tests" / "http" / "gen" / "artifacts",
        "**/service_logs/**",
        layout.lc_dir / "service_logs",
    )
    if copied_service_logs:
        add_artifact(manifest, artifact_id="service_logs_dir",
                     path="libcurl_consistency/service_logs", kind="evidence", required=False)


def _run_contract_validators(
    *, repo_root: Path, build_dir: Path, layout: EvidenceLayout, manifest: dict[str, Any],
    tier: str, run_id: str, artifact_roots: list[Path],
) -> None:
    violations = run_timeline_contract(
        repo_root, build_dir, layout.evidence_dir, manifest,
        tier=tier, run_id=run_id, artifact_roots=artifact_roots,
    )
    if tier in {"nightly", "soak"}:
        violations.extend(run_ctbp_contract(
            repo_root, layout.evidence_dir, manifest, artifact_roots=artifact_roots,
        ))
    violations.extend(run_hes_contract(
        repo_root, layout.evidence_dir, manifest, tier=tier, artifact_roots=artifact_roots,
    ))
    for code in violations:
        add_policy_violation(manifest, code)


def _run_redaction_gate(layout: EvidenceLayout, manifest: dict[str, Any]) -> None:
    report = scan_paths([layout.evidence_dir])
    report_path = layout.reports_dir / "redaction_gate.json"
    write_json(report_path, report)
    register_redaction_result(manifest, redaction_report=report, redaction_report_path=report_path)


def write_validated_state(layout: EvidenceLayout, manifest: dict[str, Any], tier: str) -> list[str]:
    """校验必需工件并保存机器可判定的终态。"""

    missing = validate_required_artifacts(manifest, layout.evidence_dir)
    if missing:
        add_policy_violation(manifest, "artifact_required_missing")
    write_manifest_and_policy_report(
        layout=layout, manifest=manifest, tier=tier, missing_required_artifacts=missing,
    )
    return missing


def record_gate_exception(layout: EvidenceLayout, manifest: dict[str, Any], exc: Exception) -> None:
    """先登记失败，再尝试写诊断日志，避免日志故障丢失原始错误。"""

    detail = {"type": type(exc).__name__, "message": str(exc)}
    manifest.setdefault("exception", detail)
    manifest.setdefault("exceptions", []).append(detail)
    add_policy_violation(manifest, "gate_exception")
    manifest["result"] = "fail"
    path = layout.logs_dir / "gate_exception.log"
    add_artifact(manifest, artifact_id="gate_exception_log", path="logs/gate_exception.log",
                 kind="log", required=True)
    add_result(manifest, result_id="gate_exception", kind="gate", result="fail",
               log_file=str(path), details={"exception_type": type(exc).__name__})
    try:
        write_text(path, json.dumps(manifest["exceptions"], ensure_ascii=False, indent=2) + "\n")
    except OSError as log_error:
        manifest["exception_log_error"] = str(log_error)
        print(f"[uce_gate] 诊断日志无法写入: {log_error}", file=sys.stderr)


def _persist_failure_state(layout: EvidenceLayout, manifest: dict[str, Any], tier: str) -> None:
    try:
        write_validated_state(layout, manifest, tier)
        return
    except Exception as exc:
        record_gate_exception(layout, manifest, exc)
    try:
        write_manifest_and_policy_report(layout=layout, manifest=manifest, tier=tier)
    except Exception as exc:
        print(f"[uce_gate] 失败证据无法完整写入: {type(exc).__name__}: {exc}", file=sys.stderr)


def save_failure_evidence(layout: EvidenceLayout, manifest: dict[str, Any], tier: str) -> None:
    """保存失败快照，至多补打包一次；元数据故障不阻止收集可用诊断。"""

    for _ in range(2):
        _persist_failure_state(layout, manifest, tier)
        try:
            _run_redaction_gate(layout, manifest)
        except Exception as exc:
            record_gate_exception(layout, manifest, exc)
        _persist_failure_state(layout, manifest, tier)
        snapshot = json.dumps(manifest, sort_keys=True)
        try:
            package_evidence_bundle(layout, manifest)
        except Exception as exc:
            record_gate_exception(layout, manifest, exc)
        if json.dumps(manifest, sort_keys=True) == snapshot:
            return

    # 连续出现新的打包故障时，不留下声称旧状态的归档。
    for path in (layout.tar_path, layout.archive_envelope_path):
        try:
            path.unlink(missing_ok=True)
        except OSError as exc:
            record_gate_exception(layout, manifest, exc)
    _persist_failure_state(layout, manifest, tier)


def _package_final_state(layout: EvidenceLayout, manifest: dict[str, Any]) -> None:
    snapshot = json.dumps(manifest, sort_keys=True)
    package_evidence_bundle(layout, manifest)
    if validate_required_artifacts(manifest, layout.evidence_dir):
        add_policy_violation(manifest, "artifact_required_missing")
    if json.dumps(manifest, sort_keys=True) != snapshot:
        raise RuntimeError("归档阶段新增失败，必须重新保存失败快照")
    report = validate_archive(layout.evidence_root, layout.evidence_dir.name)
    if not report["archive_valid"]:
        raise RuntimeError("归档校验失败: " + "; ".join(report["errors"]))


def finalize_gate_evidence(
    *, repo_root: Path, build_dir: Path, layout: EvidenceLayout, manifest: dict[str, Any],
    tier: str, run_id: str, artifact_roots: list[Path],
) -> None:
    """合并当前运行的合同与诊断证据，校验后落盘并归档。"""

    _copy_optional_evidence(repo_root, build_dir, layout, manifest)
    _run_contract_validators(
        repo_root=repo_root, build_dir=build_dir, layout=layout, manifest=manifest,
        tier=tier, run_id=run_id, artifact_roots=artifact_roots,
    )
    write_manifest_and_policy_report(layout=layout, manifest=manifest, tier=tier)
    _run_redaction_gate(layout, manifest)
    write_validated_state(layout, manifest, tier)
    _package_final_state(layout, manifest)
