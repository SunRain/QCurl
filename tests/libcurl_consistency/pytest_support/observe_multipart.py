"""Multipart semantic parsing helpers for the observable test server."""

from __future__ import annotations

from email.parser import BytesParser
from email.policy import default as email_default_policy

from tests.libcurl_consistency.pytest_support.observe_raw_headers import sha256_hex


def multipart_semantic_summary(content_type: str, body: bytes) -> dict[str, object]:
    """Parse multipart/form-data and return a stable semantic summary."""

    ct = (content_type or "").strip()
    if not ct.lower().startswith("multipart/form-data"):
        raise ValueError(f"unsupported content-type: {ct!r}")
    if not body:
        raise ValueError("empty multipart body")

    header = f"Content-Type: {ct}\r\nMIME-Version: 1.0\r\n\r\n".encode("utf-8")
    msg = BytesParser(policy=email_default_policy).parsebytes(header + body)
    if not msg.is_multipart():
        raise ValueError("not a multipart message")

    parts = []
    for part in msg.iter_parts():
        name = part.get_param("name", header="content-disposition") or ""
        filename = part.get_filename() or (part.get_param("filename", header="content-disposition") or "")
        part_ct = part.get_content_type() or ""

        payload = part.get_payload(decode=True)
        if payload is None:
            raw = part.get_payload()
            if isinstance(raw, bytes):
                payload_bytes = raw
            elif isinstance(raw, str):
                payload_bytes = raw.encode("utf-8", errors="replace")
            else:
                payload_bytes = b""
        else:
            payload_bytes = payload

        parts.append({
            "name": str(name),
            "filename": str(filename),
            "content_type": str(part_ct),
            "size": int(len(payload_bytes)),
            "sha256": sha256_hex(payload_bytes) if payload_bytes else "",
        })

    return {
        "kind": "multipart/form-data",
        "parts": parts,
    }
