#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
最小 SOCKS5 代理 stub（用于 libcurl_consistency 的 LC-39）：
- 仅实现 SOCKS5 无认证握手（method=0x00）
- 对任何 CONNECT 请求固定返回 REP=0x01（general failure）
- 观测输出：JSONL（peer/dst/cmd/rep），用于确认客户端确实走了 SOCKS5 代理路径
"""

from __future__ import annotations

import argparse
import json
import logging
import socket
import threading
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional, Tuple


log = logging.getLogger(__name__)


def _utc_ts() -> str:
    return datetime.now(tz=timezone.utc).isoformat()


def _recv_exact(conn: socket.socket, n: int) -> Optional[bytes]:
    buf = b""
    while len(buf) < n:
        chunk = conn.recv(n - len(buf))
        if not chunk:
            return None
        buf += chunk
    return buf


@dataclass(frozen=True)
class Socks5LogEntry:
    ts: str
    peer: str
    cmd: int
    atyp: int
    dst: str
    dst_port: int
    rep: int


class Socks5StubServer:
    def __init__(self, host: str, port: int, log_file: Path):
        self._host = host
        self._port = port
        self._log_file = log_file
        self._sock: Optional[socket.socket] = None
        self._stop = threading.Event()

    def serve_forever(self) -> None:
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._sock.bind((self._host, self._port))
        self._sock.listen(50)
        self._sock.settimeout(0.5)
        log.info("socks5 stub listen on %s:%d", self._host, self._port)

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

    def _append_log(self, entry: Socks5LogEntry) -> None:
        self._log_file.parent.mkdir(parents=True, exist_ok=True)
        with self._log_file.open("a", encoding="utf-8") as fp:
            fp.write(json.dumps(asdict(entry), ensure_ascii=False) + "\n")

    def _handle_conn(self, conn: socket.socket, addr: Tuple[str, int]) -> None:
        try:
            conn.settimeout(10.0)
            peer = f"{addr[0]}:{addr[1]}"

            hello = _recv_exact(conn, 2)
            if not hello:
                return
            ver, nmethods = hello[0], hello[1]
            if ver != 5 or nmethods <= 0:
                return
            methods = _recv_exact(conn, int(nmethods))
            if not methods:
                return
            if 0x00 not in methods:
                # 不支持无认证时，返回“无可接受方法”
                conn.sendall(b"\x05\xff")
                return
            conn.sendall(b"\x05\x00")

            req = _recv_exact(conn, 4)
            if not req:
                return
            ver2, cmd, _rsv, atyp = req[0], req[1], req[2], req[3]
            if ver2 != 5:
                return

            dst = ""
            if atyp == 0x01:  # IPv4
                raw = _recv_exact(conn, 4)
                if not raw:
                    return
                dst = ".".join(str(b) for b in raw)
            elif atyp == 0x03:  # DOMAIN
                ln_raw = _recv_exact(conn, 1)
                if not ln_raw:
                    return
                ln = int(ln_raw[0])
                name = _recv_exact(conn, ln)
                if name is None:
                    return
                dst = name.decode("utf-8", errors="replace")
            elif atyp == 0x04:  # IPv6
                raw = _recv_exact(conn, 16)
                if not raw:
                    return
                dst = ":".join(raw[i : i + 2].hex() for i in range(0, 16, 2))
            else:
                return

            port_raw = _recv_exact(conn, 2)
            if not port_raw:
                return
            dst_port = int.from_bytes(port_raw, byteorder="big", signed=False)

            rep = 0x01  # general failure
            self._append_log(Socks5LogEntry(
                ts=_utc_ts(),
                peer=peer,
                cmd=int(cmd),
                atyp=int(atyp),
                dst=dst,
                dst_port=dst_port,
                rep=int(rep),
            ))

            # SOCKS5 reply: VER, REP, RSV, ATYP, BND.ADDR, BND.PORT
            resp = b"\x05" + bytes([rep, 0x00, 0x01]) + b"\x00\x00\x00\x00" + b"\x00\x00"
            conn.sendall(resp)
        except Exception as exc:
            log.debug("conn error: %s", exc)
        finally:
            try:
                conn.close()
            except OSError:
                pass


def main() -> int:
    parser = argparse.ArgumentParser(description="SOCKS5 stub server (always fail connect)")
    parser.add_argument("--host", default="127.0.0.1", help="listen host (default 127.0.0.1)")
    parser.add_argument("--port", type=int, required=True, help="listen port")
    parser.add_argument("--log-file", type=str, required=True, help="JSONL log output path")
    args = parser.parse_args()

    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")

    server = Socks5StubServer(args.host, int(args.port), Path(args.log_file))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

