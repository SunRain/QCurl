"""QCurl ABI promotion manifest 的静态前置校验。"""

from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Any

if __package__:
    from .qcurl_abi_common import AbiGateError
    from .qcurl_abi_common import resolve_existing_file
else:
    from qcurl_abi_common import AbiGateError
    from qcurl_abi_common import resolve_existing_file


PROMOTION_REQUIRED_GATES = (
    "shared_package_evidence",
    "static_package_evidence",
    "dynamic_symbol_allowlist",
    "other_extras_dynamic_symbol_allowlist",
    "abi_hardbreak_report",
)
_COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")


def _manifest_entries(identity: dict[str, Any], section: str) -> list[Any]:
    value = identity.get(section)
    entries = value.get("entries") if isinstance(value, dict) else None
    if not isinstance(entries, list):
        raise AbiGateError(f"candidate manifest {section} schema is invalid")
    return entries


def _validate_toolchain(identity: dict[str, Any]) -> None:
    toolchain = identity.get("toolchain")
    compilers = toolchain.get("compilers") if isinstance(toolchain, dict) else None
    libabigail = toolchain.get("libabigail") if isinstance(toolchain, dict) else None
    values = [toolchain.get("cmake") if isinstance(toolchain, dict) else None]
    if isinstance(compilers, dict):
        values.extend(compilers.values())
    if isinstance(libabigail, dict):
        values.extend(libabigail.values())
    invalid = any(
        not isinstance(value, str) or value.startswith("<unavailable:")
        for value in values
    )
    if (
        not isinstance(toolchain, dict)
        or not isinstance(compilers, dict)
        or not compilers
        or not isinstance(libabigail, dict)
        or set(libabigail) < {"abidiff", "abidw"}
        or invalid
    ):
        raise AbiGateError("baseline promotion requires a fully recorded fixed toolchain")


def _validate_candidate_identity(identity: dict[str, Any], candidate_commit: str) -> None:
    head = identity.get("head")
    if not isinstance(head, str) or not _COMMIT_RE.fullmatch(head):
        raise AbiGateError("candidate manifest head must be a full commit SHA")
    if not _COMMIT_RE.fullmatch(candidate_commit) or head != candidate_commit:
        raise AbiGateError("candidate manifest does not match the recorded full commit SHA")
    tracked = _manifest_entries(identity, "tracked_patch")
    untracked = _manifest_entries(identity, "untracked")
    submodules = _manifest_entries(identity, "submodules")
    dirty_submodules = [
        entry for entry in submodules if isinstance(entry, dict) and entry.get("dirty")
    ]
    if tracked or untracked or dirty_submodules:
        raise AbiGateError("baseline promotion requires a clean candidate worktree and submodules")
    platform = identity.get("platform")
    if not isinstance(platform, dict) or platform.get("system") != "Linux":
        raise AbiGateError("baseline promotion requires a Linux ELF candidate")
    _validate_toolchain(identity)


def _validate_gate_prerequisites(manifest: dict[str, Any]) -> None:
    gates = manifest.get("gates")
    required = gates.get("required") if isinstance(gates, dict) else None
    results = gates.get("results") if isinstance(gates, dict) else None
    if not isinstance(required, list) or not isinstance(results, dict):
        raise AbiGateError("candidate manifest gate schema is invalid")
    if "abi_hardbreak_report" not in required:
        raise AbiGateError("promotion candidate manifest requires abi_hardbreak_report")
    if "abi_current_baseline_diff" in required:
        raise AbiGateError(
            "promotion candidate manifest must not include abi_current_baseline_diff"
        )
    for gate in PROMOTION_REQUIRED_GATES:
        result = results.get(gate)
        if (
            not isinstance(result, dict)
            or result.get("result") != "pass"
            or result.get("returncode") != 0
        ):
            raise AbiGateError(f"baseline promotion requires passing gate: {gate}")


def load_promotion_manifest(path: Path, *, candidate_commit: str) -> dict[str, Any]:
    """加载并验证 promotion candidate 的静态前置条件。"""

    try:
        manifest_path = resolve_existing_file(path, "candidate QA manifest")
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError) as exc:
        raise AbiGateError(f"invalid candidate QA manifest: {path}: {exc}") from exc
    identity = manifest.get("identity") if isinstance(manifest, dict) else None
    if manifest.get("schema") != "qa-manifest@v1" or not isinstance(identity, dict):
        raise AbiGateError("candidate manifest must use qa-manifest@v1 with an identity")
    _validate_candidate_identity(identity, candidate_commit)
    _validate_gate_prerequisites(manifest)
    return manifest
