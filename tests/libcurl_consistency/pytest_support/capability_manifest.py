"""
Capability manifest helpers for libcurl_consistency gate.
"""

from __future__ import annotations

import json
import hashlib
import os
import subprocess
from pathlib import Path
from typing import Any

import pytest


_REPO_ROOT = Path(__file__).resolve().parents[3]


def _canonical_bytes(manifest: dict[str, Any]) -> bytes:
    payload = {key: value for key, value in manifest.items() if key != "content_sha256"}
    return json.dumps(
        payload,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")


def _file_identity(path: Path) -> dict[str, object]:
    resolved = path.resolve()
    if not resolved.is_file():
        return {"path": str(resolved), "sha256": "", "size": 0}
    digest = hashlib.sha256()
    with resolved.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return {
        "path": str(resolved),
        "sha256": digest.hexdigest(),
        "size": resolved.stat().st_size,
    }


def _git_output(repo_root: Path, args: list[str]) -> bytes:
    result = subprocess.run(
        ["git", *args],
        cwd=repo_root,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        detail = result.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(f"git {' '.join(args)} failed: {detail}")
    return result.stdout


def _source_identity(repo_root: Path) -> dict[str, str]:
    head = _git_output(repo_root, ["rev-parse", "HEAD"]).decode("ascii").strip()
    digest = hashlib.sha256()
    digest.update(_git_output(repo_root, ["diff", "--binary", "HEAD", "--", "."]))
    untracked = _git_output(
        repo_root,
        ["ls-files", "--others", "--exclude-standard", "-z"],
    ).split(b"\0")
    for raw_path in sorted(path for path in untracked if path):
        digest.update(raw_path)
        path = repo_root / raw_path.decode("utf-8", errors="surrogateescape")
        if path.is_file():
            digest.update(path.read_bytes())
    return {"head": head, "worktree_sha256": digest.hexdigest()}


def capture_identity(
    *,
    repo_root: Path,
    run_id: str,
    producer: Path,
    qcurl_binary: Path,
    libcurl_binary: Path,
    cmake_cache: Path,
) -> dict[str, object]:
    """采集本轮 capability producer、源码和构建产物身份。"""

    return {
        "run_id": run_id,
        "producer": _file_identity(producer),
        "source": _source_identity(repo_root),
        "build": {
            "qcurl": _file_identity(qcurl_binary),
            "libcurl": _file_identity(libcurl_binary),
            "cmake_cache": _file_identity(cmake_cache),
        },
    }


def seal_manifest(
    manifest: dict[str, Any],
    *,
    identity: dict[str, object],
    generated_at_epoch: float,
) -> dict[str, Any]:
    """把 probe 输出封装为可核验的 v2 run-scoped manifest。"""

    sealed = json.loads(json.dumps(manifest))
    sealed["schema"] = "qcurl-lc/capabilities@v2"
    sealed["provenance"] = {**identity, "generated_at_epoch": generated_at_epoch}
    sealed["content_sha256"] = hashlib.sha256(_canonical_bytes(sealed)).hexdigest()
    return sealed


def validate_manifest(
    manifest: dict[str, Any],
    *,
    expected_identity: dict[str, object],
    gate_started_epoch: float,
    now_epoch: float,
) -> list[str]:
    """核验 manifest 的 hash、run-id、源码、构建身份与新鲜度。"""

    errors: list[str] = []
    if manifest.get("schema") != "qcurl-lc/capabilities@v2":
        errors.append("schema mismatch")
    expected_hash = hashlib.sha256(_canonical_bytes(manifest)).hexdigest()
    if manifest.get("content_sha256") != expected_hash:
        errors.append("content hash mismatch")
    provenance = manifest.get("provenance")
    if not isinstance(provenance, dict):
        return [*errors, "provenance missing"]
    if provenance.get("run_id") != expected_identity.get("run_id"):
        errors.append("run_id mismatch")
    for section in ("producer", "source"):
        if provenance.get(section) != expected_identity.get(section):
            errors.append(f"{section} identity mismatch")
    actual_build = provenance.get("build")
    expected_build = expected_identity.get("build")
    if not isinstance(actual_build, dict) or not isinstance(expected_build, dict):
        errors.append("build identity missing")
    else:
        for name in ("qcurl", "libcurl", "cmake_cache"):
            if actual_build.get(name) != expected_build.get(name):
                errors.append(f"build.{name} identity mismatch")
    generated_at = provenance.get("generated_at_epoch")
    if not isinstance(generated_at, (int, float)):
        errors.append("generated_at_epoch missing")
    elif generated_at < gate_started_epoch - 1.0 or generated_at > now_epoch + 5.0:
        errors.append("freshness mismatch")
    return errors


def _manifest_path() -> Path:
    raw = (os.environ.get("QCURL_LC_CAPABILITY_MANIFEST") or "").strip()
    if raw:
        return Path(raw).expanduser().resolve()
    return (_REPO_ROOT / "build" / "libcurl_consistency" / "reports" / "capabilities.json").resolve()


def load_capability_manifest() -> dict:
    path = _manifest_path()
    if not path.exists():
        pytest.fail(f"capability manifest missing: {path}")
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:  # pragma: no cover - defensive guard
        pytest.fail(f"failed to parse capability manifest {path}: {exc}")
    raise AssertionError("unreachable")


def guard_planned_test(file_name: str) -> dict:
    manifest = load_capability_manifest()
    tests = manifest.get("tests") if isinstance(manifest, dict) else {}
    entry = tests.get(file_name) if isinstance(tests, dict) else None
    if isinstance(entry, dict) and not bool(entry.get("enabled", True)):
        reason = str(entry.get("reason") or "disabled by capability manifest")
        pytest.fail(f"gate/planner should have excluded {file_name}: {reason}")
    return manifest
