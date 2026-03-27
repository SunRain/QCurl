#!/usr/bin/env python3
"""Run the minimal UCE gate and archive its evidence bundle."""

from __future__ import annotations

import argparse
import json
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import time
from dataclasses import dataclass
from datetime import datetime
from datetime import timezone
from pathlib import Path
from typing import Any

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from scripts.netproof_capabilities import build_report
from scripts.uce.manifest import add_artifact
from scripts.uce.manifest import add_contract
from scripts.uce.manifest import add_policy_violation
from scripts.uce.manifest import add_result
from scripts.uce.manifest import create_manifest
from scripts.uce.manifest import set_capability
from scripts.uce.manifest import write_manifest
from scripts.uce.redaction_gate import scan_paths
from tests.uce.bp.validate import validate_bp
from tests.uce.ctbp.validate import validate_ctbp
from tests.uce.hes.validate import validate_hes
from tests.uce.timeline.collect_from_lc import collect_from_lc
from tests.uce.timeline.collect_from_qt import collect_from_qt
from tests.uce.timeline.common import write_jsonl as write_timeline_jsonl
from tests.uce.timeline.validate import validate_timelines


@dataclass(frozen=True)
class GateSpec:
    """Describe a single UCE gate step."""

    gate_id: str
    kind: str
    selector: str
    policy_code: str
    requires_httpbin: bool = False


@dataclass(frozen=True)
class GateResult:
    """Store the result of an executed gate command."""

    gate_id: str
    command: list[str]
    returncode: int
    duration_s: float
    log_path: Path


def dci_seed_matrix(tier: str) -> dict[str, list[int]]:
    """Return the fixed DCI seed matrix for a tier."""

    nightly = {
        "testAsyncMockChaosPauseResume": [17, 29],
        "testAsyncMockChaosCancel": [5, 19],
        "testAsyncMockChaosDeleteLater": [23, 41],
    }
    if tier != "soak":
        return nightly

    return {
        "testAsyncMockChaosPauseResume": [17, 29, 43],
        "testAsyncMockChaosCancel": [5, 19, 31],
        "testAsyncMockChaosDeleteLater": [23, 41, 47],
    }


def _utc_now_iso() -> str:
    """Return the current UTC time in ISO-8601 format."""

    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def _safe_mkdir(path: Path) -> None:
    """Create a directory tree if needed."""

    path.mkdir(parents=True, exist_ok=True)


def _write_text(path: Path, text: str) -> None:
    """Write UTF-8 text to disk."""

    _safe_mkdir(path.parent)
    path.write_text(text, encoding="utf-8")


def _write_json(path: Path, payload: dict[str, Any]) -> None:
    """Write a JSON document to disk."""

    _write_text(path, json.dumps(payload, ensure_ascii=False, indent=2) + "\n")


def _run_capture(
    command: list[str],
    *,
    cwd: Path | None = None,
    env: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    """Run a command and capture stdout/stderr together."""

    return subprocess.run(
        command,
        cwd=str(cwd) if cwd else None,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )


def _run_gate(
    gate_id: str,
    command: list[str],
    log_path: Path,
    *,
    cwd: Path,
    env: dict[str, str] | None = None,
) -> GateResult:
    """Run a single gate command and persist its output."""

    started = time.time()
    completed = _run_capture(command, cwd=cwd, env=env)
    duration_s = round(time.time() - started, 3)
    _write_text(log_path, completed.stdout or "")
    return GateResult(
        gate_id=gate_id,
        command=command,
        returncode=int(completed.returncode),
        duration_s=duration_s,
        log_path=log_path,
    )


def _collect_versions(repo_root: Path) -> str:
    """Collect a small best-effort environment snapshot."""

    commands = (
        ["uname", "-a"],
        ["git", "rev-parse", "HEAD"],
        ["cmake", "--version"],
        ["ctest", "--version"],
        ["python3", "--version"],
        ["curl", "--version"],
        ["docker", "--version"],
    )
    lines = [f"# generated_at_utc: {_utc_now_iso()}\n", f"# platform: {platform.platform()}\n\n"]
    for command in commands:
        try:
            result = _run_capture(command, cwd=repo_root)
            lines.append(f"$ {' '.join(command)}\n")
            lines.append(f"returncode: {result.returncode}\n")
            if (result.stdout or "").strip():
                lines.append(result.stdout.rstrip() + "\n")
            lines.append("\n")
        except Exception as exc:  # pragma: no cover - best effort snapshot
            lines.append(f"$ {' '.join(command)}\nexception: {exc}\n\n")
    return "".join(lines)


def _parse_shell_exports(env_file: Path) -> dict[str, str]:
    """Parse `export NAME=value` lines from a shell env file."""

    exports: dict[str, str] = {}
    for raw in env_file.read_text(encoding="utf-8", errors="replace").splitlines():
        raw = raw.strip()
        if not raw.startswith("export "):
            continue
        _, payload = raw.split("export ", 1)
        key, _, value = payload.partition("=")
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in {"'", '"'}:
            value = value[1:-1]
        exports[key.strip()] = value
    return exports


def _tar_gz_dir(src_dir: Path, out_tgz: Path) -> None:
    """Create a tar.gz archive for the evidence bundle."""

    _safe_mkdir(out_tgz.parent)
    with tarfile.open(out_tgz, "w:gz") as tar:
        for path in sorted(src_dir.rglob("*")):
            if path.is_file():
                tar.add(path, arcname=str(path.relative_to(src_dir.parent)))


def _best_effort_copytree(src: Path, dst: Path) -> bool:
    """Copy a directory tree when it exists."""

    if not src.exists():
        return False
    _safe_mkdir(dst.parent)
    shutil.copytree(src, dst, dirs_exist_ok=True)
    return True


def _best_effort_copy_glob(src_root: Path, pattern: str, dst_root: Path) -> int:
    """Copy files matching a glob pattern into the evidence bundle."""

    if not src_root.exists():
        return 0
    copied = 0
    for path in src_root.glob(pattern):
        if not path.is_file():
            continue
        relative = path.relative_to(src_root)
        output = dst_root / relative
        _safe_mkdir(output.parent)
        shutil.copy2(path, output)
        copied += 1
    return copied


def _resolve_qt_test_binary(build_dir: Path, binary_name: str) -> Path:
    """Resolve a Qt test binary from the build tree."""

    candidates = (
        build_dir / "tests" / binary_name,
        build_dir / binary_name,
    )
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return candidates[0]


def timeline_required_providers(tier: str) -> set[str]:
    """Return providers required by the TLC contract for a tier."""

    return {"qt"} if tier == "pr" else {"qt", "libcurl_consistency"}


def ctbp_required_runners() -> set[str]:
    """Return runners that must provide CTBP evidence."""

    return {"baseline", "qcurl"}


def ctbp_required_kinds() -> set[str]:
    """Return CTBP evidence kinds that must be present."""

    return {"connection_reuse", "tls_boundary"}


def _run_timeline_contract(
    repo_root: Path,
    build_dir: Path,
    evidence_dir: Path,
    manifest: dict[str, Any],
    *,
    tier: str,
) -> list[str]:
    """Collect and validate timeline evidence."""

    timeline_dir = evidence_dir / "timeline"
    _safe_mkdir(timeline_dir)

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
    _write_json(report_path, report)

    add_artifact(
        manifest,
        artifact_id="timeline_contract",
        path="timeline/timeline@v1.yaml",
        kind="contract",
        required=True,
        media_type="application/yaml",
    )
    add_artifact(
        manifest,
        artifact_id="timeline_report",
        path="timeline/report.json",
        kind="report",
        required=True,
        media_type="application/json",
    )
    add_artifact(
        manifest,
        artifact_id="timeline_qt_jsonl",
        path="timeline/qt.timeline.jsonl",
        kind="evidence",
        required=True,
        media_type="application/x-ndjson",
    )
    add_artifact(
        manifest,
        artifact_id="timeline_lc_jsonl",
        path="timeline/libcurl_consistency.timeline.jsonl",
        kind="evidence",
        required=tier in {"nightly", "soak"},
        media_type="application/x-ndjson",
    )
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


def _run_ctbp_contract(
    repo_root: Path,
    evidence_dir: Path,
    manifest: dict[str, Any],
) -> list[str]:
    """Collect and validate CTBP evidence."""

    ctbp_dir = evidence_dir / "ctbp"
    _safe_mkdir(ctbp_dir)

    contract_src = repo_root / "tests" / "uce" / "contracts" / "ctbp@v1.yaml"
    contract_dst = ctbp_dir / "ctbp@v1.yaml"
    shutil.copy2(contract_src, contract_dst)

    lc_artifacts_root = repo_root / "curl" / "tests" / "http" / "gen" / "artifacts"
    report = validate_ctbp(
        contract_dst,
        [lc_artifacts_root],
        ctbp_required_runners(),
        ctbp_required_kinds(),
    )

    evidence_path = ctbp_dir / "evidence.json"
    _write_json(
        evidence_path,
        {
            "generated_at_utc": _utc_now_iso(),
            "entries": report["entries"],
            "scanned_artifacts": report["summary"]["scanned_artifacts"],
            "missing_roots": report["summary"]["missing_roots"],
        },
    )

    report_path = ctbp_dir / "report.json"
    _write_json(report_path, report)

    add_artifact(
        manifest,
        artifact_id="ctbp_contract",
        path="ctbp/ctbp@v1.yaml",
        kind="contract",
        required=True,
        media_type="application/yaml",
    )
    add_artifact(
        manifest,
        artifact_id="ctbp_evidence",
        path="ctbp/evidence.json",
        kind="evidence",
        required=True,
        media_type="application/json",
    )
    add_artifact(
        manifest,
        artifact_id="ctbp_report",
        path="ctbp/report.json",
        kind="report",
        required=True,
        media_type="application/json",
    )
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


def _run_hes_contract(
    repo_root: Path,
    evidence_dir: Path,
    manifest: dict[str, Any],
    *,
    tier: str,
) -> list[str]:
    """Collect and validate HES evidence."""

    hes_dir = evidence_dir / "hes"
    _safe_mkdir(hes_dir)

    contract_src = repo_root / "tests" / "uce" / "contracts" / "hes@v1.yaml"
    contract_dst = hes_dir / "hes@v1.yaml"
    shutil.copy2(contract_src, contract_dst)

    artifacts_root = repo_root / "curl" / "tests" / "http" / "gen" / "artifacts"
    report = validate_hes(contract_dst, [artifacts_root], tier)
    report_path = hes_dir / "report.json"
    _write_json(report_path, report)

    add_artifact(
        manifest,
        artifact_id="hes_contract",
        path="hes/hes@v1.yaml",
        kind="contract",
        required=True,
        media_type="application/yaml",
    )
    add_artifact(
        manifest,
        artifact_id="hes_report",
        path="hes/report.json",
        kind="report",
        required=True,
        media_type="application/json",
    )
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


def _run_bp_contract(
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
    _safe_mkdir(bp_dir)

    contract_src = repo_root / "tests" / "uce" / "contracts" / "bp@v1.yaml"
    contract_dst = bp_dir / "bp@v1.yaml"
    shutil.copy2(contract_src, contract_dst)

    add_artifact(
        manifest,
        artifact_id="bp_contract",
        path="bp/bp@v1.yaml",
        kind="contract",
        required=True,
        media_type="application/yaml",
    )
    add_artifact(
        manifest,
        artifact_id="bp_report",
        path="bp/report.json",
        kind="report",
        required=True,
        media_type="application/json",
    )
    add_artifact(
        manifest,
        artifact_id="bp_evidence_dir",
        path=f"test-artifacts/bp/{run_id}",
        kind="evidence",
        required=True,
    )
    add_artifact(
        manifest,
        artifact_id="bp_gate_log",
        path="logs/bp_testAsyncDownloadBackpressure.log",
        kind="log",
        required=True,
    )

    qt_test_binary = _resolve_qt_test_binary(build_dir, "tst_QCNetworkReply")
    results: list[GateResult] = []
    policy_codes: set[str] = set()

    out_root = build_dir / "test-artifacts" / "bp" / run_id / "testAsyncDownloadBackpressure"
    if out_root.exists():
        shutil.rmtree(out_root)

    if not qt_test_binary.exists():
        policy_codes.add("bp_binary_missing")
        report_path = bp_dir / "report.json"
        _write_json(
            report_path,
            {
                "generated_at_utc": _utc_now_iso(),
                "tier": tier,
                "qt_test_binary": str(qt_test_binary),
                "out_root": str(out_root),
                "policy_violations": sorted(policy_codes),
            },
        )
        add_result(
            manifest,
            result_id="bp_contract",
            kind="validator",
            result="fail",
            log_file=str(report_path),
            details={"qt_test_binary": str(qt_test_binary)},
        )
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

    _safe_mkdir(out_root)
    env = os.environ.copy()
    env["QCURL_LC_OUT_DIR"] = str(out_root)

    gate_result = _run_gate(
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
        str(
            Path("test-artifacts")
            / "bp"
            / run_id
            / "testAsyncDownloadBackpressure"
            / path.name
        )
        for path in evidence_files
    ]

    for code in report.get("policy_violations", []):
        if isinstance(code, str) and code:
            policy_codes.add(code)

    report["policy_violations"] = sorted(policy_codes)
    report_path = bp_dir / "report.json"
    _write_json(report_path, report)

    add_result(
        manifest,
        result_id="bp_contract",
        kind="validator",
        result="pass" if not policy_codes else "fail",
        log_file=str(report_path),
        details={
            "qt_test_binary": str(qt_test_binary),
            "returncode": gate_result.returncode,
            "evidence_files": len(evidence_files),
        },
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


def _run_dci_seed_suite(
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
    _safe_mkdir(dci_dir)

    seed_matrix = dci_seed_matrix(tier)
    seed_matrix_path = dci_dir / "seed_matrix.json"
    _write_json(
        seed_matrix_path,
        {
            "generated_at_utc": _utc_now_iso(),
            "tier": tier,
            "seed_matrix": seed_matrix,
        },
    )

    contract_src = repo_root / "tests" / "uce" / "contracts" / "dci@v1.yaml"
    contract_dst = dci_dir / "dci@v1.yaml"
    shutil.copy2(contract_src, contract_dst)

    add_artifact(
        manifest,
        artifact_id="dci_contract",
        path="dci/dci@v1.yaml",
        kind="contract",
        required=True,
        media_type="application/yaml",
    )
    add_artifact(
        manifest,
        artifact_id="dci_seed_matrix",
        path="dci/seed_matrix.json",
        kind="metadata",
        required=True,
        media_type="application/json",
    )
    add_artifact(
        manifest,
        artifact_id="dci_evidence_dir",
        path=f"test-artifacts/dci/{run_id}",
        kind="evidence",
        required=True,
    )

    qt_test_binary = _resolve_qt_test_binary(build_dir, "tst_QCNetworkReply")
    results: list[GateResult] = []
    violations: set[str] = set()
    runs: list[dict[str, Any]] = []

    if not qt_test_binary.exists():
        violations.add("dci_binary_missing")
        report_path = dci_dir / "report.json"
        _write_json(
            report_path,
            {
                "generated_at_utc": _utc_now_iso(),
                "tier": tier,
                "qt_test_binary": str(qt_test_binary),
                "seed_matrix": seed_matrix,
                "runs": [],
                "policy_violations": sorted(violations),
            },
        )
        add_artifact(
            manifest,
            artifact_id="dci_report",
            path="dci/report.json",
            kind="report",
            required=True,
            media_type="application/json",
        )
        add_result(
            manifest,
            result_id="dci_seed_suite",
            kind="gate",
            result="fail",
            log_file=str(report_path),
            details={"seed_matrix": seed_matrix, "qt_test_binary": str(qt_test_binary)},
        )
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
            _safe_mkdir(out_dir)
            gate_id = f"dci_{test_function}_seed_{seed}"
            log_name = f"{gate_id}.log"
            env = os.environ.copy()
            env["QCURL_LC_OUT_DIR"] = str(out_dir)
            env["QCURL_TEST_MOCK_CHAOS_SEED"] = str(seed)

            gate_result = _run_gate(
                gate_id,
                [str(qt_test_binary), "-o", "-", "txt", test_function],
                logs_dir / log_name,
                cwd=repo_root,
                env=env,
            )
            results.append(gate_result)

            evidence_files = sorted(out_dir.rglob("dci_evidence_*.jsonl"))
            run_record = {
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
            if gate_result.returncode != 0:
                violations.add("dci_seed_run_failed")
            if not evidence_files:
                violations.add("dci_evidence_missing")
            runs.append(run_record)

    report_path = dci_dir / "report.json"
    _write_json(
        report_path,
        {
            "generated_at_utc": _utc_now_iso(),
            "tier": tier,
            "qt_test_binary": str(qt_test_binary),
            "seed_matrix": seed_matrix,
            "run_count": len(runs),
            "runs": runs,
            "policy_violations": sorted(violations),
        },
    )
    add_artifact(
        manifest,
        artifact_id="dci_report",
        path="dci/report.json",
        kind="report",
        required=True,
        media_type="application/json",
    )
    add_result(
        manifest,
        result_id="dci_seed_suite",
        kind="gate",
        result="pass" if not violations else "fail",
        log_file=str(report_path),
        details={
            "seed_matrix": seed_matrix,
            "qt_test_binary": str(qt_test_binary),
            "run_count": len(runs),
        },
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


def build_tier_plan(tier: str) -> list[GateSpec]:
    """Return the gate plan for a UCE tier."""

    plan = [
        GateSpec("ctest_strict_offline", "ctest", "offline", "gate_offline_failed"),
        GateSpec("libcurl_consistency_p0", "libcurl_consistency", "p0", "gate_libcurl_consistency_p0_failed"),
        GateSpec("libcurl_consistency_p1", "libcurl_consistency", "p1", "gate_libcurl_consistency_p1_failed"),
    ]
    if tier in {"nightly", "soak"}:
        plan.extend(
            [
                GateSpec("ctest_strict_env", "ctest", "env", "gate_env_failed", requires_httpbin=True),
                GateSpec("libcurl_consistency_p2", "libcurl_consistency", "p2", "gate_libcurl_consistency_p2_failed"),
            ]
        )
    return plan


def validate_required_artifacts(manifest: dict[str, Any], evidence_dir: Path) -> list[str]:
    """Return missing required artifact IDs."""

    missing: list[str] = []
    artifacts = manifest.get("artifacts") or {}
    if not isinstance(artifacts, dict):
        return ["artifacts"]
    for artifact_id, payload in artifacts.items():
        if not isinstance(payload, dict) or not payload.get("required"):
            continue
        raw_path = payload.get("path")
        if not isinstance(raw_path, str) or not raw_path:
            missing.append(str(artifact_id))
            continue
        artifact_path = Path(raw_path)
        if not artifact_path.is_absolute():
            artifact_path = evidence_dir / artifact_path
        if not artifact_path.exists():
            missing.append(str(artifact_id))
    return missing


def _record_gate_result(manifest: dict[str, Any], gate: GateResult) -> None:
    """Persist a gate result into the manifest."""

    add_result(
        manifest,
        result_id=gate.gate_id,
        kind="gate",
        result="pass" if gate.returncode == 0 else "fail",
        returncode=gate.returncode,
        duration_s=gate.duration_s,
        log_file=str(gate.log_path),
    )


def _run_httpbin_gate(
    repo_root: Path,
    build_dir: Path,
    evidence_dir: Path,
    manifest: dict[str, Any],
) -> tuple[dict[str, str], list[GateResult], list[str]]:
    """Start httpbin, run the env gate, and stop httpbin."""

    logs_dir = evidence_dir / "logs"
    httpbin_dir = evidence_dir / "httpbin"
    meta_dir = evidence_dir / "meta"
    httpbin_env_file = httpbin_dir / "httpbin.env"
    start_log = logs_dir / "httpbin_start.log"
    stop_log = logs_dir / "httpbin_stop.log"

    add_artifact(manifest, artifact_id="httpbin_start_log", path="logs/httpbin_start.log", kind="log", required=True)
    add_artifact(manifest, artifact_id="httpbin_stop_log", path="logs/httpbin_stop.log", kind="log", required=True)
    add_artifact(manifest, artifact_id="httpbin_env", path="httpbin/httpbin.env", kind="metadata", required=True)
    add_artifact(manifest, artifact_id="ctest_env_list", path="meta/ctest_list_env.txt", kind="report", required=True)
    add_artifact(manifest, artifact_id="ctest_env_log", path="logs/ctest_strict_env.log", kind="log", required=True)
    add_contract(
        manifest,
        contract_id="qtest_env@v1",
        provider="ctest_strict",
        result="fail",
        required=True,
        notes=["env gate 尚未获得有效 httpbin 环境时默认视为失败"],
    )

    results: list[GateResult] = []
    violations: list[str] = []
    container_name = ""
    env_values: dict[str, str] = {}

    start_result = _run_gate(
        "httpbin_start",
        [
            str(repo_root / "tests" / "qcurl" / "httpbin" / "start_httpbin.sh"),
            "--write-env",
            str(httpbin_env_file),
        ],
        start_log,
        cwd=repo_root,
    )
    results.append(start_result)
    _record_gate_result(manifest, start_result)
    if start_result.returncode != 0:
        violations.append("env_preflight_httpbin_start_failed")

    if httpbin_env_file.exists():
        try:
            env_values = _parse_shell_exports(httpbin_env_file)
        except Exception as exc:
            _write_text(httpbin_dir / "httpbin_env_parse_error.txt", f"{exc}\n")
            add_artifact(
                manifest,
                artifact_id="httpbin_env_parse_error",
                path="httpbin/httpbin_env_parse_error.txt",
                kind="report",
                required=True,
            )
            violations.append("env_preflight_httpbin_env_parse_error")
    else:
        violations.append("env_preflight_httpbin_env_missing")

    env_url = env_values.get("QCURL_HTTPBIN_URL")
    container_name = env_values.get("QCURL_HTTPBIN_CONTAINER_NAME", "")
    if env_url:
        env_for_ctest = os.environ.copy()
        env_for_ctest.update(env_values)

        list_result = _run_gate(
            "ctest_list_env",
            ["ctest", "-N", "--no-tests=error", "-L", "env"],
            meta_dir / "ctest_list_env.txt",
            cwd=build_dir,
            env=env_for_ctest,
        )
        results.append(list_result)
        _record_gate_result(manifest, list_result)

        gate_result = _run_gate(
            "ctest_strict_env",
            [
                "python3",
                str(repo_root / "scripts" / "ctest_strict.py"),
                "--build-dir",
                str(build_dir),
                "--label-regex",
                "env",
                "--max-skips",
                "0",
            ],
            logs_dir / "ctest_strict_env.log",
            cwd=repo_root,
            env=env_for_ctest,
        )
        results.append(gate_result)
        _record_gate_result(manifest, gate_result)
        add_contract(
            manifest,
            contract_id="qtest_env@v1",
            provider="ctest_strict",
            result="pass" if gate_result.returncode == 0 else "fail",
            required=True,
            report_artifact="ctest_env_log",
        )
        if gate_result.returncode != 0:
            violations.append("gate_env_failed")
    else:
        violations.append("env_preflight_httpbin_url_missing")
        _write_text(
            httpbin_dir / "httpbin_unavailable.txt",
            "httpbin 未就绪，无法运行 LABELS=env 证据门禁。\n",
        )
        add_artifact(
            manifest,
            artifact_id="httpbin_unavailable",
            path="httpbin/httpbin_unavailable.txt",
            kind="report",
            required=True,
        )

    stop_command = [str(repo_root / "tests" / "qcurl" / "httpbin" / "stop_httpbin.sh")]
    if container_name:
        stop_command.extend(["--name", container_name])
    stop_result = _run_gate("httpbin_stop", stop_command, stop_log, cwd=repo_root)
    results.append(stop_result)
    _record_gate_result(manifest, stop_result)

    return env_values, results, violations


def main(argv: list[str] | None = None) -> int:
    """CLI entry point."""

    parser = argparse.ArgumentParser(description="Run the minimal UCE gate and archive evidence.")
    parser.add_argument(
        "--tier",
        choices=("pr", "nightly", "soak"),
        default="pr",
        help="UCE tier to execute (default: pr).",
    )
    parser.add_argument(
        "--build-dir",
        default=os.environ.get("QCURL_BUILD_DIR", "build"),
        help="CMake build directory (default: build/ or $QCURL_BUILD_DIR).",
    )
    parser.add_argument(
        "--run-id",
        default=os.environ.get("QCURL_UCE_RUN_ID", ""),
        help="Run identifier used in evidence dir name.",
    )
    parser.add_argument(
        "--evidence-root",
        default=os.environ.get("QCURL_UCE_EVIDENCE_ROOT", ""),
        help="Evidence root directory (default: <build-dir>/evidence/uce).",
    )
    args = parser.parse_args(argv)

    repo_root = Path(__file__).resolve().parent.parent
    build_dir = Path(args.build_dir)
    if not build_dir.is_absolute():
        build_dir = (repo_root / build_dir).resolve()
    if not build_dir.exists():
        sys.stderr.write(f"[run_uce_gate] build dir not found: {build_dir}\n")
        return 2

    run_id = (args.run_id or "").strip() or datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    if args.evidence_root:
        evidence_root = Path(args.evidence_root)
        if not evidence_root.is_absolute():
            evidence_root = (repo_root / evidence_root).resolve()
    else:
        evidence_root = build_dir / "evidence" / "uce"

    evidence_dir = evidence_root / run_id
    logs_dir = evidence_dir / "logs"
    meta_dir = evidence_dir / "meta"
    reports_dir = evidence_dir / "reports"
    netproof_dir = evidence_dir / "netproof"
    lc_dir = evidence_dir / "libcurl_consistency"
    for path in (logs_dir, meta_dir, reports_dir, netproof_dir, lc_dir):
        _safe_mkdir(path)

    manifest_path = evidence_dir / "manifest.json"
    policy_report_path = evidence_dir / "policy_violations.json"
    tar_path = evidence_root / f"{run_id}.tar.gz"

    manifest = create_manifest(
        gate_id="uce",
        tier=args.tier,
        run_id=run_id,
        repo_root=str(repo_root),
        build_dir=str(build_dir),
        evidence_dir=str(evidence_dir),
        tar_gz=str(tar_path),
        environment={"platform": platform.platform(), "python": sys.version},
    )
    add_artifact(manifest, artifact_id="manifest", path="manifest.json", kind="metadata", required=True)
    add_artifact(
        manifest,
        artifact_id="policy_report",
        path="policy_violations.json",
        kind="report",
        required=True,
        media_type="application/json",
    )
    add_artifact(
        manifest,
        artifact_id="versions",
        path="meta/versions.txt",
        kind="report",
        required=True,
        media_type="text/plain",
    )

    versions_path = meta_dir / "versions.txt"
    _write_text(versions_path, _collect_versions(repo_root))

    capability_report = build_report(selected_tier=args.tier)
    capability_path = netproof_dir / "capabilities.json"
    _write_json(capability_path, capability_report)
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
    missing_providers = capability_report["tiers"][args.tier]["missing_required_providers"]
    if missing_providers:
        add_policy_violation(manifest, "capability_required_provider_missing")

    results: list[GateResult] = []
    httpbin_env: dict[str, str] = {}

    offline_list = _run_gate(
        "ctest_list_offline",
        ["ctest", "-N", "--no-tests=error", "-L", "offline"],
        meta_dir / "ctest_list_offline.txt",
        cwd=build_dir,
    )
    results.append(offline_list)
    _record_gate_result(manifest, offline_list)
    add_artifact(manifest, artifact_id="ctest_offline_list", path="meta/ctest_list_offline.txt", kind="report", required=True)

    offline_gate = _run_gate(
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
    results.append(offline_gate)
    _record_gate_result(manifest, offline_gate)
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

    tier_plan = build_tier_plan(args.tier)
    if any(item.requires_httpbin for item in tier_plan):
        httpbin_env, env_results, env_violations = _run_httpbin_gate(repo_root, build_dir, evidence_dir, manifest)
        results.extend(env_results)
        for code in env_violations:
            add_policy_violation(manifest, code)

    lc_reports_dir = lc_dir / "reports"
    _safe_mkdir(lc_reports_dir)
    for gate_spec in tier_plan:
        if gate_spec.kind != "libcurl_consistency":
            continue
        suite = gate_spec.selector
        gate_result = _run_gate(
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
                str(lc_reports_dir),
            ],
            logs_dir / f"{gate_spec.gate_id}.log",
            cwd=repo_root,
            env=(os.environ.copy() | httpbin_env) if httpbin_env else None,
        )
        results.append(gate_result)
        _record_gate_result(manifest, gate_result)
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

    if args.tier in {"nightly", "soak"}:
        dci_results, dci_violations = _run_dci_seed_suite(
            repo_root,
            build_dir,
            evidence_dir,
            manifest,
            tier=args.tier,
            run_id=run_id,
        )
        results.extend(dci_results)
        for gate_result in dci_results:
            _record_gate_result(manifest, gate_result)
        for code in dci_violations:
            add_policy_violation(manifest, code)

        bp_results, bp_violations = _run_bp_contract(
            repo_root,
            build_dir,
            evidence_dir,
            manifest,
            tier=args.tier,
            run_id=run_id,
        )
        results.extend(bp_results)
        for gate_result in bp_results:
            _record_gate_result(manifest, gate_result)
        for code in bp_violations:
            add_policy_violation(manifest, code)

        netproof_report_path = netproof_dir / "strace_report.json"
        netproof_trace_dir = netproof_dir / "trace"
        netproof_result = _run_gate(
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
        results.append(netproof_result)
        _record_gate_result(manifest, netproof_result)
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
        netproof_report = {}
        if netproof_report_path.exists():
            netproof_report = json.loads(netproof_report_path.read_text(encoding="utf-8"))
        add_contract(
            manifest,
            contract_id="netproof@v1",
            provider="netproof_strace_gate",
            result="pass" if netproof_result.returncode == 0 else "fail",
            required=True,
            report_artifact="netproof_strace_report",
            evidence_artifacts=["netproof_trace_dir", "netproof_capabilities"],
            violations=list(netproof_report.get("policy_violations") or []),
            notes=["subject=ctest_strict offline under strace trace=network"],
        )
        for code in netproof_report.get("policy_violations", []):
            add_policy_violation(manifest, code)

    copied_test_artifacts = _best_effort_copytree(build_dir / "test-artifacts", evidence_dir / "test-artifacts")
    if copied_test_artifacts:
        add_artifact(
            manifest,
            artifact_id="test_artifacts_dir",
            path="test-artifacts",
            kind="evidence",
            required=False,
        )
    copied_service_logs = _best_effort_copy_glob(
        repo_root / "curl" / "tests" / "http" / "gen" / "artifacts",
        "**/service_logs/**",
        lc_dir / "service_logs",
    )
    if copied_service_logs:
        add_artifact(
            manifest,
            artifact_id="service_logs_dir",
            path="libcurl_consistency/service_logs",
            kind="evidence",
            required=False,
        )

    timeline_violations = _run_timeline_contract(repo_root, build_dir, evidence_dir, manifest, tier=args.tier)
    for code in timeline_violations:
        add_policy_violation(manifest, code)
    if args.tier in {"nightly", "soak"}:
        ctbp_violations = _run_ctbp_contract(repo_root, evidence_dir, manifest)
        for code in ctbp_violations:
            add_policy_violation(manifest, code)
    hes_violations = _run_hes_contract(repo_root, evidence_dir, manifest, tier=args.tier)
    for code in hes_violations:
        add_policy_violation(manifest, code)

    provisional_policy_report = {
        "generated_at_utc": _utc_now_iso(),
        "tier": args.tier,
        "policy_violations": manifest["policy_violations"],
    }
    _write_json(policy_report_path, provisional_policy_report)
    write_manifest(manifest_path, manifest)

    redaction_report = scan_paths([evidence_dir])
    redaction_report_path = reports_dir / "redaction_gate.json"
    _write_json(redaction_report_path, redaction_report)
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
        result="pass" if not redaction_report["violations"] and not redaction_report["missing_roots"] else "fail",
        log_file=str(redaction_report_path),
    )
    add_contract(
        manifest,
        contract_id="redaction@v1",
        provider="redaction_gate",
        result="pass" if not redaction_report["violations"] and not redaction_report["missing_roots"] else "fail",
        required=True,
        report_artifact="redaction_report",
        notes=["scan missing 或 violation 均按 fail-closed 处理"],
    )
    if redaction_report["violations"] or redaction_report["missing_roots"]:
        add_policy_violation(manifest, "redaction")

    missing_required = validate_required_artifacts(manifest, evidence_dir)
    if missing_required:
        add_policy_violation(manifest, "artifact_required_missing")

    manifest["result"] = "pass" if not manifest["policy_violations"] else "fail"
    _write_json(
        policy_report_path,
        {
            "generated_at_utc": _utc_now_iso(),
            "tier": args.tier,
            "policy_violations": manifest["policy_violations"],
            "missing_required_artifacts": missing_required,
        },
    )
    write_manifest(manifest_path, manifest)

    try:
        _tar_gz_dir(evidence_dir, tar_path)
    except Exception as exc:
        add_policy_violation(manifest, "packaging_tar_gz_failed")
        manifest["packaging"] = {"tar_gz_error": str(exc)}

    add_artifact(manifest, artifact_id="archive_bundle", path=str(tar_path), kind="archive", required=True, media_type="application/gzip")
    missing_after_packaging = validate_required_artifacts(manifest, evidence_dir)
    if missing_after_packaging:
        add_policy_violation(manifest, "artifact_required_missing")

    manifest["result"] = "pass" if not manifest["policy_violations"] else "fail"
    _write_json(
        policy_report_path,
        {
            "generated_at_utc": _utc_now_iso(),
            "tier": args.tier,
            "policy_violations": manifest["policy_violations"],
            "missing_required_artifacts": missing_after_packaging,
        },
    )
    write_manifest(manifest_path, manifest)

    return 0 if manifest["result"] == "pass" else 3


if __name__ == "__main__":
    raise SystemExit(main())
