"""Evidence layout and persistence helpers for the UCE gate."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any
import hashlib
import json

from scripts.uce.manifest import add_artifact
from scripts.uce.manifest import add_policy_violation
from scripts.uce.manifest import create_manifest
from scripts.uce.manifest import write_manifest
from scripts.uce_gate.candidate import capture_candidate_fingerprint
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
    archive_envelope_path: Path


def register_candidate_fingerprint(
    *,
    repo_root: Path,
    layout: EvidenceLayout,
    manifest: dict[str, Any],
) -> None:
    """保存当前候选指纹，并登记为归档中的必需证据。"""

    fingerprint = capture_candidate_fingerprint(
        repo_root,
        excluded_paths=(layout.evidence_dir, layout.tar_path, layout.archive_envelope_path),
    )
    fingerprint_path = layout.meta_dir / "candidate_fingerprint.json"
    write_json(fingerprint_path, fingerprint)
    manifest["candidate_fingerprint"] = fingerprint
    add_artifact(
        manifest,
        artifact_id="candidate_fingerprint",
        path="meta/candidate_fingerprint.json",
        kind="metadata",
        required=True,
        media_type="application/json",
    )


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
        archive_envelope_path=evidence_root / f"{run_id}.archive-envelope.json",
    )


def prepare_evidence_layout(layout: EvidenceLayout) -> None:
    """只创建一次运行身份，拒绝覆盖任何已存在的目录或归档。"""

    for path in (layout.tar_path, layout.archive_envelope_path):
        if path.exists() or path.is_symlink():
            raise FileExistsError(f"运行证据已存在，必须使用新的 run-id: {path}")
    layout.evidence_dir.mkdir(parents=True, exist_ok=False)
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
    """独立保存两份元数据；一个写入失败不阻止另一个保留诊断。"""

    manifest["result"] = "pass" if not manifest["policy_violations"] else "fail"
    errors: list[str] = []
    try:
        write_policy_report(
            layout.policy_report_path,
            tier=tier,
            policy_violations=list(manifest["policy_violations"]),
            missing_required_artifacts=missing_required_artifacts,
        )
    except Exception as exc:
        _invalidate_failed_metadata(layout.policy_report_path, exc, errors)
    try:
        write_manifest(layout.manifest_path, manifest)
    except Exception as exc:
        _invalidate_failed_metadata(layout.manifest_path, exc, errors)
    if errors:
        raise OSError("; ".join(errors))


def _invalidate_failed_metadata(path: Path, exc: Exception, errors: list[str]) -> None:
    errors.append(f"{path.name}: {type(exc).__name__}: {exc}")
    try:
        path.unlink(missing_ok=True)
    except OSError as cleanup_error:
        errors.append(f"{path.name}: 旧元数据无法移除: {cleanup_error}")


def package_evidence_bundle(layout: EvidenceLayout, manifest: dict[str, Any]) -> None:
    """打包当前证据快照；成功与失败都由包外 envelope 承载归档身份。"""

    try:
        if manifest["policy_violations"] and layout.manifest_path.exists():
            stored = load_json_if_exists(layout.manifest_path)
            if stored.get("result") == "pass":
                raise RuntimeError("拒绝归档未能失效的旧 PASS 元数据")
        tar_gz_dir(layout.evidence_dir, layout.tar_path)
    except Exception as exc:
        add_policy_violation(manifest, "packaging_tar_gz_failed")
        add_policy_violation(manifest, "artifact_required_missing")
        manifest.setdefault("packaging", {})["tar_gz_error"] = str(exc)
        for path in (layout.tar_path, layout.archive_envelope_path):
            try:
                path.unlink(missing_ok=True)
            except OSError as cleanup_error:
                manifest["packaging"]["cleanup_error"] = str(cleanup_error)
        return

    write_archive_envelope(layout, manifest)


def _record_envelope_failure(manifest: dict[str, Any], code: str, message: str) -> None:
    """把 envelope 生成失败登记为结构化 violation，避免裸 traceback。"""

    add_policy_violation(manifest, code)
    manifest.setdefault("packaging", {})["envelope_error"] = message


def write_archive_envelope(layout: EvidenceLayout, manifest: dict[str, Any]) -> None:
    """在包外写入归档 envelope，记录 tar 的路径、大小与 sha256。

    envelope 位于 evidence_dir 之外，因此不参与打包，也不会改变已计算的 digest。

    读取归档与写入 envelope 是两个独立失败面，分别登记：
    - 读取阶段：归档缺失或损坏（`archive_envelope_source_missing` / `archive_envelope_read_failed`）
    - 写入阶段：envelope 落盘失败，如磁盘满、权限不足、序列化错误（`archive_envelope_write_failed`）
    """

    try:
        digest = hashlib.sha256()
        with layout.tar_path.open("rb") as handle:
            for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(chunk)
        byte_count = layout.tar_path.stat().st_size
    except FileNotFoundError as exc:
        # 归档缺失 —— 打包步骤失败但未被捕获，或被外部删除
        _record_envelope_failure(manifest, "archive_envelope_source_missing", f"归档文件缺失: {exc}")
        return
    except OSError as exc:
        # 归档损坏或无法读取 —— I/O 错误、权限问题、磁盘故障
        _record_envelope_failure(manifest, "archive_envelope_read_failed", f"归档文件读取失败: {exc}")
        return

    try:
        write_json(
            layout.archive_envelope_path,
            {
                "schema": "qcurl-uce/archive-envelope@v1",
                "generated_at_utc": utc_now_iso(),
                "run_id": manifest.get("run_id"),
                "gate_id": manifest.get("gate_id"),
                "archive": {
                    "path": str(layout.tar_path),
                    "media_type": "application/gzip",
                    "byte_count": byte_count,
                    "sha256": digest.hexdigest(),
                },
                "manifest_path": str(layout.manifest_path),
            },
        )
    except Exception as exc:
        # envelope 自身落盘失败 —— 归档已可读，但身份记录未能持久化
        _record_envelope_failure(manifest, "archive_envelope_write_failed", f"envelope 写入失败: {exc}")


def load_json_if_exists(path: Path) -> dict[str, Any]:
    """Read a JSON object if present, otherwise return an empty object."""

    if not path.exists():
        return {}
    payload = json.loads(path.read_text(encoding="utf-8"))
    return payload if isinstance(payload, dict) else {}
