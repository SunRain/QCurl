from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from tests.uce.bp.validate import validate_bp


def _write_jsonl(path: Path, rows: list[dict[str, Any]], *, extra_lines: list[str] | None = None) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [json.dumps(row, ensure_ascii=False) for row in rows]
    if extra_lines:
        lines.extend(extra_lines)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def _base_event(seq: int, event: str) -> dict[str, Any]:
    return {
        "schema": "qcurl-uce/dci-evidence@v1",
        "case": "testAsyncDownloadBackpressure",
        "stream": "testAsyncDownloadBackpressure:bp-user-pause",
        "event": event,
        "seq": seq,
    }


def test_validate_bp_accepts_valid_stream(tmp_path: Path) -> None:
    evidence_root = tmp_path / "evidence"
    rows = [
        _base_event(1, "request_headers"),
        _base_event(2, "response_headers"),
        {
            **_base_event(3, "backpressure_on"),
            "buffered_bytes": 20000,
            "limit_bytes": 16384,
            "bytes_delivered_total": 20000,
            "bytes_written_total": 0,
        },
        {**_base_event(4, "pause_effective"), "bytes_delivered_total": 20000, "bytes_written_total": 0},
        {**_base_event(5, "first_byte"), "bytes_delivered_total": 20000, "bytes_written_total": 4096, "chunk_len": 0},
        {**_base_event(6, "body_chunk"), "bytes_delivered_total": 20000, "bytes_written_total": 4096, "chunk_len": 4096},
        {
            **_base_event(7, "backpressure_off"),
            "buffered_bytes": 7000,
            "limit_bytes": 16384,
            "bytes_delivered_total": 20000,
            "bytes_written_total": 4096,
        },
        {**_base_event(8, "resume_req"), "bytes_delivered_total": 24000, "bytes_written_total": 4096},
        {**_base_event(9, "body_complete"), "bytes_delivered_total": 262144, "bytes_written_total": 262144, "chunk_len": 262144},
        {**_base_event(10, "finished"), "result": "pass", "status": 200, "body_len": 262144},
    ]
    _write_jsonl(evidence_root / "dci_evidence_ok.jsonl", rows)

    report = validate_bp(Path("tests/uce/contracts/bp@v1.yaml"), [evidence_root])

    assert report["policy_violations"] == []
    assert report["summary"]["matched_events"] == 10
    assert report["summary"]["seq_first"] == 1
    assert report["summary"]["seq_last"] == 10


def test_validate_bp_reports_missing_evidence(tmp_path: Path) -> None:
    evidence_root = tmp_path / "missing"

    report = validate_bp(Path("tests/uce/contracts/bp@v1.yaml"), [evidence_root])

    assert "bp_evidence_missing" in report["policy_violations"]


def test_validate_bp_reports_contract_failures(tmp_path: Path) -> None:
    evidence_root = tmp_path / "evidence"
    rows = [
        _base_event(1, "request_headers"),
        _base_event(2, "response_headers"),
        _base_event(3, "backpressure_off"),
        _base_event(4, "backpressure_on"),
        {**_base_event(5, "pause_effective"), "bytes_delivered_total": 20000, "bytes_written_total": 0},
        {**_base_event(6, "resume_req"), "bytes_delivered_total": 50000, "bytes_written_total": 0},
        {**_base_event(7, "body_complete"), "bytes_delivered_total": 262144, "bytes_written_total": 1},
        {**_base_event(8, "finished"), "result": "fail", "status": 500, "body_len": 1},
    ]
    _write_jsonl(evidence_root / "dci_evidence_fail.jsonl", rows)

    report = validate_bp(Path("tests/uce/contracts/bp@v1.yaml"), [evidence_root])

    assert "bp_contract_failed" in report["policy_violations"]


def test_validate_bp_reports_parse_errors(tmp_path: Path) -> None:
    evidence_root = tmp_path / "evidence"
    rows = [
        _base_event(1, "request_headers"),
        _base_event(2, "response_headers"),
        {
            **_base_event(3, "backpressure_on"),
            "buffered_bytes": 20000,
            "limit_bytes": 16384,
            "bytes_delivered_total": 20000,
            "bytes_written_total": 0,
        },
        {**_base_event(4, "pause_effective"), "bytes_delivered_total": 20000, "bytes_written_total": 0},
        {
            **_base_event(5, "backpressure_off"),
            "buffered_bytes": 7000,
            "limit_bytes": 16384,
            "bytes_delivered_total": 20000,
            "bytes_written_total": 0,
        },
        {**_base_event(6, "resume_req"), "bytes_delivered_total": 24000, "bytes_written_total": 0},
        {**_base_event(7, "first_byte"), "bytes_delivered_total": 24000, "bytes_written_total": 4096, "chunk_len": 0},
        {**_base_event(8, "body_chunk"), "bytes_delivered_total": 24000, "bytes_written_total": 4096, "chunk_len": 4096},
        {**_base_event(9, "body_complete"), "bytes_delivered_total": 262144, "bytes_written_total": 262144, "chunk_len": 262144},
        {**_base_event(10, "finished"), "result": "pass", "status": 200, "body_len": 262144},
    ]
    _write_jsonl(evidence_root / "dci_evidence_parse_error.jsonl", rows, extra_lines=["not-json"])

    report = validate_bp(Path("tests/uce/contracts/bp@v1.yaml"), [evidence_root])

    assert "bp_evidence_parse_error" in report["policy_violations"]

