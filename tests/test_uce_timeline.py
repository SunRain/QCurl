from __future__ import annotations

import json
from pathlib import Path

from tests.uce.timeline.collect_from_lc import collect_from_lc
from tests.uce.timeline.collect_from_qt import collect_from_qt
from tests.uce.timeline.common import write_jsonl
from tests.uce.timeline.validate import validate_timelines


def _write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def test_collect_from_lc_reads_baseline_payload_sections(tmp_path: Path) -> None:
    artifacts_root = tmp_path / "artifacts"
    artifact_path = artifacts_root / "p1_progress" / "p1_progress_download_h2" / "baseline.json"
    _write_json(
        artifact_path,
        {
            "runner": "baseline",
            "request": {"method": "GET", "url": "https://example.test/data", "headers": {}, "body_len": 0},
            "response": {"status": 200, "http_version": "h2", "headers": {}, "body_len": 1024},
            "progress_summary": {
                "download": {"monotonic": True, "now_max": 1024, "total_max": 1024},
                "upload": {"monotonic": True, "now_max": 0, "total_max": 0},
            },
        },
    )

    result = collect_from_lc(artifacts_root)

    assert result["missing_roots"] == []
    assert result["stream_count"] == 1
    assert [event["event"] for event in result["events"]] == [
        "request_headers",
        "response_headers",
        "download_progress_summary",
        "upload_progress_summary",
        "body_complete",
        "finished",
    ]


def test_collect_from_qt_reads_qcurl_payload_and_websocket_evidence(tmp_path: Path) -> None:
    artifacts_root = tmp_path / "artifacts"
    qcurl_path = artifacts_root / "p2_pause_resume_strict" / "p2_pause_resume_strict_h2" / "qcurl.json"
    _write_json(
        qcurl_path,
        {
            "runner": "qcurl",
            "request": {"method": "GET", "url": "https://example.test/data", "headers": {}, "body_len": 0},
            "response": {"status": 200, "http_version": "h2", "headers": {}, "body_len": 128},
            "pause_resume_strict": {
                "events": [
                    {
                        "seq": 1,
                        "type": "start",
                        "t_us": 0,
                        "bytes_delivered_total": 0,
                        "bytes_written_total": 0,
                    },
                    {
                        "seq": 2,
                        "type": "pause_effective",
                        "t_us": 10,
                        "bytes_delivered_total": 64,
                        "bytes_written_total": 64,
                    },
                    {
                        "seq": 3,
                        "type": "resume_req",
                        "t_us": 20,
                        "bytes_delivered_total": 64,
                        "bytes_written_total": 64,
                    },
                    {
                        "seq": 4,
                        "type": "finished",
                        "t_us": 30,
                        "bytes_delivered_total": 128,
                        "bytes_written_total": 128,
                    },
                ]
            },
        },
    )

    qt_artifacts_root = tmp_path / "test-artifacts" / "websocket-evidence"
    ws_path = qt_artifacts_root / "ws_evidence_1.jsonl"
    write_jsonl(
        ws_path,
        [
            {"event": "handshake_ok", "case": "fragment_case", "path": "/fragment", "target": "/fragment?case=fragment_case"},
            {"event": "ws_frame", "case": "fragment_case", "opcode": 2, "fin": 0, "payload_len": 16, "payload_sha256": "a"},
            {"event": "connection_closed", "case": "fragment_case"},
        ],
    )

    result = collect_from_qt(artifacts_root, tmp_path / "test-artifacts")

    assert result["stream_count"] == 2
    event_names = [event["event"] for event in result["events"]]
    assert "pause_effective" in event_names
    assert "body_chunk" in event_names
    assert event_names.count("finished") >= 2


def test_collect_from_qt_reads_dci_jsonl_evidence(tmp_path: Path) -> None:
    artifacts_root = tmp_path / "artifacts"
    qt_artifacts_root = tmp_path / "test-artifacts" / "dci"
    evidence_path = qt_artifacts_root / "dci_evidence_pause_resume.jsonl"
    write_jsonl(
        evidence_path,
        [
            {
                "schema": "qcurl-uce/dci-evidence@v1",
                "case": "testAsyncMockChaosPauseResume",
                "stream": "testAsyncMockChaosPauseResume:attempt-1",
                "event": "request_headers",
                "seq": 1,
                "method": "GET",
            },
            {
                "schema": "qcurl-uce/dci-evidence@v1",
                "case": "testAsyncMockChaosPauseResume",
                "stream": "testAsyncMockChaosPauseResume:attempt-1",
                "event": "response_headers",
                "seq": 2,
                "status": 200,
            },
            {
                "schema": "qcurl-uce/dci-evidence@v1",
                "case": "testAsyncMockChaosPauseResume",
                "stream": "testAsyncMockChaosPauseResume:attempt-1",
                "event": "pause_effective",
                "seq": 3,
                "bytes_delivered_total": 7,
                "bytes_written_total": 7,
            },
            {
                "schema": "qcurl-uce/dci-evidence@v1",
                "case": "testAsyncMockChaosPauseResume",
                "stream": "testAsyncMockChaosPauseResume:attempt-1",
                "event": "resume_req",
                "seq": 4,
                "bytes_delivered_total": 7,
                "bytes_written_total": 7,
            },
            {
                "schema": "qcurl-uce/dci-evidence@v1",
                "case": "testAsyncMockChaosPauseResume",
                "stream": "testAsyncMockChaosPauseResume:attempt-1",
                "event": "body_chunk",
                "seq": 5,
                "chunk_len": 5,
                "bytes_delivered_total": 12,
                "bytes_written_total": 12,
            },
            {
                "schema": "qcurl-uce/dci-evidence@v1",
                "case": "testAsyncMockChaosPauseResume",
                "stream": "testAsyncMockChaosPauseResume:attempt-1",
                "event": "finished",
                "seq": 6,
                "result": "pass",
            },
        ],
    )

    result = collect_from_qt(artifacts_root, tmp_path / "test-artifacts")

    assert result["stream_count"] == 1
    event_names = [event["event"] for event in result["events"]]
    assert event_names == [
        "request_headers",
        "response_headers",
        "pause_effective",
        "resume_req",
        "body_chunk",
        "finished",
    ]
    assert result["events"][2]["source_kind"] == "dci_evidence"


def test_validate_timelines_reports_missing_provider(tmp_path: Path) -> None:
    contract_path = Path("tests/uce/contracts/timeline@v1.yaml")
    qt_timeline = tmp_path / "qt.timeline.jsonl"
    write_jsonl(
        qt_timeline,
        [
            {
                "schema": "qcurl-uce/timeline-event@v1",
                "provider": "qt",
                "stream_id": "qt:demo",
                "case_id": "demo",
                "source_kind": "artifact_payload",
                "source_path": "demo.json",
                "seq": 1,
                "event": "request_headers",
            },
            {
                "schema": "qcurl-uce/timeline-event@v1",
                "provider": "qt",
                "stream_id": "qt:demo",
                "case_id": "demo",
                "source_kind": "artifact_payload",
                "source_path": "demo.json",
                "seq": 2,
                "event": "response_headers",
            },
            {
                "schema": "qcurl-uce/timeline-event@v1",
                "provider": "qt",
                "stream_id": "qt:demo",
                "case_id": "demo",
                "source_kind": "artifact_payload",
                "source_path": "demo.json",
                "seq": 3,
                "event": "finished",
            },
        ],
    )

    report = validate_timelines(contract_path, [qt_timeline], {"qt", "libcurl_consistency"})

    assert "timeline_provider_missing" in report["policy_violations"]
    assert "timeline_contract_failed" in report["policy_violations"]
    assert report["provider_summary"]["libcurl_consistency"]["present"] is False


def test_validate_timelines_detects_terminal_quiet_violation(tmp_path: Path) -> None:
    contract_path = Path("tests/uce/contracts/timeline@v1.yaml")
    lc_timeline = tmp_path / "lc.timeline.jsonl"
    write_jsonl(
        lc_timeline,
        [
            {
                "schema": "qcurl-uce/timeline-event@v1",
                "provider": "libcurl_consistency",
                "stream_id": "baseline:demo",
                "case_id": "demo",
                "source_kind": "artifact_payload",
                "source_path": "baseline.json",
                "seq": 1,
                "event": "request_headers",
            },
            {
                "schema": "qcurl-uce/timeline-event@v1",
                "provider": "libcurl_consistency",
                "stream_id": "baseline:demo",
                "case_id": "demo",
                "source_kind": "artifact_payload",
                "source_path": "baseline.json",
                "seq": 2,
                "event": "response_headers",
            },
            {
                "schema": "qcurl-uce/timeline-event@v1",
                "provider": "libcurl_consistency",
                "stream_id": "baseline:demo",
                "case_id": "demo",
                "source_kind": "artifact_payload",
                "source_path": "baseline.json",
                "seq": 3,
                "event": "finished",
            },
            {
                "schema": "qcurl-uce/timeline-event@v1",
                "provider": "libcurl_consistency",
                "stream_id": "baseline:demo",
                "case_id": "demo",
                "source_kind": "artifact_payload",
                "source_path": "baseline.json",
                "seq": 4,
                "event": "body_complete",
                "body_len": 128,
            },
        ],
    )

    report = validate_timelines(contract_path, [lc_timeline], {"libcurl_consistency"})

    assert report["policy_violations"] == ["timeline_contract_failed"]
    failed_stream = next(item for item in report["streams"] if item["result"] == "fail")
    assert any(violation["id"] == "terminal_quiet" for violation in failed_stream["violations"])
