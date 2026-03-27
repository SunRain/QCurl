"""Validate backpressure evidence against `bp@v1`."""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from datetime import datetime
from datetime import timezone
from pathlib import Path
from typing import Any


@dataclass(frozen=True)
class EvidenceLoadResult:
    events: list[dict[str, Any]]
    scanned_files: list[str]
    parse_errors: int
    missing_roots: list[str]


def utc_now_iso() -> str:
    """Return the current UTC time in ISO-8601 format."""

    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def write_json(path: Path, payload: dict[str, Any]) -> None:
    """Write JSON payload to disk (UTF-8)."""

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def load_contract(contract_path: Path) -> dict[str, Any]:
    """Load the contract file.

    `bp@v1.yaml` is stored as JSON syntax, which remains valid YAML 1.2.
    """

    return json.loads(contract_path.read_text(encoding="utf-8"))


def _iter_evidence_paths(root: Path) -> list[Path]:
    if not root.exists():
        return []
    return sorted(path for path in root.rglob("dci_evidence_*.jsonl") if path.is_file())


def load_dci_evidence(evidence_roots: list[Path]) -> EvidenceLoadResult:
    """Load DCI-style JSONL evidence from roots."""

    events: list[dict[str, Any]] = []
    scanned_files: list[str] = []
    parse_errors = 0
    missing_roots: list[str] = []

    for root in evidence_roots:
        if not root.exists():
            missing_roots.append(str(root))
            continue
        for path in _iter_evidence_paths(root):
            scanned_files.append(str(path))
            for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
                if not raw.strip():
                    continue
                try:
                    payload = json.loads(raw)
                except json.JSONDecodeError:
                    parse_errors += 1
                    continue
                if not isinstance(payload, dict):
                    parse_errors += 1
                    continue
                payload["_evidence_path"] = str(path)
                events.append(payload)

    return EvidenceLoadResult(
        events=events,
        scanned_files=scanned_files,
        parse_errors=parse_errors,
        missing_roots=missing_roots,
    )


def _first_index(events: list[dict[str, Any]], event_name: str) -> int:
    for idx, item in enumerate(events):
        if str(item.get("event") or "") == event_name:
            return idx
    return -1


def _int_field(event: dict[str, Any], field: str) -> int | None:
    value = event.get(field)
    if isinstance(value, int):
        return value
    return None


def validate_bp(contract_path: Path, evidence_roots: list[Path]) -> dict[str, Any]:
    """Validate BP evidence and return a structured report."""

    contract = load_contract(contract_path)
    expected_schema = str(contract.get("evidence_schema") or "")
    case_name = str(contract.get("case") or "")
    stream_id = str(contract.get("stream") or "")
    required_events = [str(item) for item in contract.get("required_events") or []]
    params = contract.get("parameters") if isinstance(contract.get("parameters"), dict) else {}
    expected_status = int(params.get("expected_status") or 0)
    expected_body_len = int(params.get("expected_body_len") or 0)
    max_pause_drift_bytes = int(params.get("max_pause_drift_bytes") or 0)

    loaded = load_dci_evidence(evidence_roots)

    violations: list[dict[str, Any]] = []
    policy_codes: list[str] = []

    if loaded.parse_errors:
        violations.append(
            {
                "code": "bp_evidence_parse_error",
                "message": f"evidence parse errors: {loaded.parse_errors}",
            }
        )
        policy_codes.append("bp_evidence_parse_error")

    matched = [
        event
        for event in loaded.events
        if str(event.get("case") or "") == case_name and str(event.get("stream") or "") == stream_id
    ]

    if not loaded.scanned_files or not matched:
        violations.append(
            {
                "code": "bp_evidence_missing",
                "message": "missing bp evidence stream (no jsonl files or no matching case/stream)",
                "case": case_name,
                "stream": stream_id,
            }
        )
        if "bp_evidence_missing" not in policy_codes:
            policy_codes.append("bp_evidence_missing")

    matched = sorted(matched, key=lambda item: int(item.get("seq") or 0))

    schema_mismatch = False
    previous_seq = 0
    for idx, event in enumerate(matched):
        schema = str(event.get("schema") or "")
        if expected_schema and schema != expected_schema:
            schema_mismatch = True
        seq = event.get("seq")
        if not isinstance(seq, int) or seq <= previous_seq:
            violations.append(
                {
                    "code": "bp_contract_failed",
                    "message": f"seq not strictly increasing at idx={idx}: {seq!r}",
                }
            )
        else:
            previous_seq = seq

    if schema_mismatch:
        violations.append(
            {
                "code": "bp_contract_failed",
                "message": f"schema mismatch: expected {expected_schema!r}",
            }
        )

    present = {str(item.get("event") or "") for item in matched}
    missing_events = [name for name in required_events if name and name not in present]
    if missing_events:
        violations.append(
            {
                "code": "bp_contract_failed",
                "message": f"missing required events: {', '.join(missing_events)}",
                "missing_events": missing_events,
            }
        )

    # Ordering constraints (first occurrence).
    idx_req = _first_index(matched, "request_headers")
    idx_resp = _first_index(matched, "response_headers")
    idx_on = _first_index(matched, "backpressure_on")
    idx_off = _first_index(matched, "backpressure_off")
    idx_pause = _first_index(matched, "pause_effective")
    idx_resume = _first_index(matched, "resume_req")
    idx_finished = _first_index(matched, "finished")

    pause_event = matched[idx_pause] if 0 <= idx_pause < len(matched) else None
    resume_event = matched[idx_resume] if 0 <= idx_resume < len(matched) else None

    if idx_req >= 0 and idx_resp >= 0 and idx_resp <= idx_req:
        violations.append(
            {
                "code": "bp_contract_failed",
                "message": "response_headers appeared before request_headers",
            }
        )

    if idx_off >= 0 and (idx_on < 0 or idx_off < idx_on):
        violations.append(
            {
                "code": "bp_contract_failed",
                "message": "backpressure_off observed before backpressure_on",
            }
        )

    if idx_pause >= 0 and idx_on >= 0 and idx_pause < idx_on:
        violations.append(
            {
                "code": "bp_contract_failed",
                "message": "pause_effective observed before backpressure_on",
            }
        )

    if idx_resume >= 0 and idx_pause >= 0 and idx_resume < idx_pause:
        violations.append(
            {
                "code": "bp_contract_failed",
                "message": "resume_req observed before pause_effective",
            }
        )

    if idx_resume >= 0 and idx_off >= 0 and idx_resume < idx_off:
        violations.append(
            {
                "code": "bp_contract_failed",
                "message": "resume_req observed before backpressure_off (expected drain-before-resume)",
            }
        )

    if idx_finished >= 0:
        for later in matched[idx_finished + 1 :]:
            later_name = str(later.get("event") or "")
            if later_name and later_name != "finished":
                violations.append(
                    {
                        "code": "bp_contract_failed",
                        "message": f"event {later_name} observed after finished",
                    }
                )
                break

    # Pause window drift check (bytes_delivered_total should stay nearly quiet).
    if idx_pause >= 0 and idx_resume >= 0 and idx_resume > idx_pause:
        pause_delivered = _int_field(pause_event or {}, "bytes_delivered_total")
        resume_delivered = _int_field(resume_event or {}, "bytes_delivered_total")
        if pause_delivered is None or resume_delivered is None:
            violations.append(
                {
                    "code": "bp_contract_failed",
                    "message": "pause/resume events missing bytes_delivered_total",
                }
            )
        else:
            delta = resume_delivered - pause_delivered
            if delta < 0:
                violations.append(
                    {
                        "code": "bp_contract_failed",
                        "message": f"bytes_delivered_total regressed across pause window: {pause_delivered} -> {resume_delivered}",
                    }
                )
            if max_pause_drift_bytes and delta > max_pause_drift_bytes:
                violations.append(
                    {
                        "code": "bp_contract_failed",
                        "message": f"bytes_delivered_total drift {delta} exceeds max_pause_drift_bytes={max_pause_drift_bytes}",
                        "pause_delivered": pause_delivered,
                        "resume_delivered": resume_delivered,
                    }
                )

    # Final result expectations (status/body_len).
    finished_event = None
    if idx_finished >= 0:
        finished_event = matched[idx_finished]
        result = str(finished_event.get("result") or "")
        status = _int_field(finished_event, "status")
        body_len = _int_field(finished_event, "body_len")
        if result != "pass":
            violations.append(
                {
                    "code": "bp_contract_failed",
                    "message": f"finished.result expected 'pass', got {result!r}",
                }
            )
        if expected_status and status != expected_status:
            violations.append(
                {
                    "code": "bp_contract_failed",
                    "message": f"finished.status expected {expected_status}, got {status!r}",
                }
            )
        if expected_body_len and body_len != expected_body_len:
            violations.append(
                {
                    "code": "bp_contract_failed",
                    "message": f"finished.body_len expected {expected_body_len}, got {body_len!r}",
                }
            )

    body_complete_event = None
    idx_body_complete = _first_index(matched, "body_complete")
    if idx_body_complete >= 0:
        body_complete_event = matched[idx_body_complete]
        bytes_written = _int_field(body_complete_event, "bytes_written_total")
        if expected_body_len and bytes_written is not None and bytes_written != expected_body_len:
            violations.append(
                {
                    "code": "bp_contract_failed",
                    "message": f"body_complete.bytes_written_total expected {expected_body_len}, got {bytes_written}",
                }
            )

    if violations and "bp_contract_failed" not in policy_codes:
        policy_codes.append("bp_contract_failed")

    summary = {
        "scanned_files": len(loaded.scanned_files),
        "matched_events": len(matched),
        "parse_errors": loaded.parse_errors,
        "missing_roots": loaded.missing_roots,
        "present_events": sorted(item for item in present if item),
    }
    if matched:
        summary["seq_first"] = int(matched[0].get("seq") or 0)
        summary["seq_last"] = int(matched[-1].get("seq") or 0)

    return {
        "generated_at_utc": utc_now_iso(),
        "contract_id": contract.get("contract_id"),
        "contract_schema": contract.get("schema"),
        "evidence_schema": expected_schema,
        "case": case_name,
        "stream": stream_id,
        "parameters": {
            "expected_status": expected_status,
            "expected_body_len": expected_body_len,
            "max_pause_drift_bytes": max_pause_drift_bytes,
        },
        "summary": summary,
        "violations": violations,
        "policy_violations": policy_codes,
        "evidence": {
            "scanned_files": loaded.scanned_files,
            "events": matched,
        },
        "debug": {
            "pause_event": pause_event,
            "resume_event": resume_event,
            "body_complete_event": body_complete_event,
            "finished_event": finished_event,
        },
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Validate UCE backpressure evidence.")
    parser.add_argument("--contract", required=True, help="Path to bp@v1 contract file.")
    parser.add_argument(
        "--evidence-root",
        action="append",
        default=[],
        help="Evidence root to scan for dci_evidence_*.jsonl (repeatable).",
    )
    parser.add_argument("--report", required=True, help="Output JSON report path.")
    args = parser.parse_args(argv)

    report = validate_bp(Path(args.contract), [Path(item) for item in args.evidence_root])
    write_json(Path(args.report), report)
    return 0 if not report["policy_violations"] else 3


if __name__ == "__main__":
    raise SystemExit(main())
