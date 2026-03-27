"""Validate normalized timeline evidence against `timeline@v1`."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from tests.uce.timeline.common import utc_now_iso
from tests.uce.timeline.common import write_json


_HEADER_EVENTS = {"request_headers", "response_headers"}
_BODY_EVENTS = {
    "body_chunk",
    "body_complete",
    "first_byte",
    "download_progress_summary",
    "upload_progress_summary",
}
_CONTROL_EVENTS = {
    "pause_req",
    "pause_effective",
    "resume_req",
    "resume_effective",
    "backpressure_on",
    "backpressure_off",
    "upload_pause",
    "upload_resume",
    "close_frame",
}
_TERMINAL_EVENTS = {"finished"}
_MONOTONIC_FIELDS = ("download_now", "upload_now", "bytes_delivered_total", "bytes_written_total")


def _load_contract(contract_path: Path) -> dict[str, Any]:
    """Load the contract file.

    `timeline@v1.yaml` is stored as JSON syntax, which remains valid YAML 1.2.
    """

    return json.loads(contract_path.read_text(encoding="utf-8"))


def _load_events(timeline_paths: list[Path]) -> list[dict[str, Any]]:
    """Load timeline events from JSONL files."""

    events: list[dict[str, Any]] = []
    for path in timeline_paths:
        if not path.exists():
            continue
        for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
            if not raw.strip():
                continue
            try:
                event = json.loads(raw)
            except json.JSONDecodeError:
                continue
            if isinstance(event, dict):
                event["_timeline_path"] = str(path)
                events.append(event)
    return events


def _validate_stream(events: list[dict[str, Any]]) -> list[dict[str, str]]:
    """Return invariant violations for a single stream."""

    violations: list[dict[str, str]] = []
    request_index = -1
    response_index = -1
    first_body_index = -1
    finished_indices: list[int] = []
    last_values: dict[str, int] = {}
    pause_effective_index = -1
    resume_req_index = -1
    backpressure_on_index = -1
    backpressure_off_index = -1

    previous_seq = 0
    for index, event in enumerate(events):
        seq = event.get("seq")
        if not isinstance(seq, int) or seq <= previous_seq:
            violations.append(
                {
                    "id": "seq_monotonic",
                    "message": f"stream seq not strictly increasing at idx={index}: {seq!r}",
                }
            )
        else:
            previous_seq = seq

        event_name = str(event.get("event") or "")
        if event_name == "request_headers" and request_index < 0:
            request_index = index
        if event_name == "response_headers" and response_index < 0:
            response_index = index
        if event_name in _BODY_EVENTS | _CONTROL_EVENTS and first_body_index < 0:
            first_body_index = index
        if event_name == "finished":
            finished_indices.append(index)
        if event_name == "pause_effective" and pause_effective_index < 0:
            pause_effective_index = index
        if event_name == "resume_req" and resume_req_index < 0:
            resume_req_index = index
        if event_name == "backpressure_on" and backpressure_on_index < 0:
            backpressure_on_index = index
        if event_name == "backpressure_off" and backpressure_off_index < 0:
            backpressure_off_index = index

        if event_name.endswith("_progress_summary") and event.get("monotonic") is False:
            violations.append(
                {
                    "id": "progress_monotonic",
                    "message": f"{event_name}.monotonic=false",
                }
            )

        for field in _MONOTONIC_FIELDS:
            value = event.get(field)
            if not isinstance(value, int):
                continue
            if field in last_values and value < last_values[field]:
                violations.append(
                    {
                        "id": "progress_monotonic",
                        "message": f"{field} regressed: {value} < {last_values[field]}",
                    }
                )
            last_values[field] = value

    if request_index >= 0 and response_index >= 0 and response_index <= request_index:
        violations.append(
            {
                "id": "request_before_response",
                "message": "response_headers appeared before request_headers",
            }
        )

    if first_body_index >= 0:
        boundary = response_index if response_index >= 0 else request_index
        if boundary >= 0 and first_body_index <= boundary:
            violations.append(
                {
                    "id": "headers_before_body",
                    "message": "body/control event appeared before headers completed",
                }
            )

    if len(finished_indices) > 1:
        violations.append(
            {
                "id": "finished_once",
                "message": f"finished emitted {len(finished_indices)} times",
            }
        )

    if finished_indices:
        terminal_index = finished_indices[0]
        for later in events[terminal_index + 1 :]:
            later_name = str(later.get("event") or "")
            if later_name in _BODY_EVENTS | _CONTROL_EVENTS:
                violations.append(
                    {
                        "id": "terminal_quiet",
                        "message": f"event {later_name} observed after finished",
                    }
                )
                break

    if pause_effective_index >= 0 and resume_req_index >= 0 and resume_req_index > pause_effective_index:
        pause_ref = events[pause_effective_index]
        for later in events[pause_effective_index + 1 : resume_req_index + 1]:
            for field in ("bytes_delivered_total", "bytes_written_total"):
                baseline = pause_ref.get(field)
                candidate = later.get(field)
                if isinstance(baseline, int) and isinstance(candidate, int) and candidate != baseline:
                    violations.append(
                        {
                            "id": "pause_window_quiet",
                            "message": f"{field} changed inside pause window: {baseline} -> {candidate}",
                        }
                    )
                    break

    if backpressure_off_index >= 0 and (backpressure_on_index < 0 or backpressure_off_index < backpressure_on_index):
        violations.append(
            {
                "id": "backpressure_cycle",
                "message": "backpressure_off observed before backpressure_on",
            }
        )
    if backpressure_on_index >= 0 and backpressure_off_index < 0:
        violations.append(
            {
                "id": "backpressure_cycle",
                "message": "backpressure_on observed without matching backpressure_off",
            }
        )

    deduped: list[dict[str, str]] = []
    seen = set()
    for violation in violations:
        marker = (violation["id"], violation["message"])
        if marker in seen:
            continue
        seen.add(marker)
        deduped.append(violation)
    return deduped


def validate_timelines(contract_path: Path, timeline_paths: list[Path], required_providers: set[str]) -> dict[str, Any]:
    """Validate timeline JSONL files and return a structured report."""

    contract = _load_contract(contract_path)
    raw_events = _load_events(timeline_paths)

    grouped: dict[str, list[dict[str, Any]]] = {}
    provider_streams: dict[str, set[str]] = {}
    for event in raw_events:
        stream_id = str(event.get("stream_id") or "")
        provider = str(event.get("provider") or "")
        if not stream_id or not provider:
            continue
        grouped.setdefault(stream_id, []).append(event)
        provider_streams.setdefault(provider, set()).add(stream_id)

    stream_reports: list[dict[str, Any]] = []
    for stream_id in sorted(grouped):
        stream_events = sorted(grouped[stream_id], key=lambda item: (int(item.get("seq") or 0), item.get("event") or ""))
        provider = str(stream_events[0].get("provider") or "")
        case_id = str(stream_events[0].get("case_id") or "")
        source_kind = str(stream_events[0].get("source_kind") or "")
        violations = _validate_stream(stream_events)
        stream_reports.append(
            {
                "stream_id": stream_id,
                "provider": provider,
                "case_id": case_id,
                "source_kind": source_kind,
                "event_count": len(stream_events),
                "result": "pass" if not violations else "fail",
                "violations": violations,
            }
        )

    report_violations: list[dict[str, Any]] = []
    policy_codes: list[str] = []
    for provider in sorted(required_providers):
        if provider_streams.get(provider):
            continue
        report_violations.append(
            {
                "code": "timeline_provider_missing",
                "provider": provider,
                "message": f"required provider has no timeline streams: {provider}",
            }
        )
        if "timeline_provider_missing" not in policy_codes:
            policy_codes.append("timeline_provider_missing")

    failed_streams = [stream for stream in stream_reports if stream["result"] != "pass"]
    if failed_streams and "timeline_contract_failed" not in policy_codes:
        policy_codes.append("timeline_contract_failed")
    if report_violations and "timeline_contract_failed" not in policy_codes:
        policy_codes.append("timeline_contract_failed")

    provider_summary = {}
    for provider in sorted(set(provider_streams) | set(required_providers)):
        provider_summary[provider] = {
            "required": provider in required_providers,
            "present": bool(provider_streams.get(provider)),
            "stream_count": len(provider_streams.get(provider, set())),
        }

    return {
        "generated_at_utc": utc_now_iso(),
        "contract_id": contract.get("contract_id"),
        "contract_schema": contract.get("schema"),
        "timeline_event_schema": contract.get("timeline_event_schema"),
        "required_providers": sorted(required_providers),
        "summary": {
            "stream_count": len(stream_reports),
            "event_count": len(raw_events),
            "failed_streams": len(failed_streams),
        },
        "provider_summary": provider_summary,
        "streams": stream_reports,
        "violations": report_violations,
        "policy_violations": policy_codes,
    }


def main(argv: list[str] | None = None) -> int:
    """CLI entry point."""

    parser = argparse.ArgumentParser(description="Validate UCE timeline evidence.")
    parser.add_argument("--contract", required=True, help="Path to timeline contract YAML/JSON.")
    parser.add_argument("--timeline", action="append", default=[], help="Timeline JSONL path (repeatable).")
    parser.add_argument(
        "--required-provider",
        action="append",
        default=[],
        help="Provider that must have at least one stream.",
    )
    parser.add_argument("--report", required=True, help="Output JSON report path.")
    args = parser.parse_args(argv)

    report = validate_timelines(
        Path(args.contract),
        [Path(item) for item in args.timeline],
        set(args.required_provider),
    )
    write_json(Path(args.report), report)
    return 0 if not report["policy_violations"] else 3


if __name__ == "__main__":
    raise SystemExit(main())
