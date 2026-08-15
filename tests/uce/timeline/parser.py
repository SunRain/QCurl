"""Strict, fail-closed parsing for UCE timeline evidence."""

from __future__ import annotations

import json
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Any


TIMELINE_PARSE_POLICY = "timeline_evidence_parse_error"


@dataclass(frozen=True)
class JsonlParseResult:
    """Value result for strict JSONL parsing; errors are never dropped."""

    values: list[dict[str, Any]]
    line_numbers: list[int]
    errors: list[dict[str, Any]]


@dataclass(frozen=True)
class TimelineLoadResult:
    events: list[dict[str, Any]]
    errors: list[dict[str, Any]]


def _relative_path(path: Path, relative_to: Path | None) -> str:
    candidate = path
    if relative_to is not None:
        try:
            candidate = path.relative_to(relative_to)
        except ValueError:
            candidate = Path(path.name)
    return candidate.as_posix()


def timeline_parse_error(
    path: Path,
    *,
    code: str,
    summary: str,
    relative_to: Path | None = None,
    line: int = 1,
) -> dict[str, Any]:
    """Build a redacted structured timeline evidence error."""

    return {
        "code": TIMELINE_PARSE_POLICY,
        "path": _relative_path(path, relative_to),
        "line": line,
        "error_code": code,
        "summary": summary,
    }


def json_document_parse_error(
    path: Path,
    error: UnicodeDecodeError | json.JSONDecodeError | OSError,
    *,
    relative_to: Path,
) -> dict[str, Any]:
    """Map a JSON document exception to a redacted timeline parse error."""

    if isinstance(error, UnicodeDecodeError):
        line = error.object[: error.start].count(b"\n") + 1
        return timeline_parse_error(
            path,
            code="invalid_utf8",
            summary="artifact is not valid UTF-8",
            relative_to=relative_to,
            line=line,
        )
    if isinstance(error, json.JSONDecodeError):
        return timeline_parse_error(
            path,
            code="invalid_json",
            summary="artifact is not valid JSON",
            relative_to=relative_to,
            line=error.lineno,
        )
    return timeline_parse_error(
        path,
        code="read_error",
        summary="artifact could not be read",
        relative_to=relative_to,
        line=0,
    )


def _parse_json_line(
    path: Path,
    raw: bytes,
    line_number: int,
    relative_to: Path | None,
) -> tuple[dict[str, Any] | None, dict[str, Any] | None]:
    try:
        text = raw.decode("utf-8", errors="strict")
    except UnicodeDecodeError:
        return None, timeline_parse_error(
            path,
            code="invalid_utf8",
            summary="line is not valid UTF-8",
            relative_to=relative_to,
            line=line_number,
        )
    try:
        value = json.loads(text)
    except json.JSONDecodeError:
        return None, timeline_parse_error(
            path,
            code="invalid_json",
            summary="line is not valid JSON",
            relative_to=relative_to,
            line=line_number,
        )
    if not isinstance(value, dict):
        kind = "array" if isinstance(value, list) else "scalar"
        return None, timeline_parse_error(
            path,
            code="json_not_object",
            summary=f"JSON {kind} value is not an object",
            relative_to=relative_to,
            line=line_number,
        )
    return value, None


def iter_jsonl(path: Path, *, relative_to: Path | None = None) -> JsonlParseResult:
    """Strictly parse non-empty JSONL lines into object values and errors."""

    rows: list[dict[str, Any]] = []
    line_numbers: list[int] = []
    errors: list[dict[str, Any]] = []
    if not path.exists():
        return JsonlParseResult(rows, line_numbers, errors)
    try:
        raw_lines = path.read_bytes().split(b"\n")
    except OSError:
        errors.append(
            timeline_parse_error(
                path,
                code="read_error",
                summary="evidence file could not be read",
                relative_to=relative_to,
                line=0,
            )
        )
        return JsonlParseResult(rows, line_numbers, errors)
    for line_number, raw in enumerate(raw_lines, start=1):
        if not raw.strip():
            continue
        value, error = _parse_json_line(path, raw, line_number, relative_to)
        if error is not None:
            errors.append(error)
            continue
        assert value is not None
        rows.append(value)
        line_numbers.append(line_number)
    return JsonlParseResult(rows, line_numbers, errors)


def load_timeline_events(timeline_paths: list[Path]) -> TimelineLoadResult:
    """Load all timeline files while preserving every parse error."""

    events: list[dict[str, Any]] = []
    errors: list[dict[str, Any]] = []
    normalized_paths = [path.resolve() for path in timeline_paths]
    existing = [path for path in normalized_paths if path.exists()]
    common_root = Path(os.path.commonpath([str(path.parent) for path in existing])) if existing else None
    for path in normalized_paths:
        if not path.exists():
            continue
        parsed = iter_jsonl(path, relative_to=common_root)
        errors.extend(parsed.errors)
        for line_number, event in zip(parsed.line_numbers, parsed.values):
            event["_timeline_path"] = path.relative_to(common_root).as_posix() if common_root else path.name
            event["_timeline_line"] = line_number
            events.append(event)
    return TimelineLoadResult(events, errors)


def _event_error(event: dict[str, Any], code: str, summary: str) -> dict[str, Any]:
    return timeline_parse_error(
        Path(str(event.get("_timeline_path") or "timeline.jsonl")),
        line=int(event.get("_timeline_line") or 0),
        code=code,
        summary=summary,
    )


def validate_event_contract(
    events: list[dict[str, Any]],
    contract: dict[str, Any],
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    """Return events satisfying the versioned schema/provider/identity contract."""

    expected_schema = str(contract.get("timeline_event_schema") or "")
    allowed_providers = {item for item in contract.get("providers", []) if isinstance(item, str) and item}
    identity_fields = tuple(str(field) for field in contract.get("identity_fields", []))
    known_events = set(contract.get("events", {}))
    identities: set[tuple[Any, ...]] = set()
    valid_events: list[dict[str, Any]] = []
    errors: list[dict[str, Any]] = []

    for event in events:
        event_valid = True
        if event.get("schema") != expected_schema:
            errors.append(_event_error(event, "unknown_schema", "event schema is missing or unsupported"))
            event_valid = False
        provider = event.get("provider")
        if not isinstance(provider, str) or not provider.strip():
            errors.append(_event_error(event, "missing_provider", "event provider is missing"))
            event_valid = False
        elif provider not in allowed_providers:
            errors.append(_event_error(event, "unknown_provider", "event provider is not allowed by the contract"))
            event_valid = False
        if event.get("event") not in known_events:
            errors.append(_event_error(event, "unknown_event", "event name is missing or unsupported"))
            event_valid = False

        identity: list[Any] = []
        identity_valid = bool(identity_fields)
        for field in identity_fields:
            value = event.get(field)
            if field == "seq":
                field_valid = isinstance(value, int) and not isinstance(value, bool) and value > 0
            else:
                field_valid = isinstance(value, str) and bool(value.strip())
            if not field_valid:
                errors.append(_event_error(event, "missing_identity", f"identity field {field!r} is invalid"))
                identity_valid = False
                event_valid = False
            identity.append(value)
        identity_key = tuple(identity)
        if identity_valid and identity_key in identities:
            errors.append(_event_error(event, "duplicate_identity", "event identity duplicates an earlier line"))
            event_valid = False
        elif identity_valid:
            identities.add(identity_key)
        if event_valid:
            valid_events.append(event)
    return valid_events, errors


def merge_timeline_parse_errors(report: dict[str, Any], errors: list[dict[str, Any]]) -> None:
    """Merge collector parse errors into a validator report."""

    if not errors:
        return
    report.setdefault("violations", []).extend(errors)
    policy_codes = report.setdefault("policy_violations", [])
    if TIMELINE_PARSE_POLICY not in policy_codes:
        policy_codes.insert(0, TIMELINE_PARSE_POLICY)
    if "timeline_contract_failed" not in policy_codes:
        policy_codes.append("timeline_contract_failed")
    summary = report.setdefault("summary", {})
    summary["parse_errors"] = int(summary.get("parse_errors") or 0) + len(errors)
