from __future__ import annotations

from pathlib import Path

from tests.libcurl_consistency import run_gate
from tests.libcurl_consistency.pytest_support import case_defs


def test_minimal_set_references_cases_known_to_planner() -> None:
    text = Path("tests/libcurl_consistency/minimal_set.yaml").read_text(encoding="utf-8")
    expected_ids = {
        "http_download_serial_resume",
        "http_upload_put",
        "http_upload_post_reuse",
        "curl_data_test1531_postfields_binary",
        "curl_data_test357_expect_100_continue",
        "curl_data_test1011_redirect_post_301_to_get",
    }

    for case_id in expected_ids:
        assert f"id: {case_id}" in text

    planned_files = set()
    for suite in ("p0", "p1", "p2"):
        cfg = run_gate.GateConfig(
            repo_root=Path.cwd(),
            qcurl_build_dir=Path("build"),
            curl_build_dir=Path("build/curl"),
            capability_manifest=Path("build/libcurl_consistency/capabilities.json"),
            suite=suite,
            build=False,
            with_ext=False,
            junit_xml=Path("build/libcurl_consistency/reports/junit.xml"),
            json_report=Path("build/libcurl_consistency/reports/report.json"),
            qt_timeout_s=90,
        )
        planned_files.update(run_gate._pytest_files(cfg))

    assert "tests/libcurl_consistency/test_p0_consistency.py" in planned_files
    assert "tests/libcurl_consistency/test_p1_postfields_binary.py" in planned_files
    assert "tests/libcurl_consistency/test_p2_expect_100_continue.py" in planned_files
    assert "download_serial_resume" in case_defs.P0_CASES
    assert "upload_put" in case_defs.P0_CASES
    assert "upload_post_reuse" in case_defs.P0_CASES
    assert "postfields_binary_1531" in case_defs.P1_CASES
