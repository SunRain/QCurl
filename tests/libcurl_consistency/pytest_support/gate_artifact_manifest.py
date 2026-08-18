"""一致性 gate 的工件身份、字段和成对证据核验。"""

from __future__ import annotations

from collections import defaultdict
from dataclasses import dataclass
import json
from pathlib import Path


EVIDENCE_TYPES = {"contract", "smoke", "diagnostic"}


@dataclass(frozen=True)
class _ArtifactScanContext:
    run_id: str
    execution_token: str
    expected_identity: dict[str, object]
    planned_nodeids: set[str]
    file_evidence_types: dict[str, str]
    node_contracts: dict[str, dict[str, object]]


def nodeid_file(nodeid: str) -> str:
    return nodeid.split("::", 1)[0]


def node_evidence_type(
    nodeid: str,
    *,
    file_evidence_types: dict[str, str],
    node_contracts: dict[str, dict[str, object]],
) -> str:
    contract = node_contracts.get(nodeid)
    if isinstance(contract, dict) and contract.get("evidence_type"):
        return str(contract["evidence_type"])
    return file_evidence_types.get(nodeid_file(nodeid), "")


def add_violation(
    violations: list[dict[str, object]],
    code: str,
    detail: str,
    **context: object,
) -> None:
    violations.append({"code": code, "detail": detail, **context})


def _has_field(payload: dict[str, object], field: str) -> bool:
    current: object = payload
    for component in field.split("."):
        if not isinstance(current, dict) or component not in current:
            return False
        current = current[component]
    return True


def _read_artifact(
    path: Path,
    violations: list[dict[str, object]],
) -> dict[str, object] | None:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:
        add_violation(violations, "artifact_json_invalid", str(exc), file=str(path))
        return None
    return payload


def _artifact_nodeid(
    payload: dict[str, object],
    path: Path,
    context: _ArtifactScanContext,
    violations: list[dict[str, object]],
) -> str | None:
    evidence = payload.get("gate_evidence")
    if not isinstance(evidence, dict):
        add_violation(violations, "artifact_identity_missing", "gate_evidence missing", file=str(path))
        return None
    artifact_run_id = str(evidence.get("run_id") or "")
    artifact_token = str(evidence.get("execution_token") or "")
    nodeid = str(evidence.get("pytest_nodeid") or "")
    if artifact_run_id != context.run_id:
        add_violation(
            violations,
            "artifact_run_id_mismatch",
            f"{artifact_run_id!r} != {context.run_id!r}",
            file=str(path),
        )
    if context.execution_token and artifact_token != context.execution_token:
        add_violation(
            violations,
            "artifact_execution_token_mismatch",
            f"{artifact_token!r} != {context.execution_token!r}",
            file=str(path),
        )
    if context.expected_identity and evidence.get("identity") != context.expected_identity:
        add_violation(
            violations,
            "artifact_identity_mismatch",
            "artifact source/build identity differs from the execution plan",
            file=str(path),
        )
    if nodeid not in context.planned_nodeids:
        add_violation(
            violations,
            "artifact_nodeid_unplanned",
            f"artifact nodeid is not planned: {nodeid!r}",
            file=str(path),
        )
        return None
    evidence_type = node_evidence_type(
        nodeid,
        file_evidence_types=context.file_evidence_types,
        node_contracts=context.node_contracts,
    )
    if evidence_type not in EVIDENCE_TYPES:
        add_violation(
            violations,
            "artifact_evidence_unknown",
            f"missing evidence type for {nodeid}",
            file=str(path),
        )
        return None
    return nodeid


def _required_artifact_fields(
    node_contract: dict[str, object],
    case_key: str,
) -> list[object]:
    required_fields = list(node_contract.get("required_fields", []))
    declared_cases = node_contract.get("artifact_cases", {})
    if isinstance(declared_cases, dict):
        case_fields = declared_cases.get(case_key, [])
        if isinstance(case_fields, list):
            required_fields.extend(case_fields)
    return list(dict.fromkeys(required_fields))


def _record_artifact(
    path: Path,
    artifacts_dir: Path,
    context: _ArtifactScanContext,
    by_nodeid: dict[str, set[str]],
    artifact_cases: set[tuple[str, str]],
    violations: list[dict[str, object]],
) -> None:
    flavor = path.stem
    if flavor not in {"baseline", "qcurl"}:
        return
    payload = _read_artifact(path, violations)
    if payload is None:
        return
    nodeid = _artifact_nodeid(payload, path, context, violations)
    if nodeid is None:
        return
    node_contract = context.node_contracts.get(nodeid, {})
    case_key = str(path.parent.relative_to(artifacts_dir))
    for field in _required_artifact_fields(node_contract, case_key):
        if isinstance(field, str) and not _has_field(payload, field):
            add_violation(
                violations,
                "artifact_required_field_missing",
                f"required field missing: {field}",
                file=str(path),
                nodeid=nodeid,
            )
    by_nodeid[nodeid].add(f"{case_key}:{flavor}")
    artifact_cases.add((nodeid, case_key))


def _validate_artifact_pairs(
    by_nodeid: dict[str, set[str]],
    context: _ArtifactScanContext,
    violations: list[dict[str, object]],
) -> None:
    for nodeid, entries in by_nodeid.items():
        evidence_type = node_evidence_type(
            nodeid,
            file_evidence_types=context.file_evidence_types,
            node_contracts=context.node_contracts,
        )
        if evidence_type != "contract":
            continue
        case_flavors: dict[str, set[str]] = defaultdict(set)
        for entry in entries:
            case_key, flavor = entry.rsplit(":", 1)
            case_flavors[case_key].add(flavor)
        node_contract = context.node_contracts.get(nodeid, {})
        declared_cases = node_contract.get("artifact_cases", {})
        if isinstance(declared_cases, dict) and declared_cases:
            expected_cases = set(declared_cases)
            if set(case_flavors) != expected_cases:
                add_violation(
                    violations,
                    "artifact_case_set_mismatch",
                    "artifact case keys differ from the declared set",
                    nodeid=nodeid,
                    expected=sorted(expected_cases),
                    actual=sorted(case_flavors),
                )
        elif len(case_flavors) != 1:
            add_violation(
                violations,
                "artifact_case_count_mismatch",
                "contract nodeid must produce exactly one artifact case unless cases are declared",
                nodeid=nodeid,
                actual=sorted(case_flavors),
            )
        for case_key, flavors in case_flavors.items():
            if flavors != {"baseline", "qcurl"}:
                add_violation(
                    violations,
                    "artifact_pair_missing",
                    f"paired contract requires baseline and qcurl artifacts, got {sorted(flavors)}",
                    nodeid=nodeid,
                    case=case_key,
                )


def _artifact_counts(
    artifact_cases: set[tuple[str, str]],
    context: _ArtifactScanContext,
) -> dict[str, int]:
    counts: dict[str, int] = defaultdict(int)
    for nodeid, _case_key in artifact_cases:
        evidence_type = node_evidence_type(
            nodeid,
            file_evidence_types=context.file_evidence_types,
            node_contracts=context.node_contracts,
        )
        counts[evidence_type] += 1
    return dict(counts)


def scan_artifacts(
    artifacts_dir: Path,
    *,
    run_id: str,
    execution_token: str,
    expected_identity: dict[str, object],
    planned_nodeids: set[str],
    file_evidence_types: dict[str, str],
    node_contracts: dict[str, dict[str, object]],
    violations: list[dict[str, object]],
) -> tuple[dict[str, set[str]], int, dict[str, int]]:
    """扫描本轮成对工件并返回 nodeid、case 与 evidence type 统计。"""

    by_nodeid: dict[str, set[str]] = defaultdict(set)
    artifact_cases: set[tuple[str, str]] = set()
    if not artifacts_dir.exists():
        return by_nodeid, 0, {}
    context = _ArtifactScanContext(
        run_id,
        execution_token,
        expected_identity,
        planned_nodeids,
        file_evidence_types,
        node_contracts,
    )
    for path in sorted(artifacts_dir.rglob("*.json")):
        _record_artifact(path, artifacts_dir, context, by_nodeid, artifact_cases, violations)
    _validate_artifact_pairs(by_nodeid, context, violations)
    return by_nodeid, len(artifact_cases), _artifact_counts(artifact_cases, context)
