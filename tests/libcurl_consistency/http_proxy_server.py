#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
最小 HTTP/1.1 代理服务端（用于 libcurl_consistency 的 P1 代理一致性）：
- 支持普通 HTTP 代理转发（absolute-form 请求行）
- 支持 CONNECT 隧道（HTTPS over proxy）
- 强制 Basic proxy auth（避免“是否发送 Proxy-Authorization”的不确定性）
- 观测输出：JSONL（记录 method/target/关键 Proxy-* 头，用于一致性对比）
"""

from __future__ import annotations

import argparse
import base64
import json
import logging
import select
import socket
import threading
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, Optional, Tuple
from urllib.parse import parse_qs, urlsplit, urlencode, urlunsplit


log = logging.getLogger(__name__)


def _utc_ts() -> str:
    return datetime.now(tz=timezone.utc).isoformat()


def _strip_query_id_keep_origin(url: str) -> str:
    parts = urlsplit(url)
    q = parse_qs(parts.query, keep_blank_values=True)
    q.pop("id", None)
    query = urlencode(q, doseq=True)
    return urlunsplit((parts.scheme, parts.netloc, parts.path, query, parts.fragment))


def _extract_id_from_url(url: str) -> str:
    try:
        q = parse_qs(urlsplit(url).query, keep_blank_values=True)
        return q.get("id", [""])[0]
    except Exception:
        return ""


@dataclass(frozen=True)
class ProxyLogEntry:
    ts: str
    id: str
    method: str
    target: str
    version: str
    auth_ok: bool
    headers: Dict[str, str]


class ProxyServer:
    def __init__(self, host: str, port: int, log_file: Path, *, username: str, password: str):
        self._host = host
        self._port = port
        self._log_file = log_file
        userpass = f"{username}:{password}".encode("utf-8")
        self._auth_value = "Basic " + base64.b64encode(userpass).decode("ascii")
        self._sock: Optional[socket.socket] = None
        self._stop = threading.Event()

    def serve_forever(self) -> None:
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._sock.bind((self._host, self._port))
        self._sock.listen(50)
        self._sock.settimeout(0.5)
        log.info("proxy listen on %s:%d", self._host, self._port)

        while not self._stop.is_set():
            try:
                conn, addr = self._sock.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            t = threading.Thread(target=self._handle_conn, args=(conn, addr), daemon=True)
            t.start()

    def stop(self) -> None:
        self._stop.set()
        if self._sock:
            try:
                self._sock.close()
            except OSError:
                pass

    def _append_log(self, entry: ProxyLogEntry) -> None:
        self._log_file.parent.mkdir(parents=True, exist_ok=True)
        with self._log_file.open("a", encoding="utf-8") as fp:
            fp.write(json.dumps(asdict(entry), ensure_ascii=False) + "\n")

    def _handle_conn(self, conn: socket.socket, addr: Tuple[str, int]) -> None:
        try:
            conn.settimeout(10.0)
            req_line, headers_raw = self._read_request_head(conn)
            if not req_line:
                return

            parts = req_line.split()
            if len(parts) < 3:
                return
            method, target, version = parts[0], parts[1], parts[2]

            headers = {k.lower().strip(): v.strip() for k, v in headers_raw.items()}
            proxy_auth = headers.get("proxy-authorization", "")
            auth_ok = (proxy_auth == self._auth_value)

            req_id = ""
            if method.upper() != "CONNECT":
                req_id = _extract_id_from_url(target)

            headers_allowlist = {}
            for name in ("host", "proxy-authorization", "proxy-connection", "user-agent"):
                v = headers.get(name)
                if v:
                    headers_allowlist[name] = v

            self._append_log(ProxyLogEntry(
                ts=_utc_ts(),
                id=req_id,
                method=method.upper(),
                target=target,
                version=version,
                auth_ok=auth_ok,
                headers=headers_allowlist,
            ))

            if not auth_ok:
                self._send_407(conn)
                return

            if method.upper() == "CONNECT":
                self._handle_connect(conn, target)
                return

            self._handle_forward(conn, method.upper(), target, version, headers_raw)
        except Exception as exc:
            log.debug("conn error: %s", exc)
        finally:
            try:
                conn.close()
            except OSError:
                pass

    @staticmethod
    def _read_request_head(conn: socket.socket) -> Tuple[str, Dict[str, str]]:
        buf = b""
        while b"\r\n\r\n" not in buf and len(buf) < 64 * 1024:
            chunk = conn.recv(4096)
            if not chunk:
                break
            buf += chunk
        head, _, _ = buf.partition(b"\r\n\r\n")
        if not head:
            return "", {}
        lines = head.split(b"\r\n")
        if not lines:
            return "", {}
        req_line = lines[0].decode("utf-8", errors="replace").strip()
        headers: Dict[str, str] = {}
        for raw in lines[1:]:
            if b":" not in raw:
                continue
            k, v = raw.split(b":", 1)
            headers[k.decode("utf-8", errors="replace")] = v.decode("utf-8", errors="replace").strip()
        return req_line, headers

    @staticmethod
    def _send_407(conn: socket.socket) -> None:
        resp = (
            "HTTP/1.1 407 Proxy Authentication Required\r\n"
            "Proxy-Authenticate: Basic realm=\"qcurl_lc\"\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n"
            "\r\n"
        )
        conn.sendall(resp.encode("ascii"))

    def _handle_forward(self,
                        conn: socket.socket,
                        method: str,
                        target: str,
                        version: str,
                        headers_in: Dict[str, str]) -> None:
        parts = urlsplit(target)
        if not parts.scheme or not parts.netloc:
            return
        host = parts.hostname or ""
        if not host:
            return
        port = parts.port or (443 if parts.scheme == "https" else 80)
        path = parts.path or "/"
        if parts.query:
            path = f"{path}?{parts.query}"

        upstream = socket.create_connection((host, port), timeout=10.0)
        try:
            upstream.settimeout(10.0)
            req = f"{method} {path} {version}\r\n"
            upstream.sendall(req.encode("ascii"))

            sent_headers = {}
            for k, v in headers_in.items():
                lk = k.lower()
                if lk.startswith("proxy-"):
                    continue
                if lk == "connection":
                    continue
                sent_headers[k] = v
            sent_headers.setdefault("Host", parts.netloc)
            sent_headers["Connection"] = "close"

            for k, v in sent_headers.items():
                upstream.sendall(f"{k}: {v}\r\n".encode("utf-8"))
            upstream.sendall(b"\r\n")

            while True:
                data = upstream.recv(4096)
                if not data:
                    break
                conn.sendall(data)
        finally:
            try:
                upstream.close()
            except OSError:
                pass

    @staticmethod
    def _relay_bidirectional(a: socket.socket, b: socket.socket, *, timeout_s: float) -> None:
        a.setblocking(False)
        b.setblocking(False)
        end = datetime.now(tz=timezone.utc).timestamp() + timeout_s
        while datetime.now(tz=timezone.utc).timestamp() < end:
            r, _, _ = select.select([a, b], [], [], 0.5)
            if not r:
                continue
            for s in r:
                try:
                    data = s.recv(16384)
                except BlockingIOError:
                    continue
                if not data:
                    return
                dst = b if s is a else a
                dst.sendall(data)

    def _handle_connect(self, conn: socket.socket, target: str) -> None:
        if ":" not in target:
            return
        host, port_s = target.rsplit(":", 1)
        try:
            port = int(port_s)
        except ValueError:
            return

        upstream = socket.create_connection((host, port), timeout=10.0)
        try:
            conn.sendall(b"HTTP/1.1 200 Connection established\r\nConnection: close\r\n\r\n")
            self._relay_bidirectional(conn, upstream, timeout_s=30.0)
        finally:
            try:
                upstream.close()
            except OSError:
                pass


def main() -> int:
    parser = argparse.ArgumentParser(description="Run a minimal HTTP proxy for QCurl libcurl_consistency")
    parser.add_argument("--port", type=int, required=True, help="port to listen on")
    parser.add_argument("--log-file", type=str, required=True, help="JSONL file to write proxy observations")
    parser.add_argument("--username", type=str, default="lcuser", help="basic auth username")
    parser.add_argument("--password", type=str, default="lcpass", help="basic auth password")
    args = parser.parse_args()

    logging.basicConfig(format="%(asctime)s %(levelname)s %(message)s", level=logging.INFO)

    server = ProxyServer("localhost", args.port, Path(args.log_file), username=args.username, password=args.password)
    try:
        server.serve_forever()
    finally:
        server.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

