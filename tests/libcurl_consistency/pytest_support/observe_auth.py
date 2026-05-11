"""HTTP authentication helpers for the observable test server."""

from __future__ import annotations

from urllib.parse import urlsplit
import base64
import binascii
import hashlib
import re


AUTH_USER = "user"
AUTH_PASS = "passwd"
AUTH_REALM = "qcurl_lc"
AUTH_DIGEST_NONCE = "1053604144"


def base64_decode_padded(token: str) -> bytes:
    """Decode a Basic auth token while accepting omitted padding."""

    raw = (token or "").strip()
    if not raw:
        return b""
    pad = (-len(raw)) % 4
    raw_padded = raw + ("=" * pad)
    try:
        return base64.b64decode(raw_padded.encode("ascii"), validate=True)
    except (binascii.Error, UnicodeError):
        return b""


def basic_auth_valid(header_value: str, *, expected_user: str, expected_pass: str) -> bool:
    """Return whether an Authorization: Basic header matches expected credentials."""

    v = (header_value or "").strip()
    if not v:
        return False
    parts = v.split(None, 1)
    if len(parts) != 2 or parts[0].lower() != "basic":
        return False
    decoded = base64_decode_padded(parts[1])
    if not decoded:
        return False
    try:
        userpass = decoded.decode("iso-8859-1", errors="strict")
    except UnicodeError:
        return False
    if ":" not in userpass:
        return False
    user, passwd = userpass.split(":", 1)
    return (user == expected_user) and (passwd == expected_pass)


def parse_digest_params(value: str) -> dict[str, str]:
    """Parse Digest Authorization parameters using lowercase keys."""

    s = (value or "").strip()
    out: dict[str, str] = {}
    i = 0
    n = len(s)
    while i < n:
        while i < n and s[i] in " \t,":
            i += 1
        if i >= n:
            break

        k0 = i
        while i < n and s[i] not in "=,":
            i += 1
        key = s[k0:i].strip().lower()
        if not key:
            while i < n and s[i] != ",":
                i += 1
            continue
        if i >= n or s[i] != "=":
            while i < n and s[i] != ",":
                i += 1
            continue
        i += 1

        if i < n and s[i] == "\"":
            i += 1
            buf: list[str] = []
            while i < n:
                ch = s[i]
                if ch == "\\" and i + 1 < n:
                    buf.append(s[i + 1])
                    i += 2
                    continue
                if ch == "\"":
                    i += 1
                    break
                buf.append(ch)
                i += 1
            val = "".join(buf)
        else:
            v0 = i
            while i < n and s[i] != ",":
                i += 1
            val = s[v0:i].strip()

        out[key] = val
    return out


def hash_hex_for_algo(data: bytes, algo: str) -> str:
    """Return a hex digest for digest-auth supported algorithms."""

    base = (algo or "").strip().upper().replace("-", "")
    if base == "MD5":
        return hashlib.md5(data).hexdigest()
    if base == "SHA256":
        return hashlib.sha256(data).hexdigest()
    raise ValueError(f"unsupported digest algorithm: {algo!r}")


_NC_RE = re.compile(r"^[0-9a-fA-F]{8}$")


def digest_auth_valid(
    header_value: str,
    *,
    method: str,
    request_uri: str,
    body: bytes,
    expected_user: str,
    expected_pass: str,
    expected_realm: str,
    expected_nonce: str,
) -> bool:
    """Return whether an Authorization: Digest header matches expected credentials."""

    v = (header_value or "").strip()
    if not v or not v.lower().startswith("digest "):
        return False
    params = parse_digest_params(v[len("Digest "):])

    username = params.get("username", "")
    realm = params.get("realm", "")
    nonce = params.get("nonce", "")
    uri = params.get("uri", "")
    response = params.get("response", "")
    if not username or not realm or not nonce or not uri or not response:
        return False
    if username != expected_user or realm != expected_realm or nonce != expected_nonce:
        return False
    if uri != request_uri:
        if uri.startswith("http://") or uri.startswith("https://"):
            parsed = urlsplit(uri)
            path_query = parsed.path
            if parsed.query:
                path_query = f"{path_query}?{parsed.query}"
            if path_query != request_uri:
                return False
        else:
            return False

    algo_raw = params.get("algorithm", "MD5").strip()
    algo_upper = algo_raw.upper()
    sess = algo_upper.endswith("-SESS")
    base_algo = algo_upper[:-5] if sess else algo_upper
    base_algo_norm = base_algo.replace("-", "")
    if base_algo_norm not in ("MD5", "SHA256"):
        return False

    qop = (params.get("qop") or "").strip()
    qop_l = qop.lower()
    nc = (params.get("nc") or "").strip()
    cnonce = (params.get("cnonce") or "").strip()

    def h(text: str) -> str:
        return hash_hex_for_algo(text.encode("utf-8"), base_algo_norm)

    ha1 = h(f"{expected_user}:{expected_realm}:{expected_pass}")
    if sess:
        if not cnonce:
            return False
        ha1 = h(f"{ha1}:{expected_nonce}:{cnonce}")

    if qop_l == "auth-int":
        body_h = hash_hex_for_algo(body or b"", base_algo_norm)
        ha2 = h(f"{method}:{uri}:{body_h}")
    else:
        ha2 = h(f"{method}:{uri}")

    if qop_l:
        if not nc or not cnonce or not qop:
            return False
        if not _NC_RE.match(nc):
            return False
        if qop_l not in ("auth", "auth-int"):
            return False
        expected = h(f"{ha1}:{expected_nonce}:{nc}:{cnonce}:{qop}:{ha2}")
    else:
        expected = h(f"{ha1}:{expected_nonce}:{ha2}")

    return expected.lower() == response.strip().lower()
