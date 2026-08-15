"""QCurl ABI baseline promotion 的 manifest 重放与原子复制。"""

from __future__ import annotations

import os
import re
import shutil
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable

if __package__:
    from . import release_identity
    from .qcurl_abi_common import AbiGateError
    from .qcurl_abi_common import resolve_existing_file
    from .qcurl_abi_manifest import PROMOTION_REQUIRED_GATES
    from .qcurl_abi_manifest import load_promotion_manifest
else:
    import release_identity
    from qcurl_abi_common import AbiGateError
    from qcurl_abi_common import resolve_existing_file
    from qcurl_abi_manifest import PROMOTION_REQUIRED_GATES
    from qcurl_abi_manifest import load_promotion_manifest


PROMOTION_INPUT_ARTIFACTS = {
    "library": "candidate_core_library",
    "old_baseline": "v1_baseline",
    "candidate_snapshot": "abi_hardbreak_current_snapshot",
    "old_to_new_report": "abi_hardbreak_report",
}
PROMOTION_REQUIRED_ARTIFACTS = (
    "candidate_core_library",
    "v1_baseline",
    "abi_hardbreak_report",
    "abi_hardbreak_current_snapshot",
    "core_dynamic_symbols",
    "other_extras_dynamic_symbols",
)
_DIGEST_RE = re.compile(r"^[0-9a-fA-F]{64}$")


@dataclass(frozen=True)
class ArtifactBinding:
    """记录 promotion 输入 artifact 的不可变路径与摘要。"""

    artifact_id: str
    path: Path
    sha256: str
    kind: str


@dataclass(frozen=True)
class PromotionInputs:
    """记录 manifest 与全部 required artifact 的验证前状态。"""

    manifest_path: Path
    manifest_sha256: str
    artifacts: tuple[ArtifactBinding, ...]

    def artifact(self, artifact_id: str) -> ArtifactBinding:
        """返回指定 artifact binding，缺失时 fail closed。"""

        for binding in self.artifacts:
            if binding.artifact_id == artifact_id:
                return binding
        raise AbiGateError(f"promotion input binding missing: {artifact_id}")


def _manifest_paths(
    manifest: dict[str, Any],
    repo: Path,
) -> tuple[list[Path], list[Path]]:
    identity = manifest.get("identity", {})
    capabilities = identity.get("capabilities", {})
    build_values = capabilities.get("build_dirs", [])
    authority = identity.get("authority", {})
    authority_entries = authority.get("entries", [])
    if not isinstance(build_values, list) or not build_values:
        raise AbiGateError("candidate manifest does not record release build directories")
    if not isinstance(authority_entries, list):
        raise AbiGateError("candidate manifest does not record authority inputs")
    if any(not isinstance(value, str) for value in build_values):
        raise AbiGateError("candidate manifest contains invalid release build directories")
    if any(
        not isinstance(entry, dict) or not isinstance(entry.get("path"), str)
        for entry in authority_entries
    ):
        raise AbiGateError("candidate manifest contains invalid replay paths")
    build_dirs = [_resolve_manifest_path(value, repo) for value in build_values]
    authority_paths = [
        _resolve_manifest_path(entry["path"], repo) for entry in authority_entries
    ]
    return build_dirs, authority_paths


def _resolve_manifest_path(value: str, repo: Path) -> Path:
    path = Path(value)
    return (path if path.is_absolute() else repo / path).resolve()


def _artifact_binding(
    manifest: dict[str, Any],
    repo: Path,
    artifact_id: str,
) -> ArtifactBinding:
    artifacts = manifest.get("artifacts")
    artifact = artifacts.get(artifact_id) if isinstance(artifacts, dict) else None
    if not isinstance(artifact, dict) or artifact.get("required") is not True:
        raise AbiGateError(f"candidate manifest must register required artifact: {artifact_id}")
    value = artifact.get("path")
    digest = artifact.get("sha256")
    kind = artifact.get("kind")
    if not isinstance(value, str) or not value.strip():
        raise AbiGateError(f"promotion input path missing: {artifact_id}")
    if not isinstance(digest, str) or not _DIGEST_RE.fullmatch(digest):
        raise AbiGateError(f"promotion input digest missing or invalid: {artifact_id}")
    if not isinstance(kind, str) or not kind.strip():
        raise AbiGateError(f"promotion input kind missing or invalid: {artifact_id}")
    unresolved = Path(value) if Path(value).is_absolute() else repo / value
    if unresolved.is_symlink():
        raise AbiGateError(f"promotion input is not a regular file: {artifact_id}")
    path = resolve_existing_file(unresolved, f"promotion input: {artifact_id}")
    normalized_digest = digest.lower()
    if release_identity.file_sha256(path) != normalized_digest:
        raise AbiGateError(f"promotion input digest mismatch: {artifact_id}")
    return ArtifactBinding(artifact_id, path, normalized_digest, kind)


def _verify_identity_snapshot(
    manifest: dict[str, Any],
    repo: Path,
) -> list[list[str]]:
    build_dirs, authority_paths = _manifest_paths(manifest, repo)
    commands = manifest.get("identity", {}).get("commands")
    if not isinstance(commands, list):
        raise AbiGateError("candidate manifest does not record gate commands")
    check = release_identity.verify_snapshot(
        manifest,
        repo,
        build_dirs=build_dirs,
        authority_paths=authority_paths,
        commands=commands,
    )
    if not check.valid or check.result != "pass":
        details = "; ".join(check.reasons) or check.result
        raise AbiGateError(f"candidate QA snapshot verification failed: {details}")
    return commands


def _verify_machine_pass(manifest: dict[str, Any]) -> None:
    validation = manifest.get("validation")
    if (
        manifest.get("result") != "pass"
        or not isinstance(validation, dict)
        or validation.get("valid") is not True
    ):
        raise AbiGateError("candidate manifest is not a machine-validated PASS")
    run_guard = manifest.get("run_guard")
    if not isinstance(run_guard, dict) or run_guard.get("identity_stable") is not True:
        raise AbiGateError("candidate identity changed while release gates were running")
    payload_digest = manifest.get(release_identity.MANIFEST_PAYLOAD_DIGEST_FIELD)
    if payload_digest != release_identity.manifest_payload_digest(manifest):
        raise AbiGateError("candidate manifest payload digest mismatch")


def _verify_manifest_path(manifest: dict[str, Any], manifest_path: Path, repo: Path) -> None:
    promotion = manifest.get("promotion")
    recorded = promotion.get("manifest_path") if isinstance(promotion, dict) else None
    if not isinstance(recorded, str):
        raise AbiGateError("candidate manifest promotion path is missing")
    if _resolve_manifest_path(recorded, repo) != manifest_path.resolve():
        raise AbiGateError("candidate manifest path differs from its recorded path")


def _verify_gate_commands(manifest: dict[str, Any], commands: list[list[str]]) -> None:
    gates = manifest.get("gates", {})
    required = gates.get("required", [])
    results = gates.get("results", {})
    command_by_gate = dict(zip(required, commands))
    for gate in PROMOTION_REQUIRED_GATES:
        result = results.get(gate) if isinstance(results, dict) else None
        if gate not in required or not isinstance(result, dict):
            raise AbiGateError(f"baseline promotion requires machine gate: {gate}")
        if result.get("command") != command_by_gate.get(gate):
            raise AbiGateError(f"baseline promotion gate command mismatch: {gate}")


def _verify_cli_inputs(
    manifest: dict[str, Any],
    repo: Path,
    cli_inputs: dict[str, Path | None],
) -> None:
    missing = [name for name, path in cli_inputs.items() if path is None]
    if missing:
        raise AbiGateError("promotion inputs are required: " + ", ".join(missing))
    for artifact_id in PROMOTION_REQUIRED_ARTIFACTS:
        _artifact_binding(manifest, repo, artifact_id)
    for input_name, artifact_id in PROMOTION_INPUT_ARTIFACTS.items():
        binding = _artifact_binding(manifest, repo, artifact_id)
        cli_path = cli_inputs[input_name]
        assert cli_path is not None
        if binding.path != cli_path.resolve():
            raise AbiGateError(f"promotion input path mismatch: {input_name}")


def verify_promotion_manifest(
    manifest: dict[str, Any],
    *,
    manifest_path: Path,
    library: Path | None = None,
    old_baseline: Path | None = None,
    candidate_snapshot: Path | None = None,
    old_to_new_report: Path | None = None,
) -> None:
    """重放机器 release snapshot，并验证 promotion 输入绑定。"""

    repo_value = manifest.get("repo_root")
    if not isinstance(repo_value, str):
        raise AbiGateError("candidate manifest does not record repo_root")
    repo = Path(repo_value).resolve()
    if repo != Path(__file__).resolve().parent.parent:
        raise AbiGateError("candidate manifest belongs to a different repository")
    commands = _verify_identity_snapshot(manifest, repo)
    _verify_machine_pass(manifest)
    _verify_manifest_path(manifest, manifest_path, repo)
    _verify_gate_commands(manifest, commands)
    _verify_cli_inputs(
        manifest,
        repo,
        {
            "library": library,
            "old_baseline": old_baseline,
            "candidate_snapshot": candidate_snapshot,
            "old_to_new_report": old_to_new_report,
        },
    )


def capture_promotion_inputs(
    manifest: dict[str, Any],
    manifest_path: Path,
) -> PromotionInputs:
    """在身份重放前冻结 manifest 与全部 required artifact 摘要。"""

    repo_value = manifest.get("repo_root")
    artifacts = manifest.get("artifacts")
    if not isinstance(repo_value, str) or not isinstance(artifacts, dict):
        raise AbiGateError("candidate manifest promotion inputs are incomplete")
    repo = Path(repo_value).resolve()
    unresolved_manifest = manifest_path
    if unresolved_manifest.is_symlink():
        raise AbiGateError("candidate manifest must be a regular file")
    resolved_manifest = resolve_existing_file(unresolved_manifest, "candidate QA manifest")
    required_ids = sorted(
        artifact_id
        for artifact_id, artifact in artifacts.items()
        if isinstance(artifact, dict) and artifact.get("required") is True
    )
    if not required_ids:
        raise AbiGateError("candidate manifest has no required artifacts")
    bindings = tuple(_artifact_binding(manifest, repo, item) for item in required_ids)
    return PromotionInputs(
        resolved_manifest,
        release_identity.file_sha256(resolved_manifest),
        bindings,
    )


def assert_promotion_inputs_unchanged(inputs: PromotionInputs) -> None:
    """确认 verification 之后没有 manifest 或 artifact 漂移。"""

    if inputs.manifest_path.is_symlink() or not inputs.manifest_path.is_file():
        raise AbiGateError("candidate manifest changed after verification")
    if release_identity.file_sha256(inputs.manifest_path) != inputs.manifest_sha256:
        raise AbiGateError("candidate manifest changed after verification")
    for binding in inputs.artifacts:
        if binding.path.is_symlink() or not binding.path.is_file():
            raise AbiGateError(
                f"promotion input changed after verification: {binding.artifact_id}"
            )
        if release_identity.file_sha256(binding.path) != binding.sha256:
            raise AbiGateError(
                f"promotion input changed after verification: {binding.artifact_id}"
            )


def _fsync_directory(path: Path) -> None:
    flags = os.O_RDONLY | getattr(os, "O_DIRECTORY", 0)
    directory_fd = os.open(path, flags)
    try:
        os.fsync(directory_fd)
    finally:
        os.close(directory_fd)


def atomic_copy_verified_snapshot(
    source: Path,
    target: Path,
    expected_digest: str,
    *,
    before_replace: Callable[[], None] | None = None,
) -> None:
    """在目标同目录原子复制 snapshot，并复核复制前后摘要。"""

    source_path = resolve_existing_file(source, "verified ABI snapshot")
    if source.is_symlink() or release_identity.file_sha256(source_path) != expected_digest:
        raise AbiGateError("verified ABI snapshot changed after verification")
    target_path = target.resolve()
    target_path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{target_path.name}.",
        suffix=".tmp",
        dir=target_path.parent,
    )
    temporary = Path(temporary_name)
    try:
        with source_path.open("rb") as source_stream, os.fdopen(
            descriptor, "wb"
        ) as target_stream:
            shutil.copyfileobj(source_stream, target_stream)
            target_stream.flush()
            os.fsync(target_stream.fileno())
        if release_identity.file_sha256(temporary) != expected_digest:
            raise AbiGateError("temporary ABI snapshot digest mismatch")
        if before_replace is not None:
            before_replace()
        os.replace(temporary, target_path)
        _fsync_directory(target_path.parent)
        if release_identity.file_sha256(target_path) != expected_digest:
            raise AbiGateError("promoted ABI target digest mismatch")
    except AbiGateError:
        raise
    except OSError as exc:
        raise AbiGateError(f"atomic baseline copy failed: {exc}") from exc
    finally:
        if temporary.exists():
            temporary.unlink()
