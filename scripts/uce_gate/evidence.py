"""Evidence layout and persistence helpers for the UCE gate."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any
import json

from scripts.uce.manifest import add_artifact
from scripts.uce.manifest import add_policy_violation
from scripts.uce.manifest import create_manifest
from scripts.uce.manifest import write_manifest
from scripts.uce_gate.runtime import safe_mkdir
from scripts.uce_gate.runtime import tar_gz_dir
from scripts.uce_gate.runtime import utc_now_iso
from scripts.uce_gate.runtime import write_json


@dataclass(frozen=True)
class EvidenceLayout:
    """Resolved filesystem layout for a UCE gate run."""

    evidence_root: Path
    evidence_dir: Path
    logs_dir: Path
    meta_dir: Path
    reports_dir: Path
    netproof_dir: Path
    lc_dir: Path
    manifest_path: Path
    policy_report_path: Path
    tar_path: Path


def resolve_evidence_layout(
    repo_root: Path,
    build_dir: Path,
    *,
    run_id: str,
    evidence_root_arg: str,
) -> EvidenceLayout:
    """Resolve evidence paths while preserving the legacy CLI semantics."""

    if evidence_root_arg:
        evidence_root = Path(evidence_root_arg)
        if not evidence_root.is_absolute():
            evidence_root = (repo_root / evidence_root).resolve()
    else:
        evidence_root = build_dir / "evidence" / "uce"

    evidence_dir = evidence_root / run_id
    return EvidenceLayout(
        evidence_root=evidence_root,
        evidence_dir=evidence_dir,
        logs_dir=evidence_dir / "logs",
        meta_dir=evidence_dir / "meta",
        reports_dir=evidence_dir / "reports",
        netproof_dir=evidence_dir / "netproof",
        lc_dir=evidence_dir / "libcurl_consistency",
        manifest_path=evidence_dir / "manifest.json",
        policy_report_path=evidence_dir / "policy_violations.json",
        tar_path=evidence_root / f"{run_id}.tar.gz",
    )


def prepare_evidence_layout(layout: EvidenceLayout) -> None:
    """Create all evidence directories required before gates run."""

    for path in (layout.logs_dir, layout.meta_dir, layout.reports_dir, layout.netproof_dir, layout.lc_dir):
        safe_mkdir(path)


def create_gate_manifest(
    *,
    repo_root: Path,
    build_dir: Path,
    layout: EvidenceLayout,
    tier: str,
    run_id: str,
    environment: dict[str, Any],
) -> dict[str, Any]:
    """Create the UCE manifest and register the always-required artifacts."""

    manifest = create_manifest(
        gate_id="uce",
        tier=tier,
        run_id=run_id,
        repo_root=str(repo_root),
        build_dir=str(build_dir),
        evidence_dir=str(layout.evidence_dir),
        tar_gz=str(layout.tar_path),
        environment=environment,
    )
    add_artifact(manifest, artifact_id="manifest", path="manifest.json", kind="metadata", required=True)
    add_artifact(
        manifest,
        artifact_id="policy_report",
        path="policy_violations.json",
        kind="report",
        required=True,
        media_type="application/json",
    )
    add_artifact(
        manifest,
        artifact_id="versions",
        path="meta/versions.txt",
        kind="report",
        required=True,
        media_type="text/plain",
    )
    return manifest


def write_policy_report(
    path: Path,
    *,
    tier: str,
    policy_violations: list[str],
    missing_required_artifacts: list[str] | None = None,
) -> None:
    """Write the legacy policy violation report schema."""

    payload: dict[str, Any] = {
        "generated_at_utc": utc_now_iso(),
        "tier": tier,
        "policy_violations": policy_violations,
    }
    if missing_required_artifacts is not None:
        payload["missing_required_artifacts"] = missing_required_artifacts
    write_json(path, payload)


def write_manifest_and_policy_report(
    *,
    layout: EvidenceLayout,
    manifest: dict[str, Any],
    tier: str,
    missing_required_artifacts: list[str] | None = None,
) -> None:
    """Persist manifest and policy report using the current manifest state."""

    manifest["result"] = "pass" if not manifest["policy_violations"] else "fail"
    write_policy_report(
        layout.policy_report_path,
        tier=tier,
        policy_violations=list(manifest["policy_violations"]),
        missing_required_artifacts=missing_required_artifacts,
    )
    write_manifest(layout.manifest_path, manifest)


def package_evidence_bundle(layout: EvidenceLayout, manifest: dict[str, Any]) -> None:
    """Create the tar.gz evidence archive and register it in the manifest."""

    try:
        tar_gz_dir(layout.evidence_dir, layout.tar_path)
    except Exception as exc:
        add_policy_violation(manifest, "packaging_tar_gz_failed")
        manifest["packaging"] = {"tar_gz_error": str(exc)}

    add_artifact(
        manifest,
        artifact_id="archive_bundle",
        path=str(layout.tar_path),
        kind="archive",
        required=True,
        media_type="application/gzip",
    )


def load_json_if_exists(path: Path) -> dict[str, Any]:
    """Read a JSON object if present, otherwise return an empty object."""

    if not path.exists():
        return {}
    payload = json.loads(path.read_text(encoding="utf-8"))
    return payload if isinstance(payload, dict) else {}
