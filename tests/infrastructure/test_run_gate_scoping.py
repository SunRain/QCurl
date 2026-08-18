from __future__ import annotations

import argparse
from pathlib import Path

import pytest

from tests.libcurl_consistency import run_gate


def _args(*, reports_dir: str, run_id: str) -> argparse.Namespace:
    return argparse.Namespace(
        suite="all",
        build=False,
        with_ext=True,
        qcurl_build="build",
        curl_build="",
        reports_dir=reports_dir,
        run_id=run_id,
        qt_timeout_s="90",
    )


def test_resolve_config_places_all_mutable_evidence_under_run_directory(tmp_path, monkeypatch) -> None:
    monkeypatch.setattr(run_gate, "_detect_repo_root", lambda: tmp_path)

    cfg = run_gate._resolve_config(_args(reports_dir="evidence", run_id="run-42"))

    assert cfg.reports_root == tmp_path / "evidence"
    assert cfg.run_dir == tmp_path / "evidence" / "runs" / "run-42"
    assert cfg.capability_manifest == cfg.run_dir / "capabilities.json"
    assert cfg.junit_xml == cfg.run_dir / "junit_all.xml"
    assert cfg.json_report == cfg.run_dir / "gate_all.json"
    assert cfg.artifacts_dir == cfg.run_dir / "artifacts"


def test_resolve_config_rejects_reused_run_id(tmp_path, monkeypatch) -> None:
    monkeypatch.setattr(run_gate, "_detect_repo_root", lambda: tmp_path)
    (tmp_path / "evidence" / "runs" / "run-42").mkdir(parents=True)

    with pytest.raises(ValueError, match="run-id 已存在"):
        run_gate._resolve_config(_args(reports_dir="evidence", run_id="run-42"))


def test_gate_environment_exports_run_identity_and_scoped_paths(tmp_path) -> None:
    cfg = run_gate.GateConfig(
        repo_root=tmp_path,
        qcurl_build_dir=tmp_path / "build",
        curl_build_dir=tmp_path / "build" / "curl",
        capability_manifest=tmp_path / "run" / "capabilities.json",
        suite="p0",
        build=False,
        with_ext=False,
        junit_xml=tmp_path / "run" / "junit.xml",
        json_report=tmp_path / "run" / "gate.json",
        qt_timeout_s=90,
        reports_root=tmp_path / "reports",
        run_id="run-42",
        run_dir=tmp_path / "run",
        artifacts_dir=tmp_path / "run" / "artifacts",
    )

    env = run_gate._gate_env(cfg)

    assert env["QCURL_LC_RUN_ID"] == "run-42"
    assert env["QCURL_LC_ARTIFACTS_DIR"] == str(cfg.artifacts_dir)
    assert env["QCURL_LC_CAPABILITY_MANIFEST"] == str(cfg.capability_manifest)


def test_infrastructure_ctest_is_registered_without_curl_testenv() -> None:
    cmake = Path("tests/CMakeLists.txt").read_text(encoding="utf-8")

    assert "NAME qcurl_libcurl_consistency_infrastructure" in cmake
    assert "--noconftest" in cmake
    for test_file in (
        "test_compare_unit.py",
        "test_run_gate_unit.py",
        "test_minimal_set_consistency.py",
        "test_coverage_maps.py",
        "test_gate_execution_manifest.py",
        "test_run_gate_scoping.py",
    ):
        assert test_file in cmake
