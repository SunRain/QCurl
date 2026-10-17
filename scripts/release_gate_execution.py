"""release gate 的 manifest、artifact 和命令执行编排。"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Callable

if __package__:
    from . import release_identity
    from .release_compatibility_policy import (
        reference_input_records,
        require_clean_identity,
    )
    from .release_compatibility_verification import compatibility_reasons
    from .release_gate_model import GateStep, GateTier
    from .release_evidence_model import ARTIFACT_CONTRACTS
    from .release_evidence_model import command_digest
    from .release_tree_model import required_tree_ids, tree_path
    from .release_tree_model import tree_registry
else:
    import release_identity
    from release_compatibility_policy import (
        reference_input_records,
        require_clean_identity,
    )
    from release_compatibility_verification import compatibility_reasons
    from release_gate_model import GateStep, GateTier
    from release_evidence_model import ARTIFACT_CONTRACTS
    from release_evidence_model import command_digest
    from release_tree_model import required_tree_ids, tree_path
    from release_tree_model import tree_registry


def gate_contracts(steps: list[GateStep]) -> dict[str, dict[str, object]]:
    """从代码定义的 gate step 构造 verifier 使用的固定 producer 合同。"""

    return {
        step.name: {
            "producerTreeId": step.producer_tree_id,
            "command": list(step.command),
            "requiredArtifactIds": list(step.required_artifact_ids),
        }
        for step in steps
    }


def identity_build_dirs(args) -> list[Path]:
    """返回 manifest identity 使用的已绑定 tree 路径。"""

    return [Path(record["path"]) for record in tree_registry(args).values()]


def artifact_path(path: Path, repo_root: Path) -> str:
    try:
        return path.relative_to(repo_root).as_posix()
    except ValueError:
        return str(path)


def source_identity_stable(start: dict[str, object], finish: dict[str, object]) -> bool:
    keys = (
        "head",
        "tracked_patch",
        "untracked",
        "submodules",
        "toolchain",
        "platform",
        "commands",
        "authority",
    )
    return all(start.get(key) == finish.get(key) for key in keys)


def required_artifacts(args, steps: list[GateStep]) -> list[tuple[str, Path]]:
    """返回当前 gate 的基础 artifact 路径集合。"""

    return [
        (f"required_{index}", path)
        for index, path in enumerate(args.required_artifact, start=1)
    ]


def prepare_artifact_parents(
    step: GateStep,
    registry: dict[str, dict[str, object]],
) -> None:
    """为不独占运行目录的 producer 准备 artifact 父目录。"""

    if step.name == "uce_evidence" or step.name.startswith("compatibility_"):
        # UCE 独占创建 run 目录；预建目录会触发其拒绝覆盖已有证据的保护。
        return

    for artifact_id in step.required_artifact_ids:
        contract = ARTIFACT_CONTRACTS[artifact_id]
        tree = registry[contract.tree_id]
        (Path(tree["path"]) / contract.relative_path).parent.mkdir(
            parents=True,
            exist_ok=True,
        )


def _record_additional_artifacts(
    args, manifest: dict, repo_root: Path, steps: list[GateStep]
) -> None:
    for artifact_id, path in required_artifacts(args, steps):
        entry: dict[str, object] = {
            "path": artifact_path(path, repo_root),
            "required": True,
            "kind": "gate-output",
        }
        if path.is_file():
            entry["sha256"] = release_identity.file_sha256(path)
        manifest["artifacts"][artifact_id] = entry


def _record_gate_artifacts(
    args, manifest: dict, repo_root: Path, steps: list[GateStep], registry: dict
) -> None:
    contract_ids = {
        artifact_id for step in steps for artifact_id in step.required_artifact_ids
    }
    manifest["evidence_contract"] = {
        "artifact_ids": sorted(contract_ids),
        "stage": args.stage,
    }
    commands_by_gate = {step.name: step.command for step in steps}
    for artifact_id in sorted(contract_ids):
        contract = ARTIFACT_CONTRACTS[artifact_id]
        tree = registry[contract.tree_id]
        path = Path(tree["path"]) / contract.relative_path
        command = commands_by_gate.get(contract.gate, [])
        manifest["artifacts"][artifact_id] = {
            "path": artifact_path(path, repo_root),
            "required": True,
            "stage": args.stage,
            "treeId": contract.tree_id,
            "buildDir": str(tree["path"]),
            "command": command,
            "commandSha256": command_digest(command),
            "kind": contract.kind,
            "sha256": release_identity.file_sha256(path) if path.is_file() else None,
        }


def write_gate_manifest(
    args,
    repo_root: Path,
    steps: list[GateStep],
    authority_paths: list[Path],
    gate_results: dict[str, dict[str, object]],
    start_identity: dict[str, object],
) -> release_identity.SnapshotCheck:
    """生成绑定固定生产步骤、候选和全部必需证据的 manifest 并立即复核。"""
    commands = [step.command for step in steps]
    registry = tree_registry(args)
    manifest = release_identity.create_snapshot(
        repo_root,
        build_dirs=identity_build_dirs(args),
        authority_paths=authority_paths,
        commands=commands,
        required_gates=[step.name for step in steps],
        stage=args.stage,
        tree_registry=registry,
    )
    manifest["abiMode"] = args.abi_mode
    manifest["gates"]["results"] = gate_results
    manifest["gate_contract"] = gate_contracts(steps)
    manifest["compatibilityInputs"] = reference_input_records(args, repo_root)
    _record_additional_artifacts(args, manifest, repo_root, steps)
    _record_gate_artifacts(args, manifest, repo_root, steps, registry)
    manifest["run_guard"] = {
        "identity_stable": source_identity_stable(start_identity, manifest["identity"]),
    }
    release_identity.write_snapshot(args.manifest, manifest)
    check = release_identity.verify_snapshot(
        manifest,
        repo_root,
        build_dirs=identity_build_dirs(args),
        authority_paths=authority_paths,
        commands=commands,
        tree_registry=registry,
        gate_contracts=gate_contracts(steps),
        expected_stage=args.stage,
    )
    policy_reasons = compatibility_reasons(manifest, repo_root, registry, args=args)
    if policy_reasons:
        check = release_identity.SnapshotCheck(
            False, "blocked", (*check.reasons, *policy_reasons), check.current_identity
        )
    manifest["result"] = check.result
    manifest["validation"] = {"valid": check.valid, "reasons": list(check.reasons)}
    release_identity.write_snapshot(args.manifest, manifest)
    return check


def manifest_authority_paths(
    manifest: dict[str, object], repo_root: Path
) -> list[Path]:
    identity = manifest.get("identity")
    if not isinstance(identity, dict):
        return []
    authority = identity.get("authority")
    if not isinstance(authority, dict) or not isinstance(
        authority.get("entries"), list
    ):
        return []
    paths: list[Path] = []
    for entry in authority["entries"]:
        if isinstance(entry, dict) and isinstance(entry.get("path"), str):
            path = Path(entry["path"])
            paths.append(path if path.is_absolute() else repo_root / path)
    return paths


def _verification_inputs(
    args, manifest: dict, repo_root: Path
) -> tuple[dict, list[Path]]:
    capabilities = manifest.get("identity", {}).get("capabilities", {})
    recorded_registry = capabilities.get("tree_registry", {})
    current_registry = tree_registry(args)
    if args.stage == "final":
        if args.tier is not GateTier.FULL or set(current_registry) != set(
            required_tree_ids(GateTier.FULL)
        ):
            raise ValueError(
                "final verification requires --tier full and all five explicit tree paths"
            )
        registry = current_registry
    else:
        registry = current_registry or recorded_registry
    if not isinstance(registry, dict):
        registry = {}
    authority_paths = list(args.authority)
    if args.tier is GateTier.FULL:
        expected = release_identity.default_authority_paths(repo_root)
        if len(authority_paths) != len(expected) or set(authority_paths) != set(
            expected
        ):
            raise ValueError("full verification requires the unique current authority")
    return registry, authority_paths


def verify_manifest(args, repo_root: Path, steps: list[GateStep] | None = None) -> int:
    """独立重算固定步骤、源码身份、历史输入及全部必需发布产物。"""
    if not args.manifest.is_file():
        print(f"[release_gate] manifest missing: {args.manifest}", file=sys.stderr)
        return 1
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    if manifest.get("stage") != args.stage:
        print("[release_gate] manifest stage does not match --stage", file=sys.stderr)
        return 1
    if manifest.get("abiMode") != args.abi_mode:
        print(
            "[release_gate] manifest ABI mode does not match --abi-mode",
            file=sys.stderr,
        )
        return 1
    try:
        registry, authority_paths = _verification_inputs(args, manifest, repo_root)
    except ValueError as exc:
        print(f"[release_gate] {exc}", file=sys.stderr)
        return 2
    build_dirs = [Path(record["path"]) for record in registry.values()]
    fixed_contracts = gate_contracts(steps) if steps is not None else None
    commands = [step.command for step in steps] if steps is not None else None
    check = release_identity.verify_snapshot(
        manifest,
        repo_root,
        build_dirs=build_dirs,
        authority_paths=authority_paths,
        commands=commands,
        tree_registry=registry,
        gate_contracts=fixed_contracts,
        expected_stage=args.stage,
    )
    policy_reasons = compatibility_reasons(manifest, repo_root, registry, args=args)
    if policy_reasons:
        check = release_identity.SnapshotCheck(
            False, "blocked", (*check.reasons, *policy_reasons), check.current_identity
        )
    payload = {"result": check.result, "valid": check.valid, "reasons": check.reasons}
    print(json.dumps(payload, ensure_ascii=False, indent=2))
    return 0 if check.valid else 1


def write_snapshot_only(
    args, repo_root: Path, steps: list[GateStep], authority_paths: list[Path]
) -> int:
    manifest = release_identity.create_snapshot(
        repo_root,
        build_dirs=identity_build_dirs(args),
        authority_paths=authority_paths,
        commands=[step.command for step in steps],
        stage=args.stage,
        tree_registry=tree_registry(args),
    )
    manifest["abiMode"] = args.abi_mode
    manifest["snapshot_kind"] = "t0"
    release_identity.write_snapshot(args.manifest, manifest)
    print(f"[release_gate] snapshot written: {args.manifest}")
    return 0


def execute_gate(
    args,
    repo_root: Path,
    steps: list[GateStep],
    authority_paths: list[Path],
    *,
    run_step: Callable[[GateStep, Path], int],
) -> int:
    commands = [step.command for step in steps]
    start_identity = release_identity.build_identity(
        repo_root,
        build_dirs=identity_build_dirs(args),
        authority_paths=authority_paths,
        commands=commands,
        tree_registry=tree_registry(args),
    )
    if args.stage == "final":
        require_clean_identity(start_identity)
    gate_results: dict[str, dict[str, object]] = {}
    registry = tree_registry(args)
    for step in steps:
        prepare_artifact_parents(step, registry)
        rc = run_step(step, repo_root)
        gate_results[step.name] = {
            "result": "pass" if rc == 0 else "fail",
            "returncode": rc,
            "command": step.command,
            "producerTreeId": step.producer_tree_id,
        }
        if rc != 0:
            break
    check = write_gate_manifest(
        args, repo_root, steps, authority_paths, gate_results, start_identity
    )
    if not check.valid:
        print("[release_gate] BLOCKED: " + "; ".join(check.reasons), file=sys.stderr)
        return next(
            (
                int(item["returncode"])
                for item in gate_results.values()
                if item["returncode"]
            ),
            1,
        )
    print(f"[release_gate] {args.tier} gate passed; manifest={args.manifest}")
    return 0
