#!/usr/bin/env python3
"""Helpers for writing UCE manifests."""

from __future__ import annotations

import json
from datetime import datetime
from datetime import timezone
from pathlib import Path
from typing import Any
from typing import Mapping


def _utc_now_iso() -> str:
    """Return the current UTC time in ISO-8601 format."""

    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def _ensure_section(manifest: dict[str, Any], key: str, factory: type[list[Any]] | type[dict[str, Any]]) -> Any:
    """Return an initialized manifest section."""

    if key not in manifest or not isinstance(manifest[key], factory):
        manifest[key] = factory()
    return manifest[key]


def create_manifest(
    *,
    gate_id: str,
    tier: str,
    run_id: str,
    repo_root: str,
    build_dir: str,
    evidence_dir: str,
    tar_gz: str,
    environment: Mapping[str, Any] | None = None,
) -> dict[str, Any]:
    """Create a new manifest@v1 document."""

    return {
        "schema_version": 1,
        "gate_id": gate_id,
        "tier": tier,
        "run_id": run_id,
        "result": "fail",
        "generated_at_utc": _utc_now_iso(),
        "repo_root": repo_root,
        "build_dir": build_dir,
        "evidence_dir": evidence_dir,
        "tar_gz": tar_gz,
        "environment": dict(environment or {}),
        "results": [],
        "artifacts": {},
        "contracts": {},
        "policy_violations": [],
    }


def add_result(
    manifest: dict[str, Any],
    *,
    result_id: str,
    kind: str,
    result: str,
    returncode: int | None = None,
    duration_s: float | None = None,
    log_file: str | None = None,
    details: Mapping[str, Any] | None = None,
) -> None:
    """Append or replace a result entry by ID."""

    results = _ensure_section(manifest, "results", list)
    entry: dict[str, Any] = {
        "id": result_id,
        "kind": kind,
        "result": result,
    }
    if returncode is not None:
        entry["returncode"] = int(returncode)
    if duration_s is not None:
        entry["duration_s"] = float(duration_s)
    if log_file:
        entry["log_file"] = log_file
    if details:
        entry["details"] = dict(details)

    for index, existing in enumerate(results):
        if isinstance(existing, dict) and existing.get("id") == result_id:
            results[index] = entry
            return
    results.append(entry)


def add_artifact(
    manifest: dict[str, Any],
    *,
    artifact_id: str,
    path: str,
    kind: str,
    required: bool,
    media_type: str | None = None,
    classification: str | None = None,
    byte_count: int | None = None,
    sha256: str | None = None,
    redaction: Mapping[str, Any] | None = None,
    contract_refs: list[str] | None = None,
    notes: list[str] | None = None,
) -> None:
    """Register or update an artifact entry."""

    artifacts = _ensure_section(manifest, "artifacts", dict)
    entry: dict[str, Any] = {
        "path": path,
        "kind": kind,
        "required": bool(required),
    }
    if media_type:
        entry["media_type"] = media_type
    if classification:
        entry["classification"] = classification
    if byte_count is not None:
        entry["byte_count"] = int(byte_count)
    if sha256:
        entry["sha256"] = sha256
    if redaction:
        entry["redaction"] = dict(redaction)
    if contract_refs:
        entry["contract_refs"] = list(contract_refs)
    if notes:
        entry["notes"] = list(notes)
    artifacts[artifact_id] = entry


def add_contract(
    manifest: dict[str, Any],
    *,
    contract_id: str,
    provider: str,
    result: str,
    required: bool,
    report_artifact: str | None = None,
    evidence_artifacts: list[str] | None = None,
    violations: list[str] | None = None,
    notes: list[str] | None = None,
) -> None:
    """Register or update a contract summary."""

    contracts = _ensure_section(manifest, "contracts", dict)
    entry: dict[str, Any] = {
        "provider": provider,
        "result": result,
        "required": bool(required),
    }
    if report_artifact:
        entry["report_artifact"] = report_artifact
    if evidence_artifacts:
        entry["evidence_artifacts"] = list(evidence_artifacts)
    if violations:
        entry["violations"] = list(violations)
    if notes:
        entry["notes"] = list(notes)
    contracts[contract_id] = entry


def add_policy_violation(manifest: dict[str, Any], code: str) -> None:
    """Append a policy violation code if it is not already present."""

    violations = _ensure_section(manifest, "policy_violations", list)
    if code not in violations:
        violations.append(code)


def set_capability(manifest: dict[str, Any], capability_id: str, payload: Mapping[str, Any]) -> None:
    """Set a named capability snapshot."""

    capabilities = _ensure_section(manifest, "capabilities", dict)
    capabilities[capability_id] = dict(payload)


def write_manifest(path: str | Path, manifest: Mapping[str, Any]) -> None:
    """Write a manifest to disk with UTF-8 JSON formatting."""

    output = Path(path)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
