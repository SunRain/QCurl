from __future__ import annotations

from copy import deepcopy
from pathlib import Path

import pytest

from tests.libcurl_consistency.pytest_support.capability_manifest import seal_manifest
from tests.libcurl_consistency.pytest_support.evidence_integrity import verify_gate_evidence_integrity
from tests.libcurl_consistency.pytest_support.execution_plan import seal_execution_plan
from tests.libcurl_consistency.pytest_support.execution_plan import validate_execution_plan
from tests.libcurl_consistency.pytest_support.gate_planner import plan_pytest_files
from tests.libcurl_consistency.pytest_support.gate_runtime import GateConfig


def _identity() -> dict[str, object]:
    return {
        "run_id": "run-1",
        "producer": {"path": "/build/probe", "sha256": "producer", "size": 10},
        "source": {"head": "abc123", "worktree_sha256": "source"},
        "build": {
            "qcurl": {"path": "/build/qcurl", "sha256": "qcurl", "size": 20},
            "libcurl": {"path": "/build/curl", "sha256": "curl", "size": 30},
            "cmake_cache": {"path": "/build/cache", "sha256": "cache", "size": 40},
        },
    }


def _manifest() -> dict[str, object]:
    return seal_manifest(
        {
            "schema": "qcurl-lc/capabilities@v1",
            "tests": {
                "test_ext_http3_success_h3.py": {
                    "enabled": True,
                    "reason": "HTTP/3 available",
                }
            },
        },
        identity=_identity(),
        generated_at_epoch=101.0,
    )


def _plan() -> dict[str, object]:
    return seal_execution_plan({
        "run_id": "run-1",
        "execution_token": "token-1",
        "identity": {
            "source": _identity()["source"],
            "build": _identity()["build"],
        },
        "preflight": {"have_h3_server": False, "have_h3_curl": False},
        "planned_files": ["tests/libcurl_consistency/test_p0_consistency.py"],
    })


def _config() -> GateConfig:
    root = Path.cwd()
    return GateConfig(
        repo_root=root,
        qcurl_build_dir=root / "build",
        curl_build_dir=root / "build/curl",
        capability_manifest=root / "build/capabilities.json",
        suite="all",
        build=False,
        with_ext=True,
        junit_xml=root / "build/junit.xml",
        json_report=root / "build/gate.json",
        qt_timeout_s=90,
    )


def _verify(
    *,
    standalone_manifest: dict[str, object] | None = None,
    embedded_manifest: dict[str, object] | None = None,
    standalone_plan: dict[str, object] | None = None,
    embedded_plan: dict[str, object] | None = None,
) -> dict[str, object]:
    manifest = _manifest()
    plan = _plan()
    return verify_gate_evidence_integrity(
        standalone_manifest=standalone_manifest or manifest,
        embedded_manifest=embedded_manifest or deepcopy(manifest),
        standalone_plan=standalone_plan or plan,
        embedded_plan=embedded_plan or deepcopy(plan),
        expected_identity=_identity(),
        gate_started_epoch=100.0,
        now_epoch=102.0,
        expected_run_id="run-1",
        expected_execution_token="token-1",
    )


def test_preflight_planner_override_does_not_mutate_capability_manifest() -> None:
    manifest = _manifest()
    original = deepcopy(manifest)

    planned, exclusions = plan_pytest_files(
        _config(),
        manifest,
        planner_overrides={
            "test_ext_http3_success_h3.py": {
                "enabled": False,
                "reason": "HTTP/3 preflight unavailable",
            }
        },
    )

    assert manifest == original
    assert "tests/libcurl_consistency/test_ext_http3_success_h3.py" not in planned
    assert exclusions["tests/libcurl_consistency/test_ext_http3_success_h3.py"]


def test_execution_plan_binds_content_hash() -> None:
    plan = _plan()

    assert validate_execution_plan(
        plan,
        expected_run_id="run-1",
        expected_execution_token="token-1",
        expected_identity=plan["identity"],
    ) == []
    assert plan["schema"] == "qcurl-lc/execution-plan@v1"
    assert plan["content_sha256"]


@pytest.mark.parametrize(
    "target",
    [
        "standalone_manifest",
        "embedded_manifest",
        "standalone_plan",
        "embedded_plan",
    ],
)
def test_gate_evidence_integrity_rejects_each_tampered_copy(target: str) -> None:
    value = _manifest() if "manifest" in target else _plan()
    value["tampered"] = True

    result = _verify(**{target: value})

    assert result["valid"] is False
    assert any(target in error for error in result["errors"])
