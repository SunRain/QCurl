from __future__ import annotations

import hashlib
import io
import json
import subprocess
import sys
import tarfile
from pathlib import Path

import pytest


_REPO_ROOT = Path(__file__).resolve().parents[1]
_RUN_ID = "archive-validation-run"


def _write_json(path: Path, payload: dict[str, object]) -> None:
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def _refresh_envelope(paths: dict[str, Path]) -> None:
    archive_bytes = paths["archive"].read_bytes()
    _write_json(paths["envelope"], {
        "schema": "qcurl-uce/archive-envelope@v1",
        "generated_at_utc": "2026-09-05T00:00:00Z",
        "run_id": _RUN_ID,
        "gate_id": "uce",
        "archive": {
            "path": str(paths["archive"]),
            "media_type": "application/gzip",
            "byte_count": len(archive_bytes),
            "sha256": hashlib.sha256(archive_bytes).hexdigest(),
        },
        "manifest_path": str(paths["manifest"]),
    })


def _pack(paths: dict[str, Path]) -> None:
    with tarfile.open(paths["archive"], "w:gz") as archive:
        for path in sorted(paths["evidence"].rglob("*")):
            if path.is_file():
                archive.add(path, arcname=path.relative_to(paths["root"]).as_posix())
    _refresh_envelope(paths)


@pytest.fixture
def bundle(tmp_path: Path) -> dict[str, Path]:
    """创建独立且包含真实必需工件的 UCE 归档夹具。"""

    evidence = tmp_path / _RUN_ID
    evidence.mkdir()
    paths = {
        "root": tmp_path,
        "evidence": evidence,
        "manifest": evidence / "manifest.json",
        "policy": evidence / "policy_violations.json",
        "archive": tmp_path / f"{_RUN_ID}.tar.gz",
        "envelope": tmp_path / f"{_RUN_ID}.archive-envelope.json",
    }
    manifest = {
        "schema_version": 1, "gate_id": "uce", "tier": "nightly", "run_id": _RUN_ID,
        "result": "pass", "generated_at_utc": "2026-09-05T00:00:00Z",
        "repo_root": str(_REPO_ROOT), "build_dir": str(tmp_path),
        "evidence_dir": str(evidence), "tar_gz": str(paths["archive"]),
        "environment": {}, "results": [{"id": "offline", "kind": "gate", "result": "pass"}],
        "contracts": {}, "policy_violations": [],
        "artifacts": {
            "manifest": {"path": "manifest.json", "kind": "metadata", "required": True},
            "policy_report": {"path": "policy_violations.json", "kind": "report", "required": True},
            "log": {"path": "gate.log", "kind": "log", "required": True},
            "trace": {"path": "trace", "kind": "evidence", "required": True},
        },
    }
    _write_json(paths["manifest"], manifest)
    _write_json(paths["policy"], {
        "generated_at_utc": "2026-09-05T00:00:00Z", "tier": "nightly",
        "policy_violations": [], "missing_required_artifacts": [],
    })
    (evidence / "gate.log").write_text("gate completed\n", encoding="utf-8")
    (evidence / "trace").mkdir()
    (evidence / "trace" / "trace.log").write_text("trace completed\n", encoding="utf-8")
    _pack(paths)
    return paths


def _validate(paths: dict[str, Path], *extra: str) -> tuple[int, dict[str, object]]:
    completed = subprocess.run(
        [sys.executable, str(_REPO_ROOT / "scripts" / "verify_uce_archive.py"),
         "--evidence-root", str(paths["root"]), "--run-id", _RUN_ID, *extra],
        cwd=_REPO_ROOT, text=True, capture_output=True, check=False,
    )
    assert completed.stdout.strip(), completed.stderr
    assert "Traceback" not in completed.stderr
    return completed.returncode, json.loads(completed.stdout)


def test_complete_archive_has_distinct_integrity_and_gate_results(bundle: dict[str, Path]) -> None:
    """完整归档的完整性与原始 gate 结论必须分别报告。"""

    returncode, report = _validate(bundle)

    assert returncode == 0
    assert report["archive_valid"] is True
    assert report["gate_result"] == "pass"
    assert report["errors"] == []


@pytest.mark.parametrize("file_key", ("manifest", "policy", "archive", "envelope"))
@pytest.mark.parametrize("damage", ("missing", "empty", "corrupt"))
def test_each_required_upload_file_fails_closed(
    bundle: dict[str, Path], file_key: str, damage: str,
) -> None:
    """其余三项仍存在时，任一必需上传文件缺失、为空或损坏均必须非零。"""

    path = bundle[file_key]
    if damage == "missing":
        path.unlink()
    else:
        path.write_bytes(b"" if damage == "empty" else b"broken archive evidence")

    returncode, report = _validate(bundle)

    assert returncode == 1
    assert report["archive_valid"] is False
    assert any(path.name in message for message in report["errors"])


@pytest.mark.parametrize(("field", "value"), (
    (("schema",), "unknown"),
    (("run_id",), "another-run"),
    (("gate_id",), "another-gate"),
    (("manifest_path",), "/unrelated/manifest.json"),
    (("archive",), None),
    (("archive", "path"), "/unrelated/archive.tar.gz"),
    (("archive", "media_type"), "application/zip"),
    (("archive", "byte_count"), 0),
    (("archive", "byte_count"), True),
    (("archive", "byte_count"), 10),
    (("archive", "sha256"), "0" * 64),
    (("archive", "sha256"), ""),
))
def test_envelope_identity_must_match_actual_archive(
    bundle: dict[str, Path], field: tuple[str, ...], value: object,
) -> None:
    """即使归档仍可打开，错误 schema、身份、长度和摘要也必须阻断。"""

    envelope = json.loads(bundle["envelope"].read_text(encoding="utf-8"))
    target = envelope
    for key in field[:-1]:
        target = target[key]
    target[field[-1]] = value
    _write_json(bundle["envelope"], envelope)

    returncode, report = _validate(bundle)

    assert returncode == 1
    assert report["archive_valid"] is False
    assert any(field[-1] in message for message in report["errors"])


@pytest.mark.parametrize("file_key", ("manifest", "policy"))
def test_packaged_metadata_must_match_external_bytes(
    bundle: dict[str, Path], file_key: str,
) -> None:
    """字节差异不能被 JSON 语义相同或相同 artifact 集合掩盖。"""

    path = bundle[file_key]
    path.write_bytes(path.read_bytes() + b"\n")

    returncode, report = _validate(bundle)

    assert returncode == 1
    assert any(path.name in message for message in report["errors"])


def _replace_member(paths: dict[str, Path], target: str, damage: str) -> None:
    with tarfile.open(paths["archive"], "r:gz") as archive:
        entries = [(member, archive.extractfile(member).read()) for member in archive.getmembers()]
    with tarfile.open(paths["archive"], "w:gz") as archive:
        for member, content in entries:
            if member.name == f"{_RUN_ID}/{target}":
                if damage == "missing":
                    continue
                if damage == "empty":
                    member.size, content = 0, b""
                elif damage == "directory":
                    member.type, member.size, content = tarfile.DIRTYPE, 0, b""
                elif damage == "symlink":
                    member.type, member.size, content = tarfile.SYMTYPE, 0, b""
                    member.linkname = "../manifest.json"
                elif damage == "duplicate":
                    archive.addfile(member, io.BytesIO(content))
            archive.addfile(member, io.BytesIO(content))
    _refresh_envelope(paths)


@pytest.mark.parametrize("target", ("gate.log", "trace/trace.log", "manifest.json", "policy_violations.json"))
@pytest.mark.parametrize("damage", ("missing", "directory", "symlink", "duplicate"))
def test_archive_must_contain_unambiguous_required_files(
    bundle: dict[str, Path], target: str, damage: str,
) -> None:
    """磁盘工件仍在且 envelope 摘要已更新，也不能接受包内缺失或伪造工件。"""

    _replace_member(bundle, target, damage)
    assert (bundle["evidence"] / target).is_file()

    returncode, report = _validate(bundle)

    assert returncode == 1
    assert report["archive_valid"] is False


def test_damaged_gzip_trailer_is_rejected_even_with_matching_digest(bundle: dict[str, Path]) -> None:
    """遍历 tar 成员不一定读取 gzip 尾部，必须另行覆盖压缩流完整性。"""

    path = bundle["archive"]
    path.write_bytes(path.read_bytes()[:-4])
    _refresh_envelope(bundle)

    returncode, report = _validate(bundle)

    assert returncode == 1
    assert report["archive_valid"] is False


@pytest.mark.parametrize(("file_key", "field", "value"), (
    ("manifest", "schema_version", True),
    ("manifest", "run_id", "another-run"),
    ("manifest", "gate_id", "another-gate"),
    ("manifest", "result", "unknown"),
    ("manifest", "tier", "unknown"),
    ("manifest", "evidence_dir", "/unrelated/evidence"),
    ("manifest", "tar_gz", "/unrelated/archive.tar.gz"),
    ("manifest", "results", None),
    ("manifest", "artifacts", {}),
    ("manifest", "policy_violations", "gate_offline_failed"),
    ("policy", "tier", "unknown"),
    ("policy", "policy_violations", ["gate_offline_failed"]),
    ("policy", "missing_required_artifacts", "gate.log"),
))
def test_metadata_schema_and_run_state_must_be_consistent(
    bundle: dict[str, Path], file_key: str, field: str, value: object,
) -> None:
    """合法 JSON 不代表有效证据；同步重新打包后仍须核对字段合同。"""

    payload = json.loads(bundle[file_key].read_text(encoding="utf-8"))
    payload[field] = value
    _write_json(bundle[file_key], payload)
    _pack(bundle)

    returncode, report = _validate(bundle)

    assert returncode == 1
    assert report["archive_valid"] is False
    assert any(field in message for message in report["errors"])


def test_failed_gate_archive_is_diagnostic_and_never_a_passing_acceptance(bundle: dict[str, Path]) -> None:
    """完整失败证据允许诊断上传，但不能通过最终验收模式。"""

    manifest = json.loads(bundle["manifest"].read_text(encoding="utf-8"))
    manifest["result"] = "fail"
    manifest["policy_violations"] = ["gate_offline_failed"]
    manifest["results"][0]["result"] = "fail"
    _write_json(bundle["manifest"], manifest)
    policy = json.loads(bundle["policy"].read_text(encoding="utf-8"))
    policy["policy_violations"] = manifest["policy_violations"]
    _write_json(bundle["policy"], policy)
    _pack(bundle)

    diagnostic_code, diagnostic = _validate(bundle)
    acceptance_code, acceptance = _validate(bundle, "--require-pass")

    assert diagnostic_code == 0
    assert diagnostic["archive_valid"] is True
    assert diagnostic["gate_result"] == "fail"
    assert acceptance_code == 1
    assert acceptance["archive_valid"] is True
    assert acceptance["gate_result"] == "fail"
    assert acceptance["accepted"] is False


def test_successful_gate_and_archive_pass_acceptance_mode(bundle: dict[str, Path]) -> None:
    """最终验收模式必须同时接受完整归档和有效的成功结论。"""

    returncode, report = _validate(bundle, "--require-pass")

    assert returncode == 0
    assert report["accepted"] is True


@pytest.mark.parametrize("run_id", ("", ".", "..", "../other", "/other", "run/child", "run\\child"))
def test_run_id_cannot_escape_evidence_root(bundle: dict[str, Path], run_id: str) -> None:
    """路径型 run_id 在读取任何证据前被明确拒绝。"""

    returncode, report = _validate(bundle, "--run-id", run_id)

    assert returncode == 1
    assert report["errors"] == ["run_id 必须是单个非空路径分量"]


@pytest.mark.parametrize("file_key", ("manifest", "policy", "envelope"))
@pytest.mark.parametrize("payload", (None, [], {}))
def test_json_syntax_alone_is_not_valid_evidence(
    bundle: dict[str, Path], file_key: str, payload: object,
) -> None:
    """空对象、数组和 null 不能仅凭 JSON 可解析而成为有效归档元数据。"""

    bundle[file_key].write_text(json.dumps(payload), encoding="utf-8")

    returncode, report = _validate(bundle)

    assert returncode == 1
    assert report["archive_valid"] is False
