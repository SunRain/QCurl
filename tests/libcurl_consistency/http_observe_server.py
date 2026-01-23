#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
最小 HTTP/1.1 观测服务端（用于 libcurl_consistency 的 P2 用例）：
- /cookie：返回 200，并记录请求中的 Cookie 头
- /status/<code>：返回指定状态码（404/401/503...），并记录请求头
- /empty_200：返回 200 + Content-Length: 0（空响应体）
- /no_content：返回 204 No Content（空响应体）
- /delay_headers/<ms>：延迟指定毫秒后才发送响应头（用于“未收到响应头即超时”的一致性）
- /stall_body/<total>/<stall_ms>：先发送响应头（Content-Length=total），再延迟 stall_ms 后发送 body（用于 low-speed）
- /slow_body/<total>/<chunk>/<sleep_ms>：分块发送响应体，并在块间 sleep_ms（用于 cancel/慢速传输）
- /redir/<n>：多跳 302 重定向（用于 FOLLOWLOCATION 一致性）
- /login：返回 302 + Set-Cookie，并跳转到 /home（用于“登录态 cookie 链路”一致性）
- /login_path：返回 302 + Set-Cookie(Path=/a) 并跳转到 /a/step（用于“Cookie Path 匹配”一致性）
- /a/step：要求携带 Cookie（Path=/a 应匹配），并 302 跳转到 /b/final
- /b/final：要求不携带 Cookie（Path=/a 不匹配），最终 200
- /home：需要 Cookie: sid=lc123，否则 401
- 观测输出：JSONL（method/path/status/关键请求头）
"""

from __future__ import annotations

import argparse
import base64
import binascii
import gzip
import hashlib
import json
import logging
import re
import socket
import ssl
import time
import zlib
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from email.parser import BytesParser
from email.policy import default as email_default_policy
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Dict, Optional
from urllib.parse import parse_qs, urlsplit, urlencode, urlunsplit

try:
    import brotli  # type: ignore
except Exception:  # pragma: no cover - optional dependency
    brotli = None


log = logging.getLogger(__name__)

LOG_FILE: Optional[Path] = None
TLS_CERT: Optional[Path] = None
TLS_KEY: Optional[Path] = None

_COOKIE_SID = "lc123"


def _utc_ts() -> str:
    return datetime.now(tz=timezone.utc).isoformat()

def _strip_query_id(path_or_url: str) -> str:
    parts = urlsplit(path_or_url)
    q = parse_qs(parts.query, keep_blank_values=True)
    q.pop("id", None)
    query = urlencode(q, doseq=True)
    # path_or_url 可能是相对路径或绝对 URL；两者都能用 urlunsplit 复原
    return urlunsplit((parts.scheme, parts.netloc, parts.path, query, parts.fragment))

def _strip_query_all(path_or_url: str) -> str:
    parts = urlsplit(path_or_url)
    return urlunsplit((parts.scheme, parts.netloc, parts.path, "", parts.fragment))

def _sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

def _auth_scheme(value: str) -> str:
    """
    返回 Authorization 头的 scheme（不落盘任何凭据/参数）。

    示例：
    - "Basic dXNlcjpwYXNz" -> "Basic"
    - "Digest username=..., response=..." -> "Digest"
    """
    v = (value or "").strip()
    if not v:
        return ""
    scheme = v.split()[0].strip()
    if not scheme:
        return ""
    low = scheme.lower()
    if low == "basic":
        return "Basic"
    if low == "digest":
        return "Digest"
    if low == "bearer":
        return "Bearer"
    if low == "negotiate":
        return "Negotiate"
    if low == "ntlm":
        return "NTLM"
    return scheme

# ============================================================================
# HTTPAuth 校验（可证明级别，不落盘敏感信息）
# ============================================================================

_AUTH_USER = "user"
_AUTH_PASS = "passwd"
_AUTH_REALM = "qcurl_lc"
_AUTH_DIGEST_NONCE = "1053604144"


def _base64_decode_padded(token: str) -> bytes:
    """
    Base64 解码（允许缺省 padding）。
    - 仅用于 Basic auth 解析；不输出/不记录明文。
    """
    raw = (token or "").strip()
    if not raw:
        return b""
    pad = (-len(raw)) % 4
    raw_padded = raw + ("=" * pad)
    try:
        return base64.b64decode(raw_padded.encode("ascii"), validate=True)
    except (binascii.Error, UnicodeError):
        return b""


def _basic_auth_valid(header_value: str, *, expected_user: str, expected_pass: str) -> bool:
    """
    校验 Authorization: Basic ... 的 user/pass 是否正确。
    - 仅比较解码结果；不落盘明文、不写日志。
    """
    v = (header_value or "").strip()
    if not v:
        return False
    parts = v.split(None, 1)
    if len(parts) != 2 or parts[0].lower() != "basic":
        return False
    decoded = _base64_decode_padded(parts[1])
    if not decoded:
        return False
    # RFC 7617：默认 ISO-8859-1；本测试使用 ASCII 子集（user/passwd）
    try:
        userpass = decoded.decode("iso-8859-1", errors="strict")
    except UnicodeError:
        return False
    if ":" not in userpass:
        return False
    user, passwd = userpass.split(":", 1)
    return (user == expected_user) and (passwd == expected_pass)


def _parse_digest_params(value: str) -> Dict[str, str]:
    """
    解析 Digest Authorization 参数列表（RFC 7616 风格）。
    - 支持 quoted-string 与基础转义（\\\" 与 \\\\）。
    - 返回 key 小写化后的映射。
    """
    s = (value or "").strip()
    out: Dict[str, str] = {}
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


def _hash_hex_for_algo(data: bytes, algo: str) -> str:
    base = (algo or "").strip().upper()
    base = base.replace("-", "")
    if base == "MD5":
        return hashlib.md5(data).hexdigest()
    if base == "SHA256":
        return hashlib.sha256(data).hexdigest()
    raise ValueError(f"unsupported digest algorithm: {algo!r}")


_NC_RE = re.compile(r"^[0-9a-fA-F]{8}$")


def _digest_auth_valid(header_value: str,
                      *,
                      method: str,
                      request_uri: str,
                      body: bytes,
                      expected_user: str,
                      expected_pass: str,
                      expected_realm: str,
                      expected_nonce: str) -> bool:
    """
    校验 Authorization: Digest ... 的 response 是否正确。
    - 支持 MD5/MD5-sess（以及 SHA-256/SHA-256-sess 的容错支持）
    - 支持 qop=auth 与 qop=auth-int（auth-int 需要 body hash）
    - 不落盘、不写日志（仅用于决定 200/401）
    """
    v = (header_value or "").strip()
    if not v:
        return False
    if not v.lower().startswith("digest "):
        return False
    params = _parse_digest_params(v[len("Digest "):])

    username = params.get("username", "")
    realm = params.get("realm", "")
    nonce = params.get("nonce", "")
    uri = params.get("uri", "")
    response = params.get("response", "")
    if not username or not realm or not nonce or not uri or not response:
        return False

    if username != expected_user:
        return False
    if realm != expected_realm:
        return False
    if nonce != expected_nonce:
        return False

    # uri 必须与实际请求一致（允许 absolute-form，但必须落到同一路径+query）
    if uri != request_uri:
        if uri.startswith("http://") or uri.startswith("https://"):
            u = urlsplit(uri)
            path_query = u.path
            if u.query:
                path_query = f"{path_query}?{u.query}"
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

    def H(text: str) -> str:
        return _hash_hex_for_algo(text.encode("utf-8"), base_algo_norm)

    # HA1
    ha1 = H(f"{expected_user}:{expected_realm}:{expected_pass}")
    if sess:
        if not cnonce:
            return False
        ha1 = H(f"{ha1}:{expected_nonce}:{cnonce}")

    # HA2
    if qop_l == "auth-int":
        body_h = _hash_hex_for_algo(body or b"", base_algo_norm)
        ha2 = H(f"{method}:{uri}:{body_h}")
    else:
        ha2 = H(f"{method}:{uri}")

    # response
    if qop_l:
        # RFC 7616：qop present => require nc+cnonce
        if not nc or not cnonce or not qop:
            return False
        if not _NC_RE.match(nc):
            return False
        if qop_l not in ("auth", "auth-int"):
            return False
        expected = H(f"{ha1}:{expected_nonce}:{nc}:{cnonce}:{qop}:{ha2}")
    else:
        # RFC 2069 兼容：no qop
        expected = H(f"{ha1}:{expected_nonce}:{ha2}")

    return expected.lower() == response.strip().lower()

def _cookie_header_summary(value: str) -> str:
    """
    生成 Cookie 头的“可比较摘要”（不落盘 value 明文）。
    - names: cookie 名集合（排序，去重）
    - sha256: 对 `name=value` 规范化串的 sha256（用于对齐 value 变化，但不暴露明文）
    """
    raw = (value or "").strip()
    if not raw:
        return ""

    parts = [p.strip() for p in raw.split(";")]
    parts = [p for p in parts if p]

    items: list[tuple[str, str]] = []
    names: list[str] = []
    for p in parts:
        if "=" in p:
            name, val = p.split("=", 1)
            name = name.strip()
            val = val.strip()
        else:
            name = p.strip()
            val = ""
        if not name:
            continue
        items.append((name, val))
        names.append(name)

    names_sorted = sorted(set(names))
    items_sorted = sorted(items, key=lambda x: (x[0], x[1]))
    canonical = "; ".join(f"{n}={v}" if v != "" else n for n, v in items_sorted)
    digest = _sha256_hex(canonical.encode("utf-8")) if canonical else ""

    # 约束：输出中禁止出现 '='，避免误把摘要当作明文 cookie。
    return f"names:{','.join(names_sorted)} sha256:{digest}"

def _set_cookie_summary(value: str) -> str:
    """
    生成 Set-Cookie 的“可比较摘要”（不落盘 value 明文）。
    - name: cookie 名
    - attrs: 属性集合（path/domain/secure/httponly/samesite...；排序；值使用 ':' 分隔）
    - value_sha256: cookie value 的 sha256（可选诊断）
    """
    raw = (value or "").strip()
    if not raw:
        return ""

    parts = [p.strip() for p in raw.split(";")]
    parts = [p for p in parts if p]
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
    for p in parts[1:]:
        if "=" in p:
            k, v = p.split("=", 1)
            k = k.strip().lower()
            v = v.strip()
            attrs.append(f"{k}:{v}")
        else:
            attrs.append(p.strip().lower())
    attrs_sorted = sorted(attrs)
    val_digest = _sha256_hex(val.encode("utf-8")) if val else ""

    # 约束：输出中禁止出现 '='，避免误把摘要当作明文 cookie。
    return f"name:{name} attrs:{','.join(attrs_sorted)} value_sha256:{val_digest}"

def _multipart_semantic_summary(content_type: str, body: bytes) -> Dict[str, object]:
    """
    解析 multipart/form-data，并返回“可比的语义摘要”（不包含 boundary）。

    说明：
    - 不比较原始请求体字节（boundary 可能不同）；改为比较服务端可解析出的 parts 语义。
    - 返回字段稳定：按收到顺序输出 parts，不包含随机/不可比字段。
    """
    ct = (content_type or "").strip()
    if not ct.lower().startswith("multipart/form-data"):
        raise ValueError(f"unsupported content-type: {ct!r}")
    if not body:
        raise ValueError("empty multipart body")

    # email 解析器需要完整头部；multipart/form-data 与 MIME multipart 的结构一致
    header = f"Content-Type: {ct}\r\nMIME-Version: 1.0\r\n\r\n".encode("utf-8")
    msg = BytesParser(policy=email_default_policy).parsebytes(header + body)
    if not msg.is_multipart():
        raise ValueError("not a multipart message")

    parts = []
    for p in msg.iter_parts():
        name = p.get_param("name", header="content-disposition") or ""
        filename = p.get_filename() or (p.get_param("filename", header="content-disposition") or "")
        part_ct = p.get_content_type() or ""

        payload = p.get_payload(decode=True)
        if payload is None:
            raw = p.get_payload()
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
            "sha256": _sha256_hex(payload_bytes) if payload_bytes else "",
        })

    return {
        "kind": "multipart/form-data",
        "parts": parts,
    }


def _append_jsonl(path: Path, payload: Dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as fp:
        fp.write(json.dumps(payload, ensure_ascii=False) + "\n")


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
    response_headers: Dict[str, str]


class Handler(BaseHTTPRequestHandler):
    server_version = "qcurl-lc-observe/0.1"
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt: str, *args) -> None:
        # 禁止默认 stdout 访问日志（用 JSONL 代替）
        return

    def _drain_request_body_best_effort(self) -> int:
        """
        best-effort drain 当前请求剩余 body（避免 keep-alive 下“未消费的 body 字节污染下一次请求解析”）。

        ⚠️ 约束：
        - 不能阻塞：对于 Expect: 100-continue 场景，客户端可能根本不会发送 body（等待服务端响应），
          若服务端在这里阻塞读取将导致死锁。
        - 仅用于稳定性增强：只尝试在短窗口内读取“已到达/已缓冲”的字节；不保证一定读满 Content-Length。
        """
        try:
            length = int(self.headers.get("content-length") or "0")
        except ValueError:
            return 0
        if length <= 0:
            return 0

        sock = getattr(self, "connection", None)
        if sock is None:
            return 0

        drained = 0
        original_timeout = sock.gettimeout()
        try:
            # 置为非阻塞读取：无数据时立即抛出 BlockingIOError/timeout
            sock.settimeout(0.0)
            # 本地回环下 1MB body 通常会在极短时间内到达；这里用“短窗口 + 有进展才继续”的策略：
            # - 若持续一段时间没有任何新字节到达，则认为客户端并未发送 body（或已停止发送），提前结束。
            # - 若持续有字节到达，则尽量读满 Content-Length，降低残留污染概率。
            idle_deadline_s = 0.2
            hard_deadline_s = 1.5
            started = time.monotonic()
            last_progress = started
            while drained < length and (time.monotonic() - started) < hard_deadline_s:
                remaining = min(64 * 1024, length - drained)
                try:
                    # read1：优先消费 rfile 缓冲区（避免绕过 buffer 导致残留），并最多读 remaining。
                    chunk = self.rfile.read1(remaining)
                except (BlockingIOError, InterruptedError, TimeoutError, socket.timeout):
                    if (time.monotonic() - last_progress) >= idle_deadline_s:
                        break
                    time.sleep(0.005)
                    continue
                except OSError:
                    break
                if not chunk:
                    break
                drained += len(chunk)
                last_progress = time.monotonic()
        finally:
            try:
                sock.settimeout(original_timeout)
            except Exception:
                pass
        return drained

    def _read_chunked_body(self) -> bytes:
        """
        读取 HTTP/1.1 chunked 请求体（Transfer-Encoding: chunked）。

        说明：
        - 最小实现：仅用于测试观测/回显；不做复杂流控
        - 若解析失败，抛异常让连接失败（避免静默吞错导致用例假绿）
        """
        out = bytearray()
        max_total = 64 * 1024 * 1024  # 64MiB：测试上限，避免异常请求导致 OOM

        while True:
            line = self.rfile.readline(65536)
            if not line:
                raise ValueError("chunked body: unexpected EOF while reading chunk size")

            raw = line.strip()
            if not raw:
                continue
            size_hex = raw.split(b";", 1)[0].strip()
            try:
                n = int(size_hex, 16)
            except ValueError as exc:
                raise ValueError(f"chunked body: invalid chunk size line: {raw!r}") from exc

            if n == 0:
                # trailers (optional) until CRLF
                while True:
                    trailer = self.rfile.readline(65536)
                    if not trailer:
                        break
                    if trailer in (b"\r\n", b"\n"):
                        break
                break

            chunk = self.rfile.read(n)
            if len(chunk) != n:
                raise ValueError("chunked body: unexpected EOF while reading chunk data")

            crlf = self.rfile.read(2)
            if crlf != b"\r\n":
                raise ValueError("chunked body: missing CRLF after chunk data")

            out.extend(chunk)
            if len(out) > max_total:
                raise ValueError("chunked body: body too large")

        return bytes(out)

    def _read_body(self) -> bytes:
        te = str(self.headers.get("transfer-encoding") or "")
        if "chunked" in te.lower():
            return self._read_chunked_body()

        try:
            length = int(self.headers.get("content-length") or "0")
        except ValueError:
            length = 0
        if length <= 0:
            return b""
        return self.rfile.read(length)

    def _write_log(self, status: int, response_headers: Dict[str, str]) -> None:
        if LOG_FILE is None:
            return
        q = parse_qs(urlsplit(self.path).query, keep_blank_values=True)
        req_id = q.get("id", [""])[0]
        headers_allowlist = {}
        for name in ("cookie", "authorization", "content-length", "transfer-encoding", "host", "expect", "accept-encoding", "referer"):
            v = self.headers.get(name)
            if v is not None:
                raw = str(v)
                if name == "authorization":
                    scheme = _auth_scheme(raw)
                    if scheme:
                        headers_allowlist[name] = scheme
                elif name == "cookie":
                    summary = _cookie_header_summary(raw)
                    if summary:
                        headers_allowlist[name] = summary
                elif name == "referer":
                    headers_allowlist[name] = _strip_query_all(raw)
                else:
                    headers_allowlist[name] = raw

        resp_allowlist: Dict[str, str] = {}
        for k, v in response_headers.items():
            lk = str(k).lower().strip()
            if lk in ("location", "set-cookie", "www-authenticate", "content-encoding", "content-length"):
                # Location 中会携带 id（用于关联日志），对比时需要去掉
                if lk == "location":
                    resp_allowlist[lk] = _strip_query_id(str(v))
                elif lk == "set-cookie":
                    summary = _set_cookie_summary(str(v))
                    if summary:
                        resp_allowlist[lk] = summary
                else:
                    resp_allowlist[lk] = str(v)
        entry = ObserveLogEntry(
            ts=_utc_ts(),
            id=req_id,
            peer=f"{self.client_address[0]}:{self.client_address[1]}",
            peer_port=int(self.client_address[1]),
            method=str(self.command or "").upper(),
            path=str(self.path),
            status=int(status),
            headers=headers_allowlist,
            response_headers=resp_allowlist,
        )
        _append_jsonl(LOG_FILE, asdict(entry))

    def _handle_expect_417(self) -> bool:
        """
        Expect: 100-continue（417→重试）语义探针（参考 curl/tests/data/test357）。

        语义：
        - 当请求头包含 `Expect: 100-continue` 时：立即返回 417（保持连接可复用以触发 libcurl 的 417→重试路径）
          - 稳定性增强：best-effort drain 可能已发送/已缓冲的 request body（避免 keep-alive 下污染后续请求解析）
        - 否则：读取请求体并 200 回显
        """
        if not self.path.startswith("/expect_417"):
            return False

        expect = str(self.headers.get("expect") or "")
        if "100-continue" in expect.lower():
            self.send_response(417)
            self.send_header("Content-Length", "0")
            self.end_headers()
            self._write_log(417, {})
            self._drain_request_body_best_effort()
            return True

        body = self._read_body()
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if body:
            self.wfile.write(body)
        self._write_log(200, {})
        return True

    def do_HEAD(self) -> None:
        if self.path.startswith("/head_with_body"):
            # 语义合同：服务端“违规”在 HEAD 中写 body；客户端必须忽略（可观测 body_len=0）。
            # 为避免连接复用时残留字节污染后续请求，这里显式关闭连接。
            body = b"head-body-should-be-ignored\n"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Connection", "close")
            self.end_headers()
            try:
                self.wfile.write(body)
                self.wfile.flush()
            except BrokenPipeError:
                pass
            self._write_log(200, {})
            self.close_connection = True
            return

        if self.path.startswith("/head"):
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", "1234")
            self.end_headers()
            self._write_log(200, {})
            return

        self.send_response(404)
        self.send_header("Content-Length", "0")
        self.end_headers()
        self._write_log(404, {})

    def do_PATCH(self) -> None:
        if self.path.startswith("/method"):
            body = self._read_body()
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            if body:
                self.wfile.write(body)
            self._write_log(200, {})
            return

        self.send_response(404)
        self.send_header("Content-Length", "0")
        self.end_headers()
        self._write_log(404, {})

    def do_PUT(self) -> None:
        if self._handle_expect_417():
            return

        # 307 redirect（保持 method/body）→ /method（用于“请求体可重发”语义探针）
        if self.path.startswith("/redir_307"):
            # 必须消费请求体，否则同一连接上的后续请求会被残留 body 字节污染。
            _ = self._read_body()
            q = parse_qs(urlsplit(self.path).query, keep_blank_values=True)
            req_id = q.get("id", [""])[0]
            next_path = "/method"
            if req_id:
                next_path = f"{next_path}?id={req_id}"
            self.send_response(307)
            self.send_header("Location", next_path)
            self.send_header("Content-Length", "0")
            self.end_headers()
            self._write_log(307, {"Location": next_path})
            return

        # POST/PUT 版 auth/basic：用于“401 challenge → 重发 body”语义探针（seekable vs non-seekable）
        if self.path.startswith("/auth/basic"):
            body = self._read_body()
            auth = str(self.headers.get("authorization") or "")
            if _basic_auth_valid(auth, expected_user=_AUTH_USER, expected_pass=_AUTH_PASS):
                self.send_response(200)
                self.send_header("Content-Type", "application/octet-stream")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                if body:
                    self.wfile.write(body)
                self._write_log(200, {})
                return
            self.send_response(401)
            v = "Basic realm=\"qcurl_lc\""
            self.send_header("WWW-Authenticate", v)
            self.send_header("Content-Length", "0")
            self.end_headers()
            self._write_log(401, {"WWW-Authenticate": v})
            return

        # POST/PUT 版 auth/digest：用于“401 challenge → 重发 body”语义探针（seekable vs non-seekable）
        if self.path.startswith("/auth/digest"):
            body = self._read_body()
            auth = str(self.headers.get("authorization") or "")
            if _digest_auth_valid(
                auth,
                method="PUT",
                request_uri=str(self.path),
                body=body,
                expected_user=_AUTH_USER,
                expected_pass=_AUTH_PASS,
                expected_realm=_AUTH_REALM,
                expected_nonce=_AUTH_DIGEST_NONCE,
            ):
                self.send_response(200)
                self.send_header("Content-Type", "application/octet-stream")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                if body:
                    self.wfile.write(body)
                self._write_log(200, {})
                return
            self.send_response(401)
            # 提供稳定 challenge，并在服务端校验 response（不落盘凭据/参数）
            v = f"Digest realm=\"{_AUTH_REALM}\", nonce=\"{_AUTH_DIGEST_NONCE}\""
            self.send_header("WWW-Authenticate", v)
            self.send_header("Content-Length", "0")
            self.end_headers()
            self._write_log(401, {"WWW-Authenticate": v})
            return

        if self.path.startswith("/method"):
            body = self._read_body()
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            if body:
                self.wfile.write(body)
            self._write_log(200, {})
            return

        self.send_response(404)
        self.send_header("Content-Length", "0")
        self.end_headers()
        self._write_log(404, {})

    def do_POST(self) -> None:
        if self._handle_expect_417():
            return

        # 307 redirect（保持 method/body）→ /method（用于“请求体可重发”语义探针）
        if self.path.startswith("/redir_307"):
            # 必须消费请求体，否则同一连接上的后续请求会被残留 body 字节污染。
            _ = self._read_body()
            q = parse_qs(urlsplit(self.path).query, keep_blank_values=True)
            req_id = q.get("id", [""])[0]
            next_path = "/method"
            if req_id:
                next_path = f"{next_path}?id={req_id}"
            self.send_response(307)
            self.send_header("Location", next_path)
            self.send_header("Content-Length", "0")
            self.end_headers()
            self._write_log(307, {"Location": next_path})
            return

        # POST 版 auth/basic：用于“401 challenge → 重发 body”语义探针（seekable vs non-seekable）
        if self.path.startswith("/auth/basic"):
            body = self._read_body()
            auth = str(self.headers.get("authorization") or "")
            if _basic_auth_valid(auth, expected_user=_AUTH_USER, expected_pass=_AUTH_PASS):
                self.send_response(200)
                self.send_header("Content-Type", "application/octet-stream")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                if body:
                    self.wfile.write(body)
                self._write_log(200, {})
                return
            self.send_response(401)
            v = "Basic realm=\"qcurl_lc\""
            self.send_header("WWW-Authenticate", v)
            self.send_header("Content-Length", "0")
            self.end_headers()
            self._write_log(401, {"WWW-Authenticate": v})
            return

        # POST 版 auth/digest：用于“401 challenge → 重发 body”语义探针（seekable vs non-seekable）
        if self.path.startswith("/auth/digest"):
            body = self._read_body()
            auth = str(self.headers.get("authorization") or "")
            if _digest_auth_valid(
                auth,
                method="POST",
                request_uri=str(self.path),
                body=body,
                expected_user=_AUTH_USER,
                expected_pass=_AUTH_PASS,
                expected_realm=_AUTH_REALM,
                expected_nonce=_AUTH_DIGEST_NONCE,
            ):
                self.send_response(200)
                self.send_header("Content-Type", "application/octet-stream")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                if body:
                    self.wfile.write(body)
                self._write_log(200, {})
                return
            self.send_response(401)
            # 提供稳定 challenge，并在服务端校验 response（不落盘凭据/参数）
            v = f"Digest realm=\"{_AUTH_REALM}\", nonce=\"{_AUTH_DIGEST_NONCE}\""
            self.send_header("WWW-Authenticate", v)
            self.send_header("Content-Length", "0")
            self.end_headers()
            self._write_log(401, {"WWW-Authenticate": v})
            return

        if self.path.startswith("/method"):
            body = self._read_body()
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            if body:
                self.wfile.write(body)
            self._write_log(200, {})
            return

        # POST 301 → follow 后应变为 GET（参考 curl/tests/data/test1011）
        if self.path.startswith("/redir_post_301"):
            # 必须消费请求体，否则同一连接上的后续请求会被残留 body 字节污染（导致 501/解析异常）。
            _ = self._read_body()
            q = parse_qs(urlsplit(self.path).query, keep_blank_values=True)
            req_id = q.get("id", [""])[0]
            next_path = "/final_post_301"
            if req_id:
                next_path = f"{next_path}?id={req_id}"
            self.send_response(301)
            self.send_header("Location", next_path)
            self.send_header("Content-Length", "0")
            self.end_headers()
            self._write_log(301, {"Location": next_path})
            return

        if self.path.startswith("/final_post_301"):
            # KeepPost* 策略下，301 重定向后的目标仍可能收到 POST。
            _ = self._read_body()
            body = b"post-301-ok\n"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            self._write_log(200, {})
            return

        if self.path.startswith("/multipart"):
            body = self._read_body()
            ct = str(self.headers.get("content-type") or "")
            try:
                summary = _multipart_semantic_summary(ct, body)
                resp = json.dumps(summary, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(resp)))
                self.end_headers()
                self.wfile.write(resp)
                self._write_log(200, {})
                return
            except Exception as exc:
                payload = {
                    "kind": "error",
                    "error": str(exc),
                }
                resp = json.dumps(payload, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
                self.send_response(400)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(resp)))
                self.end_headers()
                self.wfile.write(resp)
                self._write_log(400, {})
                return

        self.send_response(404)
        self.send_header("Content-Length", "0")
        self.end_headers()
        self._write_log(404, {})

    def do_DELETE(self) -> None:
        if self.path.startswith("/method"):
            body = self._read_body()
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            if body:
                self.wfile.write(body)
            self._write_log(200, {})
            return

        self.send_response(404)
        self.send_header("Content-Length", "0")
        self.end_headers()
        self._write_log(404, {})

    def do_GET(self) -> None:
        if self.path.startswith("/auth/basic"):
            auth = str(self.headers.get("authorization") or "")
            if _basic_auth_valid(auth, expected_user=_AUTH_USER, expected_pass=_AUTH_PASS):
                body = b"basic-ok\n"
                self.send_response(200)
                self.send_header("Content-Type", "text/plain")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                self._write_log(200, {})
                return
            # 触发 libcurl 的 401 挑战路径（Content-Length=0，避免污染最终下载文件）
            self.send_response(401)
            v = "Basic realm=\"qcurl_lc\""
            self.send_header("WWW-Authenticate", v)
            self.send_header("Content-Length", "0")
            self.end_headers()
            self._write_log(401, {"WWW-Authenticate": v})
            return

        if self.path.startswith("/auth/digest"):
            auth = str(self.headers.get("authorization") or "")
            if _digest_auth_valid(
                auth,
                method="GET",
                request_uri=str(self.path),
                body=b"",
                expected_user=_AUTH_USER,
                expected_pass=_AUTH_PASS,
                expected_realm=_AUTH_REALM,
                expected_nonce=_AUTH_DIGEST_NONCE,
            ):
                body = b"digest-ok\n"
                self.send_response(200)
                self.send_header("Content-Type", "text/plain")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                self._write_log(200, {})
                return
            # KISS：仅提供最小 Digest challenge；服务端做 response 校验但不落盘凭据/参数
            self.send_response(401)
            v = f"Digest realm=\"{_AUTH_REALM}\", nonce=\"{_AUTH_DIGEST_NONCE}\""
            self.send_header("WWW-Authenticate", v)
            self.send_header("Content-Length", "0")
            self.end_headers()
            self._write_log(401, {"WWW-Authenticate": v})
            return

        if self.path.startswith("/redir_abs"):
            q = parse_qs(urlsplit(self.path).query, keep_blank_values=True)
            req_id = q.get("id", [""])[0]
            try:
                to_port = int(q.get("to_port", ["0"])[0])
            except Exception:
                to_port = 0
            if to_port <= 0:
                body = b"missing to_port\n"
                self.send_response(400)
                self.send_header("Content-Type", "text/plain")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                self._write_log(400, {})
                return

            loc = f"http://localhost:{to_port}/abs_target"
            if req_id:
                loc = f"{loc}?id={req_id}"
            self.send_response(302)
            self.send_header("Location", loc)
            self.send_header("Content-Length", "0")
            self.end_headers()
            self._write_log(302, {"Location": loc})
            return

        if self.path.startswith("/abs_target"):
            body = b"abs-target-ok\n"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            self._write_log(200, {})
            return

        if self.path.startswith("/final_post_301"):
            body = b"post-301-ok\n"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            self._write_log(200, {})
            return

        if self.path.startswith("/enc"):
            raw_body = b"accept-encoding-ok\n"
            accept = str(self.headers.get("accept-encoding") or "")
            accept_lower = accept.lower()

            encoding = ""
            out = raw_body
            if "br" in accept_lower and brotli is not None:
                try:
                    encoding = "br"
                    out = brotli.compress(raw_body)
                except Exception:
                    encoding = ""
                    out = raw_body
            elif "gzip" in accept_lower:
                encoding = "gzip"
                out = gzip.compress(raw_body)
            elif "deflate" in accept_lower:
                encoding = "deflate"
                out = zlib.compress(raw_body)

            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            if encoding:
                self.send_header("Content-Encoding", encoding)
            self.send_header("Content-Length", str(len(out)))
            self.end_headers()
            if out:
                self.wfile.write(out)
            self._write_log(200, {"Content-Encoding": encoding, "Content-Length": str(len(out))})
            return

        if self.path.startswith("/resp_headers"):
            q = parse_qs(urlsplit(self.path).query, keep_blank_values=True)
            scenario = (q.get("scenario", [""])[0] or "").strip().lower()

            if scenario == "1940":
                # 覆盖 curl/tests/data/test1940：折叠行 + TAB unfold 语义（curl_easy_header）
                #
                # 注意：这里用“手写响应头”以避免 BaseHTTPRequestHandler 自动注入 Date/Server
                # 以及 send_header() 的格式化副作用，确保可复现。
                #
                # 约束：该分支仅用于一致性用例，禁止引入动态时间/随机内容。
                self.close_connection = True
                raw = (
                    "HTTP/1.1 200 OK\r\n"
                    "Date: Thu, 09 Nov 2010 14:49:00 GMT\r\n"
                    "Server:       test with trailing space     \r\n"
                    "Content-Type: text/html\r\n"
                    "Fold: is\r\n"
                    " folding a     \r\n"
                    "   line\r\n"
                    "Content-Length: 0\r\n"
                    "Test:\t\r\n"
                    "\t \r\n"
                    "\tword\r\n"
                    "Set-Cookie: onecookie=data;\r\n"
                    "Set-Cookie: secondcookie=2data;\r\n"
                    "Set-Cookie: cookie3=data3;\r\n"
                    "Blank:\r\n"
                    "Blank2:\r\n"
                    "Location: /19400002\r\n"
                    "\r\n"
                ).encode("iso-8859-1", errors="replace")
                self.wfile.write(raw)
                self._write_log(200, {"Location": "/19400002", "Set-Cookie": "cookie3=data3;"})
                return

            # 默认：返回确定性响应头集合（用于 LC-26：重复头/大小写/顺序）
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", "0")
            self.send_header("Set-Cookie", "a=1; Path=/")
            self.send_header("Set-Cookie", "b=2; Path=/")
            self.send_header("X-Dupe", "1")
            self.send_header("X-Dupe", "2")
            self.send_header("X-Case", "A")
            self.send_header("x-case", "b")
            self.end_headers()
            self._write_log(200, {"Set-Cookie": "b=2; Path=/"})
            return

        if self.path.startswith("/empty_200"):
            self.send_response(200)
            self.send_header("Content-Length", "0")
            self.end_headers()
            self._write_log(200, {})
            return

        if self.path.startswith("/no_content"):
            self.send_response(204)
            self.send_header("Content-Length", "0")
            self.end_headers()
            self._write_log(204, {})
            return

        if self.path.startswith("/delay_headers/"):
            # 先记录请求（status=0 表示“未发送响应头”），再延迟发送响应
            self._write_log(0, {})
            try:
                seg = self.path.split("/", 3)[2]
                delay_ms = int(seg.split("?", 1)[0])
            except Exception:
                delay_ms = 0
            if delay_ms > 0:
                time.sleep(delay_ms / 1000.0)

            body = b"ok\n"
            try:
                self.send_response(200)
                self.send_header("Content-Type", "text/plain")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            except BrokenPipeError:
                return
            return

        if self.path.startswith("/stall_body/"):
            # 先发送响应头（记录 status=200），再延迟后发送 body
            try:
                seg = self.path.split("/", 4)[2]
                total = int(seg.split("?", 1)[0])
                seg2 = self.path.split("/", 4)[3]
                stall_ms = int(seg2.split("?", 1)[0])
            except Exception:
                total = 0
                stall_ms = 0

            body = b"x" * max(0, total)
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self._write_log(200, {})

            if stall_ms > 0:
                time.sleep(stall_ms / 1000.0)
            try:
                if body:
                    self.wfile.write(body)
                    self.wfile.flush()
            except BrokenPipeError:
                return
            return

        if self.path.startswith("/slow_body/"):
            # 分块发送响应体，并在块间 sleep（用于 cancel/慢速传输）
            try:
                seg_total = self.path.split("/", 5)[2]
                total = int(seg_total.split("?", 1)[0])
                seg_chunk = self.path.split("/", 5)[3]
                chunk = int(seg_chunk.split("?", 1)[0])
                seg_sleep = self.path.split("/", 5)[4]
                sleep_ms = int(seg_sleep.split("?", 1)[0])
            except Exception:
                total = 0
                chunk = 0
                sleep_ms = 0

            total = max(0, total)
            chunk = max(1, chunk)
            sleep_s = max(0, sleep_ms) / 1000.0

            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(total))
            self.end_headers()
            self._write_log(200, {})

            remaining = total
            try:
                while remaining > 0:
                    n = min(chunk, remaining)
                    self.wfile.write(b"x" * n)
                    self.wfile.flush()
                    remaining -= n
                    if remaining > 0 and sleep_s > 0:
                        time.sleep(sleep_s)
            except BrokenPipeError:
                return
            return

        # multi-hop redirect：/redir/<n> -> /redir/<n-1> ... -> /redir/0
        if self.path.startswith("/redir/"):
            try:
                seg = self.path.split("/", 3)[2]
                n = int(seg.split("?", 1)[0])
            except Exception:
                n = 0

            q = parse_qs(urlsplit(self.path).query, keep_blank_values=True)
            req_id = q.get("id", [""])[0]
            if n > 0:
                next_path = f"/redir/{n - 1}"
                if req_id:
                    next_path = f"{next_path}?id={req_id}"
                self.send_response(302)
                self.send_header("Location", next_path)
                self.send_header("Content-Length", "0")
                self.end_headers()
                self._write_log(302, {"Location": next_path})
                return

            body = b"redirect-ok\n"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            self._write_log(200, {})
            return

        # 登录模拟：/login -> 302(/home) + Set-Cookie
        if self.path.startswith("/login_path"):
            # 语义合同：Set-Cookie(Path=/a)，并通过 redirect 链验证：
            # - /a/step：Cookie 必须发送
            # - /b/final：Cookie 必须不发送（Path 不匹配）
            q = parse_qs(urlsplit(self.path).query, keep_blank_values=True)
            req_id = q.get("id", [""])[0]
            next_path = "/a/step"
            if req_id:
                next_path = f"{next_path}?id={req_id}"
            cookie_value = f"sid={_COOKIE_SID}; Path=/a; HttpOnly"
            self.send_response(302)
            self.send_header("Location", next_path)
            self.send_header("Set-Cookie", cookie_value)
            self.send_header("Content-Length", "0")
            self.end_headers()
            self._write_log(302, {"Location": next_path, "Set-Cookie": cookie_value})
            return

        if self.path.startswith("/a/step"):
            # Path=/a 必须匹配：服务端必须看到 sid cookie
            cookie = str(self.headers.get("cookie") or "")
            if f"sid={_COOKIE_SID}" not in cookie:
                body = b"missing cookie (path should match)\n"
                self.send_response(401)
                self.send_header("Content-Type", "text/plain")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                self._write_log(401, {})
                return

            q = parse_qs(urlsplit(self.path).query, keep_blank_values=True)
            req_id = q.get("id", [""])[0]
            next_path = "/b/final"
            if req_id:
                next_path = f"{next_path}?id={req_id}"
            self.send_response(302)
            self.send_header("Location", next_path)
            self.send_header("Content-Length", "0")
            self.end_headers()
            self._write_log(302, {"Location": next_path})
            return

        if self.path.startswith("/b/final"):
            # Path=/a 不应匹配：服务端不应看到 sid cookie
            cookie = str(self.headers.get("cookie") or "")
            if f"sid={_COOKIE_SID}" in cookie:
                body = b"unexpected cookie (path should NOT match)\n"
                self.send_response(400)
                self.send_header("Content-Type", "text/plain")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                self._write_log(400, {})
                return

            body = b"cookie-path-ok\n"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            self._write_log(200, {})
            return

        if self.path.startswith("/login"):
            q = parse_qs(urlsplit(self.path).query, keep_blank_values=True)
            req_id = q.get("id", [""])[0]
            next_path = "/home"
            if req_id:
                next_path = f"{next_path}?id={req_id}"
            cookie_value = f"sid={_COOKIE_SID}; Path=/; HttpOnly"
            self.send_response(302)
            self.send_header("Location", next_path)
            self.send_header("Set-Cookie", cookie_value)
            self.send_header("Content-Length", "0")
            self.end_headers()
            self._write_log(302, {"Location": next_path, "Set-Cookie": cookie_value})
            return

        if self.path.startswith("/home"):
            cookie = str(self.headers.get("cookie") or "")
            if f"sid={_COOKIE_SID}" not in cookie:
                body = b"missing cookie\n"
                self.send_response(401)
                self.send_header("WWW-Authenticate", "Basic realm=\"qcurl_lc\"")
                self.send_header("Content-Type", "text/plain")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                self._write_log(401, {"WWW-Authenticate": "Basic realm=\"qcurl_lc\""})
                return
            body = b"home-ok\n"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            self._write_log(200, {})
            return

        if self.path.startswith("/cookie"):
            body = b"cookie-ok\n"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            q = parse_qs(urlsplit(self.path).query, keep_blank_values=True)
            scenario = (q.get("scenario", [""])[0] or "").strip().lower()
            if scenario == "1920":
                # 覆盖 curl/tests/data/test1920：cookie set + reset + cookiejar 落盘语义
                self.send_header("Set-Cookie", "cookiename=cookiecontent;")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            if scenario == "1920":
                self._write_log(200, {"Set-Cookie": "cookiename=cookiecontent;"})
            else:
                self._write_log(200, {})
            return

        if self.path.startswith("/status/"):
            try:
                seg = self.path.split("/", 3)[2]
                code = int(seg.split("?", 1)[0])
            except Exception:
                code = 400
            body = f"status {code}\n".encode("utf-8")
            self.send_response(code)
            resp_headers: Dict[str, str] = {}
            if code == 401:
                v = "Basic realm=\"qcurl_lc\""
                self.send_header("WWW-Authenticate", v)
                resp_headers["WWW-Authenticate"] = v
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            self._write_log(code, resp_headers)
            return

        body = b"not found\n"
        self.send_response(404)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
        self._write_log(404, {})


def main() -> int:
    parser = argparse.ArgumentParser(description="Run a minimal observable HTTP server for QCurl libcurl_consistency")
    parser.add_argument("--port", type=int, required=True, help="port to listen on")
    parser.add_argument("--log-file", type=str, required=True, help="JSONL file to write observations")
    parser.add_argument("--tls-cert", type=str, default="", help="启用 TLS：服务端证书 PEM 路径（可选）")
    parser.add_argument("--tls-key", type=str, default="", help="启用 TLS：服务端私钥 PEM 路径（可选）")
    args = parser.parse_args()

    logging.basicConfig(format="%(asctime)s %(levelname)s %(message)s", level=logging.INFO)

    global LOG_FILE
    LOG_FILE = Path(args.log_file)

    httpd = ThreadingHTTPServer(("localhost", args.port), Handler)
    if args.tls_cert and args.tls_key:
        global TLS_CERT, TLS_KEY
        TLS_CERT = Path(args.tls_cert)
        TLS_KEY = Path(args.tls_key)
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(certfile=str(TLS_CERT), keyfile=str(TLS_KEY))
        httpd.socket = ctx.wrap_socket(httpd.socket, server_side=True)
        log.info("observe https listen on https://localhost:%d", args.port)
    else:
        log.info("observe http listen on http://localhost:%d", args.port)
    try:
        httpd.serve_forever(poll_interval=0.2)
    finally:
        httpd.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
