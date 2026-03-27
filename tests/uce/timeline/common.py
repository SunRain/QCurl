"""Shared helpers for UCE timeline collection and validation."""

from __future__ import annotations

import json
from datetime import datetime
from datetime import timezone
from pathlib import Path
from typing import Any
from typing import Iterable


TIMELINE_EVENT_SCHEMA = "qcurl-uce/timeline-event@v1"

_EVENT_NAME_MAP = {
    "pause": "pause_req",
    "resume": "resume_req",
    "bp_on": "backpressure_on",
    "bp_off": "backpressure_off",
}


def utc_now_iso() -> str:
    """Return a compact UTC ISO-8601 timestamp."""

    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def load_json(path: Path) -> dict[str, Any]:
    """Load a UTF-8 JSON document."""

    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, payload: dict[str, Any]) -> None:
    """Write a JSON document using UTF-8 without BOM."""

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def iter_jsonl(path: Path) -> list[dict[str, Any]]:
    """Load a JSONL file and ignore malformed lines."""

    rows: list[dict[str, Any]] = []
    if not path.exists():
        return rows
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not raw.strip():
            continue
        try:
            value = json.loads(raw)
        except json.JSONDecodeError:
            continue
        if isinstance(value, dict):
            rows.append(value)
    return rows


def write_jsonl(path: Path, rows: Iterable[dict[str, Any]]) -> None:
    """Write JSONL rows with UTF-8 encoding."""

    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        for row in rows:
            handle.write(json.dumps(row, ensure_ascii=False, sort_keys=True) + "\n")


def stream_identity(root: Path, artifact_path: Path, runner: str) -> tuple[str, str]:
    """Return `(case_id, stream_id)` for a payload artifact."""

    relative_parent = artifact_path.relative_to(root).parent
    case_id = relative_parent.name or artifact_path.stem
    stream_id = f"{runner}:{relative_parent.as_posix() or case_id}"
    return case_id, stream_id


def add_event(
    events: list[dict[str, Any]],
    *,
    provider: str,
    stream_id: str,
    case_id: str,
    source_kind: str,
    source_path: Path,
    event: str,
    seq: int,
    **extra: Any,
) -> int:
    """Append a normalized timeline event and return the next seq."""

    payload: dict[str, Any] = {
        "schema": TIMELINE_EVENT_SCHEMA,
        "provider": provider,
        "stream_id": stream_id,
        "case_id": case_id,
        "source_kind": source_kind,
        "source_path": str(source_path),
        "seq": seq,
        "event": event,
    }
    for key, value in extra.items():
        if value is None:
            continue
        payload[key] = value
    events.append(payload)
    return seq + 1


def add_payload_events(
    events: list[dict[str, Any]],
    *,
    provider: str,
    stream_id: str,
    case_id: str,
    source_path: Path,
    payload: dict[str, Any],
) -> None:
    """Normalize a baseline/qcurl artifact payload into timeline events."""

    seq = 1
    finished_seen = False

    def emit(event: str, **extra: Any) -> None:
        nonlocal seq
        seq = add_event(
            events,
            provider=provider,
            stream_id=stream_id,
            case_id=case_id,
            source_kind="artifact_payload",
            source_path=source_path,
            event=event,
            seq=seq,
            **extra,
        )

    request = payload.get("request")
    if isinstance(request, dict):
        emit(
            "request_headers",
            method=request.get("method"),
            url=request.get("url"),
            headers=request.get("headers"),
            body_len=request.get("body_len"),
            body_sha256=request.get("body_sha256"),
        )

    response = payload.get("response")
    if isinstance(response, dict):
        emit(
            "response_headers",
            status=response.get("status"),
            http_version=response.get("http_version"),
            headers=response.get("headers"),
            body_len=response.get("body_len"),
            body_sha256=response.get("body_sha256"),
        )

    strict = payload.get("pause_resume_strict")
    if isinstance(strict, dict):
        for raw_event in strict.get("events") or []:
            if not isinstance(raw_event, dict):
                continue
            emit(
                _EVENT_NAME_MAP.get(str(raw_event.get("type") or ""), str(raw_event.get("type") or "")),
                raw_event_type=raw_event.get("type"),
                raw_seq=raw_event.get("seq"),
                t_us=raw_event.get("t_us"),
                bytes_delivered_total=raw_event.get("bytes_delivered_total"),
                bytes_written_total=raw_event.get("bytes_written_total"),
            )
            if str(raw_event.get("type") or "") == "finished":
                finished_seen = True

    pause_resume = payload.get("pause_resume")
    if isinstance(pause_resume, dict):
        for raw_name in pause_resume.get("event_seq") or []:
            event_name = _EVENT_NAME_MAP.get(str(raw_name), str(raw_name))
            emit(
                event_name,
                pause_offset=pause_resume.get("pause_offset"),
                pause_count=pause_resume.get("pause_count"),
                resume_count=pause_resume.get("resume_count"),
            )
            if event_name == "finished":
                finished_seen = True

    backpressure = payload.get("backpressure_contract")
    if isinstance(backpressure, dict):
        for raw_name in backpressure.get("event_seq") or []:
            emit(
                _EVENT_NAME_MAP.get(str(raw_name), str(raw_name)),
                limit_bytes=backpressure.get("limit_bytes"),
                resume_bytes=backpressure.get("resume_bytes"),
                peak_buffered_bytes=backpressure.get("peak_buffered_bytes"),
            )

    upload_pause_resume = payload.get("upload_pause_resume")
    if isinstance(upload_pause_resume, dict):
        for raw_name in upload_pause_resume.get("event_seq") or []:
            emit(
                "upload_pause" if str(raw_name) == "pause" else "upload_resume",
                payload_size=upload_pause_resume.get("payload_size"),
                zero_read_count=upload_pause_resume.get("zero_read_count"),
            )
        result = upload_pause_resume.get("result") or {}
        if isinstance(result, dict) and int(result.get("qcurl_error") or 0) == 0 and not finished_seen:
            emit("finished", result="pass", payload_size=upload_pause_resume.get("payload_size"))
            finished_seen = True

    progress_summary = payload.get("progress_summary")
    if isinstance(progress_summary, dict):
        for lane in ("download", "upload"):
            lane_payload = progress_summary.get(lane)
            if not isinstance(lane_payload, dict):
                continue
            emit(
                f"{lane}_progress_summary",
                monotonic=lane_payload.get("monotonic"),
                **{
                    f"{lane}_now": lane_payload.get("now_max"),
                    f"{lane}_total": lane_payload.get("total_max"),
                },
            )

    if isinstance(response, dict):
        body_len = response.get("body_len")
        if isinstance(body_len, int) and body_len > 0:
            emit(
                "body_complete",
                body_len=body_len,
                bytes_written_total=body_len,
                download_now=body_len,
            )
        if not finished_seen:
            emit(
                "finished",
                result="pass",
                status=response.get("status"),
                body_len=response.get("body_len"),
            )

