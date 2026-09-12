"""独立验证 UCE 上传工件；不改写原始证据或原始 gate 结论。"""

from __future__ import annotations

import gzip
import hashlib
import json
import tarfile
import zlib
from pathlib import Path
from pathlib import PurePosixPath
from typing import Any


def _required_file(path: Path, errors: list[str]) -> bool:
    try:
        if not path.is_file():
            errors.append(f"{path.name}: 必需文件缺失或不是普通文件")
            return False
        if path.stat().st_size == 0:
            errors.append(f"{path.name}: 必需文件为空")
            return False
    except OSError as exc:
        errors.append(f"{path.name}: 文件无法读取: {exc}")
        return False
    return True


def _read_document(path: Path, errors: list[str]) -> tuple[dict[str, Any], bytes]:
    if not _required_file(path, errors):
        return {}, b""
    try:
        raw = path.read_bytes()
        document = json.loads(raw)
        if not isinstance(document, dict):
            raise ValueError("JSON 顶层必须是对象")
        return document, raw
    except (OSError, ValueError) as exc:
        errors.append(f"{path.name}: JSON 无效或无法读取: {exc}")
        return {}, b""


def _index_members(
    archive: tarfile.TarFile, run_id: str, errors: list[str],
) -> dict[str, tarfile.TarInfo]:
    members: dict[str, tarfile.TarInfo] = {}
    for member in archive.getmembers():
        path = PurePosixPath(member.name)
        if path.is_absolute() or ".." in path.parts or not path.parts or path.parts[0] != run_id:
            errors.append(f"归档成员 {member.name}: 路径不属于本次 run")
            continue
        name = path.as_posix()
        if name in members:
            errors.append(f"归档成员 {name}: 重复条目")
        if not (member.isfile() or member.isdir()):
            errors.append(f"归档成员 {name}: 不允许链接或特殊文件")
        members[name] = member
    return members


def _check_metadata_bytes(
    archive: tarfile.TarFile, members: dict[str, tarfile.TarInfo],
    documents: dict[str, bytes], run_id: str, errors: list[str],
) -> None:
    for name, raw in documents.items():
        member = members.get(f"{run_id}/{name}")
        if member is None or not member.isfile():
            errors.append(f"{name}: 包内必需文件缺失或不是普通文件")
            continue
        with archive.extractfile(member) as handle:
            if handle.read(len(raw) + 1) != raw:
                errors.append(f"{name}: 包内外字节不一致")


def _artifact_member_name(value: Any, evidence_dir: str, run_id: str) -> str:
    if not isinstance(value, str) or not value or "\0" in value:
        raise ValueError("path 必须是非空字符串")
    path = PurePosixPath(value)
    if ".." in path.parts:
        raise ValueError("path 不能包含父目录跳转")
    if path.is_absolute():
        path = path.relative_to(PurePosixPath(evidence_dir))
    if not path.parts:
        raise ValueError("path 不能指向证据根目录")
    return f"{run_id}/{path.as_posix()}"


def _check_required_members(
    manifest: dict[str, Any], members: dict[str, tarfile.TarInfo],
    run_id: str, errors: list[str],
) -> None:
    artifacts = manifest.get("artifacts")
    if not isinstance(artifacts, dict) or not artifacts:
        errors.append("manifest.json: artifacts 必须是非空对象")
        return
    files = {name for name, member in members.items() if member.isfile()}
    directories = {str(parent) for name in files for parent in PurePosixPath(name).parents}
    for artifact_id, artifact in artifacts.items():
        if not isinstance(artifact, dict) or type(artifact.get("required")) is not bool:
            errors.append(f"manifest.json: artifact {artifact_id} 的 required 必须是布尔值")
            continue
        try:
            name = _artifact_member_name(artifact.get("path"), manifest.get("evidence_dir", ""), run_id)
        except (ValueError, TypeError) as exc:
            errors.append(f"manifest.json: artifact {artifact_id} 的 path 无效: {exc}")
            continue
        if artifact["required"] and name not in files and name not in directories:
            errors.append(f"manifest.json: 包内必需工件缺失: {artifact_id} ({name})")


def _check_archive(
    path: Path, manifest: dict[str, Any], documents: dict[str, bytes],
    run_id: str, errors: list[str],
) -> None:
    if not _required_file(path, errors):
        return
    try:
        with gzip.open(path, "rb") as compressed:
            with tarfile.open(fileobj=compressed, mode="r:") as archive:
                members = _index_members(archive, run_id, errors)
                _check_metadata_bytes(archive, members, documents, run_id, errors)
                _check_required_members(manifest, members, run_id, errors)
            # tar 成员终止标记早于 gzip 校验尾部，继续读取以验证 CRC 和流尾完整性。
            while compressed.read(1024 * 1024):
                pass
    except (OSError, EOFError, tarfile.TarError, zlib.error) as exc:
        errors.append(f"{path.name}: 归档损坏或无法读取: {exc}")


def _archive_identity(path: Path, errors: list[str]) -> dict[str, Any]:
    digest = hashlib.sha256()
    byte_count = 0
    try:
        with path.open("rb") as handle:
            for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                byte_count += len(chunk)
                digest.update(chunk)
    except OSError as exc:
        errors.append(f"{path.name}: 归档摘要读取失败: {exc}")
        return {}
    return {"byte_count": byte_count, "sha256": digest.hexdigest()}


def _check_envelope(
    envelope: dict[str, Any], evidence_root: Path, run_id: str, errors: list[str],
) -> None:
    name = f"{run_id}.archive-envelope.json"
    expected = {
        "schema": "qcurl-uce/archive-envelope@v1",
        "run_id": run_id,
        "gate_id": "uce",
        "manifest_path": str(evidence_root / run_id / "manifest.json"),
    }
    for field, value in expected.items():
        if envelope.get(field) != value:
            errors.append(f"{name}: {field} 与本次归档不匹配")
    archive = envelope.get("archive")
    if not isinstance(archive, dict):
        errors.append(f"{name}: archive 必须是对象")
        return
    archive_path = evidence_root / f"{run_id}.tar.gz"
    expected_archive = {
        "path": str(archive_path),
        "media_type": "application/gzip",
        **_archive_identity(archive_path, errors),
    }
    for field, value in expected_archive.items():
        actual = archive.get(field)
        if type(actual) is not type(value) or actual != value:
            errors.append(f"{name}: archive.{field} 与实际归档不匹配")


def _check_document_state(
    manifest: dict[str, Any], policy: dict[str, Any],
    evidence_root: Path, run_id: str, errors: list[str],
) -> None:
    expected = {
        "schema_version": 1, "gate_id": "uce", "run_id": run_id,
        "evidence_dir": str(evidence_root / run_id),
        "tar_gz": str(evidence_root / f"{run_id}.tar.gz"),
    }
    for field, value in expected.items():
        actual = manifest.get(field)
        if type(actual) is not type(value) or actual != value:
            errors.append(f"manifest.json: {field} 与本次归档不匹配")
    if manifest.get("result") not in ("pass", "fail"):
        errors.append("manifest.json: result 必须是 pass 或 fail")
    if manifest.get("tier") not in ("pr", "nightly", "soak"):
        errors.append("manifest.json: tier 无效")
    for field, kind in (("environment", dict), ("results", list), ("contracts", dict)):
        if not isinstance(manifest.get(field), kind):
            errors.append(f"manifest.json: {field} 类型无效")
    for name, document in (("manifest.json", manifest), ("policy_violations.json", policy)):
        if not isinstance(document.get("generated_at_utc"), str) or not document["generated_at_utc"]:
            errors.append(f"{name}: generated_at_utc 缺失或无效")
        codes = document.get("policy_violations")
        if not isinstance(codes, list) or any(not isinstance(code, str) or not code for code in codes):
            errors.append(f"{name}: policy_violations 必须是字符串列表")
    if policy.get("policy_violations") != manifest.get("policy_violations"):
        errors.append("policy_violations.json: policy_violations 与 manifest.json 不一致")
    if policy.get("tier") != manifest.get("tier"):
        errors.append("policy_violations.json: tier 与 manifest.json 不一致")
    missing = policy.get("missing_required_artifacts", [])
    if not isinstance(missing, list) or any(not isinstance(path, str) for path in missing):
        errors.append("policy_violations.json: missing_required_artifacts 必须是字符串列表")
    _check_pass_result(manifest, policy, errors)


def _check_pass_result(manifest: dict[str, Any], policy: dict[str, Any], errors: list[str]) -> None:
    if manifest.get("result") != "pass":
        return
    if manifest.get("policy_violations") != [] or policy.get("missing_required_artifacts", []):
        errors.append("manifest.json: result=pass 不能含 policy_violations 或缺失工件")
    results = manifest.get("results")
    if not isinstance(results, list) or not results:
        errors.append("manifest.json: result=pass 必须含非空 results")
    elif any(not isinstance(item, dict) or item.get("result") != "pass"
             or item.get("returncode", 0) != 0 for item in results):
        errors.append("manifest.json: result=pass 与 results 子项不一致")
    contracts = manifest.get("contracts")
    if isinstance(contracts, dict):
        for contract_id, contract in contracts.items():
            if not isinstance(contract, dict) or type(contract.get("required")) is not bool:
                errors.append(f"manifest.json: contracts.{contract_id}.required 类型无效")
            elif contract["required"] and contract.get("result") != "pass":
                errors.append(f"manifest.json: result=pass 与必需 contracts.{contract_id} 不一致")


def _report(run_id: str, result: Any, errors: list[str], require_pass: bool) -> dict[str, Any]:
    archive_valid = not errors
    if require_pass and result != "pass":
        errors.append("manifest.json: 最终验收要求 gate result=pass")
    return {
        "run_id": run_id,
        "archive_valid": archive_valid,
        "gate_result": result,
        "require_pass": require_pass,
        "accepted": not errors,
        "errors": errors,
    }


def validate_archive(
    evidence_root: Path, run_id: str, *, require_pass: bool = False,
) -> dict[str, Any]:
    """核对上传文件、归档身份和包内工件，并保留独立的原始 gate 结论。

    默认只要求完整诊断归档；require_pass 还要求 gate 成功，不改写任何证据。
    """

    errors: list[str] = []
    if (run_id in ("", ".", "..") or PurePosixPath(run_id).parts != (run_id,)
            or "\\" in run_id or "\0" in run_id):
        return _report(run_id, None, ["run_id 必须是单个非空路径分量"], require_pass)
    evidence_root = evidence_root.resolve()
    evidence_dir = evidence_root / run_id
    manifest, manifest_bytes = _read_document(evidence_dir / "manifest.json", errors)
    policy, policy_bytes = _read_document(evidence_dir / "policy_violations.json", errors)
    envelope, _ = _read_document(evidence_root / f"{run_id}.archive-envelope.json", errors)
    _check_envelope(envelope, evidence_root, run_id, errors)
    _check_document_state(manifest, policy, evidence_root, run_id, errors)
    documents = {"manifest.json": manifest_bytes, "policy_violations.json": policy_bytes}
    _check_archive(evidence_root / f"{run_id}.tar.gz", manifest, documents, run_id, errors)
    return _report(run_id, manifest.get("result"), errors, require_pass)
