"""一致性 gate 的计划、执行集合与工件闭环核验。"""

from __future__ import annotations

from collections import defaultdict
from pathlib import Path
import xml.etree.ElementTree as ET

from .gate_artifact_manifest import EVIDENCE_TYPES
from .gate_artifact_manifest import add_violation
from .gate_artifact_manifest import node_evidence_type
from .gate_artifact_manifest import nodeid_file
from .gate_artifact_manifest import scan_artifacts


def collect_nodeids_from_output(output: str) -> list[str]:
    """从 pytest ``--collect-only -q`` 输出提取完整参数化 nodeid。"""

    return [
        line.strip()
        for line in output.splitlines()
        if ".py::" in line and not line.lstrip().startswith(("ERROR ", "WARNING "))
    ]


def parse_junit_nodeids(junit_xml: Path) -> dict[str, object]:
    """读取由 gate fixture 写入 JUnit property 的精确 pytest nodeid。"""

    if not junit_xml.exists():
        return {"nodeids": [], "parse_error": f"junit xml not found: {junit_xml}"}
    try:
        root = ET.parse(junit_xml).getroot()
    except Exception as exc:
        return {"nodeids": [], "parse_error": f"failed to parse junit xml: {exc}"}

    nodeids: list[str] = []
    for testcase in root.findall(".//testcase"):
        value = ""
        for prop in testcase.findall("./properties/property"):
            if prop.attrib.get("name") == "nodeid":
                value = str(prop.attrib.get("value") or "").strip()
                break
        if value:
            nodeids.append(value)
    return {"nodeids": nodeids, "parse_error": ""}


def _validate_planner_files(
    candidate_files: list[str],
    planned_files: list[str],
    planner_exclusions: dict[str, str],
    violations: list[dict[str, object]],
) -> None:
    candidate_set = set(candidate_files)
    planned_file_set = set(planned_files)
    excluded_set = set(planner_exclusions)
    if planned_file_set & excluded_set or planned_file_set | excluded_set != candidate_set:
        add_violation(
            violations,
            "planner_partition_mismatch",
            "candidate files must be partitioned into planned files and explicit exclusions",
        )
    for path, reason in planner_exclusions.items():
        if not str(reason).strip():
            add_violation(
                violations,
                "planner_exclusion_invalid",
                "planner exclusion requires a non-empty reason",
                file=path,
            )


def _validate_planned_nodeids(
    planned_files: list[str],
    planned_nodeids: list[str],
    file_evidence_types: dict[str, str],
    violations: list[dict[str, object]],
) -> set[str]:
    nodeid_set = set(planned_nodeids)
    if len(nodeid_set) != len(planned_nodeids):
        add_violation(violations, "planned_nodeid_duplicate", "planned nodeids contain duplicates")
    for path in planned_files:
        evidence_type = file_evidence_types.get(path, "")
        if evidence_type not in EVIDENCE_TYPES:
            add_violation(
                violations,
                "evidence_type_missing",
                f"planned file has no valid evidence type: {evidence_type!r}",
                file=path,
            )
        if not any(nodeid_file(nodeid) == path for nodeid in planned_nodeids):
            add_violation(
                violations,
                "planner_file_empty",
                "planned pytest file collected no nodeids",
                file=path,
            )
    return nodeid_set


def _validate_junit_nodeids(
    junit_xml: Path,
    planned_nodeids: list[str],
    violations: list[dict[str, object]],
) -> list[str]:
    junit = parse_junit_nodeids(junit_xml)
    executed_nodeids = list(junit.get("nodeids") or [])
    parse_error = str(junit.get("parse_error") or "")
    if parse_error:
        add_violation(violations, "junit_nodeids_parse_error", parse_error)
    if executed_nodeids != planned_nodeids:
        add_violation(
            violations,
            "execution_set_mismatch",
            "JUnit nodeids differ from the planned ordered nodeids",
            planned=planned_nodeids,
            executed=executed_nodeids,
        )
    return executed_nodeids


def _validate_contract_artifacts(
    planned_nodeids: list[str],
    artifacts_by_nodeid: dict[str, set[str]],
    file_evidence_types: dict[str, str],
    node_contracts: dict[str, dict[str, object]],
    violations: list[dict[str, object]],
) -> None:
    for nodeid in planned_nodeids:
        evidence_type = node_evidence_type(
            nodeid,
            file_evidence_types=file_evidence_types,
            node_contracts=node_contracts,
        )
        if evidence_type == "contract" and not artifacts_by_nodeid.get(nodeid):
            add_violation(
                violations,
                "artifact_pair_missing",
                "paired contract produced no artifacts",
                nodeid=nodeid,
            )


def _planned_counts(
    planned_nodeids: list[str],
    file_evidence_types: dict[str, str],
    node_contracts: dict[str, dict[str, object]],
) -> dict[str, int]:
    counts: dict[str, int] = defaultdict(int)
    for nodeid in planned_nodeids:
        evidence_type = node_evidence_type(
            nodeid,
            file_evidence_types=file_evidence_types,
            node_contracts=node_contracts,
        )
        counts[evidence_type] += 1
    return dict(counts)


def evaluate_execution_contract(
    *,
    run_id: str,
    candidate_files: list[str],
    planned_files: list[str],
    planner_exclusions: dict[str, str],
    planned_nodeids: list[str],
    junit_xml: Path,
    artifacts_dir: Path,
    file_evidence_types: dict[str, str],
    node_contracts: dict[str, dict[str, object]] | None = None,
    execution_token: str = "",
    expected_identity: dict[str, object] | None = None,
) -> dict[str, object]:
    """核验候选文件、精确 nodeid、JUnit 与本轮工件是否形成闭环。"""

    violations: list[dict[str, object]] = []
    node_contracts = node_contracts or {}
    _validate_planner_files(candidate_files, planned_files, planner_exclusions, violations)
    nodeid_set = _validate_planned_nodeids(
        planned_files,
        planned_nodeids,
        file_evidence_types,
        violations,
    )
    executed_nodeids = _validate_junit_nodeids(junit_xml, planned_nodeids, violations)
    artifacts_by_nodeid, artifact_cases, artifact_counts = scan_artifacts(
        artifacts_dir,
        run_id=run_id,
        execution_token=execution_token,
        expected_identity=expected_identity or {},
        planned_nodeids=nodeid_set,
        file_evidence_types=file_evidence_types,
        node_contracts=node_contracts,
        violations=violations,
    )
    _validate_contract_artifacts(
        planned_nodeids,
        artifacts_by_nodeid,
        file_evidence_types,
        node_contracts,
        violations,
    )
    return {
        "run_id": run_id,
        "execution_token": execution_token,
        "candidate_files": candidate_files,
        "planned_files": planned_files,
        "planner_exclusions": planner_exclusions,
        "planned_nodeids": planned_nodeids,
        "executed_nodeids": executed_nodeids,
        "artifact_cases": artifact_cases,
        "evidence_summary": {
            "planned_nodeids": _planned_counts(planned_nodeids, file_evidence_types, node_contracts),
            "artifact_cases": artifact_counts,
        },
        "violations": violations,
        "violation_codes": list(dict.fromkeys(str(item["code"]) for item in violations)),
    }
