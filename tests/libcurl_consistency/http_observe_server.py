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
- /home：需要 Cookie: sid=lc123，否则 401
- 观测输出：JSONL（method/path/status/关键请求头）
"""

from __future__ import annotations

import argparse
import hashlib
import json
import logging
import ssl
import time
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from email.parser import BytesParser
from email.policy import default as email_default_policy
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Dict, Optional
from urllib.parse import parse_qs, urlsplit, urlencode, urlunsplit


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

def _sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

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

    def _read_body(self) -> bytes:
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
        for name in ("cookie", "authorization", "content-length", "host"):
            v = self.headers.get(name)
            if v is not None:
                headers_allowlist[name] = str(v)

        resp_allowlist: Dict[str, str] = {}
        for k, v in response_headers.items():
            lk = str(k).lower().strip()
            if lk in ("location", "set-cookie", "www-authenticate"):
                # Location 中会携带 id（用于关联日志），对比时需要去掉
                if lk == "location":
                    resp_allowlist[lk] = _strip_query_id(str(v))
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

    def do_HEAD(self) -> None:
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

    def do_POST(self) -> None:
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
        if self.path.startswith("/resp_headers"):
            # 返回确定性响应头集合（用于 LC-26：重复头/大小写/顺序）
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
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
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
