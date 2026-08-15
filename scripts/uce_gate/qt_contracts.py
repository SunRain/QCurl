"""Qt-test based backpressure contract runner."""

from __future__ import annotations

from pathlib import Path
from typing import Any
import os
import shutil

from scripts.uce.manifest import add_artifact
from scripts.uce.manifest import add_contract
from scripts.uce.manifest import add_result
from scripts.uce_gate.runtime import GateResult
from scripts.uce_gate.runtime import resolve_qt_test_binary
from scripts.uce_gate.runtime import run_gate
from scripts.uce_gate.runtime import safe_mkdir
from scripts.uce_gate.runtime import utc_now_iso
from scripts.uce_gate.runtime import write_json
from tests.uce.bp.validate import validate_bp


def _qt_test_args(test_function: str) -> list[str]:
    return ["-o", "-,txt", test_function]


def _qt_test_env(runtime_env: dict[str, str] | None) -> dict[str, str]:
    env = os.environ.copy()
    env.update(runtime_env or {})
    return env


def _register_bp_artifacts(manifest: dict[str, Any], run_id: str) -> None:
    add_artifact(manifest, artifact_id="bp_contract", path="bp/bp@v1.yaml", kind="contract", required=True, media_type="application/yaml")
    add_artifact(manifest, artifact_id="bp_report", path="bp/report.json", kind="report", required=True, media_type="application/json")
    add_artifact(manifest, artifact_id="bp_evidence_dir", path=f"test-artifacts/bp/{run_id}", kind="evidence", required=True)
    add_artifact(manifest, artifact_id="bp_gate_log", path="logs/bp_testAsyncDownloadBackpressure.log", kind="log", required=True)


def _register_bp_result(
    manifest: dict[str, Any],
    report_path: Path,
    policy_codes: set[str],
    details: dict[str, Any],
    notes: list[str],
) -> None:
    result = "pass" if not policy_codes else "fail"
    add_result(
        manifest,
        result_id="bp_contract",
        kind="validator",
        result=result,
        log_file=str(report_path),
        details=details,
    )
    add_contract(
        manifest,
        contract_id="bp@v1",
        provider="uce_bp_validator",
        result=result,
        required=True,
        report_artifact="bp_report",
        evidence_artifacts=["bp_contract", "bp_evidence_dir", "bp_gate_log"],
        violations=sorted(policy_codes),
        notes=notes,
    )


def _missing_bp_binary_result(
    bp_dir: Path,
    manifest: dict[str, Any],
    *,
    tier: str,
    qt_test_binary: Path,
    out_root: Path,
) -> tuple[list[GateResult], list[str]]:
    policy_codes = {"bp_binary_missing"}
    report_path = bp_dir / "report.json"
    write_json(
        report_path,
        {
            "generated_at_utc": utc_now_iso(),
            "tier": tier,
            "qt_test_binary": str(qt_test_binary),
            "out_root": str(out_root),
            "policy_violations": sorted(policy_codes),
        },
    )
    _register_bp_result(
        manifest,
        report_path,
        policy_codes,
        {"qt_test_binary": str(qt_test_binary)},
        [f"tier={tier}", "qt test binary missing"],
    )
    return [], sorted(policy_codes)


def _run_bp_qt_test(
    repo_root: Path,
    logs_dir: Path,
    qt_test_binary: Path,
    out_root: Path,
    runtime_env: dict[str, str] | None,
) -> GateResult:
    safe_mkdir(out_root)
    env = _qt_test_env(runtime_env)
    env["QCURL_LC_OUT_DIR"] = str(out_root)
    return run_gate(
        "bp_testAsyncDownloadBackpressure",
        [str(qt_test_binary), *_qt_test_args("testAsyncDownloadBackpressure")],
        logs_dir / "bp_testAsyncDownloadBackpressure.log",
        cwd=repo_root,
        env=env,
    )


def _build_bp_report(
    contract_path: Path,
    out_root: Path,
    qt_test_binary: Path,
    gate_result: GateResult,
    policy_codes: set[str],
    *,
    tier: str,
    run_id: str,
) -> tuple[dict[str, Any], list[Path]]:
    report = validate_bp(contract_path, [out_root])
    report["tier"] = tier
    report["qt_test_binary"] = str(qt_test_binary)
    report["out_root"] = str(out_root)
    report["gate"] = {
        "returncode": gate_result.returncode,
        "duration_s": gate_result.duration_s,
        "log_path": str(Path("logs") / "bp_testAsyncDownloadBackpressure.log"),
    }
    evidence_files = sorted(out_root.rglob("dci_evidence_*.jsonl"))
    report["evidence_files"] = [
        str(Path("test-artifacts") / "bp" / run_id / "testAsyncDownloadBackpressure" / path.name)
        for path in evidence_files
    ]
    policy_codes.update(
        code
        for code in report.get("policy_violations", [])
        if isinstance(code, str) and code
    )
    report["policy_violations"] = sorted(policy_codes)
    return report, evidence_files


def _prepare_bp_contract(
    repo_root: Path,
    build_dir: Path,
    evidence_dir: Path,
    manifest: dict[str, Any],
    run_id: str,
) -> tuple[Path, Path, Path, Path, Path]:
    bp_dir = evidence_dir / "bp"
    logs_dir = evidence_dir / "logs"
    safe_mkdir(bp_dir)
    contract_dst = bp_dir / "bp@v1.yaml"
    shutil.copy2(repo_root / "tests" / "uce" / "contracts" / "bp@v1.yaml", contract_dst)
    _register_bp_artifacts(manifest, run_id)
    qt_test_binary = resolve_qt_test_binary(build_dir, "tst_QCNetworkReply")
    out_root = build_dir / "test-artifacts" / "bp" / run_id / "testAsyncDownloadBackpressure"
    if out_root.exists():
        shutil.rmtree(out_root)
    return bp_dir, logs_dir, contract_dst, qt_test_binary, out_root


def _finish_bp_contract(
    bp_dir: Path,
    contract_dst: Path,
    out_root: Path,
    qt_test_binary: Path,
    manifest: dict[str, Any],
    gate_result: GateResult,
    *,
    tier: str,
    run_id: str,
) -> tuple[list[GateResult], list[str]]:
    policy_codes = {"bp_test_run_failed"} if gate_result.returncode != 0 else set()
    report, evidence_files = _build_bp_report(
        contract_dst,
        out_root,
        qt_test_binary,
        gate_result,
        policy_codes,
        tier=tier,
        run_id=run_id,
    )
    report_path = bp_dir / "report.json"
    write_json(report_path, report)
    _register_bp_result(
        manifest,
        report_path,
        policy_codes,
        {
            "qt_test_binary": str(qt_test_binary),
            "returncode": gate_result.returncode,
            "evidence_files": len(evidence_files),
        },
        [f"tier={tier}", "evidence schema=qcurl-uce/dci-evidence@v1 stream=bp-user-pause"],
    )
    return [gate_result], sorted(policy_codes)


def run_bp_contract(
    repo_root: Path,
    build_dir: Path,
    evidence_dir: Path,
    manifest: dict[str, Any],
    *,
    tier: str,
    run_id: str,
    runtime_env: dict[str, str] | None = None,
) -> tuple[list[GateResult], list[str]]:
    """Run and validate backpressure contract evidence."""

    bp_dir, logs_dir, contract_dst, qt_test_binary, out_root = _prepare_bp_contract(
        repo_root,
        build_dir,
        evidence_dir,
        manifest,
        run_id,
    )

    if not qt_test_binary.exists():
        return _missing_bp_binary_result(
            bp_dir,
            manifest,
            tier=tier,
            qt_test_binary=qt_test_binary,
            out_root=out_root,
        )

    gate_result = _run_bp_qt_test(repo_root, logs_dir, qt_test_binary, out_root, runtime_env)
    return _finish_bp_contract(
        bp_dir,
        contract_dst,
        out_root,
        qt_test_binary,
        manifest,
        gate_result,
        tier=tier,
        run_id=run_id,
    )
