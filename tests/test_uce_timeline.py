from __future__ import annotations

import json
from pathlib import Path

from scripts.uce_gate import contracts as uce_contracts
from tests.uce.timeline.collect_from_lc import collect_from_lc
from tests.uce.timeline.collect_from_qt import collect_from_qt
from tests.uce.timeline.common import iter_jsonl
from tests.uce.timeline.common import write_jsonl
from tests.uce.timeline.validate import main as validate_timeline_main
from tests.uce.timeline.validate import validate_timelines


def _write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def _timeline_event(*, seq: int = 1, **overrides: object) -> dict[str, object]:
    event: dict[str, object] = {
        "schema": "qcurl-uce/timeline-event@v1",
        "provider": "qt",
        "stream_id": "qt:strict-parser",
        "case_id": "strict-parser",
        "source_kind": "artifact_payload",
        "source_path": "fixture.json",
        "seq": seq,
        "event": "request_headers",
    }
    event.update(overrides)
    return event


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


def test_collect_from_qt_keeps_current_run_seed_streams_separate(tmp_path: Path) -> None:
    artifacts_root = tmp_path / "artifacts"
    qt_artifacts_root = tmp_path / "test-artifacts"
    current_run_root = qt_artifacts_root / "dci" / "current-run"
    stale_run_root = qt_artifacts_root / "dci" / "stale-run"

    for seed, root in ((17, current_run_root), (29, current_run_root), (41, stale_run_root)):
        evidence_path = root / "testAsyncMockChaosCancel" / f"seed-{seed}" / f"dci_evidence_{seed}.jsonl"
        write_jsonl(
            evidence_path,
            [
                {
                    "schema": "qcurl-uce/dci-evidence@v1",
                    "case": "testAsyncMockChaosCancel",
                    "stream": "testAsyncMockChaosCancel:cancel",
                    "event": "request_headers",
                    "seq": 1,
                },
                {
                    "schema": "qcurl-uce/dci-evidence@v1",
                    "case": "testAsyncMockChaosCancel",
                    "stream": "testAsyncMockChaosCancel:cancel",
                    "event": "finished",
                    "seq": 2,
                },
            ],
        )

    result = collect_from_qt(
        artifacts_root,
        dci_evidence_roots=[current_run_root],
    )

    assert result["stream_count"] == 2
    assert len({event["stream_id"] for event in result["events"]}) == 2
    assert all("stale-run" not in source for source in result["source_files"])


def test_collect_from_qt_emits_finished_after_synthesized_body_event(tmp_path: Path) -> None:
    artifacts_root = tmp_path / "artifacts"
    qcurl_path = artifacts_root / "p2_pause_resume_strict" / "p2_pause_resume_strict_h2" / "qcurl.json"
    _write_json(
        qcurl_path,
        {
            "runner": "qcurl",
            "request": {"method": "GET", "url": "https://example.test/data"},
            "response": {"status": 200, "body_len": 128},
            "pause_resume_strict": {
                "events": [
                    {"seq": 1, "type": "start", "t_us": 0},
                    {"seq": 2, "type": "finished", "t_us": 10},
                ]
            },
        },
    )

    result = collect_from_qt(artifacts_root)
    event_names = [event["event"] for event in result["events"]]

    assert event_names.count("finished") == 1
    assert event_names[-2:] == ["body_complete", "finished"]


def test_run_timeline_contract_uses_only_current_run_qt_evidence(tmp_path: Path, monkeypatch) -> None:
    repo_root = tmp_path / "repo"
    build_dir = tmp_path / "build"
    evidence_dir = tmp_path / "evidence"
    contract_path = repo_root / "tests" / "uce" / "contracts" / "timeline@v1.yaml"
    contract_path.parent.mkdir(parents=True)
    contract_path.write_text("schema: qcurl-uce/timeline-contract@v1\n", encoding="utf-8")
    captured: dict[str, object] = {}

    monkeypatch.setattr(
        uce_contracts,
        "collect_from_lc",
        lambda _root: {"provider": "libcurl_consistency", "events": []},
    )

    def collect_qt(_artifacts_root, qt_artifacts_root=None, *, dci_evidence_roots=None):
        captured["qt_artifacts_root"] = qt_artifacts_root
        captured["dci_evidence_roots"] = dci_evidence_roots
        return {"provider": "qt", "events": []}

    monkeypatch.setattr(uce_contracts, "collect_from_qt", collect_qt)
    monkeypatch.setattr(
        uce_contracts,
        "validate_timelines",
        lambda *_args, **_kwargs: {
            "policy_violations": [],
            "summary": {"failed_streams": 0},
            "provider_summary": {},
        },
    )

    violations = uce_contracts.run_timeline_contract(
        repo_root,
        build_dir,
        evidence_dir,
        {},
        tier="nightly",
        run_id="current-run",
        artifact_roots=[evidence_dir / "libcurl_consistency" / "current-run" / "artifacts"],
    )

    assert violations == []
    assert captured["qt_artifacts_root"] is None
    assert captured["dci_evidence_roots"] == [
        build_dir / "test-artifacts" / "dci" / "current-run",
        build_dir / "test-artifacts" / "bp" / "current-run",
    ]


def test_run_timeline_contract_merges_collector_parse_errors(tmp_path: Path, monkeypatch) -> None:
    repo_root = tmp_path / "repo"
    build_dir = tmp_path / "build"
    evidence_dir = tmp_path / "evidence"
    contract_path = repo_root / "tests" / "uce" / "contracts" / "timeline@v1.yaml"
    contract_path.parent.mkdir(parents=True)
    contract_path.write_text(
        Path("tests/uce/contracts/timeline@v1.yaml").read_text(encoding="utf-8"),
        encoding="utf-8",
    )
    parse_error = {
        "code": "timeline_evidence_parse_error",
        "path": "dci/run/dci_evidence.jsonl",
        "line": 7,
        "error_code": "invalid_json",
        "summary": "line is not valid JSON",
    }

    monkeypatch.setattr(
        uce_contracts,
        "collect_from_lc",
        lambda _root: {"provider": "libcurl_consistency", "events": [], "errors": []},
    )
    monkeypatch.setattr(
        uce_contracts,
        "collect_from_qt",
        lambda *_args, **_kwargs: {"provider": "qt", "events": [], "errors": [parse_error]},
    )
    monkeypatch.setattr(uce_contracts, "_register_timeline_result", lambda *_args: None)

    violations = uce_contracts.run_timeline_contract(
        repo_root,
        build_dir,
        evidence_dir,
        {},
        tier="pr",
        run_id="run",
        artifact_roots=[evidence_dir / "libcurl_consistency" / "run" / "artifacts"],
    )

    report = json.loads((evidence_dir / "timeline" / "report.json").read_text(encoding="utf-8"))
    assert violations == [
        "timeline_evidence_parse_error",
        "timeline_provider_missing",
        "timeline_contract_failed",
    ]
    assert report["summary"]["parse_errors"] == 1
    assert parse_error in report["violations"]


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


def test_timeline_parser_aggregates_utf8_json_and_value_errors(tmp_path: Path) -> None:
    timeline_path = tmp_path / "bad.timeline.jsonl"
    valid_line = json.dumps(_timeline_event()).encode("utf-8")
    timeline_path.write_bytes(
        b"\n".join(
            [
                valid_line,
                b'{"authorization":"Bearer secret-utf8-\xff"}',
                b'{"token":"secret-truncated"',
                b"[]",
                b"42",
            ]
        )
        + b"\n"
    )

    parsed = iter_jsonl(timeline_path, relative_to=tmp_path)
    report = validate_timelines(
        Path("tests/uce/contracts/timeline@v1.yaml"),
        [timeline_path],
        {"qt"},
    )

    assert parsed.values == [_timeline_event()]
    assert [error["error_code"] for error in parsed.errors] == [
        "invalid_utf8",
        "invalid_json",
        "json_not_object",
        "json_not_object",
    ]
    assert report["summary"]["parse_errors"] == 4
    parse_errors = [
        violation
        for violation in report["violations"]
        if violation["code"] == "timeline_evidence_parse_error"
    ]
    assert [error["line"] for error in parse_errors] == [2, 3, 4, 5]
    assert {error["path"] for error in parse_errors} == {"bad.timeline.jsonl"}
    assert "secret" not in json.dumps(parse_errors)


def test_timeline_parser_rejects_embedded_unicode_line_separator(tmp_path: Path) -> None:
    timeline_path = tmp_path / "unicode-separator.timeline.jsonl"
    first = json.dumps(_timeline_event(seq=1), ensure_ascii=False)
    second = json.dumps(_timeline_event(seq=2), ensure_ascii=False)
    timeline_path.write_text(first + "\u2028" + second + "\n", encoding="utf-8")

    parsed = iter_jsonl(timeline_path, relative_to=tmp_path)

    assert parsed.values == []
    assert [error["error_code"] for error in parsed.errors] == ["invalid_json"]


def test_timeline_parser_enforces_schema_provider_and_identity_contract(tmp_path: Path) -> None:
    timeline_path = tmp_path / "contract-errors.timeline.jsonl"
    write_jsonl(
        timeline_path,
        [
            _timeline_event(provider=""),
            _timeline_event(seq=2, schema="qcurl-uce/timeline-event@v999"),
            _timeline_event(seq=3, provider="third_party"),
            _timeline_event(seq=4),
            _timeline_event(seq=4, event="response_headers"),
        ],
    )

    report = validate_timelines(
        Path("tests/uce/contracts/timeline@v1.yaml"),
        [timeline_path],
        {"qt"},
    )

    assert "timeline_evidence_parse_error" in report["policy_violations"]
    error_codes = {
        violation["error_code"]
        for violation in report["violations"]
        if violation["code"] == "timeline_evidence_parse_error"
    }
    assert {
        "missing_provider",
        "missing_identity",
        "unknown_schema",
        "unknown_provider",
        "duplicate_identity",
    } <= error_codes


def test_timeline_cli_writes_all_parse_errors_and_returns_nonzero(tmp_path: Path) -> None:
    first = tmp_path / "first.timeline.jsonl"
    second = tmp_path / "second.timeline.jsonl"
    report_path = tmp_path / "report.json"
    first.write_bytes(b'{"token":"secret-one"\n')
    second.write_bytes(b"[]\n\xff\n")

    rc = validate_timeline_main(
        [
            "--contract",
            "tests/uce/contracts/timeline@v1.yaml",
            "--timeline",
            str(first),
            "--timeline",
            str(second),
            "--required-provider",
            "qt",
            "--report",
            str(report_path),
        ]
    )

    report = json.loads(report_path.read_text(encoding="utf-8"))
    assert rc == 3
    assert report["summary"]["parse_errors"] == 3
    assert [error["path"] for error in report["violations"][:3]] == [
        "first.timeline.jsonl",
        "second.timeline.jsonl",
        "second.timeline.jsonl",
    ]
    assert "secret-one" not in report_path.read_text(encoding="utf-8")


def test_timeline_cli_accepts_mixed_relative_and_absolute_paths(tmp_path: Path, monkeypatch) -> None:
    monkeypatch.chdir(tmp_path)
    relative_path = Path("relative.timeline.jsonl")
    absolute_path = tmp_path / "absolute.timeline.jsonl"
    report_path = tmp_path / "report.json"
    relative_path.write_text(json.dumps(_timeline_event(seq=1)) + "\n", encoding="utf-8")
    absolute_path.write_text(json.dumps(_timeline_event(seq=2)) + "\n", encoding="utf-8")

    rc = validate_timeline_main(
        [
            "--contract",
            str(Path(__file__).resolve().parent / "uce" / "contracts" / "timeline@v1.yaml"),
            "--timeline",
            str(relative_path),
            "--timeline",
            str(absolute_path),
            "--required-provider",
            "qt",
            "--report",
            str(report_path),
        ]
    )

    assert rc == 0
