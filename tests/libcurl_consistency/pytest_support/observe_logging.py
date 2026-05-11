"""Structured JSONL logging for the observable HTTP server."""

from __future__ import annotations

from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, List
from urllib.parse import parse_qs, urlencode, urlsplit, urlunsplit
import json

from tests.libcurl_consistency.pytest_support.observe_raw_headers import auth_scheme
from tests.libcurl_consistency.pytest_support.observe_raw_headers import cookie_header_summary
from tests.libcurl_consistency.pytest_support.observe_raw_headers import raw_header_fields
from tests.libcurl_consistency.pytest_support.observe_raw_headers import redact_header_value
from tests.libcurl_consistency.pytest_support.observe_raw_headers import set_cookie_summary
from tests.libcurl_consistency.pytest_support.observe_raw_headers import sha256_hex


LOG_FILE: Path | None = None

_RAW_REQUEST_HEADER_ALLOWLIST = {
    "authorization",
    "proxy-authorization",
    "cookie",
    "x-qcurl-one",
    "x-qcurl-override",
    "x-qcurl-case",
}


@dataclass(frozen=True)
class ObserveLogEntry:
    ts: str
    id: str
    peer: str
    peer_port: int
    method: str
    path: str
    status: int
    headers: Dict[str, str]
    headers_raw_lines: List[str]
    headers_raw_len: int
    headers_raw_sha256: str
    response_headers: Dict[str, str]
    body_len: int
    body_sha256: str


def set_log_file(path: Path) -> None:
    """Set the JSONL observation log path."""

    global LOG_FILE
    LOG_FILE = path


def utc_ts() -> str:
    """Return a timestamp for an observation row."""

    return datetime.now(tz=timezone.utc).isoformat()


def strip_query_id(path_or_url: str) -> str:
    """Strip the request correlation id query item from a path or URL."""

    parts = urlsplit(path_or_url)
    q = parse_qs(parts.query, keep_blank_values=True)
    q.pop("id", None)
    query = urlencode(q, doseq=True)
    return urlunsplit((parts.scheme, parts.netloc, parts.path, query, parts.fragment))


def strip_query_all(path_or_url: str) -> str:
    """Strip all query parameters from a path or URL."""

    parts = urlsplit(path_or_url)
    return urlunsplit((parts.scheme, parts.netloc, parts.path, "", parts.fragment))


def append_jsonl(path: Path, payload: Dict[str, object]) -> None:
    """Append one JSON document to a JSONL file."""

    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as fp:
        fp.write(json.dumps(payload, ensure_ascii=False) + "\n")


def write_observe_log(handler: object, status: int, response_headers: Dict[str, str], request_body: bytes = b"") -> None:
    """Write a structured observation row for a request handler."""

    if LOG_FILE is None:
        return

    path = str(getattr(handler, "path", ""))
    q = parse_qs(urlsplit(path).query, keep_blank_values=True)
    req_id = q.get("id", [""])[0]
    headers = getattr(handler, "headers")
    headers_allowlist: dict[str, str] = {}
    for name in (
        "cookie",
        "authorization",
        "proxy-authorization",
        "content-length",
        "transfer-encoding",
        "host",
        "expect",
        "accept-encoding",
        "referer",
        "range",
        "x-qcurl-one",
        "x-qcurl-override",
        "x-qcurl-case",
    ):
        values = headers.get_all(name) if hasattr(headers, "get_all") else None
        if not values:
            continue
        raw_values = [str(v) for v in values if v is not None]
        if not raw_values:
            continue
        raw = ", ".join(raw_values)
        if name in ("authorization", "proxy-authorization"):
            scheme = auth_scheme(raw_values[-1])
            if scheme:
                headers_allowlist[name] = scheme
        elif name == "cookie":
            summary = cookie_header_summary(raw)
            if summary:
                headers_allowlist[name] = summary
        elif name == "referer":
            headers_allowlist[name] = strip_query_all(raw)
        else:
            headers_allowlist[name] = raw

    raw_fields = raw_header_fields(_raw_request_header_lines(headers))
    resp_allowlist = _response_header_allowlist(response_headers)
    client_address = getattr(handler, "client_address", ("", 0))
    entry = ObserveLogEntry(
        ts=utc_ts(),
        id=req_id,
        peer=f"{client_address[0]}:{client_address[1]}",
        peer_port=int(client_address[1]),
        method=str(getattr(handler, "command", "") or "").upper(),
        path=path,
        status=int(status),
        headers=headers_allowlist,
        headers_raw_lines=raw_fields["headers_raw_lines"],  # type: ignore[arg-type]
        headers_raw_len=int(raw_fields["headers_raw_len"]),
        headers_raw_sha256=str(raw_fields["headers_raw_sha256"]),
        response_headers=resp_allowlist,
        body_len=len(request_body or b""),
        body_sha256=sha256_hex(request_body or b"") if request_body else "",
    )
    append_jsonl(LOG_FILE, asdict(entry))


def _raw_request_header_lines(headers: object) -> list[str]:
    raw_lines: list[str] = []
    raw_header_items = getattr(headers, "_headers", None)
    if not isinstance(raw_header_items, list):
        return raw_lines

    for item in raw_header_items:
        if not isinstance(item, tuple) or len(item) != 2:
            continue
        name = str(item[0]).strip()
        value = str(item[1])
        if not name:
            continue
        if name.lower().strip() not in _RAW_REQUEST_HEADER_ALLOWLIST:
            continue
        raw_lines.append(f"{name}: {redact_header_value(name, value)}")
    return raw_lines


def _response_header_allowlist(response_headers: Dict[str, str]) -> dict[str, str]:
    resp_allowlist: dict[str, str] = {}
    for key, value in response_headers.items():
        lower_key = str(key).lower().strip()
        if lower_key not in {
            "location",
            "set-cookie",
            "www-authenticate",
            "content-encoding",
            "content-length",
            "content-range",
        }:
            continue
        if lower_key == "location":
            resp_allowlist[lower_key] = strip_query_id(str(value))
        elif lower_key == "set-cookie":
            summary = set_cookie_summary(str(value))
            if summary:
                resp_allowlist[lower_key] = summary
        else:
            resp_allowlist[lower_key] = str(value)
    return resp_allowlist
