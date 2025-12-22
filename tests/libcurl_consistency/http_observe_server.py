#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
最小 HTTP/1.1 观测服务端（用于 libcurl_consistency 的 P2 用例）：
- /cookie：返回 200，并记录请求中的 Cookie 头
- /status/<code>：返回指定状态码（404/401/503...），并记录请求头
- /redir/<n>：多跳 302 重定向（用于 FOLLOWLOCATION 一致性）
- /login：返回 302 + Set-Cookie，并跳转到 /home（用于“登录态 cookie 链路”一致性）
- /home：需要 Cookie: sid=lc123，否则 401
- 观测输出：JSONL（method/path/status/关键请求头）
"""

from __future__ import annotations

import argparse
import json
import logging
import ssl
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
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


def _append_jsonl(path: Path, payload: Dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as fp:
        fp.write(json.dumps(payload, ensure_ascii=False) + "\n")


@dataclass(frozen=True)
class ObserveLogEntry:
    ts: str
    id: str
    method: str
    path: str
    status: int
    headers: Dict[str, str]
    response_headers: Dict[str, str]


class Handler(BaseHTTPRequestHandler):
    server_version = "qcurl-lc-observe/0.1"

    def log_message(self, fmt: str, *args) -> None:
        # 禁止默认 stdout 访问日志（用 JSONL 代替）
        return

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
            method=str(self.command or "").upper(),
            path=str(self.path),
            status=int(status),
            headers=headers_allowlist,
            response_headers=resp_allowlist,
        )
        _append_jsonl(LOG_FILE, asdict(entry))

    def do_GET(self) -> None:
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
