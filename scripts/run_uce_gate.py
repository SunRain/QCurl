#!/usr/bin/env python3
"""Run the minimal UCE gate and archive its evidence bundle."""

from __future__ import annotations

import argparse
import os
import sys
from datetime import datetime, timezone
from pathlib import Path

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from scripts.netproof_capabilities import build_report
from scripts.uce.manifest import add_artifact
from scripts.uce.manifest import add_policy_violation
from scripts.uce.manifest import add_result
from scripts.uce.manifest import create_manifest
from scripts.uce.manifest import set_capability
from scripts.uce.redaction_gate import scan_paths
from scripts.uce_gate.contracts import run_ctbp_contract as _run_ctbp_contract
from scripts.uce_gate.contracts import run_hes_contract as _run_hes_contract
from scripts.uce_gate.contracts import run_timeline_contract as _run_timeline_contract
from scripts.uce_gate.evidence import create_gate_manifest
from scripts.uce_gate.evidence import load_json_if_exists
from scripts.uce_gate.evidence import package_evidence_bundle
from scripts.uce_gate.evidence import prepare_evidence_layout
from scripts.uce_gate.evidence import resolve_evidence_layout
from scripts.uce_gate.evidence import write_manifest_and_policy_report
from scripts.uce_gate.execute import register_netproof_contract as _register_netproof_contract
from scripts.uce_gate.execute import register_redaction_result as _register_redaction_result
from scripts.uce_gate.execute import run_libcurl_consistency_gates as _run_libcurl_consistency_gates
from scripts.uce_gate.execute import run_netproof_gate as _run_netproof_gate
from scripts.uce_gate.execute import run_offline_ctest_gate as _run_offline_ctest_gate
from scripts.uce_gate.httpbin import run_httpbin_gate as _run_httpbin_gate
from scripts.uce_gate.orchestrator import run_uce_gate as _run_uce_gate
from scripts.uce_gate.planner import GateSpec
from scripts.uce_gate.planner import build_tier_plan
from scripts.uce_gate.planner import ctbp_required_kinds
from scripts.uce_gate.planner import ctbp_required_runners
from scripts.uce_gate.planner import dci_seed_matrix
from scripts.uce_gate.planner import timeline_required_providers
from scripts.uce_gate.planner import validate_required_artifacts
from scripts.uce_gate.qt_contracts import run_bp_contract as _run_bp_contract
from scripts.uce_gate.qt_contracts import run_dci_seed_suite as _run_dci_seed_suite
from scripts.uce_gate.runtime import GateResult
from scripts.uce_gate.runtime import best_effort_copy_glob as _best_effort_copy_glob
from scripts.uce_gate.runtime import best_effort_copytree as _best_effort_copytree
from scripts.uce_gate.runtime import collect_versions as _collect_versions
from scripts.uce_gate.runtime import parse_shell_exports as _parse_shell_exports
from scripts.uce_gate.runtime import record_gate_result as _record_gate_result
from scripts.uce_gate.runtime import resolve_qt_test_binary as _resolve_qt_test_binary
from scripts.uce_gate.runtime import run_capture as _run_capture
from scripts.uce_gate.runtime import run_gate as _run_gate
from scripts.uce_gate.runtime import safe_mkdir as _safe_mkdir
from scripts.uce_gate.runtime import tar_gz_dir as _tar_gz_dir
from scripts.uce_gate.runtime import utc_now_iso as _utc_now_iso
from scripts.uce_gate.runtime import write_json as _write_json
from scripts.uce_gate.runtime import write_text as _write_text


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
    return _run_uce_gate(
        repo_root=repo_root,
        build_dir=build_dir,
        tier=args.tier,
        run_id=run_id,
        evidence_root_arg=args.evidence_root,
    )


if __name__ == "__main__":
    raise SystemExit(main())
