"""一致性 gate 最小回归集合的静态与运行时校验。"""

from __future__ import annotations

from pathlib import Path
from typing import Any

from .contract_map import coverage_authority_nodeids
from .contract_map import coverage_pytest_files
from .contract_map import load_coverage_map
from .contract_validation import SUITES
from .contract_validation import validate_nodeid


def _source_nodeids(source: dict[str, Any], label: str) -> tuple[list[str], list[str]]:
    nodeids = source.get("nodeids")
    if nodeids is None:
        nodeid = source.get("nodeid")
        if not isinstance(nodeid, str) or not nodeid:
            return [], [f"{label}: source.nodeid or source.nodeids missing"]
        return [nodeid], []
    if not isinstance(nodeids, list) or not nodeids or not all(isinstance(value, str) for value in nodeids):
        return [], [f"{label}: source.nodeids must be a non-empty string list"]
    return list(nodeids), []


def validate_minimal_collected_cases(
    data: dict[str, Any],
    *,
    suite: str,
    with_ext: bool,
    planned_files: list[str],
    collected_nodeids: list[str],
) -> list[str]:
    """核对本轮 planner 实际纳入的 minimal-set 精确 nodeid。"""

    active_suites = {suite} if suite != "all" else {"p0", "p1", "p2"}
    if with_ext:
        active_suites.add("ext")
    planned = set(planned_files)
    collected = set(collected_nodeids)
    errors: list[str] = []
    for case in data.get("cases", []):
        if not isinstance(case, dict) or case.get("planner") != "coverage-map":
            continue
        if case.get("suite") not in active_suites:
            continue
        source = case.get("source")
        if not isinstance(source, dict):
            continue
        nodeids, _ = _source_nodeids(source, f"minimal_set.{case.get('id')}")
        if not nodeids or nodeids[0].split("::", 1)[0] not in planned:
            continue
        for nodeid in nodeids:
            if nodeid not in collected:
                errors.append(f"minimal-set nodeid was not collected: {nodeid}")
    return errors


def _coverage_context(repo_root: Path) -> dict[str, Any]:
    path = repo_root / "tests/libcurl_consistency/coverage-map.yaml"
    return load_coverage_map(path) if path.is_file() else {}


def _validate_planner_contract(
    case: dict[str, Any],
    *,
    label: str,
    nodeids: list[str],
    coverage: dict[str, Any],
) -> list[str]:
    errors: list[str] = []
    suite = case.get("suite")
    planner = case.get("planner")
    if suite not in SUITES:
        errors.append(f"{label}: suite must be one of p0/p1/p2/ext")
    if planner not in {"coverage-map", "upstream"}:
        errors.append(f"{label}: planner must be coverage-map or upstream")
    if planner != "coverage-map" or suite not in SUITES or not nodeids:
        return errors

    path = nodeids[0].split("::", 1)[0]
    planner_suite = "all" if suite == "ext" else suite
    planned_files = coverage_pytest_files(coverage, planner_suite, with_ext=suite == "ext")
    if path not in planned_files:
        errors.append(f"{label}: pytest path is absent from the {suite} planner: {path}")
    authority = set(coverage_authority_nodeids(coverage))
    for nodeid in nodeids:
        if nodeid not in authority:
            errors.append(f"{label}: exact collected nodeid is absent from static authority: {nodeid}")

    policies = [
        policy
        for policy in coverage.get("exclusion_policies", [])
        if isinstance(policy, dict)
        and policy.get("path") == path
        and suite in policy.get("suites", [])
    ]
    declared = case.get("exclusion_policy")
    if policies and not isinstance(declared, dict):
        errors.append(f"{label}: exclusion_policy missing for capability-gated planner file")
    elif isinstance(declared, dict):
        if not any(
            declared.get("capability") == policy.get("capability")
            and str(policy.get("reason_contains") or "") in str(declared.get("reason_contains") or "")
            for policy in policies
        ):
            errors.append(f"{label}: exclusion_policy differs from coverage-map policy")
    return errors


def validate_minimal_set(data: dict[str, Any], repo_root: Path) -> list[str]:
    """验证最小集合全部条目、精确 nodeid、suite、planner 和 exclusion。"""

    cases = data.get("cases")
    if not isinstance(cases, list):
        return ["minimal set cases must be a list"]
    errors = [] if len(cases) == 31 else [f"minimal set must contain 31 cases, found {len(cases)}"]
    ids: set[str] = set()
    coverage = _coverage_context(repo_root)
    for index, case in enumerate(cases):
        label = f"minimal_set.cases[{index}]"
        if not isinstance(case, dict):
            errors.append(f"{label}: case must be a mapping")
            continue
        case_id = case.get("id")
        if not isinstance(case_id, str) or not case_id:
            errors.append(f"{label}: id missing")
        elif case_id in ids:
            errors.append(f"{label}: duplicate id: {case_id}")
        else:
            ids.add(case_id)
        source = case.get("source")
        if not isinstance(source, dict):
            errors.append(f"{label}: source missing")
            continue
        source_type = source.get("type")
        if source_type == "pytest":
            nodeids, nodeid_errors = _source_nodeids(source, label)
            errors.extend(nodeid_errors)
            for nodeid in nodeids:
                errors.extend(validate_nodeid(repo_root, nodeid, label=label))
            errors.extend(
                _validate_planner_contract(
                    case,
                    label=label,
                    nodeids=nodeids,
                    coverage=coverage,
                )
            )
        elif source_type == "curl_data":
            data_path = source.get("path")
            if not isinstance(data_path, str) or not (repo_root / data_path).is_file():
                errors.append(f"{label}: curl_data path does not exist: {data_path}")
            if not isinstance(source.get("test_number"), int):
                errors.append(f"{label}: curl_data test_number missing")
            errors.extend(
                _validate_planner_contract(
                    case,
                    label=label,
                    nodeids=[],
                    coverage=coverage,
                )
            )
        else:
            errors.append(f"{label}: unsupported source type: {source_type}")
    return errors
