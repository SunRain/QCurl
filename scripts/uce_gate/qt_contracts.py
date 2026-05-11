"""Qt-test based UCE contract runners."""

from __future__ import annotations

from pathlib import Path
from typing import Any
import os
import shutil

from scripts.uce.manifest import add_artifact
from scripts.uce.manifest import add_contract
from scripts.uce.manifest import add_result
from scripts.uce_gate.planner import dci_seed_matrix
from scripts.uce_gate.runtime import GateResult
from scripts.uce_gate.runtime import resolve_qt_test_binary
from scripts.uce_gate.runtime import run_gate
from scripts.uce_gate.runtime import safe_mkdir
from scripts.uce_gate.runtime import utc_now_iso
from scripts.uce_gate.runtime import write_json
from tests.uce.bp.validate import validate_bp


def run_bp_contract(
    repo_root: Path,
    build_dir: Path,
    evidence_dir: Path,
    manifest: dict[str, Any],
    *,
    tier: str,
    run_id: str,
) -> tuple[list[GateResult], list[str]]:
    """Run and validate backpressure contract evidence."""

    bp_dir = evidence_dir / "bp"
    logs_dir = evidence_dir / "logs"
    safe_mkdir(bp_dir)

    contract_src = repo_root / "tests" / "uce" / "contracts" / "bp@v1.yaml"
    contract_dst = bp_dir / "bp@v1.yaml"
    shutil.copy2(contract_src, contract_dst)

    add_artifact(manifest, artifact_id="bp_contract", path="bp/bp@v1.yaml", kind="contract", required=True, media_type="application/yaml")
    add_artifact(manifest, artifact_id="bp_report", path="bp/report.json", kind="report", required=True, media_type="application/json")
    add_artifact(manifest, artifact_id="bp_evidence_dir", path=f"test-artifacts/bp/{run_id}", kind="evidence", required=True)
    add_artifact(manifest, artifact_id="bp_gate_log", path="logs/bp_testAsyncDownloadBackpressure.log", kind="log", required=True)

    qt_test_binary = resolve_qt_test_binary(build_dir, "tst_QCNetworkReply")
    results: list[GateResult] = []
    policy_codes: set[str] = set()
    out_root = build_dir / "test-artifacts" / "bp" / run_id / "testAsyncDownloadBackpressure"
    if out_root.exists():
        shutil.rmtree(out_root)

    if not qt_test_binary.exists():
        policy_codes.add("bp_binary_missing")
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
        add_result(manifest, result_id="bp_contract", kind="validator", result="fail", log_file=str(report_path), details={"qt_test_binary": str(qt_test_binary)})
        add_contract(
            manifest,
            contract_id="bp@v1",
            provider="uce_bp_validator",
            result="fail",
            required=True,
            report_artifact="bp_report",
            evidence_artifacts=["bp_contract", "bp_evidence_dir", "bp_gate_log"],
            violations=sorted(policy_codes),
            notes=[f"tier={tier}", "qt test binary missing"],
        )
        return results, sorted(policy_codes)

    safe_mkdir(out_root)
    env = os.environ.copy()
    env["QCURL_LC_OUT_DIR"] = str(out_root)
    gate_result = run_gate(
        "bp_testAsyncDownloadBackpressure",
        [str(qt_test_binary), "-o", "-", "txt", "testAsyncDownloadBackpressure"],
        logs_dir / "bp_testAsyncDownloadBackpressure.log",
        cwd=repo_root,
        env=env,
    )
    results.append(gate_result)
    if gate_result.returncode != 0:
        policy_codes.add("bp_test_run_failed")

    report = validate_bp(contract_dst, [out_root])
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
    for code in report.get("policy_violations", []):
        if isinstance(code, str) and code:
            policy_codes.add(code)
    report["policy_violations"] = sorted(policy_codes)
    report_path = bp_dir / "report.json"
    write_json(report_path, report)

    add_result(
        manifest,
        result_id="bp_contract",
        kind="validator",
        result="pass" if not policy_codes else "fail",
        log_file=str(report_path),
        details={"qt_test_binary": str(qt_test_binary), "returncode": gate_result.returncode, "evidence_files": len(evidence_files)},
    )
    add_contract(
        manifest,
        contract_id="bp@v1",
        provider="uce_bp_validator",
        result="pass" if not policy_codes else "fail",
        required=True,
        report_artifact="bp_report",
        evidence_artifacts=["bp_contract", "bp_evidence_dir", "bp_gate_log"],
        violations=sorted(policy_codes),
        notes=[f"tier={tier}", "evidence schema=qcurl-uce/dci-evidence@v1 stream=bp-user-pause"],
    )
    return results, sorted(policy_codes)


def run_dci_seed_suite(
    repo_root: Path,
    build_dir: Path,
    evidence_dir: Path,
    manifest: dict[str, Any],
    *,
    tier: str,
    run_id: str,
) -> tuple[list[GateResult], list[str]]:
    """Run fixed-seed DCI Qt tests and record evidence metadata."""

    dci_dir = evidence_dir / "dci"
    logs_dir = evidence_dir / "logs"
    safe_mkdir(dci_dir)

    seed_matrix = dci_seed_matrix(tier)
    seed_matrix_path = dci_dir / "seed_matrix.json"
    write_json(seed_matrix_path, {"generated_at_utc": utc_now_iso(), "tier": tier, "seed_matrix": seed_matrix})

    contract_src = repo_root / "tests" / "uce" / "contracts" / "dci@v1.yaml"
    contract_dst = dci_dir / "dci@v1.yaml"
    shutil.copy2(contract_src, contract_dst)

    add_artifact(manifest, artifact_id="dci_contract", path="dci/dci@v1.yaml", kind="contract", required=True, media_type="application/yaml")
    add_artifact(manifest, artifact_id="dci_seed_matrix", path="dci/seed_matrix.json", kind="metadata", required=True, media_type="application/json")
    add_artifact(manifest, artifact_id="dci_evidence_dir", path=f"test-artifacts/dci/{run_id}", kind="evidence", required=True)

    qt_test_binary = resolve_qt_test_binary(build_dir, "tst_QCNetworkReply")
    results: list[GateResult] = []
    violations: set[str] = set()
    runs: list[dict[str, Any]] = []

    if not qt_test_binary.exists():
        violations.add("dci_binary_missing")
        report_path = dci_dir / "report.json"
        write_json(
            report_path,
            {
                "generated_at_utc": utc_now_iso(),
                "tier": tier,
                "qt_test_binary": str(qt_test_binary),
                "seed_matrix": seed_matrix,
                "runs": [],
                "policy_violations": sorted(violations),
            },
        )
        add_artifact(manifest, artifact_id="dci_report", path="dci/report.json", kind="report", required=True, media_type="application/json")
        add_result(manifest, result_id="dci_seed_suite", kind="gate", result="fail", log_file=str(report_path), details={"seed_matrix": seed_matrix, "qt_test_binary": str(qt_test_binary)})
        add_contract(
            manifest,
            contract_id="dci@v1",
            provider="uce_dci_runner",
            result="fail",
            required=True,
            report_artifact="dci_report",
            evidence_artifacts=["dci_contract", "dci_seed_matrix", "dci_evidence_dir"],
            violations=sorted(violations),
        )
        return results, sorted(violations)

    dci_artifacts_root = build_dir / "test-artifacts" / "dci" / run_id
    if dci_artifacts_root.exists():
        shutil.rmtree(dci_artifacts_root)

    for test_function, seeds in seed_matrix.items():
        for seed in seeds:
            out_dir = dci_artifacts_root / test_function / f"seed-{seed}"
            safe_mkdir(out_dir)
            gate_id = f"dci_{test_function}_seed_{seed}"
            log_name = f"{gate_id}.log"
            env = os.environ.copy()
            env["QCURL_LC_OUT_DIR"] = str(out_dir)
            env["QCURL_TEST_MOCK_CHAOS_SEED"] = str(seed)

            gate_result = run_gate(
                gate_id,
                [str(qt_test_binary), "-o", "-", "txt", test_function],
                logs_dir / log_name,
                cwd=repo_root,
                env=env,
            )
            results.append(gate_result)

            evidence_files = sorted(out_dir.rglob("dci_evidence_*.jsonl"))
            runs.append(
                {
                    "test_function": test_function,
                    "seed": seed,
                    "returncode": gate_result.returncode,
                    "duration_s": gate_result.duration_s,
                    "log_path": f"logs/{log_name}",
                    "evidence_files": [
                        str(Path("test-artifacts") / "dci" / run_id / path.relative_to(dci_artifacts_root))
                        for path in evidence_files
                    ],
                }
            )
            if gate_result.returncode != 0:
                violations.add("dci_seed_run_failed")
            if not evidence_files:
                violations.add("dci_evidence_missing")

    report_path = dci_dir / "report.json"
    write_json(
        report_path,
        {
            "generated_at_utc": utc_now_iso(),
            "tier": tier,
            "qt_test_binary": str(qt_test_binary),
            "seed_matrix": seed_matrix,
            "run_count": len(runs),
            "runs": runs,
            "policy_violations": sorted(violations),
        },
    )
    add_artifact(manifest, artifact_id="dci_report", path="dci/report.json", kind="report", required=True, media_type="application/json")
    add_result(
        manifest,
        result_id="dci_seed_suite",
        kind="gate",
        result="pass" if not violations else "fail",
        log_file=str(report_path),
        details={"seed_matrix": seed_matrix, "qt_test_binary": str(qt_test_binary), "run_count": len(runs)},
    )
    add_contract(
        manifest,
        contract_id="dci@v1",
        provider="uce_dci_runner",
        result="pass" if not violations else "fail",
        required=True,
        report_artifact="dci_report",
        evidence_artifacts=["dci_contract", "dci_seed_matrix", "dci_evidence_dir"],
        violations=sorted(violations),
        notes=[f"fixed seed matrix for {tier}"],
    )
    return results, sorted(violations)
