from __future__ import annotations

import json
from pathlib import Path

from tests.uce.hes.validate import validate_hes


def _write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def test_validate_hes_passes_for_pr_minimal_kinds(tmp_path: Path) -> None:
    artifacts_root = tmp_path / "artifacts"
    _write_json(
        artifacts_root / "p1_accept_encoding" / "case_a" / "baseline.json",
        {
            "runner": "baseline",
            "hes": {
                "kind": "accept_encoding",
                "request_accept_encoding": "gzip",
                "response_content_encoding": "gzip",
                "body_len": 18,
            },
        },
    )
    _write_json(
        artifacts_root / "p1_accept_encoding" / "case_a" / "qcurl.json",
        {
            "runner": "qcurl",
            "hes": {
                "kind": "accept_encoding",
                "request_accept_encoding": "gzip",
                "response_content_encoding": "gzip",
                "body_len": 18,
            },
        },
    )
    _write_json(
        artifacts_root / "p1_resp_headers" / "case_b" / "baseline.json",
        {
            "runner": "baseline",
            "hes": {
                "kind": "raw_headers",
                "headers_raw_lines": ["Set-Cookie: a=1", "Set-Cookie: b=2", "X-Dupe: A", "X-Dupe: B"],
                "set_cookie_count": 2,
                "x_dupe_count": 2,
            },
        },
    )
    _write_json(
        artifacts_root / "p1_resp_headers" / "case_b" / "qcurl.json",
        {
            "runner": "qcurl",
            "hes": {
                "kind": "raw_headers",
                "headers_raw_lines": ["Set-Cookie: a=1", "Set-Cookie: b=2", "X-Dupe: A", "X-Dupe: B"],
                "set_cookie_count": 2,
                "x_dupe_count": 2,
            },
        },
    )

    report = validate_hes(Path("tests/uce/contracts/hes@v1.yaml"), [artifacts_root], "pr")

    assert report["policy_violations"] == []


def test_validate_hes_reports_missing_runner_kind_coverage(tmp_path: Path) -> None:
    artifacts_root = tmp_path / "artifacts"
    _write_json(
        artifacts_root / "p1_accept_encoding" / "case_a" / "baseline.json",
        {
            "runner": "baseline",
            "hes": {
                "kind": "accept_encoding",
                "request_accept_encoding": "gzip",
                "response_content_encoding": "gzip",
                "body_len": 18,
            },
        },
    )

    report = validate_hes(Path("tests/uce/contracts/hes@v1.yaml"), [artifacts_root], "pr")

    assert "hes_evidence_missing" in report["policy_violations"]
    assert "hes_contract_failed" in report["policy_violations"]
