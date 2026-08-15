"""Fixed-seed DCI Qt-test contract runner."""

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


def _qt_test_args(test_function: str) -> list[str]:
    return ["-o", "-,txt", test_function]


def _qt_test_env(runtime_env: dict[str, str] | None) -> dict[str, str]:
    env = os.environ.copy()
    env.update(runtime_env or {})
    return env


def _register_dci_artifacts(manifest: dict[str, Any], run_id: str) -> None:
    add_artifact(manifest, artifact_id="dci_contract", path="dci/dci@v1.yaml", kind="contract", required=True, media_type="application/yaml")
    add_artifact(manifest, artifact_id="dci_seed_matrix", path="dci/seed_matrix.json", kind="metadata", required=True, media_type="application/json")
    add_artifact(manifest, artifact_id="dci_evidence_dir", path=f"test-artifacts/dci/{run_id}", kind="evidence", required=True)


def _register_dci_result(
    manifest: dict[str, Any],
    report_path: Path,
    violations: set[str],
    details: dict[str, Any],
    notes: list[str] | None,
) -> None:
    result = "pass" if not violations else "fail"
    add_artifact(manifest, artifact_id="dci_report", path="dci/report.json", kind="report", required=True, media_type="application/json")
    add_result(
        manifest,
        result_id="dci_seed_suite",
        kind="gate",
        result=result,
        log_file=str(report_path),
        details=details,
    )
    contract_args: dict[str, Any] = {
        "contract_id": "dci@v1",
        "provider": "uce_dci_runner",
        "result": result,
        "required": True,
        "report_artifact": "dci_report",
        "evidence_artifacts": ["dci_contract", "dci_seed_matrix", "dci_evidence_dir"],
        "violations": sorted(violations),
    }
    if notes is not None:
        contract_args["notes"] = notes
    add_contract(manifest, **contract_args)


def _missing_dci_binary_result(
    dci_dir: Path,
    manifest: dict[str, Any],
    *,
    tier: str,
    qt_test_binary: Path,
    seed_matrix: dict[str, list[int]],
) -> tuple[list[GateResult], list[str]]:
    violations = {"dci_binary_missing"}
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
    _register_dci_result(
        manifest,
        report_path,
        violations,
        {"seed_matrix": seed_matrix, "qt_test_binary": str(qt_test_binary)},
        None,
    )
    return [], sorted(violations)


def _run_dci_seed(
    repo_root: Path,
    logs_dir: Path,
    dci_artifacts_root: Path,
    qt_test_binary: Path,
    runtime_env: dict[str, str] | None,
    *,
    run_id: str,
    test_function: str,
    seed: int,
) -> tuple[GateResult, dict[str, Any]]:
    out_dir = dci_artifacts_root / test_function / f"seed-{seed}"
    safe_mkdir(out_dir)
    gate_id = f"dci_{test_function}_seed_{seed}"
    log_name = f"{gate_id}.log"
    env = _qt_test_env(runtime_env)
    env["QCURL_LC_OUT_DIR"] = str(out_dir)
    env["QCURL_TEST_MOCK_CHAOS_SEED"] = str(seed)

    gate_result = run_gate(
        gate_id,
        [str(qt_test_binary), *_qt_test_args(test_function)],
        logs_dir / log_name,
        cwd=repo_root,
        env=env,
    )
    evidence_files = sorted(out_dir.rglob("dci_evidence_*.jsonl"))
    run = {
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
    return gate_result, run


def _run_dci_matrix(
    repo_root: Path,
    logs_dir: Path,
    dci_artifacts_root: Path,
    qt_test_binary: Path,
    runtime_env: dict[str, str] | None,
    seed_matrix: dict[str, list[int]],
    run_id: str,
) -> tuple[list[GateResult], list[dict[str, Any]], set[str]]:
    results: list[GateResult] = []
    runs: list[dict[str, Any]] = []
    violations: set[str] = set()
    for test_function, seeds in seed_matrix.items():
        for seed in seeds:
            gate_result, run = _run_dci_seed(
                repo_root,
                logs_dir,
                dci_artifacts_root,
                qt_test_binary,
                runtime_env,
                run_id=run_id,
                test_function=test_function,
                seed=seed,
            )
            results.append(gate_result)
            runs.append(run)
            if gate_result.returncode != 0:
                violations.add("dci_seed_run_failed")
            if not run["evidence_files"]:
                violations.add("dci_evidence_missing")
    return results, runs, violations


def _prepare_dci_contract(
    repo_root: Path,
    build_dir: Path,
    evidence_dir: Path,
    manifest: dict[str, Any],
    *,
    tier: str,
    run_id: str,
) -> tuple[Path, Path, dict[str, list[int]], Path]:
    dci_dir = evidence_dir / "dci"
    logs_dir = evidence_dir / "logs"
    safe_mkdir(dci_dir)

    seed_matrix = dci_seed_matrix(tier)
    seed_matrix_path = dci_dir / "seed_matrix.json"
    write_json(seed_matrix_path, {"generated_at_utc": utc_now_iso(), "tier": tier, "seed_matrix": seed_matrix})

    contract_dst = dci_dir / "dci@v1.yaml"
    shutil.copy2(repo_root / "tests" / "uce" / "contracts" / "dci@v1.yaml", contract_dst)
    _register_dci_artifacts(manifest, run_id)
    return dci_dir, logs_dir, seed_matrix, resolve_qt_test_binary(build_dir, "tst_QCNetworkReply")


def _finish_dci_contract(
    dci_dir: Path,
    manifest: dict[str, Any],
    qt_test_binary: Path,
    seed_matrix: dict[str, list[int]],
    results: list[GateResult],
    runs: list[dict[str, Any]],
    violations: set[str],
    tier: str,
) -> tuple[list[GateResult], list[str]]:
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
    _register_dci_result(
        manifest,
        report_path,
        violations,
        {
            "seed_matrix": seed_matrix,
            "qt_test_binary": str(qt_test_binary),
            "run_count": len(runs),
        },
        [f"fixed seed matrix for {tier}"],
    )
    return results, sorted(violations)


def run_dci_seed_suite(
    repo_root: Path,
    build_dir: Path,
    evidence_dir: Path,
    manifest: dict[str, Any],
    *,
    tier: str,
    run_id: str,
    runtime_env: dict[str, str] | None = None,
) -> tuple[list[GateResult], list[str]]:
    """Run fixed-seed DCI Qt tests and record evidence metadata."""

    dci_dir, logs_dir, seed_matrix, qt_test_binary = _prepare_dci_contract(
        repo_root,
        build_dir,
        evidence_dir,
        manifest,
        tier=tier,
        run_id=run_id,
    )
    if not qt_test_binary.exists():
        return _missing_dci_binary_result(
            dci_dir,
            manifest,
            tier=tier,
            qt_test_binary=qt_test_binary,
            seed_matrix=seed_matrix,
        )

    dci_artifacts_root = build_dir / "test-artifacts" / "dci" / run_id
    if dci_artifacts_root.exists():
        shutil.rmtree(dci_artifacts_root)
    results, runs, violations = _run_dci_matrix(
        repo_root,
        logs_dir,
        dci_artifacts_root,
        qt_test_binary,
        runtime_env,
        seed_matrix,
        run_id,
    )
    return _finish_dci_contract(
        dci_dir,
        manifest,
        qt_test_binary,
        seed_matrix,
        results,
        runs,
        violations,
        tier,
    )
