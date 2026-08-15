"""Fail-closed schema and result derivation for release QA manifests."""

from __future__ import annotations

import json
import re
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Sequence

if __package__:
    from . import release_identity
    from .release_artifact_verification import artifact_reasons
else:
    import release_identity
    from release_artifact_verification import artifact_reasons


@dataclass(frozen=True)
class SnapshotCheck:
    """Result of recomputing a manifest identity."""

    valid: bool
    result: str
    reasons: tuple[str, ...]
    current_identity: dict[str, Any] | None = None


def create_snapshot(
    repo: Path,
    *,
    build_dirs: Sequence[Path],
    authority_paths: Sequence[Path],
    commands: Sequence[Sequence[str]],
    required_gates: Sequence[str] = (),
    stage: str = "remediation",
    tree_registry: dict[str, dict[str, Any]] | None = None,
) -> dict[str, Any]:
    """Capture a machine-owned snapshot with no PASS claim."""
    repo = repo.resolve()
    identity = release_identity.build_identity(
        repo,
        build_dirs=build_dirs,
        authority_paths=authority_paths,
        commands=commands,
        tree_registry=tree_registry,
    )
    timestamp = datetime.now(timezone.utc).replace(microsecond=0)
    manifest = {
        "schema": release_identity.SCHEMA,
        "stage": stage,
        "generated_at_utc": timestamp.isoformat().replace("+00:00", "Z"),
        "repo_root": str(repo),
        "identity": identity,
        "identity_digest": release_identity._digest(identity),
        "gates": {"required": list(required_gates), "results": {}},
        "artifacts": {},
        "result": "snapshot",
    }
    manifest[release_identity.MANIFEST_PAYLOAD_DIGEST_FIELD] = (
        release_identity.manifest_payload_digest(manifest)
    )
    return manifest


def _identity_reasons(
    manifest: dict[str, Any],
    repo: Path,
    build_dirs: Sequence[Path],
    authority_paths: Sequence[Path],
    commands: Sequence[Sequence[str]] | None,
    tree_registry: dict[str, dict[str, Any]] | None,
) -> tuple[list[str], dict[str, Any] | None]:
    expected = manifest.get("identity")
    if manifest.get("schema") != release_identity.SCHEMA or not isinstance(
        expected, dict
    ):
        return ["schema or identity missing"], None
    reasons = []
    if manifest.get("identity_digest") != release_identity._digest(expected):
        reasons.append("identity digest mismatch")
    actual = release_identity.build_identity(
        repo.resolve(),
        build_dirs=build_dirs,
        authority_paths=authority_paths,
        commands=commands if commands is not None else expected.get("commands", []),
        tree_registry=tree_registry,
    )
    keys = (
        "head",
        "tracked_patch",
        "untracked",
        "submodules",
        "capabilities",
        "toolchain",
        "platform",
        "commands",
        "authority",
    )
    reasons.extend(
        f"{key} identity mismatch"
        for key in keys
        if expected.get(key) != actual.get(key)
    )
    return reasons, actual


def _gate_reasons(manifest: dict[str, Any]) -> tuple[list[str], list[str]]:
    gates = manifest.get("gates")
    if not isinstance(gates, dict):
        return ["gate schema missing"], []
    required = gates.get("required")
    results = gates.get("results")
    if not isinstance(required, list) or not isinstance(results, dict):
        return ["gate schema missing"], []
    reasons = []
    for gate in required:
        result = results.get(gate)
        if (
            not isinstance(result, dict)
            or result.get("result") != "pass"
            or result.get("returncode") != 0
        ):
            reasons.append(f"required gate missing or failed: {gate}")
    return reasons, required


def _payload_reasons(manifest: dict[str, Any]) -> list[str]:
    recorded = manifest.get(release_identity.MANIFEST_PAYLOAD_DIGEST_FIELD)
    if not isinstance(recorded, str) or not re.fullmatch(r"[0-9a-fA-F]{64}", recorded):
        return ["manifest payload digest missing or invalid"]
    if recorded.lower() != release_identity.manifest_payload_digest(manifest):
        return ["manifest payload digest mismatch"]
    return []


def verify_snapshot(
    manifest: dict[str, Any],
    repo: Path,
    *,
    build_dirs: Sequence[Path],
    authority_paths: Sequence[Path],
    commands: Sequence[Sequence[str]] | None = None,
    tree_registry: dict[str, dict[str, Any]] | None = None,
    gate_contracts: dict[str, dict[str, Any]] | None = None,
    expected_stage: str | None = None,
) -> SnapshotCheck:
    """Recompute identity and derive status without trusting human fields."""
    reasons, actual = _identity_reasons(
        manifest, repo, build_dirs, authority_paths, commands, tree_registry
    )
    if expected_stage is not None and manifest.get("stage") != expected_stage:
        reasons.append("manifest stage does not match requested stage")
    gate_errors, required_gates = _gate_reasons(manifest)
    reasons.extend(gate_errors)
    actual_registry = None
    if isinstance(actual, dict):
        capabilities = actual.get("capabilities")
        if isinstance(capabilities, dict):
            candidate_registry = capabilities.get("tree_registry")
            if isinstance(candidate_registry, dict):
                actual_registry = candidate_registry
    reasons.extend(
        artifact_reasons(
            manifest,
            repo,
            tree_registry=actual_registry,
            gate_contracts=gate_contracts,
        )
    )
    reasons.extend(_payload_reasons(manifest))
    if manifest.get("snapshot_kind") == "t0" and (
        manifest.get("result") == "pass" or required_gates
    ):
        reasons.append("snapshot-only manifest cannot claim PASS")
    if manifest.get("result") == "pass" and not required_gates:
        reasons.append("manual result cannot establish PASS")
    run_guard = manifest.get("run_guard")
    if run_guard is not None and (
        not isinstance(run_guard, dict) or run_guard.get("identity_stable") is not True
    ):
        reasons.append("identity changed while gates were running")
    valid = not reasons
    result = (
        "pass" if valid and required_gates else ("snapshot" if valid else "blocked")
    )
    return SnapshotCheck(valid, result, tuple(reasons), actual)


def write_snapshot(path: Path, manifest: dict[str, Any]) -> None:
    """Write JSON for local evidence generation."""
    artifacts = manifest.get("artifacts")
    if isinstance(artifacts, dict):
        artifacts.pop("qa_manifest", None)
    manifest[release_identity.MANIFEST_PAYLOAD_DIGEST_FIELD] = (
        release_identity.manifest_payload_digest(manifest)
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
