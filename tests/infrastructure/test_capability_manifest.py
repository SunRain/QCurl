from __future__ import annotations

from copy import deepcopy

from tests.libcurl_consistency.pytest_support.capability_manifest import seal_manifest
from tests.libcurl_consistency.pytest_support.capability_manifest import validate_manifest


def _identity(run_id: str = "run-1") -> dict[str, object]:
    return {
        "run_id": run_id,
        "producer": {"path": "/build/probe", "sha256": "producer", "size": 10},
        "source": {"head": "abc123", "worktree_sha256": "source"},
        "build": {
            "qcurl": {"path": "/build/qcurl", "sha256": "qcurl", "size": 20},
            "libcurl": {"path": "/build/curl", "sha256": "curl", "size": 30},
            "cmake_cache": {"path": "/build/CMakeCache.txt", "sha256": "cache", "size": 40},
        },
    }


def test_capability_manifest_binds_identity_run_and_content_hash() -> None:
    manifest = seal_manifest(
        {"schema": "qcurl-lc/capabilities@v1", "tests": {}},
        identity=_identity(),
        generated_at_epoch=101.0,
    )

    assert validate_manifest(
        manifest,
        expected_identity=_identity(),
        gate_started_epoch=100.0,
        now_epoch=102.0,
    ) == []
    assert manifest["schema"] == "qcurl-lc/capabilities@v2"
    assert manifest["content_sha256"]


def test_capability_manifest_rejects_content_identity_and_freshness_drift() -> None:
    manifest = seal_manifest(
        {"schema": "qcurl-lc/capabilities@v1", "tests": {}},
        identity=_identity(),
        generated_at_epoch=90.0,
    )
    manifest["tests"]["changed.py"] = {"enabled": True}
    expected = _identity("other-run")

    errors = validate_manifest(
        manifest,
        expected_identity=expected,
        gate_started_epoch=100.0,
        now_epoch=102.0,
    )

    assert any("content hash mismatch" in error for error in errors)
    assert any("run_id mismatch" in error for error in errors)
    assert any("freshness mismatch" in error for error in errors)


def test_capability_manifest_rejects_other_build_identity() -> None:
    manifest = seal_manifest(
        {"schema": "qcurl-lc/capabilities@v1", "tests": {}},
        identity=_identity(),
        generated_at_epoch=101.0,
    )
    expected = deepcopy(_identity())
    expected["build"]["qcurl"]["sha256"] = "other"

    errors = validate_manifest(
        manifest,
        expected_identity=expected,
        gate_started_epoch=100.0,
        now_epoch=102.0,
    )

    assert any("build.qcurl identity mismatch" in error for error in errors)
