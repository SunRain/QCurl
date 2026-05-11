"""Raw header and cookie redaction helpers for observe HTTP server."""

from __future__ import annotations

from typing import Dict, List
import hashlib


def sha256_hex(data: bytes) -> str:
    """Return a hex SHA-256 digest."""

    return hashlib.sha256(data).hexdigest()


def auth_scheme(value: str) -> str:
    """Return an Authorization scheme without persisting credentials."""

    raw = (value or "").strip()
    if not raw:
        return ""
    scheme = raw.split()[0].strip()
    if not scheme:
        return ""
    normalized = scheme.lower()
    if normalized == "basic":
        return "Basic"
    if normalized == "digest":
        return "Digest"
    if normalized == "bearer":
        return "Bearer"
    if normalized == "negotiate":
        return "Negotiate"
    if normalized == "ntlm":
        return "NTLM"
    return scheme


def cookie_header_summary(value: str) -> str:
    """Return a comparable Cookie header summary without storing values."""

    raw = (value or "").strip()
    if not raw:
        return ""

    parts = [part.strip() for part in raw.split(";")]
    parts = [part for part in parts if part]

    items: list[tuple[str, str]] = []
    names: list[str] = []
    for part in parts:
        if "=" in part:
            name, val = part.split("=", 1)
            name = name.strip()
            val = val.strip()
        else:
            name = part.strip()
            val = ""
        if not name:
            continue
        items.append((name, val))
        names.append(name)

    names_sorted = sorted(set(names))
    items_sorted = sorted(items, key=lambda item: (item[0], item[1]))
    canonical = "; ".join(f"{name}={value}" if value != "" else name for name, value in items_sorted)
    digest = sha256_hex(canonical.encode("utf-8")) if canonical else ""
    return f"names:{','.join(names_sorted)} sha256:{digest}"


def set_cookie_summary(value: str) -> str:
    """Return a comparable Set-Cookie summary without storing the cookie value."""

    raw = (value or "").strip()
    if not raw:
        return ""

    parts = [part.strip() for part in raw.split(";")]
    parts = [part for part in parts if part]
    if not parts:
        return ""

    first = parts[0]
    name = ""
    val = ""
    if "=" in first:
        name, val = first.split("=", 1)
        name = name.strip()
        val = val.strip()
    else:
        name = first.strip()

    attrs: list[str] = []
    for part in parts[1:]:
        if "=" in part:
            key, attr_value = part.split("=", 1)
            attrs.append(f"{key.strip().lower()}:{attr_value.strip()}")
        else:
            attrs.append(part.strip().lower())

    val_digest = sha256_hex(val.encode("utf-8")) if val else ""
    return f"name:{name} attrs:{','.join(sorted(attrs))} value_sha256:{val_digest}"


def redact_header_value(name: str, value: str) -> str:
    """Redact sensitive header values while preserving comparable summaries."""

    lower_name = name.lower().strip()
    if lower_name in ("authorization", "proxy-authorization"):
        scheme = auth_scheme(value)
        return f"{scheme} <redacted>" if scheme else "<redacted>"
    if lower_name == "cookie":
        summary = cookie_header_summary(value)
        return summary if summary else "<redacted>"
    if lower_name == "set-cookie":
        summary = set_cookie_summary(value)
        return summary if summary else "<redacted>"
    return value


def raw_header_fields(lines: List[str]) -> Dict[str, object]:
    """Return raw header lines plus length and digest."""

    blob = "\n".join(lines).encode("utf-8")
    return {
        "headers_raw_lines": lines,
        "headers_raw_len": int(len(blob)),
        "headers_raw_sha256": sha256_hex(blob),
    }
