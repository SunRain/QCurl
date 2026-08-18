"""一致性 gate 的 YAML 合同解析与运行时映射。"""

from __future__ import annotations

from pathlib import Path
from typing import Any

import yaml


def _load_mapping(path: Path) -> dict[str, Any]:
    value = yaml.safe_load(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"YAML root must be a mapping: {path}")
    return value


def load_coverage_map(path: Path) -> dict[str, Any]:
    """使用 PyYAML 读取 libcurl consistency coverage map。"""

    return _load_mapping(path)


def load_minimal_set(path: Path) -> dict[str, Any]:
    """使用 PyYAML 读取最小回归集合。"""

    return _load_mapping(path)


def coverage_pytest_files(data: dict[str, Any], suite: str, with_ext: bool) -> list[str]:
    """返回指定 suite 的完整 pytest 文件集合。"""

    if suite not in {"p0", "p1", "p2", "all"}:
        raise ValueError(f"invalid suite: {suite}")
    entries = data.get("pytest_files")
    if not isinstance(entries, list):
        raise ValueError("coverage map pytest_files must be a list")
    selected: list[str] = []
    for entry in entries:
        if not isinstance(entry, dict):
            continue
        path = entry.get("path")
        suites = entry.get("suites")
        if not isinstance(path, str) or not isinstance(suites, list):
            continue
        suite_matches = (
            (suite == "all" and bool(set(suites) & {"p0", "p1", "p2"}))
            or suite in suites
        )
        if suite_matches or (with_ext and "ext" in suites):
            selected.append(path)
    return selected


def coverage_evidence_types(data: dict[str, Any]) -> dict[str, str]:
    """返回文件级 evidence type，供 execution plan 使用。"""

    entries = data.get("pytest_files")
    if not isinstance(entries, list):
        return {}
    result: dict[str, str] = {}
    for entry in entries:
        if isinstance(entry, dict) and isinstance(entry.get("path"), str):
            result[entry["path"]] = str(entry.get("evidence_type") or "")
    return result


def coverage_authority_nodeids(data: dict[str, Any]) -> list[str]:
    """返回与当前测试源码独立维护的完整静态 nodeid authority。"""

    entries = data.get("nodeid_authority")
    if not isinstance(entries, list) or not all(isinstance(value, str) for value in entries):
        raise ValueError("coverage map nodeid_authority must be a string list")
    return list(entries)


def validate_collected_cases(
    data: dict[str, Any],
    *,
    planned_files: list[str],
    planned_nodeids: list[str],
    authority_nodeids: list[str] | None = None,
) -> list[str]:
    """双向核对静态 authority 与本轮已规划文件的真实收集结果。"""

    errors: list[str] = []
    planned_file_set = set(planned_files)
    authority = authority_nodeids
    if authority is None:
        try:
            authority = coverage_authority_nodeids(data)
        except ValueError as exc:
            return [str(exc)]
    expected_nodeids = [
        nodeid for nodeid in authority if nodeid.split("::", 1)[0] in planned_file_set
    ]
    expected_set = set(expected_nodeids)
    planned_set = set(planned_nodeids)
    for nodeid in expected_nodeids:
        if nodeid not in planned_set:
            errors.append(f"static authority nodeid was not collected: {nodeid}")
    for nodeid in planned_nodeids:
        if nodeid not in expected_set:
            errors.append(f"collected nodeid is absent from static authority: {nodeid}")
    if planned_set == expected_set and planned_nodeids != expected_nodeids:
        errors.append("collected nodeid order differs from static authority")
    contracts = data.get("contracts")
    if not isinstance(contracts, dict):
        return ["coverage map contracts must be a mapping"]
    for contract_name, contract in contracts.items():
        if not isinstance(contract, dict):
            continue
        cases = contract.get("cases")
        if not isinstance(cases, list):
            continue
        for case in cases:
            if not isinstance(case, dict) or case.get("pytest") not in planned_file_set:
                continue
            nodeid = case.get("nodeid")
            if not isinstance(nodeid, str):
                continue
            if not any(value == nodeid or value.startswith(f"{nodeid}[") for value in planned_nodeids):
                errors.append(
                    f"contracts.{contract_name}.{case.get('id')}: nodeid was not collected: {nodeid}"
                )
    return errors


def validate_planner_exclusions(
    data: dict[str, Any],
    *,
    suite: str,
    exclusions: dict[str, str],
    with_ext: bool = False,
) -> list[str]:
    """验证每个 exclusion 均由 suite、文件和 capability policy 显式授权。"""

    policies = data.get("exclusion_policies")
    if not isinstance(policies, list):
        return ["coverage map exclusion_policies must be a list"] if exclusions else []
    active_suites = {suite} if suite != "all" else {"p0", "p1", "p2"}
    if with_ext:
        active_suites.add("ext")
    errors: list[str] = []
    for path, reason in exclusions.items():
        matching = []
        for policy in policies:
            if not isinstance(policy, dict) or policy.get("path") != path:
                continue
            policy_suites = policy.get("suites")
            if isinstance(policy_suites, list) and active_suites.intersection(policy_suites):
                matching.append(policy)
        authorized = any(
            isinstance(policy.get("capability"), str)
            and isinstance(policy.get("reason_contains"), str)
            and policy["reason_contains"] in reason
            for policy in matching
        )
        if not authorized:
            errors.append(f"planner exclusion is not authorized by static policy: {path}: {reason}")
    return errors


def coverage_node_contracts(
    data: dict[str, Any],
    *,
    planned_nodeids: list[str],
) -> dict[str, dict[str, object]]:
    """把文件级 evidence type 与 case required fields 展开到精确 nodeid。"""

    file_types = coverage_evidence_types(data)
    contracts_by_nodeid = {
        nodeid: {
            "evidence_type": file_types.get(nodeid.split("::", 1)[0], ""),
            "required_fields": [],
            "artifact_cases": {},
        }
        for nodeid in planned_nodeids
    }
    contracts = data.get("contracts")
    if not isinstance(contracts, dict):
        return contracts_by_nodeid
    for contract in contracts.values():
        if not isinstance(contract, dict):
            continue
        default_type = str(contract.get("evidence_type") or "")
        cases = contract.get("cases")
        if not isinstance(cases, list):
            continue
        for case in cases:
            if not isinstance(case, dict) or not isinstance(case.get("nodeid"), str):
                continue
            prefix = case["nodeid"]
            fields = case.get("required_fields")
            artifact_cases = case.get("artifact_cases")
            for nodeid in planned_nodeids:
                if nodeid != prefix and not nodeid.startswith(f"{prefix}["):
                    continue
                node_contract = contracts_by_nodeid[nodeid]
                node_contract["evidence_type"] = str(case.get("evidence_type") or default_type)
                if isinstance(fields, list):
                    current = node_contract["required_fields"]
                    node_contract["required_fields"] = list(dict.fromkeys([*current, *fields]))
                if isinstance(artifact_cases, dict):
                    current_cases = node_contract["artifact_cases"]
                    if isinstance(current_cases, dict):
                        current_cases.update(artifact_cases)
    return contracts_by_nodeid
