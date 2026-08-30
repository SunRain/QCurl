"""一致性 gate 的计划、执行与最终报告编排。"""

from __future__ import annotations

from copy import deepcopy
import json
import os
import sys
import time
from dataclasses import dataclass, field
from typing import Dict, List

from .contract_map import coverage_authority_nodeids
from .contract_map import coverage_evidence_types
from .contract_map import coverage_node_contracts
from .contract_map import load_coverage_map
from .contract_map import load_minimal_set
from .contract_map import validate_collected_cases
from .contract_map import validate_planner_exclusions
from .coverage_contract_validation import validate_coverage_map
from .minimal_set_contract import validate_minimal_collected_cases
from .minimal_set_contract import validate_minimal_set
from .gate_evidence import add_python_lock_summary
from .gate_evidence import write_nghttpx_version_snapshot
from .gate_evidence import write_python_env_snapshot
from .evidence_integrity import verify_gate_evidence_files
from .execution_plan import seal_execution_plan
from .gate_manifest import collect_nodeids_from_output
from .gate_manifest import evaluate_execution_contract
from .gate_report import create_initial_report
from .gate_report import policy_violations_from_report
from .gate_report import redact_text
from .gate_runtime import FORBIDDEN_LOCAL_HTTPBIN_ENDPOINTS
from .gate_runtime import GateConfig
from .gate_runtime import build_targets
from .gate_runtime import candidate_files
from .gate_runtime import check_artifact_schema
from .gate_runtime import check_redaction
from .gate_runtime import check_required_inputs
from .gate_runtime import ensure_parent
from .gate_runtime import evaluate_http3_preflight
from .gate_runtime import forbid_gate_httpbin
from .gate_runtime import gate_environment
from .gate_runtime import load_capability_manifest
from .gate_runtime import plan_files
from .gate_runtime import run_command
from .gate_runtime import parse_junit_counts


@dataclass
class GateRunState:
    """一次运行在计划、执行与收尾阶段共享的可变状态。"""

    started: float
    gate_env: Dict[str, str]
    report: Dict[str, object]
    candidate_pytest_files: List[str] = field(default_factory=list)
    planned_pytest_files: List[str] = field(default_factory=list)
    planner_exclusions: Dict[str, str] = field(default_factory=dict)
    planned_nodeids: List[str] = field(default_factory=list)
    file_evidence_types: Dict[str, str] = field(default_factory=dict)
    node_contracts: Dict[str, Dict[str, object]] = field(default_factory=dict)
    evidence_identity: Dict[str, object] = field(default_factory=dict)
    capability_identity: Dict[str, object] = field(default_factory=dict)
    capability_manifest: Dict[str, object] = field(default_factory=dict)
    http3_preflight: Dict[str, object] = field(default_factory=dict)
    execution_plan: Dict[str, object] = field(default_factory=dict)
    minimal_set: Dict[str, object] = field(default_factory=dict)


def pytest_command(*arguments: str) -> List[str]:
    """Build a pytest command that preserves the current interpreter environment."""

    return [sys.executable, "-m", "pytest", *arguments]


def _create_state(config: GateConfig) -> GateRunState:
    ensure_parent(config.junit_xml)
    assert config.artifacts_dir is not None
    assert config.run_dir is not None
    config.artifacts_dir.mkdir(parents=True, exist_ok=True)
    started = time.time()
    require_http3_raw = (os.environ.get("QCURL_REQUIRE_HTTP3") or "").strip()
    require_http3_enabled = require_http3_raw.lower() in ("1", "true", "yes", "on")
    gate_env = gate_environment(config)
    report = create_initial_report(
        config,
        gate_env,
        require_http3_raw=require_http3_raw,
        require_http3_enabled=require_http3_enabled,
    )
    report.update(
        {
            "run_id": config.run_id,
            "execution_token": config.execution_token,
            "run_dir": str(config.run_dir),
            "artifacts_dir": str(config.artifacts_dir),
        }
    )
    add_python_lock_summary(report, config.repo_root)
    return GateRunState(started=started, gate_env=gate_env, report=report)


def _check_local_httpbin(config: GateConfig, report: Dict[str, object]) -> None:
    violations = forbid_gate_httpbin(config)
    report["preflight_forbid_local_httpbin_8935"] = {
        "forbidden_endpoints": list(FORBIDDEN_LOCAL_HTTPBIN_ENDPOINTS),
        "violations": violations,
    }
    if not violations:
        return
    lines = [
        "preflight failed: forbidden local httpbin dependency detected (localhost:8935).",
        "consistency gate must not depend on httpbin; use curl testenv + http_observe_server.py instead.",
        "violations:",
    ]
    lines.extend(f" - {value.get('file')}: {value.get('hits')}" for value in violations)
    raise RuntimeError("\n".join(lines))


def _load_contract_maps(config: GateConfig, state: GateRunState) -> Dict[str, object]:
    coverage_map = load_coverage_map(
        config.repo_root / "tests/libcurl_consistency/coverage-map.yaml"
    )
    coverage_errors = validate_coverage_map(coverage_map, config.repo_root)
    if coverage_errors:
        raise RuntimeError("coverage map validation failed:\n" + "\n".join(coverage_errors))
    minimal_set = load_minimal_set(
        config.repo_root / "tests/libcurl_consistency/minimal_set.yaml"
    )
    minimal_errors = validate_minimal_set(minimal_set, config.repo_root)
    if minimal_errors:
        raise RuntimeError("minimal set validation failed:\n" + "\n".join(minimal_errors))
    state.minimal_set = minimal_set
    return coverage_map


def _configure_capabilities(config: GateConfig, state: GateRunState) -> Dict[str, object]:
    if config.build:
        build_targets(config)
    capability_manifest = load_capability_manifest(
        config,
        gate_started_epoch=state.started,
    )
    http3_enabled = bool(state.report["preflight_http3_required"]["enabled"])
    state.http3_preflight = evaluate_http3_preflight(
        config,
        state.gate_env,
        require_http3_enabled=http3_enabled,
    )
    state.report["warnings"].extend(state.http3_preflight.get("warnings", []))
    state.report["preflight_http3_required"] = {
        key: deepcopy(state.http3_preflight.get(key))
        for key in ("enabled", "have_h3_server", "have_h3_curl", "violations")
    }
    state.capability_manifest = capability_manifest
    provenance = capability_manifest.get("provenance")
    if isinstance(provenance, dict):
        state.capability_identity = deepcopy(provenance)
        state.evidence_identity = {
            "source": provenance.get("source", {}),
            "build": provenance.get("build", {}),
        }
        state.gate_env["QCURL_LC_EVIDENCE_IDENTITY_JSON"] = json.dumps(
            state.evidence_identity,
            ensure_ascii=False,
            sort_keys=True,
        )
    return capability_manifest


def _plan_gate_files(
    config: GateConfig,
    state: GateRunState,
    coverage_map: Dict[str, object],
    capability_manifest: Dict[str, object],
) -> None:
    state.candidate_pytest_files = candidate_files(config)
    state.planned_pytest_files, state.planner_exclusions = plan_files(
        config,
        capability_manifest,
        planner_overrides=state.http3_preflight.get("planner_overrides", {}),
    )
    exclusion_errors = validate_planner_exclusions(
        coverage_map,
        suite=config.suite,
        exclusions=state.planner_exclusions,
        with_ext=config.with_ext,
    )
    if exclusion_errors:
        raise RuntimeError("planner exclusion validation failed:\n" + "\n".join(exclusion_errors))
    state.report.update(
        {
            "capability_manifest": deepcopy(capability_manifest),
            "planner_exclusions": state.planner_exclusions,
            "pytest_files": state.planned_pytest_files,
            "candidate_pytest_files": state.candidate_pytest_files,
        }
    )
    if not state.planned_pytest_files:
        raise RuntimeError("capability planner excluded every pytest file; gate would have no executable cases")
    check_required_inputs(
        config,
        state.gate_env,
        state.planned_pytest_files,
        state.report,
    )


def _load_gate_inputs(config: GateConfig, state: GateRunState) -> Dict[str, object]:
    coverage_map = _load_contract_maps(config, state)
    _check_local_httpbin(config, state.report)
    capability_manifest = _configure_capabilities(config, state)
    _plan_gate_files(config, state, coverage_map, capability_manifest)
    return coverage_map


def _collect_planned_nodeids(
    config: GateConfig,
    state: GateRunState,
    coverage_map: Dict[str, object],
) -> List[str]:
    command = pytest_command("--collect-only", "-q", *state.planned_pytest_files)
    state.report["commands"].append(command)
    result = run_command(command, cwd=config.repo_root, env=state.gate_env, capture=True)
    if result.returncode != 0:
        raise RuntimeError(
            f"pytest collect failed: rc={result.returncode}\n{result.stdout}\n{result.stderr}"
        )
    state.planned_nodeids = collect_nodeids_from_output(result.stdout)
    if not state.planned_nodeids:
        raise RuntimeError("pytest collect produced no nodeids")
    errors = validate_collected_cases(
        coverage_map,
        planned_files=state.planned_pytest_files,
        planned_nodeids=state.planned_nodeids,
        authority_nodeids=coverage_authority_nodeids(coverage_map),
    )
    errors.extend(
        validate_minimal_collected_cases(
            state.minimal_set,
            suite=config.suite,
            with_ext=config.with_ext,
            planned_files=state.planned_pytest_files,
            collected_nodeids=state.planned_nodeids,
        )
    )
    if errors:
        raise RuntimeError("coverage map collection validation failed:\n" + "\n".join(errors))
    return state.planned_nodeids


def _bind_execution_contracts(state: GateRunState, coverage_map: Dict[str, object]) -> None:
    all_evidence_types = coverage_evidence_types(coverage_map)
    state.file_evidence_types = {
        path: all_evidence_types[path]
        for path in state.candidate_pytest_files
    }
    state.node_contracts = coverage_node_contracts(
        coverage_map,
        planned_nodeids=state.planned_nodeids,
    )


def _write_execution_plan(
    config: GateConfig,
    state: GateRunState,
) -> None:
    execution_plan = seal_execution_plan({
        "run_id": config.run_id,
        "execution_token": config.execution_token,
        "identity": state.evidence_identity,
        "preflight_http3": state.report["preflight_http3_required"],
        "planner_overrides": state.http3_preflight.get("planner_overrides", {}),
        "candidate_files": state.candidate_pytest_files,
        "planned_files": state.planned_pytest_files,
        "planner_exclusions": state.planner_exclusions,
        "planned_nodeids": state.planned_nodeids,
        "file_evidence_types": state.file_evidence_types,
        "node_contracts": state.node_contracts,
    })
    state.execution_plan = execution_plan
    assert config.run_dir is not None
    execution_plan_path = config.run_dir / "execution_plan.json"
    execution_plan_path.write_text(
        json.dumps(execution_plan, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )
    state.report["execution_plan_path"] = str(execution_plan_path)
    state.report["execution_plan"] = deepcopy(execution_plan)


def _collect_execution_plan(
    config: GateConfig,
    state: GateRunState,
    coverage_map: Dict[str, object],
) -> None:
    _collect_planned_nodeids(config, state, coverage_map)
    _bind_execution_contracts(state, coverage_map)
    _write_execution_plan(config, state)


def _execute_pytest(config: GateConfig, state: GateRunState) -> None:
    write_python_env_snapshot(config, state.gate_env, state.report, run_command=run_command)
    write_nghttpx_version_snapshot(config, state.gate_env, state.report, run_command=run_command)
    command = pytest_command(
        "-q",
        "--maxfail=1",
        "--junitxml",
        str(config.junit_xml),
        *state.planned_nodeids,
    )
    state.report["commands"].append(command)
    result = run_command(command, cwd=config.repo_root, env=state.gate_env, capture=True)
    state.report["pytest_returncode"] = result.returncode
    redacted_stdout = redact_text(result.stdout)
    redacted_stderr = redact_text(result.stderr)
    state.report["pytest_stdout"] = redacted_stdout
    state.report["pytest_stderr"] = redacted_stderr
    if redacted_stdout:
        sys.stdout.write(redacted_stdout)
    if redacted_stderr:
        sys.stderr.write(redacted_stderr)


def _finalize_report(config: GateConfig, state: GateRunState) -> None:
    state.report["duration_s"] = round(time.time() - state.started, 3)
    state.report["junit_counts"] = parse_junit_counts(config.junit_xml)
    config.json_report.write_text(
        json.dumps(state.report, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )
    state.report["postflight_artifacts_schema_check"] = check_artifact_schema(
        config,
        since_ts=state.started,
    )
    state.report["postflight_redaction_scan"] = check_redaction(
        config,
        since_ts=state.started,
    )
    assert config.artifacts_dir is not None
    state.report["execution_contract"] = evaluate_execution_contract(
        run_id=config.run_id,
        candidate_files=state.candidate_pytest_files,
        planned_files=state.planned_pytest_files,
        planner_exclusions=state.planner_exclusions,
        planned_nodeids=state.planned_nodeids,
        junit_xml=config.junit_xml,
        artifacts_dir=config.artifacts_dir,
        file_evidence_types=state.file_evidence_types,
        node_contracts=state.node_contracts,
        execution_token=config.execution_token,
        expected_identity=state.evidence_identity,
    )
    state.report["evidence_integrity"] = verify_gate_evidence_files(
        manifest_path=config.capability_manifest,
        embedded_manifest=state.report.get("capability_manifest", {}),
        plan_path=config.run_dir / "execution_plan.json",
        embedded_plan=state.report.get("execution_plan", {}),
        expected_identity=state.capability_identity,
        gate_started_epoch=state.started,
        now_epoch=time.time(),
        expected_run_id=config.run_id,
        expected_execution_token=config.execution_token,
    )
    gate_returncode = int(state.report.get("pytest_returncode", 2))
    violations = policy_violations_from_report(state.report)
    state.report["policy_violations"] = violations
    state.report["gate_returncode"] = 3 if violations else gate_returncode
    config.json_report.write_text(
        json.dumps(state.report, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )


def execute_gate(config: GateConfig) -> int:
    """执行完整 gate，并保证异常路径也生成最终报告。"""

    state = _create_state(config)
    try:
        coverage_map = _load_gate_inputs(config, state)
        _collect_execution_plan(config, state, coverage_map)
        _execute_pytest(config, state)
    except Exception as exc:
        state.report["exception"] = redact_text(str(exc))
        state.report["pytest_returncode"] = 2
    finally:
        _finalize_report(config, state)
    return int(state.report.get("gate_returncode", state.report.get("pytest_returncode", 2)))
