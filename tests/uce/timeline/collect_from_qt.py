"""Collect Qt-side timeline evidence from qcurl artifacts and Qt test artifacts."""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Any

from tests.uce.timeline.common import add_event
from tests.uce.timeline.common import add_payload_events
from tests.uce.timeline.common import iter_jsonl
from tests.uce.timeline.common import load_json
from tests.uce.timeline.common import stream_identity
from tests.uce.timeline.common import write_json
from tests.uce.timeline.common import write_jsonl


def _collect_qcurl_payloads(result: dict[str, Any], artifacts_root: Path, events: list[dict[str, Any]]) -> None:
    """Collect `qcurl.json` payload timelines."""

    if not artifacts_root.exists():
        result["missing_roots"].append(str(artifacts_root))
        return

    for artifact_path in sorted(artifacts_root.rglob("qcurl.json")):
        try:
            payload = load_json(artifact_path)
        except Exception as exc:
            result["errors"].append(f"{artifact_path}: {exc}")
            continue
        case_id, stream_id = stream_identity(artifacts_root, artifact_path, "qcurl")
        stream_events: list[dict[str, Any]] = []
        add_payload_events(
            stream_events,
            provider="qt",
            stream_id=stream_id,
            case_id=case_id,
            source_path=artifact_path,
            payload=payload,
        )
        if not stream_events:
            continue
        events.extend(stream_events)
        result["source_files"].append(str(artifact_path))
        result["stream_count"] += 1


def _collect_websocket_evidence(result: dict[str, Any], qt_artifacts_root: Path, events: list[dict[str, Any]]) -> None:
    """Collect websocket JSONL evidence from `build/test-artifacts`."""

    if not qt_artifacts_root.exists():
        result["missing_roots"].append(str(qt_artifacts_root))
        return

    for jsonl_path in sorted(qt_artifacts_root.rglob("ws_evidence_*.jsonl")):
        rows = iter_jsonl(jsonl_path)
        if not rows:
            continue
        grouped: dict[str, list[dict[str, Any]]] = {}
        for row in rows:
            case_id = str(row.get("case") or "").strip()
            if not case_id:
                continue
            grouped.setdefault(case_id, []).append(row)
        if not grouped:
            continue

        for case_id, case_rows in grouped.items():
            seq = 1
            stream_id = f"websocket:{jsonl_path.stem}:{case_id}"
            for row in case_rows:
                raw_event = str(row.get("event") or "")
                if raw_event == "handshake_ok":
                    seq = add_event(
                        events,
                        provider="qt",
                        stream_id=stream_id,
                        case_id=case_id,
                        source_kind="websocket_evidence",
                        source_path=jsonl_path,
                        event="request_headers",
                        seq=seq,
                        path=row.get("path"),
                        target=row.get("target"),
                    )
                    continue
                if raw_event == "ws_frame":
                    opcode = int(row.get("opcode") or 0)
                    event_name = "close_frame" if opcode == 0x8 else "body_chunk"
                    seq = add_event(
                        events,
                        provider="qt",
                        stream_id=stream_id,
                        case_id=case_id,
                        source_kind="websocket_evidence",
                        source_path=jsonl_path,
                        event=event_name,
                        seq=seq,
                        opcode=opcode,
                        fin=row.get("fin"),
                        payload_len=row.get("payload_len"),
                        payload_sha256=row.get("payload_sha256"),
                        close_code=row.get("close_code"),
                        close_reason=row.get("close_reason"),
                    )
                    continue
                if raw_event == "connection_closed":
                    seq = add_event(
                        events,
                        provider="qt",
                        stream_id=stream_id,
                        case_id=case_id,
                        source_kind="websocket_evidence",
                        source_path=jsonl_path,
                        event="finished",
                        seq=seq,
                    )
            result["stream_count"] += 1
        result["source_files"].append(str(jsonl_path))


def _collect_dci_evidence(
    result: dict[str, Any],
    search_roots: list[Path],
    events: list[dict[str, Any]],
) -> None:
    """Collect normalized DCI JSONL evidence from Qt test artifacts."""

    seen_paths: set[Path] = set()
    for root in search_roots:
        if not root.exists():
            continue
        for jsonl_path in sorted(root.rglob("dci_evidence_*.jsonl")):
            if jsonl_path in seen_paths:
                continue
            seen_paths.add(jsonl_path)

            rows = iter_jsonl(jsonl_path)
            if not rows:
                continue

            grouped: dict[str, list[dict[str, Any]]] = {}
            for row in rows:
                case_id = str(row.get("case") or "").strip()
                stream_id = str(row.get("stream") or "").strip()
                if not case_id:
                    continue
                if not stream_id:
                    stream_id = f"dci:{jsonl_path.stem}:{case_id}"
                grouped.setdefault(f"{case_id}|{stream_id}", []).append(row)

            if not grouped:
                continue

            for _, case_rows in grouped.items():
                case_rows.sort(key=lambda item: int(item.get("seq") or 0))
                case_id = str(case_rows[0].get("case") or "").strip()
                stream_id = str(case_rows[0].get("stream") or "").strip() or f"dci:{jsonl_path.stem}:{case_id}"
                seq = 1
                for row in case_rows:
                    event_name = str(row.get("event") or "").strip()
                    if not event_name:
                        continue
                    extra = {
                        key: value
                        for key, value in row.items()
                        if key not in {"schema", "case", "stream", "event", "seq"}
                    }
                    seq_value = row.get("seq")
                    if isinstance(seq_value, int) and seq_value > 0:
                        seq = seq_value
                    seq = add_event(
                        events,
                        provider="qt",
                        stream_id=stream_id,
                        case_id=case_id,
                        source_kind="dci_evidence",
                        source_path=jsonl_path,
                        event=event_name,
                        seq=seq,
                        **extra,
                    )
                result["stream_count"] += 1
            result["source_files"].append(str(jsonl_path))


def collect_from_qt(artifacts_root: Path, qt_artifacts_root: Path | None = None) -> dict[str, Any]:
    """Collect normalized timeline events for Qt/qcurl providers."""

    result: dict[str, Any] = {
        "provider": "qt",
        "artifacts_root": str(artifacts_root),
        "qt_artifacts_root": str(qt_artifacts_root) if qt_artifacts_root else "",
        "stream_count": 0,
        "event_count": 0,
        "source_files": [],
        "missing_roots": [],
        "errors": [],
        "events": [],
    }

    events: list[dict[str, Any]] = []
    _collect_qcurl_payloads(result, artifacts_root, events)
    if qt_artifacts_root is not None:
        _collect_websocket_evidence(result, qt_artifacts_root, events)
    search_roots = [artifacts_root]
    if qt_artifacts_root is not None:
        search_roots.append(qt_artifacts_root)
    _collect_dci_evidence(result, search_roots, events)

    result["events"] = events
    result["event_count"] = len(events)
    return result


def main(argv: list[str] | None = None) -> int:
    """CLI entry point."""

    parser = argparse.ArgumentParser(description="Collect Qt timeline evidence into JSONL.")
    parser.add_argument("--artifacts-root", required=True, help="Root of curl/tests/http/gen/artifacts.")
    parser.add_argument(
        "--qt-artifacts-root",
        default="",
        help="Optional root of build/test-artifacts for websocket evidence.",
    )
    parser.add_argument("--output", required=True, help="Output JSONL path.")
    parser.add_argument("--report", default="", help="Optional JSON report path.")
    args = parser.parse_args(argv)

    qt_root = Path(args.qt_artifacts_root) if args.qt_artifacts_root else None
    result = collect_from_qt(Path(args.artifacts_root), qt_root)
    write_jsonl(Path(args.output), result["events"])
    if args.report:
        report_payload = {key: value for key, value in result.items() if key != "events"}
        write_json(Path(args.report), report_payload)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
