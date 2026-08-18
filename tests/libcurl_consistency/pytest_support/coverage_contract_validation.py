"""coverage map 的静态结构校验。"""

from __future__ import annotations

from pathlib import Path
from typing import Any

from .contract_map import coverage_authority_nodeids
from .contract_validation import SUITES
from .contract_validation import validate_nodeid


def _validate_evidence_types(data: dict[str, Any], errors: list[str]) -> set[str] | None:
    evidence_types = data.get("evidence_types")
    if not isinstance(evidence_types, dict):
        errors.append("coverage map evidence_types must be a mapping")
        return None
    known_types = set(evidence_types)
    if not known_types >= {"contract", "regression", "smoke", "diagnostic"}:
        errors.append("coverage map evidence_types missing required names")
    return known_types


def _validate_pytest_files(
    data: dict[str, Any],
    repo_root: Path,
    known_types: set[str],
    errors: list[str],
) -> dict[str, dict[str, Any]]:
    files = data.get("pytest_files")
    file_index: dict[str, dict[str, Any]] = {}
    if not isinstance(files, list):
        errors.append("coverage map pytest_files must be a list")
        return file_index
    for index, entry in enumerate(files):
        label = f"pytest_files[{index}]"
        if not isinstance(entry, dict):
            errors.append(f"{label}: entry must be a mapping")
            continue
        path = entry.get("path")
        if not isinstance(path, str):
            errors.append(f"{label}: path missing")
            continue
        if path in file_index:
            errors.append(f"{label}: duplicate path: {path}")
        file_index[path] = entry
        if not (repo_root / path).is_file():
            errors.append(f"{label}: pytest path does not exist: {path}")
        if entry.get("evidence_type") not in known_types:
            errors.append(f"{label}: unknown evidence type: {entry.get('evidence_type')}")
        suites = entry.get("suites")
        if not isinstance(suites, list) or not suites or not set(suites) <= SUITES:
            errors.append(f"{label}: suites must contain only p0/p1/p2/ext")
    return file_index


def _validate_authority(
    data: dict[str, Any],
    repo_root: Path,
    file_index: dict[str, dict[str, Any]],
    errors: list[str],
) -> None:
    try:
        authority_nodeids = coverage_authority_nodeids(data)
    except ValueError as exc:
        errors.append(str(exc))
        authority_nodeids = []
    if len(authority_nodeids) != len(set(authority_nodeids)):
        errors.append("coverage map nodeid_authority contains duplicates")
    for index, nodeid in enumerate(authority_nodeids):
        label = f"nodeid_authority[{index}]"
        errors.extend(validate_nodeid(repo_root, nodeid, label=label))
        if nodeid.split("::", 1)[0] not in file_index:
            errors.append(f"{label}: nodeid path is not in pytest_files")


def _validate_exclusion_policies(
    data: dict[str, Any],
    file_index: dict[str, dict[str, Any]],
    errors: list[str],
) -> None:
    exclusion_policies = data.get("exclusion_policies")
    if not isinstance(exclusion_policies, list):
        errors.append("coverage map exclusion_policies must be a list")
        return
    for index, policy in enumerate(exclusion_policies):
        label = f"exclusion_policies[{index}]"
        if not isinstance(policy, dict):
            errors.append(f"{label}: policy must be a mapping")
            continue
        path = policy.get("path")
        if path not in file_index:
            errors.append(f"{label}: path is not in pytest_files: {path}")
        suites = policy.get("suites")
        if not isinstance(suites, list) or not suites or not set(suites) <= SUITES:
            errors.append(f"{label}: suites must contain only p0/p1/p2/ext")
        if not isinstance(policy.get("capability"), str) or not policy["capability"]:
            errors.append(f"{label}: capability missing")
        if not isinstance(policy.get("reason_contains"), str) or not policy["reason_contains"]:
            errors.append(f"{label}: reason_contains missing")


def _validate_artifact_cases(case_label: str, artifact_cases: object, errors: list[str]) -> None:
    if not isinstance(artifact_cases, dict) or not artifact_cases:
        errors.append(f"{case_label}: artifact_cases must be a non-empty mapping")
        return
    for artifact_case, artifact_fields in artifact_cases.items():
        if not isinstance(artifact_case, str) or not artifact_case or artifact_case.startswith("/"):
            errors.append(f"{case_label}: artifact case key must be relative")
        if (
            not isinstance(artifact_fields, list)
            or not artifact_fields
            or not all(isinstance(field, str) for field in artifact_fields)
        ):
            errors.append(f"{case_label}: artifact case fields must be a non-empty string list")


def _validate_contract_case(
    case: object,
    *,
    case_label: str,
    evidence_type: object,
    known_types: set[str],
    file_index: dict[str, dict[str, Any]],
    repo_root: Path,
    case_ids: set[str],
    errors: list[str],
) -> None:
    if not isinstance(case, dict):
        errors.append(f"{case_label}: case must be a mapping")
        return
    case_id = case.get("id")
    if not isinstance(case_id, str) or not case_id:
        errors.append(f"{case_label}: id missing")
    elif case_id in case_ids:
        errors.append(f"{case_label}: duplicate case id: {case_id}")
    else:
        case_ids.add(case_id)
    path = case.get("pytest")
    if path not in file_index:
        errors.append(f"{case_label}: pytest path is not in pytest_files: {path}")
    if isinstance(path, str) and not (repo_root / path).is_file():
        errors.append(f"{case_label}: pytest path does not exist: {path}")
    case_type = case.get("evidence_type", evidence_type)
    if case_type not in known_types:
        errors.append(f"{case_label}: unknown evidence type: {case_type}")
    fields = case.get("required_fields")
    artifact_cases = case.get("artifact_cases")
    if case_type == "contract" and not fields and not artifact_cases:
        errors.append(f"{case_label}: contract requires required_fields or artifact_cases")
    if fields is not None and (
        not isinstance(fields, list) or not fields or not all(isinstance(field, str) for field in fields)
    ):
        errors.append(f"{case_label}: required_fields must be a non-empty string list")
    if artifact_cases is not None:
        _validate_artifact_cases(case_label, artifact_cases, errors)
    nodeid = case.get("nodeid")
    if not isinstance(nodeid, str):
        errors.append(f"{case_label}: nodeid missing")
    else:
        errors.extend(validate_nodeid(repo_root, nodeid, label=case_label))


def _validate_contracts(
    data: dict[str, Any],
    repo_root: Path,
    known_types: set[str],
    file_index: dict[str, dict[str, Any]],
    errors: list[str],
) -> None:
    contracts = data.get("contracts")
    if not isinstance(contracts, dict):
        errors.append("coverage map contracts must be a mapping")
        return
    case_ids: set[str] = set()
    for contract_name, contract in contracts.items():
        label = f"contracts.{contract_name}"
        if not isinstance(contract, dict):
            errors.append(f"{label}: contract must be a mapping")
            continue
        evidence_type = contract.get("evidence_type")
        if evidence_type not in known_types:
            errors.append(f"{label}: unknown evidence type: {evidence_type}")
        if contract.get("suite") not in SUITES:
            errors.append(f"{label}: invalid suite: {contract.get('suite')}")
        cases = contract.get("cases")
        if not isinstance(cases, list) or not cases:
            errors.append(f"{label}: cases must be a non-empty list")
            continue
        for index, case in enumerate(cases):
            _validate_contract_case(
                case,
                case_label=f"{label}.cases[{index}]",
                evidence_type=evidence_type,
                known_types=known_types,
                file_index=file_index,
                repo_root=repo_root,
                case_ids=case_ids,
                errors=errors,
            )


def validate_coverage_map(data: dict[str, Any], repo_root: Path) -> list[str]:
    """验证 map 的路径、case、nodeid、evidence type 和 required fields。"""

    errors: list[str] = []
    if data.get("schema_version") != 1:
        errors.append("coverage map schema_version must be 1")
    known_types = _validate_evidence_types(data, errors)
    if known_types is None:
        return errors
    file_index = _validate_pytest_files(data, repo_root, known_types, errors)
    _validate_authority(data, repo_root, file_index, errors)
    _validate_exclusion_policies(data, file_index, errors)
    _validate_contracts(data, repo_root, known_types, file_index, errors)
    return errors
