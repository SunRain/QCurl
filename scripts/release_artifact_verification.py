"""release manifest required artifact 的独立验证逻辑。"""

from __future__ import annotations

import re
from pathlib import Path
from typing import Any

if __package__:
    from . import release_identity
    from .release_evidence_model import ARTIFACT_CONTRACTS
    from .release_evidence_model import ArtifactContract
    from .release_evidence_model import artifact_schema_valid
    from .release_evidence_model import command_digest
    from .release_evidence_model import required_artifact_ids
else:
    import release_identity
    from release_evidence_model import ARTIFACT_CONTRACTS
    from release_evidence_model import ArtifactContract
    from release_evidence_model import artifact_schema_valid
    from release_evidence_model import command_digest
    from release_evidence_model import required_artifact_ids


def _contract_reasons(
    manifest: dict[str, Any],
    required_gates: object,
) -> tuple[set[str], list[str]]:
    """从 required gate 重算 artifact 集合并核对 manifest 声明。"""

    contract_ids = required_artifact_ids(required_gates)
    evidence_contract = manifest.get("evidence_contract")
    reasons: list[str] = []
    if contract_ids:
        if not isinstance(evidence_contract, dict):
            reasons.append("required artifact contract missing")
        else:
            recorded_ids = evidence_contract.get("artifact_ids")
            if not isinstance(recorded_ids, list) or set(recorded_ids) != contract_ids:
                reasons.append("required artifact contract mismatch")
            if evidence_contract.get("stage") != manifest.get("stage"):
                reasons.append("required artifact contract stage mismatch")
    elif isinstance(evidence_contract, dict):
        if evidence_contract.get("artifact_ids") not in ([], None):
            reasons.append("unexpected required artifact contract")
    return contract_ids, reasons


def _artifact_path(artifact: dict[str, Any], repo: Path) -> tuple[Path, str | None]:
    value = artifact.get("path")
    if not isinstance(value, str):
        return Path(), None
    path = Path(value)
    return (path if path.is_absolute() else repo / path), value


def _file_reasons(
    artifact_id: str,
    artifact: dict[str, Any],
    path: Path,
    value: str | None,
) -> list[str]:
    """验证 required artifact 的文件类型、大小和内容摘要。"""

    reasons: list[str] = []
    kind = artifact.get("kind")
    if not isinstance(kind, str) or not kind.strip():
        reasons.append(f"required artifact kind missing: {artifact_id}")
    regular = value is not None and not path.is_symlink() and path.is_file()
    if not regular:
        reasons.append(
            f"required artifact is not a regular file or has invalid digest: {artifact_id}"
        )
    elif path.stat().st_size == 0:
        reasons.append(f"required artifact is empty: {artifact_id}")
    digest = artifact.get("sha256")
    if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-fA-F]{64}", digest):
        reasons.append(f"required artifact digest missing or invalid: {artifact_id}")
    elif regular and release_identity.file_sha256(path) != digest.lower():
        reasons.append(f"required artifact digest mismatch: {artifact_id}")
    return reasons


def _location_reasons(
    artifact_id: str,
    artifact: dict[str, Any],
    path: Path,
    contract: ArtifactContract,
    registry: dict[str, dict[str, Any]],
    stage: object,
) -> list[str]:
    """核对 stage、producer tree、固定路径、buildDir 和 schema。"""

    reasons: list[str] = []
    if artifact.get("stage") != stage:
        reasons.append(f"required artifact stage mismatch: {artifact_id}")
    if artifact.get("treeId") != contract.tree_id:
        reasons.append(f"required artifact producer tree mismatch: {artifact_id}")
    tree = registry.get(contract.tree_id)
    if not isinstance(tree, dict) or not isinstance(tree.get("path"), str):
        reasons.append(f"required artifact producer tree registry missing: {artifact_id}")
    else:
        expected_path = Path(tree["path"]) / contract.relative_path
        if path.resolve() != expected_path.resolve():
            reasons.append(f"required artifact path is outside producer tree: {artifact_id}")
        if artifact.get("buildDir") != tree["path"]:
            reasons.append(f"required artifact build directory mismatch: {artifact_id}")
    if artifact.get("kind") != contract.kind:
        reasons.append(f"required artifact kind mismatch: {artifact_id}")
    elif path.is_file() and not path.is_symlink():
        if not artifact_schema_valid(path, contract.kind):
            reasons.append(f"required artifact schema mismatch: {artifact_id}")
    return reasons


def _command_reasons(
    artifact_id: str,
    artifact: dict[str, Any],
    contract: ArtifactContract,
    gates: object,
    gate_contracts: dict[str, dict[str, Any]] | None,
) -> list[str]:
    """核对 artifact、gate result 和代码固定 gate 的命令 provenance。"""

    reasons: list[str] = []
    command = artifact.get("command")
    recorded_digest = artifact.get("commandSha256")
    valid_command = isinstance(command, list) and all(
        isinstance(part, str) for part in command
    )
    if not valid_command or not isinstance(recorded_digest, str):
        reasons.append(f"required artifact command provenance missing: {artifact_id}")
    elif recorded_digest != command_digest(command):
        reasons.append(f"required artifact command digest mismatch: {artifact_id}")
    results = gates.get("results", {}) if isinstance(gates, dict) else {}
    gate_result = results.get(contract.gate) if isinstance(results, dict) else None
    if not isinstance(gate_result, dict):
        reasons.append(f"required artifact gate result missing: {artifact_id}")
        return reasons
    if gate_result.get("producerTreeId") != contract.tree_id:
        reasons.append(f"required artifact gate producer mismatch: {artifact_id}")
    if isinstance(gate_result.get("command"), list) and command != gate_result["command"]:
        reasons.append(f"required artifact gate command mismatch: {artifact_id}")
    if gate_contracts is not None:
        reasons.extend(
            _fixed_gate_reasons(artifact_id, command, contract, gate_contracts)
        )
    return reasons


def _fixed_gate_reasons(
    artifact_id: str,
    command: object,
    contract: ArtifactContract,
    gate_contracts: dict[str, dict[str, Any]],
) -> list[str]:
    expected = gate_contracts.get(contract.gate)
    if not isinstance(expected, dict):
        return [f"required artifact fixed gate contract missing: {artifact_id}"]
    reasons: list[str] = []
    if expected.get("producerTreeId") != contract.tree_id:
        reasons.append(f"fixed gate producer contract mismatch: {artifact_id}")
    if command != expected.get("command"):
        reasons.append(f"required artifact command mismatch: {artifact_id}")
    return reasons


def artifact_reasons(
    manifest: dict[str, Any],
    repo: Path,
    *,
    tree_registry: dict[str, dict[str, Any]] | None = None,
    gate_contracts: dict[str, dict[str, Any]] | None = None,
) -> list[str]:
    """验证 manifest 中全部 required artifact，不信任自报 required 集合。"""

    artifacts = manifest.get("artifacts", {})
    if not isinstance(artifacts, dict):
        return ["artifact schema missing"]
    gates = manifest.get("gates", {})
    required_gates = gates.get("required", []) if isinstance(gates, dict) else []
    contract_ids, reasons = _contract_reasons(manifest, required_gates)
    stage = manifest.get("stage")
    if stage not in {"remediation", "promotion", "final"}:
        reasons.append("manifest stage is invalid")
    registry = tree_registry if isinstance(tree_registry, dict) else {}
    for artifact_id in sorted(contract_ids):
        artifact = artifacts.get(artifact_id)
        if not isinstance(artifact, dict) or artifact.get("required") is not True:
            reasons.append(f"required release artifact missing: {artifact_id}")
    for artifact_id, artifact in artifacts.items():
        if not isinstance(artifact, dict) or artifact.get("required") is not True:
            continue
        if artifact_id == "qa_manifest":
            reasons.append("manifest must not be registered as a required artifact: qa_manifest")
            continue
        path, value = _artifact_path(artifact, repo)
        reasons.extend(_file_reasons(artifact_id, artifact, path, value))
        contract = ARTIFACT_CONTRACTS.get(artifact_id)
        if contract is None:
            continue
        reasons.extend(
            _location_reasons(
                artifact_id, artifact, path, contract, registry, stage
            )
        )
        reasons.extend(
            _command_reasons(
                artifact_id, artifact, contract, gates, gate_contracts
            )
        )
    return reasons
