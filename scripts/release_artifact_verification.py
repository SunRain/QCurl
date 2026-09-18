"""release manifest required artifact 的独立验证逻辑。"""

from __future__ import annotations

import json
import re
import subprocess
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
            if (
                not isinstance(recorded_ids, list)
                or not all(isinstance(value, str) for value in recorded_ids)
                or len(recorded_ids) != len(contract_ids)
                or set(recorded_ids) != contract_ids
            ):
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


def _sanitizer_option_reasons(report: dict[str, Any]) -> list[str]:
    """确保必需检测及失败退出语义未在报告中被关闭。"""

    required = {
        "ASAN_OPTIONS": {
            "detect_leaks": "1",
            "leak_check_at_exit": "1",
            "halt_on_error": "1",
            "exitcode": "1",
        },
        "UBSAN_OPTIONS": {"halt_on_error": "1", "exitcode": "1"},
        "LSAN_OPTIONS": {
            "detect_leaks": "1",
            "leak_check_at_exit": "1",
            "exitcode": "23",
        },
    }
    recorded = report.get("sanitizer_options")
    if not isinstance(recorded, dict):
        return ["sanitizer runtime options missing"]
    reasons = []
    for name, options in required.items():
        value = recorded.get(name)
        items = value.split(":") if isinstance(value, str) else []
        pairs = [item.split("=", 1) for item in items if "=" in item]
        for key, expected in options.items():
            if [item[1] for item in pairs if item[0] == key] != [expected]:
                reasons.append(
                    f"sanitizer required runtime option mismatch: {name}.{key}"
                )
    return reasons


def _sanitizer_candidate_reasons(
    report: dict[str, Any], repo: Path, path: Path
) -> list[str]:
    """复用 runner 的完整候选身份，拒绝内部一致但来自旧候选的报告。"""

    from scripts.uce_gate.candidate import capture_candidate_fingerprint

    before, after = report.get("candidate_before", {}), report.get(
        "candidate_after", {}
    )
    if not isinstance(before, dict) or not isinstance(after, dict):
        return ["sanitizer candidate identity missing"]
    if not (
        report.get("candidate_unchanged") is True
        and before.get("available") is True
        and after.get("available") is True
        and before.get("combined")
        and before.get("combined") == after.get("combined")
    ):
        return ["sanitizer candidate identity changed or missing"]
    try:
        current = capture_candidate_fingerprint(repo, excluded_paths=(path.parent,))
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        return [f"sanitizer current candidate identity unavailable: {error}"]
    if current.get("available") is not True or before["combined"] != current.get(
        "combined"
    ):
        return ["sanitizer report candidate does not match current candidate"]
    return []


def _sanitizer_report_reasons(
    path: Path, artifact: dict[str, Any], repo: Path
) -> list[str]:
    """拒绝只声明 PASS、禁用泄漏检测或检测进程未成功的 sanitizer 报告。"""

    try:
        report = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError):
        return ["sanitizer report is unreadable"]
    if not isinstance(report, dict):
        return ["sanitizer report schema mismatch"]
    reasons = []
    if (
        report.get("schema") != "qcurl-uce/sanitizer-report@v1"
        or report.get("profile") != "asan-ubsan-lsan"
    ):
        reasons.append("sanitizer report schema or profile mismatch")
    if report.get("build_dir") != artifact.get("buildDir") or report.get(
        "output_dir"
    ) != str(path.parent):
        reasons.append("sanitizer report producer path mismatch")
    if report.get("result") != "pass" or report.get("policy_violations") != []:
        reasons.append("sanitizer report did not pass")
    for stage in ("configure", "build", "subject"):
        if report.get(f"{stage}_returncode") != 0:
            reasons.append(f"sanitizer {stage} missing or failed")
    reasons.extend(_sanitizer_candidate_reasons(report, repo, path))
    commands = report.get("subject_commands")
    records = report.get("command_results")
    if not isinstance(commands, list) or not commands or not isinstance(records, list):
        reasons.append("sanitizer subject execution evidence missing")
    else:
        for command in commands:
            matches = [
                entry
                for entry in records
                if isinstance(entry, dict) and entry.get("command") == command
            ]
            if (
                not command
                or len(matches) != 1
                or matches[0].get("returncode") != 0
                or matches[0].get("timed_out") is not False
            ):
                reasons.append("sanitizer subject execution missing or failed")
    reasons.extend(_sanitizer_option_reasons(report))
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
        if artifact_id == "asan_ubsan_lsan_report":
            reasons.extend(_sanitizer_report_reasons(path, artifact, repo))
    return reasons
